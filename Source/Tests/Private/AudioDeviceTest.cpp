#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioDevice.h"
#include "AudioRenderer.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Time.h"

#include <chrono>
#include <iostream>
#include <thread>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

namespace {

	bool WaitForCallbacks(const AudioRenderer& renderer, u64 callbacks, std::chrono::milliseconds budget = std::chrono::milliseconds(5000)) {
		const auto deadline = std::chrono::steady_clock::now() + budget;
		while (renderer.GetCounters().callbacks.load(std::memory_order_relaxed) < callbacks) {
			if (std::chrono::steady_clock::now() > deadline) {
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return true;
	}

	bool WaitForStarts(const AudioDevice& device, u32 starts, std::chrono::milliseconds budget = std::chrono::milliseconds(5000)) {
		const auto deadline = std::chrono::steady_clock::now() + budget;
		while (device.GetEvents().started < starts) {
			if (std::chrono::steady_clock::now() > deadline) {
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return true;
	}

	f64 TicksToMs(u64 ticks) {
		return static_cast<f64>(ticks) * 1000.0 / static_cast<f64>(Time::GetTicksPerSecond());
	}

} // namespace

TEST(AudioDeviceTest, NullDeviceRendersAndReports) {
	AudioRenderer renderer;
	AudioDevice device;
	ASSERT_TRUE(device.Open(AudioBackendKind::Null, 10, renderer));
	ASSERT_TRUE(device.IsOpen());

	const AudioDeviceFormat& format = device.GetFormat();
	EXPECT_EQ(format.backend, AudioBackendKind::Null);
	EXPECT_EQ(format.sampleRate, 48000u);
	EXPECT_EQ(format.channels, 2u);
	EXPECT_EQ(format.periodFrames, 480u);
	EXPECT_GT(format.periods, 0u);

	EXPECT_TRUE(renderer.IsPrepared());
	EXPECT_EQ(renderer.GetChannels(), format.channels);
	EXPECT_EQ(renderer.GetSampleRate(), format.sampleRate);

	ASSERT_TRUE(WaitForCallbacks(renderer, 10));
	const std::thread::id renderThread = renderer.GetCounters().renderThread.load();
	EXPECT_NE(renderThread, std::thread::id{});
	EXPECT_NE(renderThread, std::this_thread::get_id());

	EXPECT_GE(renderer.GetCounters().frames.load(), 10u * 480u / 2u);

	device.Close();
	EXPECT_FALSE(device.IsOpen());
}

TEST(AudioDeviceTest, NullDeviceGivesAskedPeriod) {
	for (const auto [milliseconds, frames] : {std::pair<u32, u32>{5, 240}, std::pair<u32, u32>{10, 480}, std::pair<u32, u32>{20, 960}}) {
		AudioRenderer renderer;
		AudioDevice device;
		ASSERT_TRUE(device.Open(AudioBackendKind::Null, milliseconds, renderer)) << milliseconds << " ms";
		EXPECT_EQ(device.GetFormat().periodFrames, frames) << milliseconds << " ms";
		device.Close();
	}
}

TEST(AudioDeviceTest, CloseJoinsAndFreesAll) {
	const size_t baseline = MemoryManager::GetBytesForTag(MemTag::Audio);
	for (u32 round = 0; round < 3; ++round) {
		AudioRenderer renderer;
		AudioDevice device;
		ASSERT_TRUE(device.Open(AudioBackendKind::Null, 5, renderer));
		EXPECT_GT(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
		ASSERT_TRUE(WaitForCallbacks(renderer, 3));

		device.Close();
		EXPECT_FALSE(renderer.IsPrepared());
		const u64 settled = renderer.GetCounters().callbacks.load();
		std::this_thread::sleep_for(std::chrono::milliseconds(30));
		EXPECT_EQ(renderer.GetCounters().callbacks.load(), settled);
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline) << "round " << round;
	}
}

TEST(AudioDeviceTest, EveryOpenCountsTheStartItMade) {
	AudioRenderer renderer;
	AudioDevice device;
	ASSERT_TRUE(device.Open(AudioBackendKind::Null, 10, renderer));
	ASSERT_TRUE(WaitForStarts(device, 1));
	const AudioDeviceEvents events = device.GetEvents();
	EXPECT_EQ(events.started, 1u);
	EXPECT_EQ(events.stopped, 0u);
	EXPECT_EQ(events.rerouted, 0u);
	device.Close();
	EXPECT_EQ(device.GetEvents().started, 0u);
}

TEST(AudioDeviceTest, StartOpenAskedForIsNeverRestart) {
	AudioRenderer renderer;
	AudioDevice device;
	ASSERT_TRUE(device.Open(AudioBackendKind::Null, 10, renderer));
	EXPECT_EQ(device.GetEvents().requested, 1u);
	EXPECT_EQ(device.GetEvents().Restarts(), 0u);
	device.Close();

	AudioDeviceEvents early;
	early.requested = 1;
	AudioDeviceEvents told = early;
	told.started = 1;
	AudioDeviceEvents back = told;
	back.started = 2;
	EXPECT_EQ(early.Restarts(), 0u);
	EXPECT_EQ(told.Restarts(), 0u);
	EXPECT_EQ(back.Restarts(), 1u);
}

TEST(AudioDeviceTest, SystemDevicePacesPeriod) {
	AudioRenderer renderer;
	AudioDevice device;
	if (!device.Open(AudioBackendKind::System, 10, renderer)) {
		GTEST_SKIP() << "no output device the system would open";
	}
	const AudioDeviceFormat format = device.GetFormat();
	ASSERT_TRUE(WaitForCallbacks(renderer, 20));
	const u64 firstCallbacks = renderer.GetCounters().callbacks.load();
	std::this_thread::sleep_for(std::chrono::milliseconds(2000));

	const AudioRenderCounters& counters = renderer.GetCounters();
	const u64 callbacks = counters.callbacks.load();
	const u64 frames = counters.frames.load();
	const f64 shortest = TicksToMs(counters.shortestIntervalTicks.load());
	const f64 longest = TicksToMs(counters.longestIntervalTicks.load());
	const f64 meanCost = TicksToMs(counters.costTicks.load()) / static_cast<f64>(callbacks);
	const f64 worstCost = TicksToMs(counters.worstCostTicks.load());
	const u64 overruns = counters.overruns.load();
	const f64 period = static_cast<f64>(format.periodFrames) * 1000.0 / static_cast<f64>(format.deviceSampleRate);
	const f64 meanFramesPerCallback = static_cast<f64>(frames) / static_cast<f64>(callbacks);
	device.Close();

	std::cout << "[ audio    ] '" << format.name << "': " << format.deviceSampleRate << " Hz, " << format.channels
			  << " ch, period " << format.periodFrames << " frames (" << period << " ms) x " << format.periods
			  << "; " << callbacks << " callbacks, " << meanFramesPerCallback << " frames each, paced "
			  << shortest << "-" << longest << " ms; render " << meanCost << " ms mean, " << worstCost
			  << " ms worst; " << overruns << " overrun(s)\n";
	RecordProperty("periodFrames", static_cast<int>(format.periodFrames));
	RecordProperty("pacedShortestMicroseconds", static_cast<int>(shortest * 1000.0));
	RecordProperty("pacedLongestMicroseconds", static_cast<int>(longest * 1000.0));

	EXPECT_GT(callbacks, firstCallbacks);
	EXPECT_GT(format.periodFrames, 0u);
	EXPECT_GT(meanFramesPerCallback, static_cast<f64>(format.periodFrames) * 0.5);
	EXPECT_LT(meanFramesPerCallback, static_cast<f64>(format.periodFrames) * 2.0);
}

#endif
