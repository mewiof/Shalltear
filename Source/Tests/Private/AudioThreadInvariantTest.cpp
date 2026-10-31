#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioBuiltinSpatializer.h"
#include "AudioClipData.h"
#include "AudioDevice.h"
#include "AudioImportOps.h"
#include "AudioMessages.h"
#include "AudioOfflineMixer.h"
#include "AudioRenderer.h"
#include "AudioStream.h"
#include "AudioStreamer.h"
#include "Effects/BuiltinEffects.h"
#include "ThreadAllocationWatch.h"

#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioGenerator.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

namespace {

	struct ScopedWatch {
		explicit ScopedWatch(std::thread::id thread) { ThreadAllocationWatch::Watch(thread); }
		~ScopedWatch() { ThreadAllocationWatch::Stop(); }
	};

	bool WaitForRenderThread(const AudioRenderer& renderer, std::thread::id& out) {
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
		for (;;) {
			out = renderer.GetCounters().renderThread.load(std::memory_order_relaxed);
			if (out != std::thread::id{}) {
				return true;
			}
			if (std::chrono::steady_clock::now() > deadline) {
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}

	void WatchALiveDevice(AudioBackendKind backend, std::chrono::milliseconds window, u64& allocations, u64& blocks, bool& opened) {
		AudioRenderer renderer;
		AudioDevice device;
		opened = device.Open(backend, 10, renderer);
		if (!opened) {
			return;
		}
		std::thread::id renderThread;
		ASSERT_TRUE(WaitForRenderThread(renderer, renderThread));

		const u64 blocksBefore = renderer.GetCounters().blocks.load();
		{
			ScopedWatch watch(renderThread);
			std::this_thread::sleep_for(window);
			allocations = ThreadAllocationWatch::GetCount();
		}
		blocks = renderer.GetCounters().blocks.load() - blocksBefore;

		EXPECT_EQ(renderer.GetCounters().renderThread.load(), renderThread);
		device.Close();
	}

} // namespace

TEST(AudioThreadInvariantTest, WatchSeesEveryAllocation) {
	std::atomic<bool> watching{false};
	std::atomic<bool> done{false};
	std::thread worker([&]() {
		while (!watching.load()) {
			std::this_thread::yield();
		}
		int* raw = new int(7);
		delete raw;
		void* tagged = Allocate(64, MemTag::Audio);
		Free(tagged);
		Vector<u32> grown{GetMemoryResource(MemTag::Audio)};
		for (u32 value = 0; value < 64; ++value) {
			grown.push_back(value);
		}
		done.store(true);
	});

	{
		ScopedWatch watch(worker.get_id());
		std::vector<int> unwatched(1024, 1);
		(void)unwatched;
		watching.store(true);
		while (!done.load()) {
			std::this_thread::yield();
		}
		EXPECT_GE(ThreadAllocationWatch::GetCount(), 3u);
	}
	worker.join();
}

TEST(AudioThreadInvariantTest, BusyBlocksAllocateNothing) {
	DecodedAudio source;
	source.layout = MakeStandardLayout(2);
	source.sampleRate = 44100;
	const u32 frames = 5120 * 3 + 77;
	source.samples.resize(static_cast<size_t>(frames) * 2);
	for (u32 frame = 0; frame < frames; ++frame) {
		source.samples[static_cast<size_t>(frame) * 2] = 0.4f * std::sin(static_cast<f32>(frame) * 0.031f);
		source.samples[static_cast<size_t>(frame) * 2 + 1] = 0.3f * std::sin(static_cast<f32>(frame) * 0.017f);
	}
	str error;
	AudioClipData* clips[2] = {Encode(source, AudioCompressionFormat::PCM, 0.7f, error), Encode(source, AudioCompressionFormat::QOA, 0.7f, error)};
	ASSERT_NE(clips[0], nullptr) << error;
	ASSERT_NE(clips[1], nullptr) << error;

	AudioRenderer renderer;
	renderer.Initialize();
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(480 * 2, 0.0f);

	constexpr u64 BLOCKS = 10000;
	std::atomic<bool> go{false};
	std::atomic<u64> allocations{~0ull};
	std::thread renderThread([&]() {
		while (!go.load()) {
			std::this_thread::yield();
		}
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			renderer.Render(output.data(), 480);
		}
		allocations.store(ThreadAllocationWatch::GetCount());
	});

	constexpr u32 VOICES = 32;
	u32 generations[VOICES]{};
	bool live[VOICES]{};
	u64 sent = 0;
	u64 ended = 0;
	const auto push = [&](const AudioCommand& command) {
		if (renderer.GetCommands().TryPush(command)) {
			++sent;
			return true;
		}
		return false;
	};
	const auto play = [&](u32 slot, u64 step) {
		AudioCommand command;
		command.kind = AudioCommandKind::Play;
		command.voice = slot;
		command.generation = generations[slot] + 1;
		command.clip = clips[slot % 2];
		command.loop = slot % 3 != 0;
		command.frame = (step * 7919) % frames;
		command.pitch = 0.5f + 0.05f * static_cast<f32>(slot);
		command.stereoPan = -1.0f + 2.0f * static_cast<f32>(slot) / static_cast<f32>(VOICES);
		if (push(command)) {
			generations[slot] = command.generation;
			live[slot] = true;
		}
	};
	const auto to = [&](u32 slot, AudioCommandKind kind, f32 value = 0.0f, u64 frame = 0) {
		AudioCommand command;
		command.kind = kind;
		command.voice = slot;
		command.generation = generations[slot];
		command.value = value;
		command.frame = frame;
		push(command);
	};

	{
		ScopedWatch watch(renderThread.get_id());
		AudioCommand gate;
		gate.kind = AudioCommandKind::SetListenerPresent;
		gate.value = 1.0f;
		push(gate);
		for (u32 slot = 0; slot < VOICES; ++slot) {
			play(slot, slot);
		}
		go.store(true);

		u64 step = 0;
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			AudioEvent event;
			while (renderer.GetEvents().TryPop(event)) {
				if (event.kind == AudioEventKind::VoiceEnded && event.slot < VOICES && event.generation == generations[event.slot]) {
					live[event.slot] = false;
					++ended;
				}
			}
			const u32 slot = static_cast<u32>(step % VOICES);
			if (!live[slot]) {
				play(slot, step);
			} else {
				switch (step % 8) {
				case 0:
					to(slot, AudioCommandKind::SetPitch, slot % 2 == 0 ? -2.5f + 0.1f * static_cast<f32>(step % 50) : 0.3f + 0.05f * static_cast<f32>(step % 50));
					break;
				case 1:
					to(slot, AudioCommandKind::SetStereoPan, -1.0f + 0.04f * static_cast<f32>(step % 50));
					break;
				case 2:
					to(slot, AudioCommandKind::Seek, 0.0f, (step * 997) % frames);
					break;
				case 3:
					to(slot, AudioCommandKind::Pause);
					break;
				case 4:
					to(slot, AudioCommandKind::Resume);
					break;
				case 5:
					to(slot, AudioCommandKind::SetVolume, 0.02f * static_cast<f32>(step % 50));
					break;
				case 6:
					to(slot, AudioCommandKind::SetLoop, (step / 8) % 2 == 0 ? 1.0f : 0.0f);
					break;
				case 7:
					to(slot, AudioCommandKind::Stop);
					break;
				}
			}
			if (step % 997 == 0) {
				gate.value = (step / 997) % 2 == 0 ? 0.0f : 1.0f;
				push(gate);
			}
			++step;
		}
		renderThread.join();
	}

