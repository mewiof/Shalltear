#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>

#ifndef SHALLTEAR_SHARED
#include "EffectKernelHarness.h"
#endif

using namespace Shalltear;

namespace {

	namespace Ops = EffectGraphEditOps;

	GraphDocument ReadGraph(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	bool Compiles(std::string_view text, GraphEffect& effect, Vector<GraphDiagnostic>& diagnostics) {
		diagnostics.clear();
		return GraphEffectTarget::Compile(ReadGraph(text), "events.sleffect", effect, diagnostics);
	}

	bool Said(const Vector<GraphDiagnostic>& diagnostics, u32 node, std::string_view what) {
		return std::any_of(diagnostics.begin(), diagnostics.end(),
						   [&](const GraphDiagnostic& diagnostic) { return diagnostic.node == node && diagnostic.message.find(what) != str::npos; });
	}

	str First(const Vector<GraphDiagnostic>& diagnostics) { return diagnostics.empty() ? str() : diagnostics[0].message; }

	struct Family {
		u32 capacity = 64;
		std::string_view nodes;
		std::string_view parentInitialize;
		std::string_view parentUpdate;
		u32 trigger = 20;
		std::string_view childSettings = R"({"Capacity":512})";
		std::string_view childInitialize;
		std::string_view childUpdate;
		bool childFirst = false;
	};

	str FamilyEffect(const Family& family) {
		const str parent = std::format(R"({{"id":1,"def":"Context/Initialize","settings":{{"Capacity":{}}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
			{{"id":5,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}})",
									   family.capacity, family.parentInitialize, family.parentUpdate);
		const str child = std::format(R"({{"id":3,"def":"Context/Initialize","flow":[[6,"In"]],"settings":{},"blocks":[{}]}},
			{{"id":4,"def":"Context/Update","flow":[[3,"In"]],"blocks":[{}]}},
			{{"id":7,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"blocks":[]}})",
									  family.childSettings, family.childInitialize, family.childUpdate);
		const str event = family.trigger == 0 ? str(R"({"id":6,"def":"Context/GPUEvent","blocks":[]})")
											  : std::format(R"({{"id":6,"def":"Context/GPUEvent","in":{{"Evt":{{"from":[{},"Evt"]}}}},"blocks":[]}})", family.trigger);
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":200,"nodes":[{}],"contexts":[{},{},{}]}})", family.nodes,
						   family.childFirst ? child : parent, event, family.childFirst ? parent : child);
	}

	constexpr std::string_view LIFETIME_TEN = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}})";
	constexpr std::string_view CHILD_LIFETIME_TEN =
		R"({"id":35,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}})";

	str Trigger(std::string_view mode, std::string_view port = "Count", f32 value = 1.0f, bool clampToOne = true, u32 id = 20) {
		return std::format(R"({{"id":{},"def":"Block/TriggerEvent","settings":{{"Clamp To One":{},"Mode":"{}"}},"in":{{"{}":{{"value":[{}]}}}}}})", id,
						   clampToOne ? "true" : "false", mode, port, value);
	}

} // anonymous namespace

