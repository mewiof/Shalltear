#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphEffect.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <cmath>

#ifndef SHALLTEAR_SHARED
#include "EffectClock.h"
#endif

using namespace Shalltear;

namespace {

	namespace Ops = EffectGraphEditOps;

	GraphDocument EffectDocument() {
		GraphDocument document;
		document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
		return document;
	}

	str Stored(const GraphDocument& document, std::string_view key) {
		for (const GraphSetting& setting : document.settings) {
			if (setting.key == key) {
				return setting.value;
			}
		}
		return {};
	}

} // anonymous namespace

TEST(GraphEffectTimingTest, TimingDefaultsAsNewAsset) {
	const GraphEffectTiming timing = GraphEffectTarget::Timing(EffectDocument());
	EXPECT_TRUE(timing.fixedDeltaTime);
	EXPECT_FALSE(timing.exactFixedTime);
	EXPECT_FALSE(timing.ignoreTimeScale);
	EXPECT_EQ(timing.prewarmStepCount, 0u);
	EXPECT_EQ(timing.prewarmDeltaTime, 0.0f);
	EXPECT_EQ(timing.cullingFlags, 3u);

	GraphDocument loop;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(1), loop, error)) << error;
	EXPECT_TRUE(loop.settings.empty());
}

TEST(GraphEffectTimingTest, CompileReadsGraphsSettingsInsideLimits) {
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(1), document, error)) << error;
	document.settings.push_back({"fixedDeltaTime", "false"});
	document.settings.push_back({"exactFixedTime", "true"});
	document.settings.push_back({"ignoreTimeScale", "true"});
	document.settings.push_back({"preWarmStepCount", "10"});
	document.settings.push_back({"preWarmDeltaTime", "0.25"});
	document.settings.push_back({"cullingFlags", "1"});
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(document, "Timed", effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	EXPECT_FALSE(effect.timing.fixedDeltaTime);
	EXPECT_TRUE(effect.timing.exactFixedTime);
	EXPECT_TRUE(effect.timing.ignoreTimeScale);
	EXPECT_EQ(effect.timing.prewarmStepCount, 10u);
	EXPECT_EQ(effect.timing.prewarmDeltaTime, 0.25f);
	EXPECT_EQ(effect.timing.cullingFlags, 1u);

	const auto prewarm = [](std::string_view steps, std::string_view delta) {
		GraphDocument held = EffectDocument();
		held.settings.push_back({"preWarmStepCount", str(steps)});
		held.settings.push_back({"preWarmDeltaTime", str(delta)});
		const GraphEffectTiming timing = GraphEffectTarget::Timing(held);
		return std::pair<u32, f32>(timing.prewarmStepCount, timing.prewarmDeltaTime);
	};
	EXPECT_EQ(prewarm("5000", "0.1"), (std::pair<u32, f32>(2400u, 0.1f)));
	EXPECT_EQ(prewarm("3", "0.0001"), (std::pair<u32, f32>(3u, 1.0f / 800.0f)));
	EXPECT_EQ(prewarm("2.9", "0.1"), (std::pair<u32, f32>(2u, 0.1f)));
	EXPECT_EQ(prewarm("0", "0.5"), (std::pair<u32, f32>(0u, 0.0f)));
	EXPECT_EQ(prewarm("4", "0"), (std::pair<u32, f32>(0u, 0.0f)));
	EXPECT_EQ(prewarm("-4", "-1"), (std::pair<u32, f32>(0u, 0.0f)));
	EXPECT_EQ(prewarm("\"ten\"", "0.1"), (std::pair<u32, f32>(0u, 0.0f)));
}

TEST(GraphEffectTimingTest, UpdateModeStoredWhenChanged) {
	GraphDocument document = EffectDocument();
	EXPECT_FALSE(Ops::SetUpdateMode(document, GRAPH_EFFECT_FIXED_DELTA_TIME, true));
	ASSERT_TRUE(Ops::SetUpdateMode(document, GRAPH_EFFECT_FIXED_DELTA_TIME, false));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_FIXED_DELTA_TIME), "false");
	EXPECT_FALSE(GraphEffectTarget::Timing(document).fixedDeltaTime);
	ASSERT_TRUE(Ops::SetUpdateMode(document, GRAPH_EFFECT_EXACT_FIXED_TIME, true));
	ASSERT_TRUE(Ops::SetUpdateMode(document, GRAPH_EFFECT_IGNORE_TIME_SCALE, true));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_EXACT_FIXED_TIME), "true");
	const GraphEffectTiming timing = GraphEffectTarget::Timing(document);
	EXPECT_TRUE(timing.exactFixedTime && timing.ignoreTimeScale);

	ASSERT_TRUE(Ops::SetUpdateMode(document, GRAPH_EFFECT_FIXED_DELTA_TIME, true));
	ASSERT_TRUE(Ops::SetUpdateMode(document, GRAPH_EFFECT_EXACT_FIXED_TIME, false));
	ASSERT_TRUE(Ops::SetUpdateMode(document, GRAPH_EFFECT_IGNORE_TIME_SCALE, false));
	EXPECT_TRUE(document.settings.empty());
	EXPECT_FALSE(Ops::SetUpdateMode(document, GRAPH_EFFECT_PREWARM_STEP_COUNT, true));
	EXPECT_TRUE(document.settings.empty());
}