	EXPECT_GE(renderer.GetCounters().blocks.load(), BLOCKS);
	EXPECT_EQ(allocations.load(), 0u);
	EXPECT_GT(sent, 10000u);
	EXPECT_GT(ended, 100u);
	EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

	renderer.Release();
	renderer.Shutdown();
	ReleaseClipData(clips[0]);
	ReleaseClipData(clips[1]);
}

TEST(AudioThreadInvariantTest, StreamedVoicesAllocateNothing) {
	DecodedAudio source;
	source.layout = MakeStandardLayout(2);
	source.sampleRate = 48000;
	const u32 frames = 5120 * 12 + 77;
	source.samples.resize(static_cast<size_t>(frames) * 2);
	for (u32 frame = 0; frame < frames; ++frame) {
		source.samples[static_cast<size_t>(frame) * 2] = 0.4f * std::sin(static_cast<f32>(frame) * 0.029f);
		source.samples[static_cast<size_t>(frame) * 2 + 1] = 0.3f * std::sin(static_cast<f32>(frame) * 0.013f);
	}
	str error;
	constexpr u32 CODECS = 3;
	AudioClipData* resident[CODECS] = {Encode(source, AudioCompressionFormat::PCM, 0.7f, error), Encode(source, AudioCompressionFormat::QOA, 0.7f, error),
									   Encode(source, AudioCompressionFormat::Opus, 0.7f, error)};
	for (AudioClipData* clip : resident) {
		ASSERT_NE(clip, nullptr) << error;
	}
	AudioClipData* clips[CODECS]{};
	for (u32 index = 0; index < CODECS; ++index) {
		const u8* bytes = nullptr;
		u64 size = 0;
		GetPayload(*resident[index], bytes, size);
		clips[index] = CreateStreamedClipData(resident[index]->encoding, bytes, size, resident[index]->layout, 48000, frames, 8192);
		ASSERT_NE(clips[index], nullptr);
	}

	for (u32 watched = 0; watched < 2; ++watched) {
		AudioRenderer renderer;
		renderer.Initialize();
		ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
		AudioStreamer streamer;
		streamer.Start(64, false);
		std::vector<f32> output(480 * 2, 0.0f);

		constexpr u64 BLOCKS = 10000;
		std::atomic<bool> go{false};
		std::atomic<bool> rendered{false};
		std::atomic<u64> allocations{~0ull};
		std::thread renderThread([&]() {
			while (!go.load()) {
				std::this_thread::yield();
			}
			while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
				renderer.Render(output.data(), 480);
			}
			if (watched == 0) {
				allocations.store(ThreadAllocationWatch::GetCount());
			}
			rendered.store(true);
		});
		std::thread streamThread([&]() {
			while (!go.load()) {
				std::this_thread::yield();
			}
			while (!rendered.load()) {
				streamer.Pump();
				std::this_thread::yield();
			}
			if (watched == 1) {
				allocations.store(ThreadAllocationWatch::GetCount());
			}
		});

		constexpr u32 VOICES = 8;
		u32 generations[VOICES]{};
		bool live[VOICES]{};
		AudioStream* streams[VOICES]{};
		std::vector<AudioStream*> retired;
		u64 ended = 0;
		u64 freed = 0;
		const auto push = [&](const AudioCommand& command) { return renderer.GetCommands().TryPush(command); };
		const auto play = [&](u32 slot, u64 step) {
			AudioStream* stream = CreateStream(clips[(slot + step) % CODECS], 32 * AudioRenderer::BLOCK_FRAMES);
			AudioCommand command;
			command.kind = AudioCommandKind::Play;
			command.voice = slot;
			command.generation = generations[slot] + 1;
			command.clip = stream->clip;
			command.loop = slot % 3 != 0;
			command.frame = (step * 7919) % frames;
			command.pitch = slot % 4 == 3 ? -1.2f : 0.6f + 0.1f * static_cast<f32>(slot);
			AudioCommand attach;
			attach.kind = AudioCommandKind::SetVoiceStream;
			attach.voice = slot;
			attach.generation = command.generation;
			attach.stream = stream;
			if (push(command) && push(attach)) {
				generations[slot] = command.generation;
				live[slot] = true;
				streams[slot] = stream;
				streamer.Add(stream);
			} else {
				DestroyStream(stream);
			}
		};
		const auto to = [&](u32 slot, AudioCommandKind kind, f32 value = 0.0f, u64 frame = 0) {
			AudioCommand command;
			command.kind = kind;
			command.voice = slot;
			command.generation = generations[slot];
			command.value = value;
			command.frame = frame;
			push(command);
		};

		{
			ScopedWatch watch(watched == 0 ? renderThread.get_id() : streamThread.get_id());
			AudioCommand gate;
			gate.kind = AudioCommandKind::SetListenerPresent;
			gate.value = 1.0f;
			push(gate);
			for (u32 slot = 0; slot < VOICES; ++slot) {
				play(slot, slot);
			}
			go.store(true);

			u64 step = 0;
			while (!rendered.load()) {
				AudioEvent event;
				while (renderer.GetEvents().TryPop(event)) {
					if (event.kind == AudioEventKind::VoiceEnded && event.slot < VOICES && event.generation == generations[event.slot]) {
						live[event.slot] = false;
						++ended;
						streams[event.slot]->retiring.store(true);
						retired.push_back(streams[event.slot]);
						streams[event.slot] = nullptr;
					}
				}
				std::erase_if(retired, [&](AudioStream* stream) {
					if (!stream->released.load()) {
						return false;
					}
					DestroyStream(stream);
					++freed;
					return true;
				});
				const u32 slot = static_cast<u32>(step % VOICES);
				if (!live[slot]) {
					play(slot, step);
				} else {
					switch (step % 6) {
					case 0:
						to(slot, AudioCommandKind::Seek, 0.0f, (step * 997) % frames);
						break;
					case 1:
						to(slot, AudioCommandKind::SetPitch, step % 12 < 6 ? 1.3f : -0.8f);
						break;
					case 2:
						to(slot, AudioCommandKind::SetLoop, (step / 6) % 2 == 0 ? 1.0f : 0.0f);
						break;
					case 3:
						to(slot, AudioCommandKind::SetVolume, 0.02f * static_cast<f32>(step % 50));
						break;
					case 4:
						break;
					case 5:
						to(slot, AudioCommandKind::Stop);
						break;
					}
				}
				++step;
			}
			renderThread.join();
			streamThread.join();
		}

		EXPECT_EQ(allocations.load(), 0u) << (watched == 0 ? "the render path allocated reading streams" : "the streaming thread allocated");
		EXPECT_GT(ended, 20u);
		EXPECT_GT(freed, 0u);
		EXPECT_EQ(streamer.GetFailures(), 0u);
		EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

		streamer.Stop();
		for (AudioStream* stream : retired) {
			DestroyStream(stream);
		}
		for (AudioStream* stream : streams) {
			DestroyStream(stream);
		}
		renderer.Release();
		renderer.Shutdown();
	}
	for (u32 index = 0; index < CODECS; ++index) {
		ReleaseClipData(resident[index]);
		ReleaseClipData(clips[index]);
	}
}