TEST(GraphEffectEventTest, GpuEventFeedsInitialize) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	Family family;
	family.parentInitialize = LIFETIME_TEN;
	const str trigger = Trigger("Always", "Count", 3.0f);
	family.parentUpdate = trigger;
	family.childInitialize = CHILD_LIFETIME_TEN;
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	ASSERT_EQ(effect.systems.size(), 2u);
	const GraphEffectSystem& parent = effect.systems[0];
	const GraphEffectSystem& child = effect.systems[1];
	EXPECT_EQ(parent.initialize, 1u);
	EXPECT_EQ(parent.parent, GRAPH_EFFECT_NO_BINDING);
	EXPECT_EQ(child.parent, 0u);
	EXPECT_NE(child.parentBinding, GRAPH_EFFECT_NO_BINDING);
	EXPECT_TRUE(child.spawners.empty());
	EXPECT_EQ(child.batchStride, 4u);
	ASSERT_EQ(parent.updateEvents.size(), 1u);
	EXPECT_TRUE(parent.initializeEvents.empty());
	EXPECT_EQ(parent.updateEvents[0].child, 1u);
	EXPECT_NE(parent.updateEvents[0].listsBinding, parent.updateEvents[0].argumentsBinding);
	EXPECT_NE(parent.updateKernel.find("SltParticleAppendEvents"), str::npos) << parent.updateKernel;
	EXPECT_NE(child.initializeKernel.find("SltParticleEventSource"), str::npos) << child.initializeKernel;
	EXPECT_NE(parent.initializeKernel.find("SltParticleSpawnCount()"), str::npos);
	EXPECT_EQ(child.initializeKernel.find("SltParticleSpawnCount()"), str::npos);
	family.trigger = 0;
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	ASSERT_EQ(effect.systems.size(), 2u);
	EXPECT_EQ(effect.systems[1].parent, GRAPH_EFFECT_NO_BINDING);
	EXPECT_TRUE(effect.systems[0].updateEvents.empty());
}

TEST(GraphEffectEventTest, ParentRunsFirst) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	Family family;
	family.parentInitialize = LIFETIME_TEN;
	const str trigger = Trigger("Always");
	family.parentUpdate = trigger;
	family.childFirst = true;
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	ASSERT_EQ(effect.systems.size(), 2u);
	EXPECT_EQ(effect.systems[0].initialize, 1u);
	EXPECT_EQ(effect.systems[1].initialize, 3u);
	EXPECT_EQ(effect.systems[1].parent, 0u);
}

TEST(GraphEffectEventTest, ParentStoresChildSources) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const auto stores = [&](const GraphEffectSystem& system, std::string_view attribute) {
		return std::any_of(system.stored.begin(), system.stored.end(), [&](const GraphEffectStored& stored) { return stored.attribute == attribute; });
	};
	Family family;
	const str parentInitialize = str(LIFETIME_TEN) + R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"targetPosition"},"in":{"TargetPosition":{"value":[1,2,3]}}})";
	family.parentInitialize = parentInitialize;
	const str trigger = Trigger("Always");
	family.parentUpdate = trigger;
	family.childInitialize = "";
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	EXPECT_FALSE(stores(effect.systems[0], "targetPosition"));
	family.nodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"targetPosition","Location":"Source"}})";
	family.childInitialize = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"targetPosition","Source":"Source"}})";
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	EXPECT_FALSE(stores(effect.systems[0], "targetPosition"));
	family.childInitialize = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[40,"targetPosition"]}}})";
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	EXPECT_TRUE(stores(effect.systems[0], "targetPosition"));
	EXPECT_NE(effect.systems[1].initializeKernel.find("sltParentAttributes"), str::npos);
	family.nodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"angle","Location":"Source"}})";
	family.childInitialize = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[40,"angle"]}}})";
	ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
	EXPECT_FALSE(stores(effect.systems[0], "angle"));
}

TEST(GraphEffectEventTest, RateAccumulatorPerParticle) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const str accumulator = GraphEffectAttributes::RateCount(20);
	const auto stores = [&](std::string_view attribute) {
		const auto& stored = effect.systems[0].stored;
		return std::any_of(stored.begin(), stored.end(), [&](const GraphEffectStored& entry) { return entry.attribute == attribute; });
	};
	for (const std::string_view mode : GRAPH_EFFECT_TRIGGER_MODES) {
		SCOPED_TRACE(mode);
		Family family;
		family.parentInitialize = LIFETIME_TEN;
		const bool rate = mode == "OverTime" || mode == "OverDistance";
		const str trigger = Trigger(mode, rate ? "Rate" : "Count", 2.0f);
		family.parentUpdate = trigger;
		ASSERT_TRUE(Compiles(FamilyEffect(family), effect, diagnostics)) << First(diagnostics);
		EXPECT_EQ(stores(accumulator), rate);
	}
}

