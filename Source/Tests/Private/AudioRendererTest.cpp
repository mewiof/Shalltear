#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioRenderer.h"

#include "Shalltear/Core/Memory.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

namespace {

	constexpr u32 BLOCK = AudioRenderer::BLOCK_FRAMES;

	const f32 SENTINEL = std::numeric_limits<f32>::quiet_NaN();

	u64 BlocksFor(u64 frames) { return (frames + BLOCK - 1) / BLOCK; }

} // namespace

TEST(AudioRendererTest, EverySampleWrittenAnySlice) {
	for (u32 channels : {1u, 2u, 6u, 8u}) {
		for (u32 slice : {1u, 255u, 256u, 257u, 480u, 1024u, 4096u}) {
			AudioRenderer renderer;
			ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(channels), 48000));

			std::vector<f32> output(static_cast<size_t>(slice) * channels, SENTINEL);
			renderer.Render(output.data(), slice);

			u32 unwritten = 0;
			for (f32 sample : output) {
				if (sample != 0.0f) {
					++unwritten;
				}
			}
			EXPECT_EQ(unwritten, 0u) << channels << ' ' << slice << " frames";
		}
	}
}

TEST(AudioRendererTest, BlockRenderedWhenCarryOverEnds) {
	AudioRenderer renderer;
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	std::vector<f32> output(8192, 0.0f);
	const AudioRenderCounters& counters = renderer.GetCounters();

	renderer.Render(output.data(), 100);
	EXPECT_EQ(counters.blocks.load(), 1u);
	renderer.Render(output.data(), BLOCK - 100);
	EXPECT_EQ(counters.blocks.load(), 1u);
	renderer.Render(output.data(), 1);
	EXPECT_EQ(counters.blocks.load(), 2u);

	AudioRenderer uneven;
	ASSERT_TRUE(uneven.Prepare(MakeStandardLayout(2), 48000));
	u64 delivered = 0;
	for (u32 call = 0; call < 500; ++call) {
		const u32 slice = 1 + (call * 37) % 700;
		uneven.Render(output.data(), slice);
		delivered += slice;
	}
	EXPECT_EQ(uneven.GetCounters().frames.load(), delivered);
	EXPECT_EQ(uneven.GetCounters().blocks.load(), BlocksFor(delivered));
	EXPECT_EQ(uneven.GetCounters().callbacks.load(), 500u);
}

TEST(AudioRendererTest, OverrunIsCallbackOutlastingAudio) {
	std::vector<f32> output(4096 * 8, 0.0f);

	AudioRenderer ordinary;
	ASSERT_TRUE(ordinary.Prepare(MakeStandardLayout(8), 48000));
	for (u32 call = 0; call < 8; ++call) {
		ordinary.Render(output.data(), 4096);
	}
	EXPECT_EQ(ordinary.GetCounters().overruns.load(), 0u);

	AudioRenderer impossible;
	ASSERT_TRUE(impossible.Prepare(MakeStandardLayout(8), 4000000000u));
	for (u32 call = 0; call < 8; ++call) {
		impossible.Render(output.data(), 4096);
	}
	EXPECT_EQ(impossible.GetCounters().overruns.load(), 8u);
}

TEST(AudioRendererTest, PrepareStartsTheCountersAgain) {
	AudioRenderer renderer;
	std::vector<f32> output(4096, 0.0f);
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000));
	renderer.Render(output.data(), 1000);
	renderer.Render(output.data(), 1000);
	ASSERT_EQ(renderer.GetCounters().callbacks.load(), 2u);
	ASSERT_GT(renderer.GetCounters().longestIntervalTicks.load(), 0u);

	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 44100));
	EXPECT_EQ(renderer.GetCounters().callbacks.load(), 0u);
	EXPECT_EQ(renderer.GetCounters().frames.load(), 0u);
	EXPECT_EQ(renderer.GetCounters().blocks.load(), 0u);
	EXPECT_EQ(renderer.GetCounters().longestIntervalTicks.load(), 0u);
	EXPECT_EQ(renderer.GetCounters().renderThread.load(), std::thread::id{});
}

TEST(AudioRendererTest, IntervalsWaitForPacing) {
	AudioRenderer renderer;
	ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(2), 48000, 960));
	std::vector<f32> output(480 * 2, 0.0f);

	renderer.Render(output.data(), 480);
	renderer.Render(output.data(), 480);
	EXPECT_EQ(renderer.GetCounters().longestIntervalTicks.load(), 0u);
	EXPECT_EQ(renderer.GetCounters().shortestIntervalTicks.load(), ~0ull);

	renderer.Render(output.data(), 480);
	EXPECT_NE(renderer.GetCounters().shortestIntervalTicks.load(), ~0ull);
}

TEST(AudioRendererTest, BlockIsOneAllocation) {
	const size_t baseline = MemoryManager::GetBytesForTag(MemTag::Audio);
	{
		AudioRenderer renderer;
		ASSERT_TRUE(renderer.Prepare(MakeStandardLayout(6), 48000));
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio) - baseline, static_cast<size_t>(6) * BLOCK * sizeof(f32));

		std::vector<f32> output(6 * 1024, 0.0f);
		const size_t before = MemoryManager::GetBytesForTag(MemTag::Audio);
		renderer.Render(output.data(), 1024);
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), before);

		renderer.Release();
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
		renderer.Release();
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
	}
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
}

TEST(AudioRendererTest, PrepareRefusesEmptyFormat) {
	const size_t baseline = MemoryManager::GetBytesForTag(MemTag::Audio);
	AudioRenderer renderer;
	EXPECT_FALSE(renderer.Prepare(MakeStandardLayout(0), 48000));
	EXPECT_FALSE(renderer.Prepare(MakeStandardLayout(2), 0));
	EXPECT_FALSE(renderer.IsPrepared());
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
}

#endif