TEST(AudioThreadInvariantTest, ChurningBusGraphAllocatesNothing) {
	DecodedAudio source;
	source.layout = MakeStandardLayout(2);
	source.sampleRate = 48000;
	const u32 frames = 9000;
	source.samples.resize(static_cast<size_t>(frames) * 2);
	for (u32 frame = 0; frame < frames; ++frame) {
		source.samples[static_cast<size_t>(frame) * 2] = 0.2f * std::sin(static_cast<f32>(frame) * 0.029f);
		source.samples[static_cast<size_t>(frame) * 2 + 1] = 0.2f * std::sin(static_cast<f32>(frame) * 0.011f);
	}
	str error;
	AudioClipData* clip = Encode(source, AudioCompressionFormat::PCM, 0.7f, error);
	ASSERT_NE(clip, nullptr) << error;

	AudioRenderer renderer;
	renderer.Initialize();
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(480 * 2, 0.0f);

	constexpr u32 BUSES = 12;
	AudioTesting::BusBlock blocks[BUSES + 1];

	constexpr u64 BLOCKS = 10000;
	std::atomic<bool> go{false};
	std::atomic<u64> allocations{~0ull};
	std::thread renderThread([&]() {
		while (!go.load()) {
			std::this_thread::yield();
		}
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			renderer.Render(output.data(), 480);
		}
		allocations.store(ThreadAllocationWatch::GetCount());
	});

	u32 busGenerations[BUSES + 1]{};
	bool busLive[BUSES + 1]{};
	bool busPending[BUSES + 1]{};
	constexpr u32 VOICES = 24;
	u32 voiceGenerations[VOICES]{};
	bool voiceLive[VOICES]{};
	u64 sent = 0;
	u64 released = 0;
	const auto push = [&](const AudioCommand& command) {
		if (renderer.GetCommands().TryPush(command)) {
			++sent;
			return true;
		}
		return false;
	};
	const auto busCommand = [&](AudioCommandKind kind, u32 slot) {
		AudioCommand command;
		command.kind = kind;
		command.bus = slot;
		command.busGeneration = busGenerations[slot];
		return command;
	};

	{
		ScopedWatch watch(renderThread.get_id());
		AudioCommand gate;
		gate.kind = AudioCommandKind::SetListenerPresent;
		gate.value = 1.0f;
		push(gate);
		go.store(true);

		u64 step = 0;
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			AudioEvent event;
			while (renderer.GetEvents().TryPop(event)) {
				if (event.kind == AudioEventKind::VoiceEnded && event.slot < VOICES && event.generation == voiceGenerations[event.slot]) {
					voiceLive[event.slot] = false;
				} else if (event.kind == AudioEventKind::BusReleased && event.slot <= BUSES && event.generation == busGenerations[event.slot]) {
					busPending[event.slot] = false;
					++released;
				}
			}

			const u32 bus = 1 + static_cast<u32>(step % BUSES);
			const u32 other = 1 + static_cast<u32>((step * 7) % BUSES);
			if (!busLive[bus]) {
				if (!busPending[bus]) {
					AudioCommand create = busCommand(AudioCommandKind::CreateBus, bus);
					create.busGeneration = ++busGenerations[bus];
					const u32 parent = bus > 1 && busLive[bus - 1] ? bus - 1 : 0;
					create.other = parent;
					create.otherGeneration = parent == 0 ? 1 : busGenerations[parent];
					create.block = blocks[bus].Get();
					create.mode = static_cast<u8>(step % 2);
					create.flag = step % 5 == 0;
					create.volume = 0.8f;
					create.pitch = step % 3 == 0 ? 1.25f : 1.0f;
					if (push(create)) {
						busLive[bus] = true;
					}
				}
			} else {
				switch (step % 11) {
				case 0: {
					AudioCommand command = busCommand(AudioCommandKind::DestroyBus, bus);
					if (push(command)) {
						busLive[bus] = false;
						busPending[bus] = true;
					}
					break;
				}
				case 1: {
					AudioCommand command = busCommand(AudioCommandKind::SetBusFader, bus);
					command.value = 0.1f * static_cast<f32>(step % 10);
					command.frames = static_cast<u32>(step % 700);
					push(command);
					break;
				}
				case 2: {
					AudioCommand command = busCommand(AudioCommandKind::SetBusPitch, bus);
					command.value = 0.5f + 0.1f * static_cast<f32>(step % 15);
					command.frames = 256;
					push(command);
					break;
				}
				case 3:
					if (other < bus && busLive[other]) {
						const bool preFader = step % 2 == 0;
						AudioCommand command = busCommand(AudioCommandKind::SetBusRow, bus);
						command.mode = preFader ? 0 : 1;
						command.frames = static_cast<u32>(AudioBusRowKind::Send);
						command.other = other;
						command.otherGeneration = busGenerations[other];
						command.value = 0.3f;
						push(command);
						if (preFader) {
							AudioCommand fader = busCommand(AudioCommandKind::SetBusRow, bus);
							fader.mode = 1;
							fader.frames = static_cast<u32>(AudioBusRowKind::Fader);
							push(fader);
						}
					}
					break;
				case 4: {
					AudioCommand command = busCommand(AudioCommandKind::SetBusRowCount, bus);
					command.mode = 1;
					push(command);
					break;
				}
				case 5: {
					AudioCommand command = busCommand(AudioCommandKind::SetBusMute, bus);
					command.flag = (step / 11) % 2 == 0;
					push(command);
					break;
				}
				case 6: {
					AudioCommand command = busCommand(AudioCommandKind::SetBusSolo, bus);
					command.flag = (step / 11) % 3 == 0;
					push(command);
					break;
				}
				case 7: {
					AudioCommand command = busCommand(AudioCommandKind::SetBusClock, bus);
					command.mode = static_cast<u8>((step / 11) % 2);
					command.flag = (step / 11) % 4 == 0;
					push(command);
					break;
				}
				case 8:
					if (other < bus) {
						AudioCommand command = busCommand(AudioCommandKind::SetBusParent, bus);
						command.other = busLive[other] ? other : 0;
						command.otherGeneration = busLive[other] ? busGenerations[other] : 1;
						push(command);
					}
					break;
				case 9: {
					AudioCommand command;
					command.kind = AudioCommandKind::SetGameClock;
					command.flag = (step / 11) % 7 != 0;
					command.value = static_cast<f32>((step / 11) % 3) * 0.5f;
					push(command);
					break;
				}
				case 10: {
					AudioCommand command;
					command.kind = AudioCommandKind::SetGlobalVolume;
					command.value = 0.5f + 0.05f * static_cast<f32>(step % 10);
					push(command);
					break;
				}
				}
			}

			const u32 voice = static_cast<u32>(step % VOICES);
			if (!voiceLive[voice]) {
				AudioCommand play;
				play.kind = AudioCommandKind::Play;
				play.voice = voice;
				play.generation = voiceGenerations[voice] + 1;
				play.clip = clip;
				play.loop = voice % 2 == 0;
				play.bus = 1 + (voice % BUSES);
				play.busGeneration = busGenerations[play.bus];
				if (push(play)) {
					voiceGenerations[voice] = play.generation;
					voiceLive[voice] = true;
				}
			} else if (step % 13 == 0) {
				AudioCommand move;
				move.kind = AudioCommandKind::SetVoiceOutput;
				move.voice = voice;
				move.generation = voiceGenerations[voice];
				move.bus = other;
				move.busGeneration = busGenerations[other];
				push(move);
			} else if (step % 17 == 0) {
				AudioCommand stop;
				stop.kind = AudioCommandKind::Stop;
				stop.voice = voice;
				stop.generation = voiceGenerations[voice];
				push(stop);
			}
			++step;
		}
		renderThread.join();
	}

	EXPECT_GE(renderer.GetCounters().blocks.load(), BLOCKS);
	EXPECT_EQ(allocations.load(), 0u);
	EXPECT_GT(sent, 10000u);
	EXPECT_GT(released, 100u);
	EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

	renderer.Release();
	renderer.Shutdown();
	ReleaseClipData(clip);
}