TEST(GraphEffectEventTest, GpuEventRulesEnforced) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const str trigger = Trigger("Always");
	for (const std::string_view cpu : {R"({"id":8,"def":"Context/Spawn","blocks":[]})", R"({"id":8,"def":"Context/Event","blocks":[]})"}) {
		Family family;
		family.parentUpdate = trigger;
		str text = FamilyEffect(family);
		const size_t at = text.find(R"("flow":[[6,"In"]])");
		ASSERT_NE(at, str::npos);
		text.replace(at, 17, R"("flow":[[6,"In"],[8,"In"]])");
		text.replace(text.rfind("]}"), 2, "," + str(cpu) + "]}");
		EXPECT_FALSE(Compiles(text, effect, diagnostics)) << cpu;
		EXPECT_TRUE(Said(diagnostics, 3, "by a GPU Event and by a Spawn or an Event")) << First(diagnostics);
	}
	{
		Family family;
		const str update = str(trigger) + "," + Trigger("OnDie", "Count", 1.0f, true, 21);
		family.parentUpdate = update;
		str text = FamilyEffect(family);
		const size_t at = text.find(R"("flow":[[6,"In"]])");
		text.replace(at, 17, R"("flow":[[6,"In"],[8,"In"]])");
		text.replace(text.rfind("]}"), 2, R"(,{"id":8,"def":"Context/GPUEvent","in":{"Evt":{"from":[21,"Evt"]}},"blocks":[]}]})");
		EXPECT_FALSE(Compiles(text, effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 3, "2 GPU Events")) << First(diagnostics);
	}
	{
		Family family;
		family.trigger = 21;
		const str update = Trigger("Always", "Count", 1.0f, true, 21);
		family.childUpdate = update;
		EXPECT_FALSE(Compiles(FamilyEffect(family), effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 3, "a Trigger Event of its system")) << First(diagnostics);
	}
	{
		Family family;
		family.parentUpdate = trigger;
		const str update = Trigger("Always", "Count", 1.0f, true, 21);
		family.childUpdate = update;
		str text = FamilyEffect(family);
		const size_t at = text.find(R"({"id":1,"def":"Context/Initialize",)");
		text.insert(at + std::string_view(R"({"id":1,"def":"Context/Initialize",)").size(), R"("flow":[[8,"In"]],)");
		text.replace(text.rfind("]}"), 2, R"(,{"id":8,"def":"Context/GPUEvent","in":{"Evt":{"from":[21,"Evt"]}},"blocks":[]}]})");
		EXPECT_FALSE(Compiles(text, effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 1, "a cycle of GPU events") || Said(diagnostics, 3, "a cycle of GPU events")) << First(diagnostics);
	}
	{
		Family family;
		family.parentUpdate = trigger;
		family.childSettings = R"({"Data Type":"ParticleStrip","Strip Capacity":64,"Particle Per Strip Count":8})";
		str text = FamilyEffect(family);
		text.replace(text.rfind("]}"), 2,
					 R"(,{"id":8,"def":"Context/GPUEvent","in":{"Evt":{"from":[20,"Evt"]}},"blocks":[]},
			{"id":9,"def":"Context/Initialize","flow":[[8,"In"]],"settings":{"Data Type":"ParticleStrip","Strip Capacity":64,"Particle Per Strip Count":8},"blocks":[]}]})");
		EXPECT_FALSE(Compiles(text, effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 9, "2nd strip system") || Said(diagnostics, 3, "2nd strip system")) << First(diagnostics);
	}
	{
		Family family;
		family.nodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"age"}})";
		family.trigger = 40;
		EXPECT_FALSE(Compiles(FamilyEffect(family), effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 6, "isn't a Trigger Event")) << First(diagnostics);
	}
	{
		Family family;
		family.parentUpdate = trigger;
		family.nodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"alive","Location":"Source"}})";
		family.childInitialize = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"alive"},"in":{"Alive":{"from":[40,"alive"]}}})";
		EXPECT_FALSE(Compiles(FamilyEffect(family), effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 3, "source's alive")) << First(diagnostics);
	}
}