TEST(GraphEffectTimingTest, CullingFlagsStoredWhenChanged) {
	GraphDocument document = EffectDocument();
	EXPECT_FALSE(Ops::SetCullingFlags(document, GRAPH_EFFECT_CULL_DEFAULT));
	ASSERT_TRUE(Ops::SetCullingFlags(document, GRAPH_EFFECT_CULL_SIMULATION));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_CULLING_FLAGS), "1");
	EXPECT_EQ(GraphEffectTarget::Timing(document).cullingFlags, 1u);
	ASSERT_TRUE(Ops::SetCullingFlags(document, 0));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_CULLING_FLAGS), "0");
	EXPECT_EQ(GraphEffectTarget::Timing(document).cullingFlags, 0u);
	EXPECT_FALSE(Ops::SetCullingFlags(document, 4));
	ASSERT_TRUE(Ops::SetCullingFlags(document, GRAPH_EFFECT_CULL_DEFAULT));
	EXPECT_TRUE(document.settings.empty());

	for (std::string_view stored : {"1.5", "-1", "4", "\"1\""}) {
		GraphDocument held = EffectDocument();
		held.settings.push_back({str(GRAPH_EFFECT_CULLING_FLAGS), str(stored)});
		EXPECT_EQ(GraphEffectTarget::Timing(held).cullingFlags, 3u) << stored;
	}
}

TEST(GraphEffectInstancingTest, InstancingStoredWhenChanged) {
	GraphDocument document = EffectDocument();
	GraphEffectInstancing held = GraphEffectTarget::Instancing(document);
	EXPECT_EQ(held.mode, GRAPH_EFFECT_INSTANCING_AUTOMATIC);
	EXPECT_EQ(held.capacity, 64u);
	EXPECT_EQ(held.refused, 0u);
	EXPECT_FALSE(Ops::SetInstancingMode(document, GRAPH_EFFECT_INSTANCING_AUTOMATIC));
	ASSERT_TRUE(Ops::SetInstancingMode(document, GRAPH_EFFECT_INSTANCING_DISABLED));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_INSTANCING_MODE), "-1");
	ASSERT_TRUE(Ops::SetInstancingMode(document, GRAPH_EFFECT_INSTANCING_CUSTOM));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_INSTANCING_MODE), "1");
	EXPECT_EQ(GraphEffectTarget::Instancing(document).mode, GRAPH_EFFECT_INSTANCING_CUSTOM);
	EXPECT_FALSE(Ops::SetInstancingMode(document, 2));
	EXPECT_FALSE(Ops::SetInstancingCapacity(document, 64));
	ASSERT_TRUE(Ops::SetInstancingCapacity(document, 5));
	EXPECT_EQ(Stored(document, GRAPH_EFFECT_INSTANCING_CAPACITY), "5");
	ASSERT_TRUE(Ops::SetInstancingCapacity(document, 0));
	EXPECT_EQ(GraphEffectTarget::Instancing(document).capacity, 1u);
	ASSERT_TRUE(Ops::SetInstancingCapacity(document, 70000));
	EXPECT_EQ(GraphEffectTarget::Instancing(document).capacity, GRAPH_EFFECT_INSTANCING_MAX_CAPACITY);
	ASSERT_TRUE(Ops::SetInstancingCapacity(document, 64));
	ASSERT_TRUE(Ops::SetInstancingMode(document, GRAPH_EFFECT_INSTANCING_AUTOMATIC));
	EXPECT_TRUE(document.settings.empty());

	for (std::string_view stored : {"2", "0.5", "-2", "\"1\""}) {
		GraphDocument read = EffectDocument();
		read.settings.push_back({str(GRAPH_EFFECT_INSTANCING_MODE), str(stored)});
		EXPECT_EQ(GraphEffectTarget::Instancing(read).mode, GRAPH_EFFECT_INSTANCING_AUTOMATIC) << stored;
	}
	for (const auto& [stored, capacity] : {std::pair<std::string_view, u32>{"-3", 1u}, {"7.9", 7u}, {"\"7\"", 64u}}) {
		GraphDocument read = EffectDocument();
		read.settings.push_back({str(GRAPH_EFFECT_INSTANCING_CAPACITY), str(stored)});
		EXPECT_EQ(GraphEffectTarget::Instancing(read).capacity, capacity) << stored;
	}
}