TEST(AudioThreadInvariantTest, PlacedVoicesAllocateNothing) {
	DecodedAudio source;
	source.layout = MakeStandardLayout(2);
	source.sampleRate = 44100;
	const u32 frames = 7000;
	source.samples.resize(static_cast<size_t>(frames) * 2);
	for (u32 frame = 0; frame < frames; ++frame) {
		source.samples[static_cast<size_t>(frame) * 2] = 0.2f * std::sin(static_cast<f32>(frame) * 0.023f);
		source.samples[static_cast<size_t>(frame) * 2 + 1] = 0.2f * std::sin(static_cast<f32>(frame) * 0.013f);
	}
	str error;
	AudioClipData* clip = Encode(source, AudioCompressionFormat::PCM, 0.7f, error);
	ASSERT_NE(clip, nullptr) << error;

	for (const u32 outputs : {2u, 6u}) {
		AudioRenderer renderer;
		renderer.Initialize();
		ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(outputs), 48000));
		std::vector<f32> output(static_cast<size_t>(480) * outputs, 0.0f);

		constexpr u32 VOICES = 32;
		AudioSpatializerContext context;
		context.sampleRate = 48000;
		context.blockFrames = AudioRenderer::BLOCK_FRAMES;
		context.inputChannels = clip->layout.count;
		for (u32 channel = 0; channel < clip->layout.count; ++channel) {
			context.inputSpeakers[channel] = clip->layout.positions[channel];
		}
		context.outputChannels = renderer.GetLayout().count;
		for (u32 channel = 0; channel < renderer.GetLayout().count; ++channel) {
			context.outputSpeakers[channel] = renderer.GetLayout().positions[channel];
		}
		void* states[VOICES];
		for (u32 voice = 0; voice < VOICES; ++voice) {
			states[voice] = GetBuiltinSpatializer().CreateVoiceState(context);
		}

		constexpr u64 BLOCKS = 10000;
		std::atomic<bool> go{false};
		std::atomic<u64> allocations{~0ull};
		std::thread renderThread([&]() {
			while (!go.load()) {
				std::this_thread::yield();
			}
			while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
				renderer.Render(output.data(), 480);
			}
			allocations.store(ThreadAllocationWatch::GetCount());
		});

		u32 generations[VOICES]{};
		bool live[VOICES]{};
		u64 placed = 0;
		const auto placement = [&](u32 voice, u64 step) {
			const f32 angle = 0.01f * static_cast<f32>(step) + 0.2f * static_cast<f32>(voice);
			AudioVoicePlacement next;
			next.generation = generations[voice];
			next.spatializer = &GetBuiltinSpatializer();
			next.state = states[voice];
			next.params.position = Vec3(4.0f * std::sin(angle), 2.0f * std::sin(angle * 0.7f), static_cast<f32>(step % 9) * std::cos(angle));
			next.params.spread = static_cast<f32>((step * 13 + voice * 29) % 361);
			next.params.occlusion = static_cast<f32>((step + voice) % 5) * 0.25f;
			next.params.distanceAttenuation = 0.2f + 0.1f * static_cast<f32>(step % 8);
			next.blend = static_cast<f32>((step / 7 + voice) % 5) * 0.25f;
			next.gain = next.params.distanceAttenuation;
			next.doppler = 0.5f + 0.1f * static_cast<f32>((step + voice) % 15);
			return next;
		};

		{
			ScopedWatch watch(renderThread.get_id());
			AudioCommand gate;
			gate.kind = AudioCommandKind::SetListenerPresent;
			gate.value = 1.0f;
			renderer.GetCommands().TryPush(gate);
			go.store(true);

			u64 step = 0;
			while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
				AudioEvent event;
				while (renderer.GetEvents().TryPop(event)) {
					if (event.kind == AudioEventKind::VoiceEnded && event.slot < VOICES && event.generation == generations[event.slot]) {
						live[event.slot] = false;
					}
				}
				const u32 voice = static_cast<u32>(step % VOICES);
				if (!live[voice]) {
					AudioCommand play;
					play.kind = AudioCommandKind::Play;
					play.voice = voice;
					play.generation = generations[voice] + 1;
					play.clip = clip;
					play.loop = voice % 4 != 0;
					generations[voice] = play.generation;
					renderer.PostPlacement(voice, placement(voice, step));
					live[voice] = renderer.GetCommands().TryPush(play);
				} else {
					renderer.PostPlacement(voice, placement(voice, step));
					++placed;
					if (step % 23 == 0) {
						AudioCommand stop;
						stop.kind = AudioCommandKind::Stop;
						stop.voice = voice;
						stop.generation = generations[voice];
						renderer.GetCommands().TryPush(stop);
					}
				}
				++step;
			}
			renderThread.join();
		}

		EXPECT_GE(renderer.GetCounters().blocks.load(), BLOCKS);
		EXPECT_EQ(allocations.load(), 0u);
		EXPECT_GT(placed, 10000u);
		EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

		renderer.Release();
		renderer.Shutdown();
		for (u32 voice = 0; voice < VOICES; ++voice) {
			GetBuiltinSpatializer().DestroyVoiceState(states[voice]);
		}
	}
	ReleaseClipData(clip);
}

