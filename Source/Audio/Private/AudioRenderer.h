#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioBus.h"
#include "AudioChannels.h"
#include "AudioMailbox.h"
#include "AudioMessages.h"
#include "AudioRing.h"
#include "AudioVoice.h"

#include <atomic>
#include <thread>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioClockStamp {
			u64 frames = 0;
			u64 time = 0;
			u64 ticks = 0;
			bool running = false;
		};

		struct AudioRenderCounters {
			std::atomic<u64> callbacks{0};
			std::atomic<u64> frames{0};
			std::atomic<u64> blocks{0};
			std::atomic<u64> costTicks{0};
			std::atomic<u64> worstCostTicks{0};
			std::atomic<u64> shortestIntervalTicks{~0ull};
			std::atomic<u64> longestIntervalTicks{0};
			std::atomic<u64> overruns{0};
			std::atomic<u32> voices{0};
			std::atomic<u32> realVoices{0};
			std::atomic<u64> decoderShortfalls{0};
			std::atomic<u64> lateStarts{0};
			std::atomic<u64> droppedEvents{0};
			std::atomic<u64> streamUnderruns{0};
			// f32 bits of the loudest sample
			std::atomic<u32> lastBlockPeak{0};
			std::atomic<u32> peak{0};
			std::atomic<std::thread::id> renderThread{};
		};

		struct AudioMeterTap {
			std::atomic<u32> meanSquare[MAX_AUDIO_CHANNELS]{};
			std::atomic<u32> peak[MAX_AUDIO_CHANNELS]{};
		};

		enum class AudioMeterPoint : u8 {
			Output = 0,
			Fader = 1,
		};

		struct AudioBusMeter {
			AudioMeterTap output;
			AudioMeterTap fader;
		};

		struct AudioBusLevel {
			u32 channels = 0;
			f32 rms[MAX_AUDIO_CHANNELS]{};
			f32 peak[MAX_AUDIO_CHANNELS]{};
		};

		class AudioRenderer {
		public:
			static constexpr u32 BLOCK_FRAMES = 256;
			static_assert(BLOCK_FRAMES == BUS_BUFFER_FRAMES, "a bus's buffers are sized in AudioBus.h for this block");

			static constexpr u32 DEFAULT_VOICES = 256;
			static constexpr u32 DEFAULT_DECODERS = 256;

			static constexpr u32 MAX_BUSES = 256;

			static constexpr u32 ROOT_BUS = 0;

			static constexpr f32 MAX_BUS_RATE = 16.0f;

			AudioRenderer() = default;
			~AudioRenderer();
			AudioRenderer(const AudioRenderer&) = delete;
			AudioRenderer& operator=(const AudioRenderer&) = delete;

			void Initialize(u32 voices = DEFAULT_VOICES, u32 decoders = DEFAULT_DECODERS);
			void Shutdown();
			bool IsInitialized() const { return _voices != nullptr; }

			// main thread with no callback running

			void ApplyPendingCommands();

			void Resize(u32 voices, u32 decoders);

			u32 GetVoiceCapacity() const { return _voiceCapacity; }
			u32 GetDecoderCapacity() const { return _decoderCapacity; }

			// main thread with no callback running

			bool Prepare(const ChannelLayout& layout, u32 sampleRate, u32 primingFrames = 0);

			void Release();

			bool IsPrepared() const { return _block != nullptr; }
			u32 GetChannels() const { return _layout.count; }
			u32 GetSampleRate() const { return _sampleRate; }
			const ChannelLayout& GetLayout() const { return _layout; }

			AudioRing<AudioCommand>& GetCommands() { return _commands; }
			AudioRing<AudioEvent>& GetEvents() { return _events; }

			void PostPlacement(u32 slot, const AudioVoicePlacement& placement);

			void Render(f32* interleaved, u32 frameCount) [[clang::nonblocking]];

			const AudioRenderCounters& GetCounters() const { return _counters; }

			bool ReadClock(AudioClockStamp& stamp) const;

			u64 GetVoicePosition(u32 slot) const;

			// freed only in Shutdown (render thread may still write them)
			bool EnableMetering(bool enabled);

			const AudioBusMeter* GetMeters() const { return _meters; }
		private:
			void RenderBlock() [[clang::nonblocking]];
			void Apply(const AudioCommand& command, u64 blockStart) [[clang::nonblocking]];
			void ApplyBus(const AudioCommand& command) [[clang::nonblocking]];
			void EndVoice(u32 slot) [[clang::nonblocking]];

			AudioVoiceDecoder* AcquireDecoder(AudioVoiceState& voice) [[clang::nonblocking]];
			void ReleaseDecoder(AudioVoiceState& voice) [[clang::nonblocking]];

			void LetGoOfVirtualState(AudioVoiceState& voice) [[clang::nonblocking]];

			void AllocateVoicePools(u32 voices, u32 decoders);
			void FreeVoicePools();
			void DestroyBus(u32 slot) [[clang::nonblocking]];

			u32 LiveOrRoot(u32 slot, u32 generation) const [[clang::nonblocking]];

			void RebuildOrder() [[clang::nonblocking]];

			void ProcessChain(AudioBusState& bus, AudioBusMeter* meter) [[clang::nonblocking]];

			void RunEffect(AudioBusRow& row, f32* buffer, const f32* sidechain) [[clang::nonblocking]];

			f32* TouchReceive(AudioBusState& bus, u32 row) [[clang::nonblocking]];

			bool IsChainQuiet(const AudioBusState& bus) const [[clang::nonblocking]];

			void DeriveBusStates() [[clang::nonblocking]];

			f32* Touch(AudioBusState& bus) [[clang::nonblocking]];

			void NoteCallback(u64 startTicks, u64 endTicks, u32 frameCount) [[clang::nonblocking]];
			void PublishClock(u64 ticks) [[clang::nonblocking]];

			[[clang::noinline]] void Meter(AudioMeterTap& tap, const f32* buffer) [[clang::nonblocking]];
			[[clang::noinline]] void ClearMeter(u32 slot) [[clang::nonblocking]];

			u64 MixerFrameOf(u64 time, u32 bus) const [[clang::nonblocking]] {
				if (time == 0) {
					return 0;
				}
				return _buses && bus < MAX_BUSES && _buses[bus].clock == AudioBusClock::Device ? time : time + _stoppedFrames;
			}

			AudioVoiceState* _voices = nullptr;
			std::atomic<u64>* _positions = nullptr;
			AudioMailbox<AudioVoicePlacement>* _placements = nullptr;
			u32 _voiceCapacity = 0;
			u64* _active = nullptr;
			u32 _activeWords = 0;
			AudioVoiceDecoder* _decoders = nullptr;
			u32* _freeDecoders = nullptr;
			u32 _decoderCapacity = 0;
			u32 _freeDecoderCount = 0;
			AudioBusState* _buses = nullptr;
			AudioRing<AudioCommand> _commands;
			AudioRing<AudioEvent> _events;
			f32* _dry = nullptr;
			f32* _wet = nullptr;
			f32* _post = nullptr;
			f32* _gather = nullptr;
			u64 _mixerFrames = 0;
			u64 _stoppedFrames = 0;
			f32 _listenerGain = 0.0f;
			f32 _listenerTarget = 0.0f;
			AudioRamp _globalVolume;

			bool _gameplayRunning = true;
			AudioRamp _timeScale;
			f32 _timeScaleTarget = 1.0f;

			u32 _order[MAX_BUSES]{};
			u32 _orderCount = 0;
			u32 _pending[MAX_BUSES]{};
			bool _graphDirty = true;
			bool _anySolo = false;

			// planar, channel c frame i = _block[c * BLOCK_FRAMES + i]
			f32* _block = nullptr;
			ChannelLayout _layout;
			u32 _sampleRate = 0;
			u32 _cursor = BLOCK_FRAMES;
			u64 _ticksPerSecond = 0;
			u64 _lastCallbackTicks = 0;
			u64 _primingFrames = 0;
			u64 _framesDelivered = 0;

			AudioRenderCounters _counters;

			AudioBusMeter* _meters = nullptr;
			AudioBusMeter* _metered = nullptr;
			f32 _meterAverage = 0.0f;
			f32 _meterRelease = 0.0f;

			std::atomic<u32> _clockSequence{0};
			std::atomic<u64> _clockFrames{0};
			std::atomic<u64> _clockTime{0};
			std::atomic<u64> _clockTicks{0};
			std::atomic<bool> _clockRunning{false};
		};

	} // namespace AudioImpl

} // namespace Shalltear
