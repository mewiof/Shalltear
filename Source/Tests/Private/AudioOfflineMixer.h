#pragma once

#ifndef SHALLTEAR_SHARED

#include "AudioBuiltinSpatializer.h"
#include "AudioClipData.h"
#include "AudioMessages.h"
#include "AudioRenderer.h"

#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace AudioTesting {

	using namespace Shalltear;
	using namespace Shalltear::AudioImpl;

	class BusBlock {
	public:
		static constexpr size_t BYTES = BUS_ROWS_BYTES + static_cast<size_t>(MAX_AUDIO_CHANNELS) * AudioRenderer::BLOCK_FRAMES * sizeof(f32);

		BusBlock() : _bytes(static_cast<u8*>(::operator new(BYTES, std::align_val_t{64}))) {
			std::memset(_bytes, 0, BYTES);
			for (u32 row = 0; row < MAX_BUS_ROWS; ++row) {
				new (&BusRowsOf(_bytes)[row]) AudioBusRow();
			}
		}
		~BusBlock() { ::operator delete(_bytes, std::align_val_t{64}); }
		BusBlock(const BusBlock&) = delete;
		BusBlock& operator=(const BusBlock&) = delete;

		void* Get() { return _bytes; }
	private:
		u8* _bytes;
	};

	class ClipBlock {
	public:
		explicit ClipBlock(AudioClipData* data) : _data(data) {}
		~ClipBlock() { ReleaseClipData(_data); }
		ClipBlock(const ClipBlock&) = delete;
		ClipBlock& operator=(const ClipBlock&) = delete;

		const AudioClipData* Get() const { return _data; }
		explicit operator bool() const { return _data != nullptr; }
	private:
		AudioClipData* _data = nullptr;
	};

	inline AudioClipData* MakePcm(const std::vector<f32>& interleaved, u32 channels, u32 rate) {
		return CreatePcmClipData(interleaved.data(), interleaved.size() / channels, MakeStandardLayout(channels), rate);
	}

	struct OfflineBus {
		u32 slot = AudioRenderer::ROOT_BUS;
		u32 generation = 1;
	};

	class OfflineMixer {
	public:
		static constexpr u32 BLOCK = AudioRenderer::BLOCK_FRAMES;

		explicit OfflineMixer(u32 channels = 2, u32 rate = 48000, bool openGate = true, u32 voices = AudioRenderer::DEFAULT_VOICES,
							  u32 decoders = AudioRenderer::DEFAULT_DECODERS)
			: _channels(channels) {
			_renderer.Initialize(voices, decoders);
			_renderer.Prepare(MakeStandardLayout(channels), rate);
			if (openGate) {
				SetListener(true);
				Render(BLOCK);
			}
		}

		~OfflineMixer() {
			_renderer.Release();
			_renderer.Shutdown();
		}

		OfflineBus Bus(u32 slot, OfflineBus parent = {}, f32 fader = 1.0f, f32 pitch = 1.0f,
					   AudioBusClock clock = AudioBusClock::Wall, bool followTimeScale = false) {
			_blocks.push_back(std::make_unique<BusBlock>());
			const OfflineBus bus{slot, ++_busGeneration};
			AudioCommand command = BusCommand(AudioCommandKind::CreateBus, bus);
			command.other = parent.slot;
			command.otherGeneration = parent.generation;
			command.block = _blocks.back()->Get();
			command.mode = static_cast<u8>(clock);
			command.flag = followTimeScale;
			command.volume = fader;
			command.pitch = pitch;
			Push(command);
			return bus;
		}

		static AudioCommand BusCommand(AudioCommandKind kind, OfflineBus bus, f32 value = 0.0f, u32 frames = 0) {
			AudioCommand command;
			command.kind = kind;
			command.bus = bus.slot;
			command.busGeneration = bus.generation;
			command.value = value;
			command.frames = frames;
			return command;
		}

		static AudioCommand Row(OfflineBus bus, u32 index, AudioBusRowKind kind, OfflineBus target = {}, f32 level = 0.0f, IAudioEffect* effect = nullptr,
								bool bypass = false, bool wetMixing = false, u8 row = NO_BUS_ROW) {
			AudioCommand command = BusCommand(AudioCommandKind::SetBusRow, bus, level);
			command.mode = static_cast<u8>(index);
			command.frames = static_cast<u32>(kind);
			command.other = target.slot;
			command.otherGeneration = target.generation;
			command.effect = effect;
			command.flag = bypass;
			command.loop = wetMixing;
			command.frame = row;
			return command;
		}

		bool Receives(OfflineBus bus, u32 count) {
			_receives.push_back(std::make_unique<std::vector<f32>>(static_cast<size_t>(count) * BUS_BUFFER_SAMPLES, 0.0f));
			AudioCommand command = BusCommand(AudioCommandKind::SetBusReceives, bus);
			command.block = _receives.back()->data();
			command.mode = static_cast<u8>(count);
			return Push(command);
		}

		static AudioCommand RowCount(OfflineBus bus, u32 count) {
			AudioCommand command = BusCommand(AudioCommandKind::SetBusRowCount, bus);
			command.mode = static_cast<u8>(count);
			return command;
		}

		bool Send(OfflineBus bus, OfflineBus target, bool preFader, f32 level) {
			const AudioBusRowKind first = preFader ? AudioBusRowKind::Send : AudioBusRowKind::Fader;
			const AudioBusRowKind second = preFader ? AudioBusRowKind::Fader : AudioBusRowKind::Send;
			return Push(Row(bus, 0, first, target, level)) && Push(Row(bus, 1, second, target, level)) && Push(RowCount(bus, 2));
		}

		static AudioCommand Flag(AudioCommandKind kind, OfflineBus bus, bool flag) {
			AudioCommand command = BusCommand(kind, bus);
			command.flag = flag;
			return command;
		}

		bool GameClock(bool running, f32 timeScale) {
			AudioCommand command;
			command.kind = AudioCommandKind::SetGameClock;
			command.flag = running;
			command.value = timeScale;
			return Push(command);
		}

		static AudioCommand Into(AudioCommand play, OfflineBus bus) {
			play.bus = bus.slot;
			play.busGeneration = bus.generation;
			return play;
		}

		OfflineMixer(const OfflineMixer&) = delete;
		OfflineMixer& operator=(const OfflineMixer&) = delete;

		bool Push(const AudioCommand& command) { return _renderer.GetCommands().TryPush(command); }

		void SetListener(bool present) {
			AudioCommand command;
			command.kind = AudioCommandKind::SetListenerPresent;
			command.value = present ? 1.0f : 0.0f;
			Push(command);
		}

		AudioCommand Play(u32 slot, const AudioClipData* clip, u64 at = 0, u64 frame = 0) {
			AudioCommand command;
			command.kind = AudioCommandKind::Play;
			command.voice = slot;
			command.generation = ++_generation;
			command.clip = clip;
			command.at = at;
			command.frame = frame;
			return command;
		}

		static AudioCommand To(const AudioCommand& play, AudioCommandKind kind, f32 value = 0.0f, u64 at = 0) {
			AudioCommand command;
			command.kind = kind;
			command.voice = play.voice;
			command.generation = play.generation;
			command.value = value;
			command.at = at;
			return command;
		}

		AudioSpatializerContext ContextFor(const AudioClipData* clip) {
			AudioSpatializerContext context;
			context.sampleRate = _renderer.GetSampleRate();
			context.blockFrames = BLOCK;
			context.inputChannels = clip->layout.count;
			for (u32 channel = 0; channel < clip->layout.count; ++channel) {
				context.inputSpeakers[channel] = clip->layout.positions[channel];
			}
			const ChannelLayout& output = _renderer.GetLayout();
			context.outputChannels = output.count;
			for (u32 channel = 0; channel < output.count; ++channel) {
				context.outputSpeakers[channel] = output.positions[channel];
			}
			return context;
		}

		void Place(const AudioCommand& play, AudioVoicePlacement placement) {
			placement.generation = play.generation;
			_renderer.PostPlacement(play.voice, placement);
		}

		std::vector<f32> Render(u32 frames) {
			std::vector<f32> out(static_cast<size_t>(frames) * _channels, 0.0f);
			_renderer.Render(out.data(), frames);
			_delivered += frames;
			return out;
		}

		u64 Now() const { return _delivered; }

		std::vector<AudioEvent> TakeEvents() {
			std::vector<AudioEvent> events;
			AudioEvent event;
			while (_renderer.GetEvents().TryPop(event)) {
				events.push_back(event);
			}
			return events;
		}

		AudioRenderer& Get() { return _renderer; }
		u32 GetChannels() const { return _channels; }
	private:
		AudioRenderer _renderer;
		u32 _channels = 0;
		u32 _generation = 0;
		u32 _busGeneration = 1;
		u64 _delivered = 0;
		std::vector<std::unique_ptr<BusBlock>> _blocks;
		std::vector<std::unique_ptr<std::vector<f32>>> _receives;
	};

	inline f32 At(const std::vector<f32>& out, u32 channels, u64 frame, u32 channel = 0) {
		return out[static_cast<size_t>(frame) * channels + channel];
	}

} // namespace AudioTesting

#endif