namespace {

	class InvariantEffect final : public IAudioEffect {
	public:
		void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
			(void)sidechain;
			for (u32 channel = 0; channel < block.channels && channel < AUDIO_MAX_CHANNELS; ++channel) {
				f32* row = block.data + static_cast<size_t>(channel) * block.stride;
				f32 memory = _memory[channel];
				for (u32 frame = 0; frame < block.frames; ++frame) {
					memory += (row[frame] - memory) * _coefficient;
					row[frame] = memory;
				}
				_memory[channel] = memory;
			}
			++processed;
		}
		void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
			if (index == 0) {
				_coefficient = value;
			}
		}
		bool IsQuiet() const [[clang::nonblocking]] override {
			for (f32 memory : _memory) {
				if (memory > 1e-6f || memory < -1e-6f) {
					return false;
				}
			}
			return true;
		}
		void Release() override {}

		u64 processed = 0;
	private:
		f32 _memory[AUDIO_MAX_CHANNELS]{};
		f32 _coefficient = 0.2f;
	};

	class InvariantGenerator final : public IAudioGeneratorInstance {
	public:
		u32 Process(AudioBlock& out, u64 fromFrame) [[clang::nonblocking]] override {
			const u64 left = fromFrame < length ? length - fromFrame : 0;
			const u32 written = left < out.frames ? static_cast<u32>(left) : out.frames;
			for (u32 channel = 0; channel < out.channels; ++channel) {
				for (u32 frame = 0; frame < written; ++frame) {
					out.data[static_cast<size_t>(channel) * out.stride + frame] = 0.1f * std::sin(static_cast<f32>(fromFrame + frame) * step);
				}
			}
			++processed;
			return written;
		}
		void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
			if (index == 0) {
				step = value;
			}
		}
		void Release() override {}

		u64 length = ~0ull;
		f32 step = 0.05f;
		u64 processed = 0;
	};

} // namespace

