#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"
#include "ThreadAllocationWatch.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr u32 SOURCES = 2000;
	constexpr u32 REAL = 32;

	class AudioCostFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;

		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxVirtualVoices = 2048\nmaxRealVoices = 32\n"); }

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("cost");
			ASSERT_TRUE(_scene.IsValid());
		}

		std::vector<Handle<AudioSource>> AddSources(AudioClip* clip) {
			std::vector<Handle<AudioSource>> sources;
			for (u32 index = 0; index < SOURCES; ++index) {
				Handle<SceneObject> object = SceneSystem::CreateObject("source", _scene);
				object->SetWorldPosition(Vec3{2.0f + 0.25f * static_cast<f32>(index), 0.0f, 0.0f});
				Handle<AudioSource> source = object->AddComponent<AudioSource>();
				source->SetResource(ResourceOf(clip));
				source->SetLoop(true);
				source->SetSpatialBlend(1.0f);
				sources.push_back(source);
			}
			return sources;
		}

		static std::vector<bool> HeardSet(const std::vector<Handle<AudioSource>>& sources) {
			std::vector<bool> heard;
			for (const Handle<AudioSource>& source : sources) {
				heard.push_back(!source->IsVirtual());
			}
			return heard;
		}

		static const AudioRenderCounters& Counters() { return GetAudioSystem().GetRenderer().GetCounters(); }
	};

	struct ScopedWatch {
		explicit ScopedWatch(std::thread::id thread) { ThreadAllocationWatch::Watch(thread); }
		~ScopedWatch() { ThreadAllocationWatch::Stop(); }
	};

} // namespace

TEST_F(AudioCostFixture, Frames240Of2000SourcesRetainNothing) {
	Handle<SceneObject> listener = SceneSystem::CreateObject("listener", _scene);
	listener->AddComponent<AudioListener>();
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	const std::vector<Handle<AudioSource>> sources = AddSources(clip);

	u32 frame = 0;
	const auto step = [&]() {
		listener->SetWorldPosition(Vec3{0.25f * static_cast<f32>(frame++), 0.0f, 0.0f});
		RunFrame();
	};
	const auto settle = [&]() {
		size_t previous = ~size_t(0);
		for (u32 attempt = 0; attempt < 32; ++attempt) {
			RunFrame();
			ASSERT_TRUE(WaitForBlocks(3));
			RunFrame();
			const size_t now = MemoryManager::GetAllocationCountForTag(MemTag::Audio);
			if (now == previous) {
				return;
			}
			previous = now;
		}
	};
	for (u32 warm = 0; warm < 30; ++warm) {
		step();
	}
	settle();
	const std::vector<bool> heardBefore = HeardSet(sources);
	const size_t audioBytes = MemoryManager::GetBytesForTag(MemTag::Audio);
	const size_t audioCount = MemoryManager::GetAllocationCountForTag(MemTag::Audio);
	const size_t sceneBytes = MemoryManager::GetBytesForTag(MemTag::Scene);
	const size_t sceneCount = MemoryManager::GetAllocationCountForTag(MemTag::Scene);

	for (u32 measured = 0; measured < 240; ++measured) {
		step();
	}
	settle();

	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Audio), audioCount);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), audioBytes);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Scene), sceneCount);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), sceneBytes);

	EXPECT_NE(HeardSet(sources), heardBefore);
	EXPECT_TRUE(WaitFor([]() { return Counters().realVoices.load() == REAL; }));
	EXPECT_EQ(Counters().voices.load(), SOURCES);
}