TEST(GraphEffectEventTest, InitializeEventIsSource) {
	constexpr std::string_view Linked = R"({"_type":"EffectGraph","version":4,"nextId":200,"nodes":[
		{"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"position","Location":"Source"}}],"contexts":[
		{"id":2,"def":"Context/Event","settings":{"Event Name":"Hit"},"blocks":[]},
		{"id":3,"def":"Context/Spawn","blocks":[{"id":30,"def":"Block/Burst","in":{"Count":{"value":[2]}}}]},
		{"id":4,"def":"Context/Initialize","flow":[[2,"In"],[3,"In"]],"settings":{"Capacity":64},"blocks":[
			{"id":31,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[40,"position"]}}}]},
		{"id":5,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"blocks":[]}]})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compiles(Linked, effect, diagnostics)) << First(diagnostics);
	ASSERT_EQ(effect.systems.size(), 1u);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_EQ(system.events, (Vector<str>{{"Hit"}, GetMemoryResource(MemTag::Editor)}));
	EXPECT_EQ(system.spawners.size(), 1u);
	ASSERT_EQ(effect.spawners.size(), 1u);
	EXPECT_EQ(effect.spawners[0].start, (Vector<str>{{"OnPlay"}, GetMemoryResource(MemTag::Editor)}));
	EXPECT_NE(std::find(effect.events.begin(), effect.events.end(), "Hit"), effect.events.end());
	ASSERT_EQ(system.source.size(), 1u);
	EXPECT_EQ(system.source[0].name, "position");
	EXPECT_NE(system.initializeKernel.find("SltParticleEffectBatch"), str::npos);

	str twice(Linked);
	constexpr std::string_view Flows = R"([[2,"In"],[3,"In"]])";
	twice.replace(twice.find(Flows), Flows.size(), R"([[2,"In"],[6,"In"]])");
	twice.replace(twice.rfind("]}"), 2, R"(,{"id":6,"def":"Context/Event","settings":{"Event Name":"Hit"},"blocks":[]}]})");
	ASSERT_TRUE(Compiles(twice, effect, diagnostics)) << First(diagnostics);
	EXPECT_EQ(effect.systems[0].events.size(), 2u);
	EXPECT_TRUE(effect.systems[0].spawners.empty());
}

TEST(GraphEffectEventTest, OutputEventSendsName) {
	constexpr std::string_view Sent = R"({"_type":"EffectGraph","version":4,"nextId":200,"nodes":[],"contexts":[
		{"id":2,"def":"Context/Event","settings":{"Event Name":"Hit"},"blocks":[]},
		{"id":3,"def":"Context/Spawn","blocks":[{"id":30,"def":"Block/Burst","in":{"Count":{"value":[2]}}}]},
		{"id":6,"def":"Context/OutputEvent","flow":[[3,"In"]],"settings":{"Event Name":"Burst"},"blocks":[]},
		{"id":7,"def":"Context/OutputEvent","flow":[[2,"In"]],"settings":{"Event Name":"Echo"},"blocks":[]}]})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compiles(Sent, effect, diagnostics)) << First(diagnostics);
	EXPECT_TRUE(effect.systems.empty());
	ASSERT_EQ(effect.spawners.size(), 1u);
	EXPECT_EQ(effect.spawners[0].outputEvents, (Vector<str>{{"Burst"}, GetMemoryResource(MemTag::Editor)}));
	ASSERT_EQ(effect.relays.size(), 1u);
	EXPECT_EQ(effect.relays[0].event, "Hit");
	EXPECT_EQ(effect.relays[0].output, "Echo");

	EXPECT_TRUE(GraphEffectRules::MayFeed(GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_OUTPUT_EVENT, GRAPH_EFFECT_IN));
	EXPECT_TRUE(GraphEffectRules::MayFeed(GRAPH_EFFECT_EVENT, GRAPH_EFFECT_OUTPUT_EVENT, GRAPH_EFFECT_IN));
	EXPECT_FALSE(GraphEffectRules::MayFeed(GRAPH_EFFECT_GPU_EVENT, GRAPH_EFFECT_OUTPUT_EVENT, GRAPH_EFFECT_IN));
	EXPECT_FALSE(GraphEffectRules::MayFeed(GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_OUTPUT_EVENT, GRAPH_EFFECT_IN));
	GraphDocument document;
	document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 sent = Ops::AddContext(document, GRAPH_EFFECT_EVENT, Vec2());
	const u32 output = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT_EVENT, Vec2());
	EXPECT_TRUE(Ops::Link(document, spawn, output, GRAPH_EFFECT_IN));
	EXPECT_TRUE(Ops::Link(document, sent, output, GRAPH_EFFECT_IN));
}

