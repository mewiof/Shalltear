#include "AudioEditor.h"

#include "AudioWaveform.h"

#include "Shalltear/Audio/AudioClip.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace Shalltear::AudioEditor {

	namespace {

		using EditorTheme::ColorToken;

		constexpr u32 SPANS = 2048;
		constexpr u32 CAPACITY = 64;
		constexpr u32 READERS = 4;
		constexpr f64 SLICE_SECONDS = 0.008;
		constexpr u64 READ_FRAMES = 16384;
		constexpr u32 THUMBNAIL = 256;

		struct Waveform {
			Guid guid;
			AudioImpl::AudioEnvelopeBuilder builder;
			AudioImpl::AudioEnvelope published;
			JobHandle job;
			std::atomic<bool> reading{false};
			std::atomic<bool> stop{false};
			bool done = false;
			u64 used = 0;
			bool declined = false;
		};

		struct WaveformState {
			Vector<Waveform*> entries{GetMemoryResource(MemTag::Editor)};
			u64 clock = 0;
			bool pumping = false;
			Vector<u8> pixels{GetMemoryResource(MemTag::Editor)};
			Vector<f32> low{GetMemoryResource(MemTag::Editor)};
			Vector<f32> high{GetMemoryResource(MemTag::Editor)};
		};

		WaveformState* _state = nullptr;

		const char PUMP_TOKEN = 0;

		WaveformState& State() {
			if (!_state) {
				_state = New<WaveformState>(MemTag::Editor);
			}
			return *_state;
		}

		Waveform* Find(const Guid& guid) {
			if (!_state) {
				return nullptr;
			}
			for (Waveform* entry : _state->entries) {
				if (entry->guid == guid) {
					return entry;
				}
			}
			return nullptr;
		}

		const AudioClip* AsClip(const Asset* asset) {
			return asset && TypeRegistry::IsDerivedFrom(asset->GetType(), AudioClip::GetReflectType()) ? static_cast<const AudioClip*>(asset) : nullptr;
		}

		const AudioImpl::AudioEnvelope& Drawn(const Waveform& entry) {
			return entry.done ? entry.builder.Get() : entry.published;
		}

		void ReadSlice(Waveform* entry, u64 sliceTicks) {
			const u64 start = Time::GetTicks();
			while (!entry->builder.IsFinished() && !entry->stop.load(std::memory_order_relaxed) && Time::GetTicks() - start < sliceTicks) {
				entry->builder.Advance(READ_FRAMES);
			}
			entry->reading.store(false, std::memory_order_release);
		}

		void Discard(Waveform* entry) {
			if (entry->job.IsValid()) {
				entry->stop.store(true, std::memory_order_relaxed);
				JobSystem::WaitFor(entry->job);
			}
			Delete(entry);
		}

		void Evict(WaveformState& state) {
			while (state.entries.size() > CAPACITY) {
				auto oldest = std::min_element(state.entries.begin(), state.entries.end(),
											   [](const Waveform* a, const Waveform* b) { return a->used < b->used; });
				Discard(*oldest);
				state.entries.erase(oldest);
			}
		}

		void Pump(const FrameStageArgs&, void*) {
			UpdatePreview();
			UpdateGameMute();
			if (!_state) {
				return;
			}
			u32 readers = 0;
			for (Waveform* entry : _state->entries) {
				if (!entry->job.IsValid()) {
					continue;
				}
				if (entry->reading.load(std::memory_order_acquire)) {
					++readers;
					continue;
				}
				entry->job = JobHandle{};
				if (!entry->builder.IsFinished()) {
					entry->published = entry->builder.Get();
					continue;
				}
				entry->builder.Release();
				entry->done = true;
				entry->published = AudioImpl::AudioEnvelope{};
				if (entry->declined) {
					entry->declined = false;
					EditorPreviewStore::Invalidate(entry->guid);
				}
			}

			const u64 sliceTicks = static_cast<u64>(static_cast<f64>(Time::GetTicksPerSecond()) * SLICE_SECONDS);
			for (; readers < READERS; ++readers) {
				Waveform* next = nullptr;
				for (Waveform* entry : _state->entries) {
					if (!entry->done && !entry->job.IsValid() && (!next || entry->used > next->used)) {
						next = entry;
					}
				}
				if (!next) {
					return;
				}
				next->reading.store(true, std::memory_order_relaxed);
				next->job = JobSystem::Submit([next, sliceTicks]() { ReadSlice(next, sliceTicks); });
			}
		}

		void Rasterize(const AudioImpl::AudioEnvelope& envelope, WaveformState& state) {
			state.pixels.assign(static_cast<size_t>(THUMBNAIL) * THUMBNAIL * 4, 0);
			state.low.resize(THUMBNAIL);
			state.high.resize(THUMBNAIL);
			const Color32 ink = EditorTheme::GetColor32(ColorToken::AudioWaveform);
			const u32 channels = envelope.channels > 0 ? envelope.channels : 1;
			const f32 lane = static_cast<f32>(THUMBNAIL) / static_cast<f32>(channels);
			for (u32 channel = 0; channel < channels; ++channel) {
				AudioImpl::GetEnvelopeColumns(envelope, channel, THUMBNAIL, state.low.data(), state.high.data());
				const f32 mid = lane * (static_cast<f32>(channel) + 0.5f);
				const f32 bottomOfLane = lane * static_cast<f32>(channel + 1);
				for (u32 x = 0; x < THUMBNAIL; ++x) {
					const f32 top = std::clamp(mid - state.high[x] * lane * 0.5f, lane * static_cast<f32>(channel), bottomOfLane - 1.0f);
					const f32 bottom = std::clamp(mid - state.low[x] * lane * 0.5f, top + 1.0f, bottomOfLane);
					for (u32 y = static_cast<u32>(top); y < static_cast<u32>(std::ceil(bottom)) && y < THUMBNAIL; ++y) {
						u8* pixel = state.pixels.data() + (static_cast<size_t>(y) * THUMBNAIL + x) * 4;
						pixel[0] = ink.r;
						pixel[1] = ink.g;
						pixel[2] = ink.b;
						pixel[3] = ink.a;
					}
				}
			}
		}

		bool ProduceThumbnail(const AssetPreviewRequest& request, AssetPreviewImage& out) {
			const AudioImpl::AudioEnvelope* envelope = FindWaveform(request.guid, request.asset);
			Waveform* entry = Find(request.guid);
			if (!envelope || !entry) {
				return false;
			}
			if (!envelope->IsComplete()) {
				entry->declined = !entry->done;
				return false;
			}
			WaveformState& state = State();
			Rasterize(*envelope, state);
			out.pixels = {state.pixels.data(), state.pixels.size()};
			out.pixelsFormat = RHI::Format::RGBA8SRGB;
			out.size = {static_cast<i32>(THUMBNAIL), static_cast<i32>(THUMBNAIL)};
			return true;
		}

	} // anonymous namespace

	const AudioImpl::AudioEnvelope* FindWaveform(const Guid& guid, const Asset* loaded) {
		if (!guid.IsValid()) {
			return nullptr;
		}
		WaveformState& state = State();
		if (Waveform* found = Find(guid)) {
			found->used = ++state.clock;
			return &Drawn(*found);
		}
		Asset* held = loaded ? nullptr : AssetDatabase::LoadAssetByGuid(guid);
		const AudioClip* clip = AsClip(loaded ? loaded : held);
		Waveform* entry = New<Waveform>(MemTag::Editor);
		const bool began = clip && entry->builder.Begin(*clip, SPANS);
		if (held) {
			AssetDatabase::UnloadAsset(held);
		}
		if (!began) {
			Delete(entry);
			return nullptr;
		}
		entry->guid = guid;
		entry->used = ++state.clock;
		entry->published = entry->builder.Get();
		state.entries.push_back(entry);
		Evict(state);
		return &Drawn(*entry);
	}

	const AudioImpl::AudioEnvelope* PeekWaveform(const Guid& guid) {
		const Waveform* found = Find(guid);
		return found ? &Drawn(*found) : nullptr;
	}

	void ForgetWaveform(const Guid& guid) {
		if (_state) {
			std::erase_if(_state->entries, [&guid](Waveform* entry) {
				if (entry->guid != guid) {
					return false;
				}
				Discard(entry);
				return true;
			});
		}
		EditorPreviewStore::Invalidate(guid);
	}

	void RegisterWaveforms(const void* owner) {
		const StringHash clip = AudioClip::GetReflectType()->name;
		EditorPreviews::RegisterProducer(clip, &ProduceThumbnail, owner);
		EditorPreviews::AssetBadges::Register(clip, EditorIcons::Glyph::AssetAudioClip, ColorToken::AssetAudio, owner);
		WaveformState& state = State();
		if (!state.pumping) {
			state.pumping = FrameStages::Register(FrameStage::EndFrame, FrameStagePosition::Late, &Pump, nullptr, &PUMP_TOKEN);
		}
	}

	void ReleaseWaveforms() {
		FrameStages::UnregisterOwnedBy(&PUMP_TOKEN);
		if (!_state) {
			return;
		}
		for (Waveform* entry : _state->entries) {
			Discard(entry);
		}
		Delete(_state);
		_state = nullptr;
	}

} // namespace Shalltear::AudioEditor
