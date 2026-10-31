#include "AudioDevice.h"

#include "AudioRenderer.h"
#include "Realtime.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include "MiniaudioShared.h"

#include <atomic>
#include <new>
#include <string_view>
#include <thread>

namespace Shalltear {

	namespace AudioImpl {

		// never moved (miniaudio points here)
		struct AudioDeviceBackend {
			ma_log log{};
			ma_context context{};
			ma_device device{};

			AudioBackendKind kind = AudioBackendKind::Null;
			AudioRenderer* renderer = nullptr;

			bool logReady = false;
			bool contextReady = false;
			bool deviceReady = false;

			std::atomic<bool> silent{false};

			std::atomic<u32> started{0};
			std::atomic<u32> stopped{0};
			std::atomic<u32> rerouted{0};
			std::atomic<u32> interrupted{0};
			u32 requested = 0;
		};

		namespace {

			void OnLog(void* user, ma_uint32 level, const char* message) {
				auto* backend = static_cast<AudioDeviceBackend*>(user);
				if (!backend || backend->silent.load(std::memory_order_acquire) || !message) {
					return;
				}
				if (backend->renderer && backend->renderer->GetCounters().renderThread.load(std::memory_order_relaxed) == std::this_thread::get_id()) {
					return;
				}

				std::string_view text(message);
				while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
					text.remove_suffix(1);
				}
				if (text.empty()) {
					return;
				}

				switch (level) {
				case MA_LOG_LEVEL_ERROR:
					SLT_LOG_ERROR("Audio", "miniaudio: {}", text);
					break;
				case MA_LOG_LEVEL_WARNING:
					SLT_LOG_WARN("Audio", "miniaudio: {}", text);
					break;
				case MA_LOG_LEVEL_INFO:
					SLT_LOG_DEBUG("Audio", "miniaudio: {}", text);
					break;
				default:
					SLT_LOG_TRACE("Audio", "miniaudio: {}", text);
					break;
				}
			}

			void OnData(ma_device* device, void* output, const void*, ma_uint32 frameCount) [[clang::nonblocking]] {
				auto* backend = static_cast<AudioDeviceBackend*>(device->pUserData);
				backend->renderer->Render(static_cast<f32*>(output), frameCount);
			}

			void OnNotification(const ma_device_notification* notification) [[clang::nonblocking]] {
				auto* backend = static_cast<AudioDeviceBackend*>(notification->pDevice->pUserData);
				switch (notification->type) {
				case ma_device_notification_type_started:
					backend->started.fetch_add(1, std::memory_order_release);
					break;
				case ma_device_notification_type_stopped:
					backend->stopped.fetch_add(1, std::memory_order_release);
					break;
				case ma_device_notification_type_rerouted:
					backend->rerouted.fetch_add(1, std::memory_order_release);
					break;
				case ma_device_notification_type_interruption_began:
					backend->interrupted.fetch_add(1, std::memory_order_release);
					break;
				default:
					break;
				}
			}

			void ReadFormat(const AudioDeviceBackend& backend, AudioDeviceFormat& out) {
				const ma_device& device = backend.device;
				out.backend = backend.kind;
				out.sampleRate = device.sampleRate;
				out.channels = device.playback.channels;
				out.layout = {};
				if (out.channels <= MAX_AUDIO_CHANNELS) {
					out.layout.count = out.channels;
					for (u32 channel = 0; channel < out.channels; ++channel) {
						out.layout.positions[channel] = ToSpeakerPosition(device.playback.channelMap[channel]);
					}
				}
				out.periodFrames = device.playback.internalPeriodSizeInFrames;
				out.periods = device.playback.internalPeriods;
				out.deviceSampleRate = device.playback.internalSampleRate;
				out.name = device.playback.name;
			}

		} // anonymous namespace

		AudioDevice::~AudioDevice() {
			if (_backend) {
				_backend->silent.store(true, std::memory_order_release);
			}
			Close();
		}