TEST(GraphEffectEventTest, WindowLinksAndOffersWhatCompileTakes) {
	EXPECT_TRUE(GraphEffectRules::MayFeed(GRAPH_EFFECT_GPU_EVENT, GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_IN));
	EXPECT_FALSE(GraphEffectRules::MayFeed(GRAPH_EFFECT_GPU_EVENT, GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_IN));
	EXPECT_FALSE(GraphEffectRules::MayFeed(GRAPH_EFFECT_GPU_EVENT, GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_START));
	const auto contexts = Ops::Contexts();
	EXPECT_EQ(std::count(contexts.begin(), contexts.end(), GRAPH_EFFECT_GPU_EVENT), 1);

	GraphDocument document;
	document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 event = Ops::AddContext(document, GRAPH_EFFECT_GPU_EVENT, Vec2());
	const u32 other = Ops::AddContext(document, GRAPH_EFFECT_GPU_EVENT, Vec2());
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 output = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT, Vec2());
	EXPECT_TRUE(Ops::FlowOutput(document, event));
	EXPECT_TRUE(Ops::FlowInputs(document, event).empty());
	EXPECT_FALSE(Ops::CanLink(document, event, update, GRAPH_EFFECT_IN));
	ASSERT_TRUE(Ops::Link(document, event, initialize, GRAPH_EFFECT_IN));
	str why;
	EXPECT_FALSE(Ops::CanLink(document, spawn, initialize, GRAPH_EFFECT_IN, &why));
	EXPECT_FALSE(Ops::CanLink(document, other, initialize, GRAPH_EFFECT_IN, &why));
	EXPECT_NE(why.find("GPU Event"), str::npos) << why;
	const u32 second = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	ASSERT_TRUE(Ops::Link(document, spawn, second, GRAPH_EFFECT_IN));
	EXPECT_FALSE(Ops::CanLink(document, event, second, GRAPH_EFFECT_IN));
	const u32 sent = Ops::AddContext(document, GRAPH_EFFECT_EVENT, Vec2());
	EXPECT_TRUE(Ops::CanLink(document, sent, second, GRAPH_EFFECT_IN));
	EXPECT_FALSE(Ops::CanLink(document, sent, initialize, GRAPH_EFFECT_IN, &why));
	EXPECT_NE(why.find("GPU Event"), str::npos) << why;
	const u32 third = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	ASSERT_TRUE(Ops::Link(document, sent, third, GRAPH_EFFECT_IN));
	EXPECT_FALSE(Ops::CanLink(document, other, third, GRAPH_EFFECT_IN));

	const auto triggers = [&](u32 context) {
		Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		Ops::BlockChoices(document, context, choices);
		return std::count_if(choices.begin(), choices.end(), [](const Ops::BlockChoice& choice) { return choice.def == GRAPH_EFFECT_TRIGGER_EVENT; });
	};
	EXPECT_EQ(triggers(initialize), 5);
	EXPECT_EQ(triggers(update), 5);
	EXPECT_EQ(triggers(output), 0);
	EXPECT_EQ(triggers(spawn), 0);
	EXPECT_EQ(triggers(event), 0);
	Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	Ops::BlockChoices(document, update, choices);
	const auto overTime = std::find_if(choices.begin(), choices.end(), [](const Ops::BlockChoice& choice) { return choice.name == "Trigger Event Over Time"; });
	ASSERT_NE(overTime, choices.end());
	const u32 block = Ops::AddBlock(document, update, 0, *overTime);
	ASSERT_NE(block, 0u);
	GraphDefinitions definitions;
	const GraphNode* made = GraphDocumentOps::FindNode(document, block);
	ASSERT_NE(made, nullptr);
	const GraphNodeDefinition* definition = definitions.Find(document, *made);
	ASSERT_NE(definition, nullptr);
	EXPECT_EQ(definition->name, std::string_view("Trigger Event Over Time"));
	EXPECT_TRUE(NodeShaderEditOps::CanConnect(document, block, GRAPH_EFFECT_EVT, event, GRAPH_EFFECT_EVT, nullptr, &definitions));
	EXPECT_FALSE(NodeShaderEditOps::CanConnect(document, block, GRAPH_EFFECT_EVT, initialize, "Bounds Size", nullptr, &definitions));
}