TEST(AudioThreadInvariantTest, EffectsAndGeneratorsAllocateNothing) {
	AudioRenderer renderer;
	renderer.Initialize();
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(480 * 2, 0.0f);

	constexpr u32 BUSES = 4;
	AudioTesting::BusBlock blocks[BUSES + 1];
	InvariantEffect effects[BUSES + 1][2];
	constexpr u32 VOICES = 16;
	InvariantGenerator generators[VOICES];
	AudioClipData* voiceBlocks[VOICES]{};
	u32 generations[VOICES]{};
	bool live[VOICES]{};
	bool orphaned[VOICES]{};

	u64 sent = 0;
	const auto push = [&](const AudioCommand& command) {
		if (renderer.GetCommands().TryPush(command)) {
			++sent;
			return true;
		}
		return false;
	};
	const auto busCommand = [](AudioCommandKind kind, u32 slot) {
		AudioCommand command;
		command.kind = kind;
		command.bus = slot;
		command.busGeneration = 1;
		return command;
	};
	const auto voiceCommand = [&](AudioCommandKind kind, u32 voice) {
		AudioCommand command;
		command.kind = kind;
		command.voice = voice;
		command.generation = generations[voice];
		return command;
	};

	std::vector<f32> receives(2 * BUS_BUFFER_SAMPLES, 0.0f);
	for (u32 bus = 1; bus <= BUSES; ++bus) {
		AudioCommand create = busCommand(AudioCommandKind::CreateBus, bus);
		create.other = AudioRenderer::ROOT_BUS;
		create.otherGeneration = 1;
		create.block = blocks[bus].Get();
		create.volume = 0.7f;
		create.pitch = 1.0f;
		ASSERT_TRUE(push(create));
		if (bus == BUSES) {
			AudioCommand buffers = busCommand(AudioCommandKind::SetBusReceives, bus);
			buffers.block = receives.data();
			buffers.mode = 2;
			ASSERT_TRUE(push(buffers));
		}
		u32 row = 0;
		AudioCommand effect = busCommand(AudioCommandKind::SetBusRow, bus);
		effect.mode = static_cast<u8>(row++);
		effect.frames = static_cast<u32>(AudioBusRowKind::Effect);
		effect.effect = &effects[bus][0];
		effect.loop = bus % 2 == 0;
		effect.value = 0.5f;
		effect.frame = bus == BUSES ? 0 : NO_BUS_ROW;
		ASSERT_TRUE(push(effect));
		if (bus == BUSES) {
			AudioCommand receive = busCommand(AudioCommandKind::SetBusRow, bus);
			receive.mode = static_cast<u8>(row++);
			receive.frames = static_cast<u32>(AudioBusRowKind::Receive);
			receive.frame = 1;
			ASSERT_TRUE(push(receive));
		}
		if (bus < BUSES) {
			AudioCommand send = busCommand(AudioCommandKind::SetBusRow, bus);
			send.mode = static_cast<u8>(row++);
			send.frames = static_cast<u32>(AudioBusRowKind::Send);
			send.other = BUSES;
			send.otherGeneration = 1;
			send.value = 0.3f;
			send.frame = bus == 1 ? 0 : (bus == 2 ? 1 : NO_BUS_ROW);
			ASSERT_TRUE(push(send));
		}
		AudioCommand fader = busCommand(AudioCommandKind::SetBusRow, bus);
		fader.mode = static_cast<u8>(row);
		fader.frames = static_cast<u32>(AudioBusRowKind::Fader);
		ASSERT_TRUE(push(fader));
	}

	constexpr u64 BLOCKS = 10000;
	std::atomic<bool> go{false};
	std::atomic<u64> allocations{~0ull};
	std::thread renderThread([&]() {
		while (!go.load()) {
			std::this_thread::yield();
		}
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			renderer.Render(output.data(), 480);
		}
		allocations.store(ThreadAllocationWatch::GetCount());
	});

	u64 ended = 0;
	{
		ScopedWatch watch(renderThread.get_id());
		AudioCommand gate;
		gate.kind = AudioCommandKind::SetListenerPresent;
		gate.value = 1.0f;
		push(gate);
		go.store(true);

		u64 step = 0;
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			AudioEvent event;
			while (renderer.GetEvents().TryPop(event)) {
				if (event.kind == AudioEventKind::VoiceEnded && event.slot < VOICES && event.generation == generations[event.slot] && live[event.slot]) {
					live[event.slot] = false;
					ReleaseClipData(voiceBlocks[event.slot]);
					voiceBlocks[event.slot] = nullptr;
					++ended;
				}
			}

			const u32 bus = 1 + static_cast<u32>(step % BUSES);
			switch (step % 6) {
			case 0: {
				AudioCommand command = busCommand(AudioCommandKind::SetBusEffectParameter, bus);
				command.mode = 0;
				command.frames = 0;
				command.value = 0.05f + 0.1f * static_cast<f32>(step % 9);
				push(command);
				break;
			}
			case 1: {
				AudioCommand command = busCommand(AudioCommandKind::SetBusRowBypass, bus);
				command.mode = 0;
				command.flag = (step / 6) % 2 == 0;
				push(command);
				break;
			}
			case 2: {
				AudioCommand command = busCommand(AudioCommandKind::SetBusBypassEffects, bus);
				command.flag = (step / 6) % 3 == 0;
				push(command);
				break;
			}
			case 3: {
				AudioCommand command = busCommand(AudioCommandKind::SetBusRowLevel, bus);
				command.mode = 0;
				command.value = 0.25f * static_cast<f32>(step % 5);
				command.frames = 300;
				push(command);
				break;
			}
			case 4: {
				AudioCommand command = busCommand(AudioCommandKind::SetBusRowEffect, bus);
				command.mode = 0;
				command.effect = (step / 6) % 5 == 0 ? nullptr : &effects[bus][(step / 6) % 2];
				push(command);
				break;
			}
			case 5: {
				AudioCommand command = busCommand(AudioCommandKind::SetBusFader, bus);
				command.value = 0.1f * static_cast<f32>(step % 10);
				command.frames = static_cast<u32>(step % 700);
				push(command);
				break;
			}
			}

			const u32 voice = static_cast<u32>(step % VOICES);
			if (!live[voice] && !voiceBlocks[voice]) {
				InvariantGenerator& generator = generators[voice];
				generator.length = voice % 3 == 0 ? 3000 + step % 5000 : ~0ull;
				voiceBlocks[voice] = CreateGeneratorClipData(voice % 2 == 0 ? 48000 : 24000, MakeStandardLayout(1 + voice % 2), &generator);
				AudioCommand play;
				play.kind = AudioCommandKind::Play;
				play.voice = voice;
				play.generation = generations[voice] + 1;
				play.clip = voiceBlocks[voice];
				play.loop = voice % 4 == 0;
				play.bus = 1 + voice % BUSES;
				play.busGeneration = 1;
				if (push(play)) {
					generations[voice] = play.generation;
					live[voice] = true;
					orphaned[voice] = false;
				} else {
					ReleaseClipData(voiceBlocks[voice]);
					voiceBlocks[voice] = nullptr;
				}
			} else if (live[voice]) {
				switch (step % 5) {
				case 0: {
					AudioCommand command = voiceCommand(AudioCommandKind::SetGeneratorParameter, voice);
					command.frames = 0;
					command.value = 0.01f + 0.01f * static_cast<f32>(step % 7);
					push(command);
					break;
				}
				case 1: {
					AudioCommand command = voiceCommand(AudioCommandKind::Seek, voice);
					command.frame = (step * 97) % 100000;
					push(command);
					break;
				}
				case 2: {
					AudioCommand command = voiceCommand(AudioCommandKind::SetPitch, voice);
					command.value = 0.5f + 0.25f * static_cast<f32>(step % 6);
					push(command);
					break;
				}
				case 3: {
					AudioCommand command = voiceCommand(AudioCommandKind::SetVoiceGenerator, voice);
					command.generator = orphaned[voice] ? &generators[voice] : nullptr;
					if (push(command)) {
						orphaned[voice] = !orphaned[voice];
					}
					break;
				}
				case 4:
					if (step % 31 == 0) {
						push(voiceCommand(AudioCommandKind::Stop, voice));
					}
					break;
				}
			}
			++step;
		}
		renderThread.join();
	}

	u64 processed = 0;
	for (u32 bus = 1; bus <= BUSES; ++bus) {
		processed += effects[bus][0].processed + effects[bus][1].processed;
	}
	u64 generated = 0;
	for (const InvariantGenerator& generator : generators) {
		generated += generator.processed;
	}
	EXPECT_GE(renderer.GetCounters().blocks.load(), BLOCKS);
	EXPECT_EQ(allocations.load(), 0u);
	EXPECT_GT(sent, 10000u);
	EXPECT_GT(processed, 1000u);
	EXPECT_GT(generated, 1000u);
	EXPECT_GT(ended, 10u);
	EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

	renderer.Release();
	renderer.Shutdown();
	for (AudioClipData* block : voiceBlocks) {
		if (block) {
			ReleaseClipData(block);
		}
	}
}