TEST(GraphEffectInstancingTest, GpuEventRefusesInstancing) {
	GraphDocument plain = EffectDocument();
	EXPECT_EQ(GraphEffectTarget::Instancing(plain).refused, 0u);
	const auto with = [](std::string_view def) {
		GraphDocument document = EffectDocument();
		GraphContext context;
		context.node.id = 1;
		context.node.def = str(def);
		document.contexts.push_back(std::move(context));
		Ops::SetInstancingMode(document, GRAPH_EFFECT_INSTANCING_CUSTOM);
		return GraphEffectTarget::Instancing(document);
	};
	const GraphEffectInstancing events = with(GRAPH_EFFECT_GPU_EVENT);
	EXPECT_EQ(events.refused, GRAPH_EFFECT_INSTANCING_GPU_EVENT);
	EXPECT_EQ(events.mode, GRAPH_EFFECT_INSTANCING_CUSTOM);
	EXPECT_EQ(with(GRAPH_EFFECT_OUTPUT_EVENT).refused, 0u);
	EXPECT_EQ(with(GRAPH_EFFECT_INITIALIZE).refused, 0u);
}

TEST(GraphEffectTimingTest, Prewarms3FieldsLinkInInspector) {
	GraphDocument document = EffectDocument();
	const auto held = [&document]() {
		const GraphEffectTiming timing = GraphEffectTarget::Timing(document);
		return std::pair<u32, f32>(timing.prewarmStepCount, timing.prewarmDeltaTime);
	};
	using Field = Ops::PrewarmField;
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::TotalTime, 2.0));
	EXPECT_EQ(held(), (std::pair<u32, f32>(1u, 2.0f)));
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::StepCount, 4.0));
	EXPECT_EQ(held(), (std::pair<u32, f32>(4u, 0.5f)));
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::DeltaTime, 1.0));
	EXPECT_EQ(held(), (std::pair<u32, f32>(4u, 1.0f)));
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::DeltaTime, 0.3));
	EXPECT_EQ(held(), (std::pair<u32, f32>(13u, 0.3f)));
	EXPECT_FALSE(Ops::SetPrewarm(document, Field::DeltaTime, 0.3));
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::StepCount, 99999.0));
	EXPECT_EQ(held().first, 2400u);
	EXPECT_FLOAT_EQ(held().second, 3.9f / 2400.0f);
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::DeltaTime, 0.0));
	EXPECT_EQ(held(), (std::pair<u32, f32>(2400u, 1.0f / 800.0f)));
	ASSERT_TRUE(Ops::SetPrewarm(document, Field::TotalTime, 0.0));
	EXPECT_EQ(held(), (std::pair<u32, f32>(0u, 0.0f)));
	EXPECT_TRUE(document.settings.empty());
	EXPECT_FALSE(Ops::SetPrewarm(document, Field::TotalTime, std::nan("")));
}

#ifndef SHALLTEAR_SHARED

using namespace VisualEffectsImpl;

namespace {

	VisualEffectsSettings Quarters(f32 maxDeltaTime = 3600.0f) {
		VisualEffectsSettings settings;
		settings.fixedTimeStep = 0.25f;
		settings.maxDeltaTime = maxDeltaTime;
		return settings;
	}

	Vector<f32> Updates(const EffectTiming& timing, const EffectFrameTime& time, f32 rate = 1.0f) {
		Vector<f32> out{GetMemoryResource(MemTag::Graphics)};
		EffectUpdates(timing, time, rate, out);
		return out;
	}

} // anonymous namespace