#ifndef SHALLTEAR_SHARED

using namespace Shalltear::EffectKernelHarness;

namespace {

	size_t Near(const Vector<f32>& values, f32 value) {
		return static_cast<size_t>(std::count_if(values.begin(), values.end(), [value](f32 held) { return std::fabs(held - value) < 0.01f; }));
	}

	constexpr f32 STEP = 0.125f;

} // anonymous namespace

TEST_F(GraphEffectSystemsFixture, ChildSpawnsSameFrame) {
	SystemsRun run;
	Family family;
	family.parentInitialize = LIFETIME_TEN;
	const str trigger = Trigger("Always", "Count", 3.0f);
	family.parentUpdate = trigger;
	family.nodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleId"}})";
	const str childInitialize = str(CHILD_LIFETIME_TEN) + R"(,{"id":31,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[40,"particleId"]}}})";
	family.childInitialize = childInitialize;
	if (!Begin(run, FamilyEffect(family), "Exit")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {8, 0});
	EXPECT_EQ(AliveCount(run, 0), 8u);
	EXPECT_EQ(AliveCount(run, 1), 0u);
	Advance(run, STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 24u);
	Advance(run, 2 * STEP, STEP, {});
	ASSERT_EQ(AliveCount(run, 1), 48u);
	EXPECT_EQ(DroppedOf(run, 1), 0u);
	const Vector<f32> ids = Sorted(Values(run, 1, "size"));
	ASSERT_EQ(ids.size(), 48u);
	for (u32 i = 0; i < ids.size(); ++i) {
		EXPECT_EQ(ids[i], static_cast<f32>(i));
	}
}

TEST_F(GraphEffectSystemsFixture, UpdatesRunParentFirst) {
	SystemsRun run;
	Family family;
	family.parentInitialize = LIFETIME_TEN;
	const str trigger = Trigger("Always", "Count", 3.0f);
	family.parentUpdate = trigger;
	family.childInitialize = CHILD_LIFETIME_TEN;
	if (!Begin(run, FamilyEffect(family), "Updates")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {8, 0});
	const f32 deltas[] = {STEP, 2 * STEP};
	AdvanceUpdates(run, STEP, deltas, {});
	EXPECT_EQ(run.systems[0].GetSimulatedFrames(), 3u);
	const Vector<f32> parents = Values(run, 0, "age");
	ASSERT_EQ(parents.size(), 8u);
	EXPECT_EQ(Near(parents, 3 * STEP), 8u);
	ASSERT_EQ(AliveCount(run, 1), 48u);
	const Vector<f32> children = Values(run, 1, "age");
	ASSERT_EQ(children.size(), 48u);
	EXPECT_EQ(Near(children, 2 * STEP), 24u);
	EXPECT_EQ(Near(children, 0.0f), 24u);
}