TEST(AudioThreadInvariantTest, NullDeviceAllocatesNothing) {
	u64 allocations = ~0ull;
	u64 blocks = 0;
	bool opened = false;
	WatchALiveDevice(AudioBackendKind::Null, std::chrono::milliseconds(1000), allocations, blocks, opened);
	ASSERT_TRUE(opened);
	EXPECT_GE(blocks, 150u);
	EXPECT_EQ(allocations, 0u);
}

TEST(AudioThreadInvariantTest, SystemDeviceAllocatesNothing) {
	u64 allocations = ~0ull;
	u64 blocks = 0;
	bool opened = false;
	WatchALiveDevice(AudioBackendKind::System, std::chrono::milliseconds(1000), allocations, blocks, opened);
	if (!opened) {
		GTEST_SKIP() << "no output device the system would open";
	}
	EXPECT_GE(blocks, 150u);
	EXPECT_EQ(allocations, 0u);
}

TEST(AudioThreadInvariantTest, BuiltinEffectsAllocateNothing) {
	AudioRenderer renderer;
	renderer.Initialize();
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(480 * 2, 0.0f);

	const std::span<const AudioBuiltinEffect> builtins = GetBuiltinEffects();
	ASSERT_LT(builtins.size(), MAX_BUS_ROWS);
	std::vector<IAudioEffect*> effects;
	u32 ducker = 0;
	for (u32 index = 0; index < builtins.size(); ++index) {
		std::vector<f32> values;
		for (const AudioParameterDefinition& parameter : builtins[index].definition->GetParameters()) {
			values.push_back(parameter.defaultValue);
		}
		AudioEffectContext context;
		context.sampleRate = 48000;
		context.blockFrames = AudioRenderer::BLOCK_FRAMES;
		context.channels = 2;
		context.parameters = {values.data(), values.size()};
		effects.push_back(builtins[index].definition->CreateEffect(context));
		ASSERT_NE(effects.back(), nullptr) << builtins[index].name;
		ducker = builtins[index].name == "Duck Volume" ? index : ducker;
	}

	std::vector<f32> tone(48000);
	for (size_t frame = 0; frame < tone.size(); ++frame) {
		tone[frame] = 0.4f * std::sin(static_cast<f32>(frame) * 0.043f);
	}
	AudioClipData* clip = AudioTesting::MakePcm(tone, 1, 48000);
	ASSERT_NE(clip, nullptr);

	u64 sent = 0;
	const auto push = [&](const AudioCommand& command) {
		if (renderer.GetCommands().TryPush(command)) {
			++sent;
			return true;
		}
		return false;
	};
	const auto busCommand = [](AudioCommandKind kind, u32 slot) {
		AudioCommand command;
		command.kind = kind;
		command.bus = slot;
		command.busGeneration = 1;
		return command;
	};

	AudioTesting::BusBlock blocks[3];
	std::vector<f32> receives(BUS_BUFFER_SAMPLES, 0.0f);
	for (u32 bus = 1; bus <= 2; ++bus) {
		AudioCommand create = busCommand(AudioCommandKind::CreateBus, bus);
		create.other = AudioRenderer::ROOT_BUS;
		create.otherGeneration = 1;
		create.block = blocks[bus].Get();
		create.volume = 0.5f;
		create.pitch = 1.0f;
		ASSERT_TRUE(push(create));
	}
	AudioCommand buffers = busCommand(AudioCommandKind::SetBusReceives, 1);
	buffers.block = receives.data();
	buffers.mode = 1;
	ASSERT_TRUE(push(buffers));
	for (u32 index = 0; index <= builtins.size(); ++index) {
		AudioCommand row = busCommand(AudioCommandKind::SetBusRow, 1);
		row.mode = static_cast<u8>(index);
		if (index < builtins.size()) {
			row.frames = static_cast<u32>(AudioBusRowKind::Effect);
			row.effect = effects[index];
			row.value = 1.0f;
			row.frame = index == ducker ? 0 : NO_BUS_ROW;
		} else {
			row.frames = static_cast<u32>(AudioBusRowKind::Fader);
		}
		ASSERT_TRUE(push(row));
	}
	AudioCommand count = busCommand(AudioCommandKind::SetBusRowCount, 1);
	count.mode = static_cast<u8>(builtins.size() + 1);
	ASSERT_TRUE(push(count));
	AudioCommand send = busCommand(AudioCommandKind::SetBusRow, 2);
	send.frames = static_cast<u32>(AudioBusRowKind::Send);
	send.other = 1;
	send.otherGeneration = 1;
	send.value = 1.0f;
	send.frame = ducker;
	ASSERT_TRUE(push(send));
	AudioCommand fader = busCommand(AudioCommandKind::SetBusRow, 2);
	fader.mode = 1;
	fader.frames = static_cast<u32>(AudioBusRowKind::Fader);
	ASSERT_TRUE(push(fader));
	AudioCommand trim = busCommand(AudioCommandKind::SetBusRowCount, 2);
	trim.mode = 2;
	ASSERT_TRUE(push(trim));
	for (u32 voice = 0; voice < 2; ++voice) {
		AudioCommand play;
		play.kind = AudioCommandKind::Play;
		play.voice = voice;
		play.generation = 1;
		play.clip = clip;
		play.loop = true;
		play.bus = 1 + voice;
		play.busGeneration = 1;
		ASSERT_TRUE(push(play));
	}

	constexpr u64 BLOCKS = 4000;
	std::atomic<bool> go{false};
	std::atomic<u64> allocations{~0ull};
	std::thread renderThread([&]() {
		while (!go.load()) {
			std::this_thread::yield();
		}
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			renderer.Render(output.data(), 480);
		}
		allocations.store(ThreadAllocationWatch::GetCount());
	});

	{
		ScopedWatch watch(renderThread.get_id());
		AudioCommand gate;
		gate.kind = AudioCommandKind::SetListenerPresent;
		gate.value = 1.0f;
		push(gate);
		go.store(true);

		u64 step = 0;
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			const u32 row = static_cast<u32>(step % builtins.size());
			const std::span<const AudioParameterDefinition> table = builtins[row].definition->GetParameters();
			const u32 parameter = static_cast<u32>((step / builtins.size()) % table.size());
			const f32 share = static_cast<f32>((step * 37) % 101) / 100.0f;
			AudioCommand command = busCommand(AudioCommandKind::SetBusEffectParameter, 1);
			command.mode = static_cast<u8>(row);
			command.frames = parameter;
			command.value = table[parameter].minimum + (table[parameter].maximum - table[parameter].minimum) * share;
			push(command);
			++step;
			std::this_thread::yield();
		}
		renderThread.join();
	}

	EXPECT_GE(renderer.GetCounters().blocks.load(), BLOCKS);
	EXPECT_EQ(allocations.load(), 0u);
	EXPECT_GT(sent, 1000u);
	EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

	renderer.Release();
	renderer.Shutdown();
	for (IAudioEffect* effect : effects) {
		effect->Release();
	}
	ReleaseClipData(clip);
}