TEST(EffectClockTest, DeltaBoundedByMax) {
	EffectClock clock;
	const VisualEffectsSettings defaults;
	EXPECT_EQ(defaults.fixedTimeStep, 1.0f / 60.0f);
	EXPECT_EQ(defaults.maxDeltaTime, 0.05f);
	EffectFrameTime time = clock.Advance(0.02f, 0.03f, defaults);
	EXPECT_EQ(time.scaled.delta, 0.02f);
	EXPECT_EQ(time.unscaled.delta, 0.03f);
	time = clock.Advance(0.5f, 0.5f, defaults);
	EXPECT_EQ(time.scaled.delta, 0.05f);
	time = clock.Advance(-1.0f, std::nanf(""), defaults);
	EXPECT_EQ(time.scaled.delta, 0.0f);
	EXPECT_EQ(time.unscaled.delta, 0.0f);
}

TEST(EffectClockTest, FixedDeltaBanksRemainder) {
	EffectClock clock;
	const VisualEffectsSettings settings = Quarters();
	EffectFrameTime time = clock.Advance(0.125f, 0.0f, settings);
	EXPECT_EQ(time.step, 0.25f);
	EXPECT_EQ(time.scaled.steps, 0u);
	EXPECT_EQ(time.scaled.fixedDelta, 0.0f);
	time = clock.Advance(0.125f, 0.0f, settings);
	EXPECT_EQ(time.scaled.steps, 1u);
	EXPECT_EQ(time.scaled.fixedDelta, 0.25f);
	time = clock.Advance(0.625f, 0.25f, settings);
	EXPECT_EQ(time.scaled.steps, 2u);
	EXPECT_EQ(time.scaled.fixedDelta, 0.5f);
	EXPECT_EQ(time.unscaled.steps, 1u);
	time = clock.Advance(0.125f, 0.0f, settings);
	EXPECT_EQ(time.scaled.steps, 1u);
	EXPECT_EQ(time.unscaled.steps, 0u);
}

TEST(EffectClockTest, StallLeavesNoBacklog) {
	EffectClock clock;
	const VisualEffectsSettings defaults;
	EffectFrameTime time = clock.Advance(1.0f, 1.0f, defaults);
	EXPECT_EQ(time.scaled.steps, 3u);
	EXPECT_EQ(time.scaled.fixedDelta, 0.05f);
	time = clock.Advance(0.0f, 0.0f, defaults);
	EXPECT_EQ(time.scaled.steps, 0u);
	time = clock.Advance(1.0f / 60.0f, 1.0f / 60.0f, defaults);
	EXPECT_EQ(time.scaled.steps, 1u);
	EXPECT_EQ(time.scaled.fixedDelta, 1.0f / 60.0f);
}

TEST(EffectClockTest, NoStepUsesDelta) {
	EffectClock clock;
	VisualEffectsSettings settings;
	settings.fixedTimeStep = 0.0f;
	settings.maxDeltaTime = 0.0f;
	const EffectFrameTime time = clock.Advance(0.75f, 0.5f, settings);
	EXPECT_EQ(time.scaled.delta, 0.75f);
	EXPECT_EQ(time.scaled.fixedDelta, 0.75f);
	EXPECT_EQ(time.unscaled.fixedDelta, 0.5f);
	EXPECT_EQ(time.scaled.steps, 0u);
}

TEST(EffectClockTest, EachModeMakesUpdatesThenRate) {
	EffectFrameTime time;
	time.scaled = {0.125f, 0.5f, 2};
	time.unscaled = {0.375f, 0.75f, 3};
	time.step = 0.25f;
	EffectTiming timing;
	timing.fixedDeltaTime = false;
	EXPECT_EQ(Updates(timing, time), (Vector<f32>{{0.125f}, GetMemoryResource(MemTag::Graphics)}));
	timing.fixedDeltaTime = true;
	EXPECT_EQ(Updates(timing, time), (Vector<f32>{{0.5f}, GetMemoryResource(MemTag::Graphics)}));
	timing.exactFixedTime = true;
	EXPECT_EQ(Updates(timing, time), (Vector<f32>{{0.25f, 0.25f}, GetMemoryResource(MemTag::Graphics)}));
	EXPECT_EQ(Updates(timing, time, 2.0f), (Vector<f32>{{0.5f, 0.5f}, GetMemoryResource(MemTag::Graphics)}));
	timing.ignoreTimeScale = true;
	EXPECT_EQ(Updates(timing, time), (Vector<f32>{{0.25f, 0.25f, 0.25f}, GetMemoryResource(MemTag::Graphics)}));
	time.unscaled = {0.0f, 0.0f, 0};
	EXPECT_EQ(Updates(timing, time), (Vector<f32>{{0.0f}, GetMemoryResource(MemTag::Graphics)}));
}

#endif