TEST_F(GraphEffectSystemsFixture, InitializeTriggerSpawnsChild) {
	SystemsRun run;
	Family family;
	const str parentInitialize = str(LIFETIME_TEN) + "," + Trigger("Always", "Count", 2.0f);
	family.parentInitialize = parentInitialize;
	family.childInitialize = CHILD_LIFETIME_TEN;
	if (!Begin(run, FamilyEffect(family), "InInitialize")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {5, 0});
	EXPECT_EQ(AliveCount(run, 1), 10u);
	Advance(run, STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 10u);
}

TEST_F(GraphEffectSystemsFixture, ChildReadsTriggerSource) {
	SystemsRun run;
	Family family;
	family.nodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleId"}},
		{"id":41,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnCount","Location":"Source"}})";
	const str parentInitialize = str(LIFETIME_TEN) + R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[40,"particleId"]}}})";
	family.parentInitialize = parentInitialize;
	const str trigger = Trigger("Always");
	family.parentUpdate = trigger;
	const str childInitialize = str(CHILD_LIFETIME_TEN) + R"(,{"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"position","Source":"Source"}},
		{"id":31,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[41,"spawnCount"]}}})";
	family.childInitialize = childInitialize;
	if (!Begin(run, FamilyEffect(family), "Source")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {4, 0});
	Advance(run, STEP, STEP, {});
	const Vector<f32> x = Sorted(Values(run, 1, "position", 0));
	ASSERT_EQ(x.size(), 4u);
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_EQ(x[i], static_cast<f32>(i));
	}
	for (const f32 count : Values(run, 1, "size")) {
		EXPECT_EQ(count, 4.0f);
	}
}

TEST_F(GraphEffectSystemsFixture, OnDieTriggersOnceWhereParentDied) {
	SystemsRun run;
	Family family;
	family.parentInitialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[0.3]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[1,0,0]}}})";
	const str trigger = Trigger("OnDie", "Count", 2.0f);
	family.parentUpdate = trigger;
	const str childInitialize = str(CHILD_LIFETIME_TEN) + R"(,{"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"position","Source":"Source"}})";
	family.childInitialize = childInitialize;
	if (!Begin(run, FamilyEffect(family), "OnDie")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {3, 0});
	Advance(run, STEP, STEP, {});
	Advance(run, 2 * STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 0), 3u);
	EXPECT_EQ(AliveCount(run, 1), 0u);
	Advance(run, 3 * STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 0), 0u);
	ASSERT_EQ(AliveCount(run, 1), 6u);
	EXPECT_EQ(Near(Values(run, 1, "position", 0), 3 * STEP), 6u);
	Advance(run, 4 * STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 6u);
}

TEST_F(GraphEffectSystemsFixture, OverTimeCarriesFraction) {
	struct Case {
		f32 rate;
		bool clamp;
		u32 afterFour;
	};
	for (const Case c : {Case{4.0f, true, 2}, Case{20.0f, false, 10}, Case{20.0f, true, 4}}) {
		SCOPED_TRACE(std::format("rate {} clamped {}", c.rate, c.clamp));
		SystemsRun run;
		Family family;
		family.parentInitialize = LIFETIME_TEN;
		const str trigger = Trigger("OverTime", "Rate", c.rate, c.clamp);
		family.parentUpdate = trigger;
		family.childInitialize = CHILD_LIFETIME_TEN;
		if (!Begin(run, FamilyEffect(family), std::format("OverTime{}{}", c.rate, c.clamp))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, STEP, {1, 0});
		for (u32 frame = 1; frame <= 4; ++frame) {
			Advance(run, frame * STEP, STEP, {});
		}
		EXPECT_EQ(AliveCount(run, 1), c.afterFour);
	}
}

