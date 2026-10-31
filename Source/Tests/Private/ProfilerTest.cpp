#include "TestsPCH.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/WindowSystem.h"

using namespace Shalltear;

namespace {

	class ProfilerFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
			Profiler::SetEnabled(true);
		}

		void TearDown() override {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
		}

		static u32 Site(const char* name) { return Profiler::RegisterSite(name); }

		static const Profiler::ZoneResult* Find(const Profiler::FrameResults& frame, std::string_view name) {
			for (u32 i = 0; i < frame.count; ++i) {
				if (frame.zones[i].name == name) {
					return &frame.zones[i];
				}
			}
			ADD_FAILURE() << "no zone " << name << " (" << frame.count << " zones, " << frame.droppedSites << " dropped)";
			static const Profiler::ZoneResult missing{};
			return &missing;
		}

		static void Spin(f64 milliseconds) {
			const u64 frequency = Time::GetTicksPerSecond();
			const u64 target = static_cast<u64>(static_cast<f64>(frequency) * milliseconds / 1000.0);
			const u64 start = Time::GetTicks();
			while (Time::GetTicks() - start < target) {
			}
		}
	};

} // anonymous namespace

TEST_F(ProfilerFixture, ZoneEntered10000TimesIsOneRecord) {
	const u32 site = Site("Aggregation.Hot");
	for (u32 i = 0; i < 10000; ++i) {
		Profiler::Enter(site);
		Profiler::Leave();
	}
	Profiler::NextFrame();

	const Profiler::ZoneResult* zone = Find(Profiler::GetFrameResults(), "Aggregation.Hot");
	ASSERT_NE(zone, nullptr);
	EXPECT_EQ(zone->calls, 10000u);
}

TEST_F(ProfilerFixture, FrameStartsFromZeroNotAccumulating) {
	const u32 site = Site("Aggregation.Reset");
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();
	ASSERT_EQ(Find(Profiler::GetFrameResults(), "Aggregation.Reset")->calls, 1u);

	Profiler::NextFrame();
	const Profiler::ZoneResult* zone = Find(Profiler::GetFrameResults(), "Aggregation.Reset");
	ASSERT_NE(zone, nullptr);
	EXPECT_EQ(zone->calls, 0u);
	EXPECT_EQ(zone->milliseconds, 0.0);
	EXPECT_EQ(zone->maxMilliseconds, 0.0);
}

TEST_F(ProfilerFixture, NameRegisteredTwiceIsOneSite) {
	const u32 first = Site("Reload.Same");
	Profiler::NextFrame();
	const u32 sites = Profiler::GetFrameResults().count;

	const u32 again = Site("Reload.Same");
	EXPECT_EQ(again, first);
	Profiler::Enter(first);
	Profiler::Leave();
	Profiler::Enter(again);
	Profiler::Leave();
	Profiler::NextFrame();

	EXPECT_EQ(Profiler::GetFrameResults().count, sites);
	EXPECT_EQ(Find(Profiler::GetFrameResults(), "Reload.Same")->calls, 2u);
}

TEST_F(ProfilerFixture, NestingRecordedAsDepth) {
	const u32 outer = Site("Nesting.Outer");
	const u32 middle = Site("Nesting.Middle");
	const u32 inner = Site("Nesting.Inner");
	Profiler::Enter(outer);
	Profiler::Enter(middle);
	Profiler::Enter(inner);
	Profiler::Leave();
	Profiler::Leave();
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::FrameResults frame = Profiler::GetFrameResults();
	EXPECT_EQ(Find(frame, "Nesting.Outer")->depth, 0u);
	EXPECT_EQ(Find(frame, "Nesting.Middle")->depth, 1u);
	EXPECT_EQ(Find(frame, "Nesting.Inner")->depth, 2u);
}

TEST_F(ProfilerFixture, ParentContainsChildNotExcludingIt) {
	const u32 parent = Site("Inclusive.Parent");
	const u32 child = Site("Inclusive.Child");
	Profiler::Enter(parent);
	Profiler::Enter(child);
	Spin(1.0);
	Profiler::Leave();
	Spin(1.0);
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::FrameResults frame = Profiler::GetFrameResults();
	const f64 parentMs = Find(frame, "Inclusive.Parent")->milliseconds;
	const f64 childMs = Find(frame, "Inclusive.Child")->milliseconds;
	EXPECT_GT(childMs, 0.0);
	EXPECT_GE(parentMs, childMs);
}

TEST_F(ProfilerFixture, WorstEntryIsKeptBesideTotal) {
	const u32 site = Site("Max.Uneven");
	Profiler::Enter(site);
	Spin(2.0);
	Profiler::Leave();
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::ZoneResult* zone = Find(Profiler::GetFrameResults(), "Max.Uneven");
	ASSERT_EQ(zone->calls, 2u);
	EXPECT_LE(zone->maxMilliseconds, zone->milliseconds);
	EXPECT_GT(zone->maxMilliseconds, zone->milliseconds / 2.0);
}