TEST(AudioThreadInvariantTest, VoicesWithBuiltinsAllocateNothing) {
	AudioRenderer renderer;
	renderer.Initialize();
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(480 * 2, 0.0f);

	const std::span<const AudioBuiltinEffect> builtins = GetBuiltinEffects();
	const u32 count = static_cast<u32>(builtins.size());
	std::vector<IAudioEffect*> effects[2];
	std::vector<IAudioEffectDefinition*> definitions[2];
	AudioVoiceChain chains[2];
	for (u32 voice = 0; voice < 2; ++voice) {
		for (const AudioBuiltinEffect& builtin : builtins) {
			std::vector<f32> values;
			for (const AudioParameterDefinition& parameter : builtin.definition->GetParameters()) {
				values.push_back(parameter.defaultValue);
			}
			AudioEffectContext context;
			context.sampleRate = 48000;
			context.blockFrames = AudioRenderer::BLOCK_FRAMES;
			context.channels = voice == 0 ? 1 : 2;
			context.parameters = {values.data(), values.size()};
			effects[voice].push_back(builtin.definition->CreateEffect(context));
			ASSERT_NE(effects[voice].back(), nullptr) << builtin.name;
			definitions[voice].push_back(builtin.definition);
		}
		chains[voice].effects = effects[voice].data();
		chains[voice].definitions = definitions[voice].data();
		chains[voice].count = count;
		chains[voice].onOutput = voice == 1;
	}

	std::vector<f32> tone(48000);
	for (size_t frame = 0; frame < tone.size(); ++frame) {
		tone[frame] = 0.4f * std::sin(static_cast<f32>(frame) * 0.043f);
	}
	AudioClipData* clip = AudioTesting::MakePcm(tone, 1, 48000);
	ASSERT_NE(clip, nullptr);

	u64 sent = 0;
	const auto push = [&](const AudioCommand& command) {
		if (renderer.GetCommands().TryPush(command)) {
			++sent;
			return true;
		}
		return false;
	};
	for (u32 voice = 0; voice < 2; ++voice) {
		AudioCommand play;
		play.kind = AudioCommandKind::Play;
		play.voice = voice;
		play.generation = 1;
		play.clip = clip;
		play.loop = true;
		ASSERT_TRUE(push(play));
		AudioCommand chain;
		chain.kind = AudioCommandKind::SetVoiceEffects;
		chain.voice = voice;
		chain.generation = 1;
		chain.chain = &chains[voice];
		ASSERT_TRUE(push(chain));
	}
	AudioTesting::BusBlock sendBlock;
	AudioCommand createBus;
	createBus.kind = AudioCommandKind::CreateBus;
	createBus.bus = 1;
	createBus.busGeneration = 1;
	createBus.other = AudioRenderer::ROOT_BUS;
	createBus.otherGeneration = 1;
	createBus.block = sendBlock.Get();
	createBus.volume = 0.5f;
	createBus.pitch = 1.0f;
	ASSERT_TRUE(push(createBus));
	const auto sendTo = [&](u32 voice, u32 generation, f32 level) {
		AudioCommand send;
		send.kind = AudioCommandKind::SetVoiceSend;
		send.voice = voice;
		send.generation = 1;
		send.bus = 1;
		send.busGeneration = generation;
		send.value = level;
		return push(send);
	};
	ASSERT_TRUE(sendTo(0, 1, 0.5f));
	ASSERT_TRUE(sendTo(1, 1, 0.5f));

	constexpr u64 BLOCKS = 4000;
	std::atomic<bool> go{false};
	std::atomic<u64> allocations{~0ull};
	std::thread renderThread([&]() {
		while (!go.load()) {
			std::this_thread::yield();
		}
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			renderer.Render(output.data(), 480);
		}
		allocations.store(ThreadAllocationWatch::GetCount());
	});

	{
		ScopedWatch watch(renderThread.get_id());
		AudioCommand gate;
		gate.kind = AudioCommandKind::SetListenerPresent;
		gate.value = 1.0f;
		push(gate);
		go.store(true);

		u64 step = 0;
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			const u32 voice = static_cast<u32>(step % 2);
			const u32 row = static_cast<u32>((step / 2) % count);
			const std::span<const AudioParameterDefinition> table = builtins[row].definition->GetParameters();
			const u32 parameter = static_cast<u32>((step / (2 * count)) % table.size());
			const f32 share = static_cast<f32>((step * 37) % 101) / 100.0f;
			AudioCommand command;
			command.kind = AudioCommandKind::SetVoiceEffectParameter;
			command.voice = voice;
			command.generation = 1;
			command.other = row;
			command.frames = parameter;
			command.value = table[parameter].minimum + (table[parameter].maximum - table[parameter].minimum) * share;
			push(command);
			if (step % 97 == 0) {
				AudioCommand bypass;
				bypass.kind = AudioCommandKind::SetVoiceBypassEffects;
				bypass.voice = voice;
				bypass.generation = 1;
				bypass.flag = (step / 97) % 3 == 1;
				push(bypass);
			}
			if (step % 89 == 0) {
				sendTo(voice, (step / 89) % 3 == 2 ? 0u : 1u, share);
			}
			++step;
			std::this_thread::yield();
		}
		renderThread.join();
	}

	EXPECT_GE(renderer.GetCounters().blocks.load(), BLOCKS);
	EXPECT_EQ(allocations.load(), 0u);
	EXPECT_GT(sent, 1000u);
	EXPECT_GT(renderer.GetCounters().peak.load(), 0u);

	renderer.Release();
	renderer.Shutdown();
	for (u32 voice = 0; voice < 2; ++voice) {
		for (IAudioEffect* effect : effects[voice]) {
			effect->Release();
		}
	}
	ReleaseClipData(clip);
}

#endif