TEST_F(GraphEffectSystemsFixture, OverDistanceCountsMotion) {
	SystemsRun run;
	Family family;
	family.parentInitialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[3,0,0]}}})";
	const str parentUpdate = str(R"({"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"position","Composition":"Add"},"in":{"Position":{"value":[0,0.5,0]}}},)") +
							 Trigger("OverDistance", "Rate", 8.0f, false);
	family.parentUpdate = parentUpdate;
	family.childInitialize = CHILD_LIFETIME_TEN;
	if (!Begin(run, FamilyEffect(family), "OverDistance")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {2, 0});
	Advance(run, STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 10u);
	Advance(run, 2 * STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 20u);
}

TEST_F(GraphEffectSystemsFixture, OnCollideFollowsCollision) {
	SystemsRun run;
	Family family;
	family.parentInitialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0.75,0]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,-4,0]}}})";
	const str parentUpdate = str(R"({"id":13,"def":"Block/CollisionShape","settings":{"Collision Attributes":"WritePunctualContactOnly","Shape":"Plane"}},)") +
							 Trigger("OnCollide");
	family.parentUpdate = parentUpdate;
	family.childInitialize = CHILD_LIFETIME_TEN;
	if (!Begin(run, FamilyEffect(family), "OnCollide")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {3, 0});
	Advance(run, STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 0u);
	for (u32 frame = 2; frame <= 6; ++frame) {
		Advance(run, frame * STEP, STEP, {});
	}
	EXPECT_EQ(AliveCount(run, 1), 3u);
}

TEST_F(GraphEffectSystemsFixture, OverflowEventsCounted) {
	SystemsRun run;
	Family family;
	family.parentInitialize = LIFETIME_TEN;
	const str trigger = Trigger("Always", "Count", 3.0f);
	family.parentUpdate = trigger;
	family.childSettings = R"({"Capacity":16})";
	family.childInitialize = CHILD_LIFETIME_TEN;
	if (!Begin(run, FamilyEffect(family), "Capacity")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, STEP, {8, 0});
	Advance(run, STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 16u);
	EXPECT_EQ(DroppedOf(run, 1), 8u);
	Advance(run, 2 * STEP, STEP, {});
	EXPECT_EQ(AliveCount(run, 1), 16u);
	EXPECT_EQ(DroppedOf(run, 1), 32u);
}

TEST_F(GraphEffectSystemsFixture, StripParentHoldsIndex) {
	SystemsRun run;
	Family family;
	family.capacity = 2;
	family.parentInitialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[0.3]}}})";
	const str trigger = Trigger("Always");
	family.parentUpdate = trigger;
	family.childSettings = R"({"Data Type":"ParticleStrip","Strip Capacity":2,"Particle Per Strip Count":8})";
	family.childInitialize = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[0.6]}}})";
	if (!Begin(run, FamilyEffect(family), "StripHold")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	f32 time = 0.0f;
	const auto frame = [&](u32 parents) {
		Advance(run, time, STEP, {parents, 0});
		time += STEP;
	};
	frame(2);
	frame(0);
	frame(0);
	frame(0);
	ASSERT_EQ(AliveCount(run, 0), 0u);
	EXPECT_EQ(Strip(run.systems[1], 0).count, 3u);
	EXPECT_EQ(Strip(run.systems[1], 1).count, 3u);
	EXPECT_EQ(Strip(run.systems[1], 0).held, 1u);
	frame(1);
	EXPECT_EQ(AliveCount(run, 0), 0u);
	EXPECT_EQ(DroppedOf(run, 0), 1u);
	for (u32 i = 0; i < 4; ++i) {
		frame(0);
	}
	EXPECT_EQ(AliveCount(run, 1), 0u);
	EXPECT_EQ(Strip(run.systems[1], 0).held, 0u);
	frame(2);
	EXPECT_EQ(AliveCount(run, 0), 2u);
	EXPECT_EQ(DroppedOf(run, 0), 1u);
}

#endif