TEST_F(ProfilerFixture, TheFrameOnlyChangesAtNextFrame) {
	const u32 site = Site("Publish.Boundary");
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();
	ASSERT_EQ(Find(Profiler::GetFrameResults(), "Publish.Boundary")->calls, 1u);

	for (u32 i = 0; i < 5; ++i) {
		Profiler::Enter(site);
		Profiler::Leave();
		EXPECT_EQ(Find(Profiler::GetFrameResults(), "Publish.Boundary")->calls, 1u);
	}
	Profiler::NextFrame();
	EXPECT_EQ(Find(Profiler::GetFrameResults(), "Publish.Boundary")->calls, 5u);
}

TEST_F(ProfilerFixture, TwoReadersSeeTheSameFrame) {
	const u32 site = Site("Publish.Shared");
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::FrameResults first = Profiler::GetFrameResults();
	const Profiler::FrameResults second = Profiler::GetFrameResults();
	EXPECT_EQ(first.zones, second.zones);
	EXPECT_EQ(first.count, second.count);
}

TEST_F(ProfilerFixture, ADisabledProfilerRecordsNothing) {
	const u32 site = Site("Gate.Disabled");
	Profiler::SetEnabled(false);
	EXPECT_FALSE(Profiler::IsEnabled());
	for (u32 i = 0; i < 100; ++i) {
		Profiler::Enter(site);
		Profiler::Leave();
	}
	Profiler::NextFrame();
	EXPECT_EQ(Find(Profiler::GetFrameResults(), "Gate.Disabled")->calls, 0u);
}

TEST_F(ProfilerFixture, EnablingMidFrameDoesNotUnbalanceStack) {
	const u32 outer = Site("Gate.Midframe.Outer");
	const u32 inner = Site("Gate.Midframe.Inner");
	Profiler::Enter(outer);
	Profiler::SetEnabled(false);
	Profiler::Leave();
	Profiler::SetEnabled(true);

	Profiler::Enter(inner);
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::FrameResults frame = Profiler::GetFrameResults();
	EXPECT_EQ(Find(frame, "Gate.Midframe.Inner")->depth, 0u);
	EXPECT_EQ(Find(frame, "Gate.Midframe.Inner")->calls, 1u);
}

TEST_F(ProfilerFixture, StackOverflowCounted) {
	const u32 deep = Site("Overflow.Deep");
	const u32 shallow = Site("Overflow.Shallow");

	Profiler::Enter(shallow);
	constexpr u32 TOO_DEEP = 40;
	for (u32 i = 0; i < TOO_DEEP; ++i) {
		Profiler::Enter(deep);
	}
	for (u32 i = 0; i < TOO_DEEP; ++i) {
		Profiler::Leave();
	}
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::FrameResults frame = Profiler::GetFrameResults();
	EXPECT_GT(frame.droppedDepth, 0u);
	const Profiler::ZoneResult* zone = Find(frame, "Overflow.Shallow");
	ASSERT_NE(zone, nullptr);
	EXPECT_EQ(zone->calls, 1u);
	EXPECT_EQ(zone->depth, 0u);
}

TEST_F(ProfilerFixture, OverflowCountBelongsToFrameThatOverflowed) {
	const u32 deep = Site("Overflow.Frame");
	for (u32 i = 0; i < 40; ++i) {
		Profiler::Enter(deep);
	}
	for (u32 i = 0; i < 40; ++i) {
		Profiler::Leave();
	}
	Profiler::NextFrame();
	EXPECT_GT(Profiler::GetFrameResults().droppedDepth, 0u);

	Profiler::NextFrame();
	EXPECT_EQ(Profiler::GetFrameResults().droppedDepth, 0u);
}

TEST_F(ProfilerFixture, LongNameIsTruncatedNotLost) {
	const u32 site = Site("Name.WayTooLongToFitInsideTheTableAtAll");
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::FrameResults frame = Profiler::GetFrameResults();
	const Profiler::ZoneResult* zone = nullptr;
	for (u32 i = 0; i < frame.count; ++i) {
		if (std::string_view(frame.zones[i].name).starts_with("Name.WayTooLong")) {
			zone = &frame.zones[i];
		}
	}
	ASSERT_NE(zone, nullptr);
	EXPECT_EQ(std::string_view(zone->name).size(), 31u);
	EXPECT_EQ(zone->calls, 1u);
}

TEST_F(ProfilerFixture, EngineWorkCountedPerFrame) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	WindowSystem::Initialize();
	RenderSystem::Initialize();
	UISystem::Initialize();

	for (u32 frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		UISystem::Update();
		Profiler::NextFrame();

		const Profiler::ZoneResult* zone = Find(Profiler::GetFrameResults(), "UI.Update");
		ASSERT_NE(zone, nullptr) << frame;
		EXPECT_EQ(zone->calls, 1u) << zone->calls;
	}

	UISystem::Terminate();
	RenderSystem::Terminate();
	WindowSystem::Terminate();
#endif
}

TEST_F(ProfilerFixture, NameIsCopiedNotPointedAt) {
	char borrowed[] = "Name.Borrowed";
	const u32 site = Profiler::RegisterSite(borrowed);
	borrowed[0] = 'X';
	Profiler::Enter(site);
	Profiler::Leave();
	Profiler::NextFrame();

	const Profiler::ZoneResult* zone = Find(Profiler::GetFrameResults(), "Name.Borrowed");
	ASSERT_NE(zone, nullptr);
	EXPECT_NE(zone->name, borrowed);
}