		bool AudioDevice::Open(AudioBackendKind kind, u32 periodMilliseconds, AudioRenderer& renderer, AudioDevicePreparedFn prepared, void* user) {
			Close();

			AudioDeviceBackend* backend = New<AudioDeviceBackend>(MemTag::Audio);
			backend->kind = kind;
			backend->renderer = &renderer;

			const ma_allocation_callbacks callbacks = MakeMiniaudioAllocationCallbacks();
			const char* backendName = kind == AudioBackendKind::System ? "WASAPI" : "null";

			ma_result result = ma_log_init(&callbacks, &backend->log);
			if (result == MA_SUCCESS) {
				backend->logReady = true;
				ma_log_register_callback(&backend->log, ma_log_callback_init(&OnLog, backend));
			}

			ma_context_config contextConfig = ma_context_config_init();
			contextConfig.pLog = backend->logReady ? &backend->log : nullptr;
			contextConfig.allocationCallbacks = callbacks;
			const ma_backend backends[1] = {kind == AudioBackendKind::System ? ma_backend_wasapi : ma_backend_null};
			result = ma_context_init(backends, 1, &contextConfig, &backend->context);
			if (result != MA_SUCCESS) {
				SLT_LOG_WARN("Audio", "the {} backend couldn't create a context: {}", backendName, ma_result_description(result));
				_backend = backend;
				Close();
				return false;
			}
			backend->contextReady = true;

			ma_device_config config = ma_device_config_init(ma_device_type_playback);
			config.playback.format = ma_format_f32;
			config.playback.channels = 0;
			config.sampleRate = 0;
			config.periodSizeInMilliseconds = periodMilliseconds;
			config.performanceProfile = ma_performance_profile_low_latency;
			config.noPreSilencedOutputBuffer = MA_TRUE;
			config.noFixedSizedCallback = MA_TRUE;
			config.dataCallback = &OnData;
			config.notificationCallback = &OnNotification;
			config.pUserData = backend;
			config.wasapi.usage = ma_wasapi_usage_pro_audio;

			result = ma_device_init(&backend->context, &config, &backend->device);
			if (result == MA_SUCCESS && backend->device.playback.channels > MAX_AUDIO_CHANNELS) {
				ma_device_uninit(&backend->device);
				config.playback.channels = 2;
				result = ma_device_init(&backend->context, &config, &backend->device);
			}
			if (result != MA_SUCCESS) {
				SLT_LOG_WARN("Audio", "the {} backend couldn't open a playback device: {}", backendName, ma_result_description(result));
				_backend = backend;
				Close();
				return false;
			}
			backend->deviceReady = true;

			ReadFormat(*backend, _format);
			const u32 primingFrames = _format.periodFrames * _format.periods;
			if (!renderer.Prepare(_format.layout, _format.sampleRate, primingFrames)) {
				SLT_LOG_WARN("Audio", "the {} device opened with a format the mixer can't render ({} channel(s) at {} Hz)", backendName, _format.channels, _format.sampleRate);
				_backend = backend;
				Close();
				return false;
			}

			_backend = backend;
			if (prepared) {
				prepared(user);
			}
			result = ma_device_start(&backend->device);
			++backend->requested;
			if (result != MA_SUCCESS) {
				SLT_LOG_WARN("Audio", "the {} device opened and wouldn't start: {}", backendName, ma_result_description(result));
				Close();
				return false;
			}
			return true;
		}

		void AudioDevice::Close() {
			AudioDeviceBackend* backend = _backend;
			if (!backend) {
				return;
			}
			_backend = nullptr;

			// device first (uninit joins the worker)
			if (backend->deviceReady) {
				ma_device_uninit(&backend->device);
			}
			if (backend->renderer) {
				backend->renderer->Release();
			}
			if (backend->contextReady) {
				ma_context_uninit(&backend->context);
			}
			if (backend->logReady) {
				ma_log_uninit(&backend->log);
			}
			Delete(backend);
			_format = {};
		}

		AudioDeviceEvents AudioDevice::GetEvents() const {
			AudioDeviceEvents events;
			if (_backend) {
				events.started = _backend->started.load(std::memory_order_acquire);
				events.stopped = _backend->stopped.load(std::memory_order_acquire);
				events.rerouted = _backend->rerouted.load(std::memory_order_acquire);
				events.interrupted = _backend->interrupted.load(std::memory_order_acquire);
				events.requested = _backend->requested;
			}
			return events;
		}

		void AudioDevice::RefreshFormat() {
			if (!_backend || !_backend->deviceReady) {
				return;
			}
			// WASAPI notification thread writes these under this lock. crap API
			if (_backend->kind == AudioBackendKind::System) {
				ma_mutex_lock(&_backend->device.wasapi.rerouteLock);
				ReadFormat(*_backend, _format);
				ma_mutex_unlock(&_backend->device.wasapi.rerouteLock);
			} else {
				ReadFormat(*_backend, _format);
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