TEST(AudioCostTest, AudioThreadAllocatesNothing) {
	constexpr u64 BLOCKS = 10000;
	ClipBlock clip(MakePcm(std::vector<f32>(480, 0.1f), 1, 48000));
	AudioRenderer renderer;
	renderer.Initialize(SOURCES + 512, 2 * REAL);
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(480 * 2, 0.0f);

	u32 generations[SOURCES]{};
	for (u32 slot = 0; slot < SOURCES; ++slot) {
		AudioCommand play;
		play.kind = AudioCommandKind::Play;
		play.voice = slot;
		play.generation = generations[slot] = 1;
		play.clip = clip.Get();
		play.loop = true;
		play.flag = slot >= REAL;
		ASSERT_TRUE(renderer.GetCommands().TryPush(play));
	}
	AudioCommand gate;
	gate.kind = AudioCommandKind::SetListenerPresent;
	gate.value = 1.0f;
	ASSERT_TRUE(renderer.GetCommands().TryPush(gate));

	std::atomic<bool> go{false};
	std::atomic<u64> allocations{~0ull};
	std::atomic<u64> ticks{0};
	std::thread renderThread([&]() {
		while (!go.load()) {
			std::this_thread::yield();
		}
		const u64 start = Time::GetTicks();
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			renderer.Render(output.data(), 480);
		}
		ticks.store(Time::GetTicks() - start);
		allocations.store(ThreadAllocationWatch::GetCount());
	});

	u32 first = 0;
	u64 promotions = 0;
	{
		ScopedWatch watch(renderThread.get_id());
		go.store(true);
		u64 lastBlock = 0;
		while (renderer.GetCounters().blocks.load(std::memory_order_relaxed) < BLOCKS) {
			const u64 block = renderer.GetCounters().blocks.load(std::memory_order_relaxed);
			if (block >= lastBlock + 8) {
				lastBlock = block;
				bool pushed = true;
				for (u32 index = 0; index < 8 && pushed; ++index) {
					AudioCommand demote;
					demote.kind = AudioCommandKind::SetVoiceReal;
					demote.voice = (first + index) % SOURCES;
					demote.generation = 1;
					demote.flag = false;
					pushed = renderer.GetCommands().TryPush(demote);
				}
				for (u32 index = 0; index < 8 && pushed; ++index) {
					AudioCommand promote;
					promote.kind = AudioCommandKind::SetVoiceReal;
					promote.voice = (first + REAL + index) % SOURCES;
					promote.generation = 1;
					promote.flag = true;
					pushed = renderer.GetCommands().TryPush(promote);
					promotions += pushed ? 1u : 0u;
				}
				first = (first + 8) % SOURCES;
			}
			std::this_thread::yield();
		}
		renderThread.join();
	}

	EXPECT_EQ(allocations.load(), 0u);
	EXPECT_GT(promotions, 1000u);
	EXPECT_EQ(renderer.GetCounters().decoderShortfalls.load(), 0u);
	EXPECT_EQ(renderer.GetCounters().voices.load(), SOURCES);

	const f64 microseconds = static_cast<f64>(ticks.load()) * 1.0e6 / static_cast<f64>(Time::GetTicksPerSecond()) / static_cast<f64>(BLOCKS);
	std::printf("[ READING  ] %u voices, %u real: %.2f us per %u-frame block\n", SOURCES, REAL, microseconds, AudioRenderer::BLOCK_FRAMES);

	renderer.Release();
	renderer.Shutdown();
}

TEST_F(AudioCostFixture, NullDeviceRenders2000VoicesWithoutOverrun) {
	GetAudioSystem().SetListenerPresent(true);
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	AudioPlayParams params;
	params.loop = true;
	for (u32 index = 0; index < SOURCES; ++index) {
		params.volume = 0.001f * static_cast<f32>(index % 1000 + 1);
		ASSERT_TRUE(Audio::Play(ResourceOf(clip), params).IsValid()) << "voice " << index;
	}
	RunFrame();
	ASSERT_TRUE(WaitFor([]() { return Counters().voices.load() == SOURCES; }));
	const u64 overruns = Counters().overruns.load();
	ASSERT_TRUE(WaitForBlocks(375));
	EXPECT_EQ(Counters().overruns.load(), overruns);
	EXPECT_EQ(Counters().realVoices.load(), REAL);
	EXPECT_EQ(Counters().decoderShortfalls.load(), 0u);
}

#endif
