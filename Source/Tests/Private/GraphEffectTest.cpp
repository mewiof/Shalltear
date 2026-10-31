#include "TestsPCH.h"

#include "GraphCurves.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifndef SHALLTEAR_SHARED
#include "EffectKernelHarness.h"
#include "EffectProgram.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
using namespace Shalltear::EffectKernelHarness;
#endif

namespace {

	constexpr std::string_view Fountain = R"({
	"_type": "EffectGraph",
	"version": 2,
	"nextId": 20,
	"settings": {},
	"properties": [
		{"id":1,"name":"Rate","reference":"rate","type":"Float","default":[25]}
	],
	"nodes": [
		{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}}
	],
	"contexts": [
		{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[
			{"id":3,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[10,"Out"]}}}
		]},
		{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":64},"flow":[[2,"In"]],"blocks":[
			{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}},
			{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[1,0,0]}}},
			{"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Composition":"Add"},"in":{"Velocity":{"value":[0,2,0]}}}
		]},
		{"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},
		{"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[8,"In"]],"blocks":[]}
	],
	"groups": [],
	"notes": []
}
)";

	GraphDocument Read(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	str With(std::string_view text, std::string_view from, std::string_view to) {
		str out(text);
		const size_t at = out.find(from);
		EXPECT_NE(at, str::npos) << from;
		EXPECT_EQ(out.find(from, at + 1), str::npos) << from;
		if (at != str::npos) {
			out.replace(at, from.size(), to);
		}
		return out;
	}

	bool Compile(std::string_view text, GraphEffect& out, Vector<GraphDiagnostic>& diagnostics) {
		const GraphDocument document = Read(text);
		return GraphEffectTarget::Compile(document, "test.sleffect", out, diagnostics);
	}

	Vector<str> StoredNames(const GraphEffectSystem& system) {
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		for (const GraphEffectStored& stored : system.stored) {
			names.push_back(stored.attribute);
		}
		return names;
	}

	bool ReadsExternal(const GraphProgram& program, std::string_view name) {
		for (const GraphValue& value : program.values) {
			if (value.op == GraphOp::External && program.externals[value.payload].name == name) {
				return true;
			}
		}
		return false;
	}

} // anonymous namespace

TEST(GraphEffectTest, TheContextsCarryTheirFlowSlots) {
	struct Expected {
		std::string_view id;
		std::span<const std::string_view> slots;
		bool output;
	};
	constexpr std::string_view SPAWN[] = {"Start", "Stop"};
	constexpr std::string_view IN[] = {"In"};
	const Expected contexts[] = {
		{GRAPH_EFFECT_EVENT, {}, true},
		{GRAPH_EFFECT_SPAWN, SPAWN, true},
		{GRAPH_EFFECT_INITIALIZE, IN, true},
		{GRAPH_EFFECT_UPDATE, IN, true},
		{GRAPH_EFFECT_OUTPUT, IN, false},
	};
	for (const Expected& expected : contexts) {
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(expected.id);
		ASSERT_NE(definition, nullptr) << expected.id;
		EXPECT_TRUE(std::equal(definition->flowInputs.begin(), definition->flowInputs.end(), expected.slots.begin(), expected.slots.end()))
			<< expected.id;
		EXPECT_EQ(definition->flowOutput, expected.output) << expected.id;
	}
}

TEST(GraphEffectTest, SetAttributeIsBuiltForItsAttribute) {
	constexpr std::string_view Blocks = R"({"_type":"EffectGraph","version":1,"contexts":[{"id":1,"def":"Context/Initialize","blocks":[
		{"id":2,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Composition":"Blend"}},
		{"id":3,"def":"Block/SetAttribute","settings":{"Attribute":"alive"}},
		{"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"size","Source":"Source"}},
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"alive","Composition":"Add"}}]}]})";
	GraphDocument document = Read(Blocks);
	GraphDefinitions definitions;
	const GraphContext& context = document.contexts[0];

	const GraphNodeDefinition* velocity = definitions.Find(document, context.blocks[0]);
	ASSERT_NE(velocity, nullptr);
	ASSERT_EQ(velocity->inputs.size(), 2u);
	EXPECT_EQ(velocity->inputs[0].name, "Velocity");
	EXPECT_EQ(velocity->inputs[0].type, GraphPortType::Float3);
	EXPECT_EQ(velocity->inputs[1].name, "Blend");
	ASSERT_EQ(velocity->outputs.size(), 1u);
	EXPECT_EQ(velocity->outputs[0].name, "velocity");
	EXPECT_EQ(velocity->name, "Blend Velocity");

	const GraphNodeDefinition* alive = definitions.Find(document, context.blocks[1]);
	ASSERT_NE(alive, nullptr);
	ASSERT_EQ(alive->inputs.size(), 1u);
	EXPECT_EQ(alive->inputs[0].type, GraphPortType::Boolean);
	EXPECT_EQ(alive->inputs[0].defaultValue[0], 1.0f);

	const GraphNodeDefinition* size = definitions.Find(document, context.blocks[2]);
	ASSERT_NE(size, nullptr);
	EXPECT_TRUE(size->inputs.empty());
	EXPECT_EQ(size->name, "Set Size from Source");

	str error;
	EXPECT_EQ(definitions.Find(document, context.blocks[3], &error), nullptr);
	EXPECT_NE(error.find("Boolean"), str::npos) << error;
}

TEST(GraphEffectTest, CurveAttributePerSampleMode) {
	constexpr std::string_view Blocks = R"({"_type":"EffectGraph","version":1,"contexts":[{"id":1,"def":"Context/Update","blocks":[
		{"id":2,"def":"Block/AttributeFromCurve","settings":{"Attribute":"size"}},
		{"id":3,"def":"Block/AttributeFromCurve","settings":{"Attribute":"velocity","Sample Mode":"Custom"}},
		{"id":4,"def":"Block/AttributeFromCurve","settings":{"Attribute":"scale","Mode":"Uniform","Sample Mode":"BySpeed"}},
		{"id":5,"def":"Block/AttributeFromCurve","settings":{"Attribute":"color","Composition":"Multiply","Alpha Composition":"Blend","Sample Mode":"RandomConstantPerParticle"}},
		{"id":6,"def":"Block/AttributeFromCurve","settings":{"Attribute":"color","Mode":"Uniform","Color Mode":"Alpha"}},
		{"id":7,"def":"Block/AttributeFromCurve","settings":{"Attribute":"alive"}}]}]})";
	GraphDocument document = Read(Blocks);
	GraphDefinitions definitions;
	const GraphContext& context = document.contexts[0];
	const auto names = [](std::span<const GraphPortDefinition> ports) {
		str joined;
		for (const GraphPortDefinition& port : ports) {
			joined += (joined.empty() ? "" : ",") + str(port.name);
		}
		return joined;
	};

	const GraphNodeDefinition* size = definitions.Find(document, context.blocks[0]);
	ASSERT_NE(size, nullptr);
	EXPECT_EQ(size->name, "Set Size Over Life");
	EXPECT_EQ(names(size->inputs), "Size");
	EXPECT_EQ(size->inputs[0].type, GraphPortType::Curve);
	EXPECT_EQ(names(size->outputs), "size");

	const GraphNodeDefinition* velocity = definitions.Find(document, context.blocks[1]);
	ASSERT_NE(velocity, nullptr);
	EXPECT_EQ(velocity->name, "Set Velocity Custom");
	EXPECT_EQ(names(velocity->inputs), "Velocity_x,Velocity_y,Velocity_z,Sample Time");

	const GraphNodeDefinition* scale = definitions.Find(document, context.blocks[2]);
	ASSERT_NE(scale, nullptr);
	EXPECT_EQ(scale->name, "Set Scale By Speed");
	EXPECT_EQ(names(scale->inputs), "Scale,Speed Range");
	EXPECT_EQ(scale->inputs[1].defaultValue[1], 1.0f);

	const GraphNodeDefinition* color = definitions.Find(document, context.blocks[3]);
	ASSERT_NE(color, nullptr);
	EXPECT_EQ(color->name, "Multiply Color Random Constant/Particle");
	EXPECT_EQ(names(color->inputs), "Color,Seed,Blend");
	EXPECT_EQ(color->inputs[0].type, GraphPortType::Gradient);
	EXPECT_FALSE(color->inputs[0].defaultJson.empty());
	EXPECT_EQ(names(color->outputs), "color,alpha");

	const GraphNodeDefinition* alpha = definitions.Find(document, context.blocks[4]);
	ASSERT_NE(alpha, nullptr);
	EXPECT_EQ(alpha->inputs[0].type, GraphPortType::Curve);
	EXPECT_EQ(names(alpha->outputs), "alpha");

	str error;
	EXPECT_EQ(definitions.Find(document, context.blocks[5], &error), nullptr);
	EXPECT_NE(error.find("Boolean"), str::npos) << error;

	const GraphNodeDefinition* table = GraphNodeLibrary::Find(GRAPH_EFFECT_ATTRIBUTE_FROM_CURVE);
	ASSERT_NE(table, nullptr);
	const auto shown = [&](u32 block, std::string_view key) {
		for (u32 index = 0; index < table->settings.size(); ++index) {
			if (table->settings[index].key == key) {
				return GraphNodeLibrary::SettingActive(table->settings, index, context.blocks[block]);
			}
		}
		ADD_FAILURE() << "no setting " << key;
		return false;
	};
	EXPECT_FALSE(shown(0, "Color Mode"));
	EXPECT_FALSE(shown(0, "Mode"));
	EXPECT_TRUE(shown(1, "Mode"));
	EXPECT_FALSE(shown(1, "Alpha Composition"));
	EXPECT_TRUE(shown(3, "Color Mode"));
	EXPECT_TRUE(shown(3, "Alpha Composition"));
	EXPECT_TRUE(shown(3, "Sample Mode"));
}

TEST(GraphEffectTest, FountainStoresWhatIsRead) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Fountain, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.systems.size(), 1u);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_EQ(system.initialize, 4u);
	EXPECT_EQ(system.update, 8u);
	EXPECT_EQ(system.capacity, 64u);
	EXPECT_FALSE(system.world);

	const Vector<str> stored = StoredNames(system);
	const Vector<str> expected{{"position", "velocity", "age", "lifetime"}, GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(stored, expected);
	ASSERT_EQ(system.stored.size(), 4u);
	EXPECT_EQ(system.stored[1].slot, 3u);
	EXPECT_EQ(system.stored[3].slot, 7u);
	EXPECT_EQ(system.attributeCount, 8u);

	ASSERT_EQ(system.outputs.size(), 1u);
	EXPECT_EQ(system.outputs[0].blend, RHI::BlendMode::Alpha);
	EXPECT_NE(system.initializeKernel.find("#pragma shalltear_kernel"), str::npos);
	EXPECT_NE(system.updateKernel.find("#pragma shalltear_kernel"), str::npos);
	EXPECT_EQ(system.initializeLines.size(), static_cast<size_t>(std::count(system.initializeKernel.begin(), system.initializeKernel.end(), '\n')));

	ASSERT_EQ(system.spawners.size(), 1u);
	ASSERT_EQ(effect.spawners.size(), 1u);
	const GraphEffectSpawner& spawner = effect.spawners[0];
	EXPECT_EQ(spawner.start, (Vector<str>{{"OnPlay"}, GetMemoryResource(MemTag::Editor)}));
	EXPECT_EQ(spawner.stop, (Vector<str>{{"OnStop"}, GetMemoryResource(MemTag::Editor)}));
	ASSERT_EQ(spawner.blocks.size(), 1u);
	EXPECT_EQ(spawner.blocks[0].kind, GraphEffectSpawnBlock::Kind::ConstantRate);
	ASSERT_EQ(spawner.blocks[0].program.roots.size(), 1u);
	EXPECT_TRUE(ReadsExternal(spawner.blocks[0].program, "rate"));

	ASSERT_EQ(effect.propertyFields.size(), 1u);
	EXPECT_EQ(effect.propertyFields[0].offset, GraphEffectTarget::FRAME_HEADER_BYTES);
	EXPECT_EQ(effect.batchesOffset, GraphEffectTarget::FRAME_HEADER_BYTES + 16);
	ASSERT_EQ(system.bounds.roots.size(), 3u);
}

TEST(GraphEffectTest, BoundsCenterIsInSpacePortReads) {
	GraphDefinitions definitions;
	const GraphDocument fountain = Read(Fountain);
	GraphNode initialize;
	initialize.def = str(GRAPH_EFFECT_INITIALIZE);
	const GraphNodeDefinition* definition = definitions.Find(fountain, initialize);
	ASSERT_NE(definition, nullptr);
	const auto space = [&](std::string_view name) {
		for (const GraphPortDefinition& port : definition->inputs) {
			if (port.name == name) {
				return port.space;
			}
		}
		ADD_FAILURE() << "Initialize has no " << name;
		return GraphPortSpace::None;
	};
	EXPECT_EQ(space("Bounds Center"), GraphPortSpace::Position);
	EXPECT_EQ(space("Bounds Size"), GraphPortSpace::None);

	const str world = With(Fountain, R"("settings":{"Capacity":64},)", R"("settings":{"Capacity":64,"Space":"World"},)");
	for (const bool worldBox : {false, true}) {
		const str graph = worldBox ? With(world, R"("flow":[[2,"In"]],"blocks":[)", R"("in":{"Bounds Center":{"value":[0,1,0],"space":"World"}},"flow":[[2,"In"]],"blocks":[)")
								   : world;
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
		ASSERT_EQ(effect.systems.size(), 1u);
		EXPECT_TRUE(effect.systems[0].world);
		EXPECT_EQ(effect.systems[0].boundsWorld, worldBox) << (worldBox ? "a World center was read Local" : "a World system's center is not Local by default");
	}
}

namespace {

	str FountainWith(std::string_view settings, std::string_view inputs = {}) {
		return With(Fountain, R"("settings":{"Capacity":64},"flow":[[2,"In"]],)",
					std::format(R"("settings":{{"Capacity":64{}}},{}"flow":[[2,"In"]],)", settings, inputs));
	}

	bool IsZero(const GraphProgram& program, u32 root) {
		const GraphValue& value = program.values[program.roots[root].value];
		if (value.op != GraphOp::Constant) {
			return false;
		}
		const u32 components = GraphTypes::Components(value.type);
		return std::all_of(program.constants.begin() + value.payload, program.constants.begin() + value.payload + components, [](f32 c) { return c == 0.0f; });
	}

} // anonymous namespace

TEST(GraphEffectTest, BoundsModeShowsPorts) {
	GraphDefinitions definitions;
	const GraphDocument fountain = Read(Fountain);
	const auto shows = [&](std::string_view mode, std::string_view port) {
		GraphNode initialize;
		initialize.def = str(GRAPH_EFFECT_INITIALIZE);
		if (!mode.empty()) {
			initialize.settings.push_back({str(GRAPH_EFFECT_BOUNDS_SETTING_MODE), "\"" + str(mode) + "\""});
		}
		const GraphNodeDefinition* definition = definitions.Find(fountain, initialize);
		for (u32 index = 0; definition && index < definition->inputs.size(); ++index) {
			if (definition->inputs[index].name == port) {
				return GraphNodeLibrary::InputActive(*definition, index, initialize);
			}
		}
		ADD_FAILURE() << "Initialize has no " << port;
		return false;
	};
	for (const std::string_view mode : {std::string_view(), std::string_view("Recorded")}) {
		EXPECT_TRUE(shows(mode, GRAPH_EFFECT_BOUNDS_CENTER) && shows(mode, GRAPH_EFFECT_BOUNDS_SIZE) && shows(mode, GRAPH_EFFECT_BOUNDS_PADDING));
	}
	EXPECT_TRUE(shows("Manual", GRAPH_EFFECT_BOUNDS_CENTER) && shows("Manual", GRAPH_EFFECT_BOUNDS_SIZE));
	EXPECT_FALSE(shows("Manual", GRAPH_EFFECT_BOUNDS_PADDING));
	EXPECT_FALSE(shows("Automatic", GRAPH_EFFECT_BOUNDS_CENTER) || shows("Automatic", GRAPH_EFFECT_BOUNDS_SIZE));
	EXPECT_TRUE(shows("Automatic", GRAPH_EFFECT_BOUNDS_PADDING));

	constexpr std::string_view STORED = R"("in":{"Bounds Center":{"value":[3,3,3]},"Bounds Size":{"value":[4,4,4]},"Bounds Padding":{"value":[5,5,5]}},)";
	const auto program = [&](std::string_view mode, GraphProgram& out) {
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(Compile(FountainWith(std::format(R"(,"Bounds Setting Mode":"{}")", mode), STORED), effect, diagnostics))
			<< (diagnostics.empty() ? str() : diagnostics[0].message);
		EXPECT_EQ(effect.systems[0].boundsMode, mode == "Recorded" ? GRAPH_EFFECT_BOUNDS_RECORDED : mode == "Manual" ? GRAPH_EFFECT_BOUNDS_MANUAL
																													 : GRAPH_EFFECT_BOUNDS_AUTOMATIC);
		out = effect.systems[0].bounds;
	};
	GraphProgram recorded;
	program("Recorded", recorded);
	ASSERT_EQ(recorded.roots.size(), 3u);
	EXPECT_FALSE(IsZero(recorded, 0) || IsZero(recorded, 1) || IsZero(recorded, 2));
	GraphProgram manual;
	program("Manual", manual);
	ASSERT_EQ(manual.roots.size(), 3u);
	EXPECT_FALSE(IsZero(manual, 0) || IsZero(manual, 1));
	EXPECT_TRUE(IsZero(manual, 2));
	GraphProgram automatic;
	program("Automatic", automatic);
	ASSERT_EQ(automatic.roots.size(), 3u);
	EXPECT_TRUE(IsZero(automatic, 0) && IsZero(automatic, 1));
	EXPECT_FALSE(IsZero(automatic, 2));

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(FountainWith(R"(,"Bounds Setting Mode":"Sideways")"), effect, diagnostics));
	EXPECT_TRUE(!diagnostics.empty() && diagnostics[0].node == 4 && diagnostics[0].message.find("Bounds Setting Mode") != str::npos);
}

TEST(GraphEffectTest, AutomaticSystemsMeasured) {
	const auto compile = [](const str& text, const GraphEffectCompileOptions& options, GraphEffect& out) {
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		const bool compiled = GraphEffectTarget::Compile(Read(text), "test.sleffect", out, diagnostics, nullptr, options);
		EXPECT_TRUE(compiled) << (diagnostics.empty() ? str() : diagnostics[0].message);
		return compiled && out.systems.size() == 1;
	};
	const auto measured = [](const GraphEffectSystem& system) {
		const bool kernels = system.updateKernel.find("SltParticleReduceBounds") != str::npos && system.initializeKernel.find("SltParticleReduceBounds") != str::npos;
		EXPECT_EQ(kernels, system.measuresBounds);
		EXPECT_EQ(system.updateBoundsRoot != GRAPH_EFFECT_NO_BINDING, system.measuresBounds);
		return system.measuresBounds;
	};
	const GraphEffectCompileOptions recording{.recordBounds = true};
	GraphEffect effect;
	ASSERT_TRUE(compile(str(Fountain), {}, effect));
	EXPECT_FALSE(measured(effect.systems[0]));
	EXPECT_EQ(effect.timing.cullingFlags, GRAPH_EFFECT_CULL_DEFAULT);
	ASSERT_TRUE(compile(str(Fountain), recording, effect));
	EXPECT_TRUE(measured(effect.systems[0]));
	ASSERT_TRUE(compile(FountainWith(R"(,"Bounds Setting Mode":"Manual")"), recording, effect));
	EXPECT_FALSE(measured(effect.systems[0]));
	ASSERT_TRUE(compile(With(FountainWith("", R"("in":{"Bounds Size":{"from":[11,"Out"]}},)"), R"("nodes": [)",
							 R"("nodes": [{"id":11,"def":"Input/Vector3","in":{"X":{"value":[2]}}},)"),
						recording, effect));
	EXPECT_FALSE(measured(effect.systems[0]));

	const str automatic = With(FountainWith(R"(,"Bounds Setting Mode":"Automatic")"), R"("settings": {},)", R"("settings": {"cullingFlags":3},)");
	ASSERT_TRUE(compile(automatic, {}, effect));
	EXPECT_TRUE(measured(effect.systems[0]));
	EXPECT_NE(effect.systems[0].updateKernel.find("#include \"VisualEffects/ParticleBounds.hlsli\""), str::npos);
	EXPECT_EQ(effect.timing.cullingFlags, 0u);
	EXPECT_TRUE(GraphEffectTarget::HasAutomaticBounds(Read(automatic)));
	EXPECT_EQ(GraphEffectTarget::Timing(Read(automatic)).cullingFlags, 0u);
	EXPECT_FALSE(GraphEffectTarget::HasAutomaticBounds(Read(Fountain)));

	ASSERT_TRUE(compile(FountainWith(R"(,"Bounds Setting Mode":"Automatic","Simulation Target":"CPU")"), {}, effect));
	const GraphEffectSystem& cpu = effect.systems[0];
	ASSERT_TRUE(cpu.measuresBounds);
	const auto roots = [](const str& assembly) {
		u32 count = 0;
		for (size_t at = assembly.find("root "); at != str::npos; at = assembly.find("root ", at + 1)) {
			++count;
		}
		return count;
	};
	ASSERT_NE(cpu.initializeBoundsRoot, GRAPH_EFFECT_NO_BINDING);
	ASSERT_NE(cpu.updateBoundsRoot, GRAPH_EFFECT_NO_BINDING);
	EXPECT_EQ(roots(cpu.initializeAssembly), cpu.initializeBoundsRoot + 2);
	EXPECT_EQ(roots(cpu.updateAssembly), cpu.updateBoundsRoot + 2);
}

TEST(GraphEffectTest, UnreadAttributeNotStored) {
	str graph = With(Fountain, R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}},)",
					 R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"mass"},"in":{"Mass":{"value":[3]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[0.5]}}},)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const Vector<str> expected{{"position", "velocity", "size"}, GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(StoredNames(effect.systems[0]), expected);
}

TEST(GraphEffectTest, ASourceAttributeRidesTheSpawnBatch) {
	str graph = With(Fountain, R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}},)",
					 R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"position","Source":"Source"}},)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const GraphEffectSystem& system = effect.systems[0];
	ASSERT_EQ(system.source.size(), 1u);
	EXPECT_EQ(system.source[0].name, "position");
	EXPECT_EQ(system.source[0].offset, 4u);
	EXPECT_EQ(system.source[0].components, 3u);
	EXPECT_EQ(system.batchStride, 16u);
	EXPECT_NE(system.initializeKernel.find("SltParticleEffectBatch"), str::npos);

	GraphEffect plain;
	ASSERT_TRUE(Compile(Fountain, plain, diagnostics));
	EXPECT_TRUE(plain.systems[0].source.empty());
	EXPECT_EQ(plain.systems[0].initializeKernel.find("SltParticleEffectBatch"), str::npos);
}

TEST(GraphEffectTest, EventsStartAndStopSpawn) {
	str graph = With(Fountain, R"({"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[)",
					 R"({"id":12,"def":"Context/Event","x":0,"y":-200,"settings":{"Event Name":"Hit"},"blocks":[]},
		{"id":13,"def":"Context/Event","x":200,"y":-200,"settings":{"Event Name":"Halt"},"blocks":[]},
		{"id":2,"def":"Context/Spawn","x":0,"y":0,"flow":[[12,"Start"],[13,"Stop"]],"blocks":[)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.spawners.size(), 1u);
	EXPECT_EQ(effect.spawners[0].start, (Vector<str>{{"Hit"}, GetMemoryResource(MemTag::Editor)}));
	EXPECT_EQ(effect.spawners[0].stop, (Vector<str>{{"Halt"}, GetMemoryResource(MemTag::Editor)}));
	EXPECT_EQ(effect.events, (Vector<str>{{"Halt", "Hit", "OnPlay", "OnStop"}, GetMemoryResource(MemTag::Editor)}));
}

TEST(GraphEffectTest, EverySystemIsNamedByUsualRule) {
	constexpr std::string_view typed[] = {"Sparks", "", "Sparks (3)", "Sparks", "", "Smoke (x)"};
	constexpr std::string_view named[] = {"Sparks", "System", "Sparks (1)", "Sparks (2)", "System (1)", "Smoke (x)"};
	str contexts;
	for (u32 i = 0; i < std::size(typed); ++i) {
		const str setting = typed[i].empty() && i == 1 ? str() : std::format(R"(,"System Name":"{}")", typed[i]);
		contexts += std::format(R"({}{{"id":{},"def":"Context/Initialize","settings":{{"Capacity":8{}}},"blocks":[]}},
{{"id":{},"def":"Context/OutputParticlePrimitive","flow":[[{},"In"]],"blocks":[]}})",
								i == 0 ? "" : ",", 10 + 2 * i, setting, 11 + 2 * i, 10 + 2 * i);
	}
	const str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[],"contexts":[{}]}})", contexts);
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.systems.size(), std::size(named));
	for (u32 i = 0; i < std::size(named); ++i) {
		EXPECT_EQ(effect.systems[i].name, named[i]) << "system " << i << ", typed '" << typed[i] << "'";
	}
}

TEST(GraphEffectTest, ARefusalNamesWhatTheUserFixes) {
	constexpr std::string_view Block5 = R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}},)";
	constexpr std::string_view Update = R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},)";
	struct Case {
		const char* what;
		str graph;
		u32 node;
	};
	const Case cases[] = {
		{"a spawn block in an Initialize", With(Fountain, Block5, R"({"id":5,"def":"Block/ConstantSpawnRate"},)"), 5},
		{"a Set Attribute in a Spawn", With(Fountain, R"({"id":3,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[10,"Out"]}}})", R"({"id":3,"def":"Block/SetAttribute","settings":{"Attribute":"size"}})"),
		 3},
		{"a capacity of 0", With(Fountain, R"("settings":{"Capacity":64})", R"("settings":{"Capacity":0})"), 4},
		{"a Spawn feeding a Spawn", With(Fountain, Update, str(Update) + R"({"id":14,"def":"Context/Spawn","x":0,"y":0,"flow":[[2,"Start"]],"blocks":[]},)"), 14},
		{"a link at a slot the context has not", With(Fountain, R"("flow":[[2,"In"]])", R"("flow":[[2,"Start"]])"), 4},
		{"an Update fed by two Initializes",
		 With(Fountain, Update,
			  R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"],[15,"In"]],"blocks":[]},{"id":15,"def":"Context/Initialize","x":300,"y":200,"blocks":[]},)"),
		 8},
		{"a Boolean added to", With(Fountain, Block5, R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"alive","Composition":"Add"}},)"), 5},
		{"a source read in an Update", With(Fountain, R"("flow":[[4,"In"]],"blocks":[])", R"("flow":[[4,"In"]],"blocks":[{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"size","Source":"Source"}}])"),
		 16},
		{"a texture property",
		 With(Fountain, R"("default":[25]})", R"("default":[25]},
		{"id":17,"name":"Albedo","reference":"albedo","type":"Texture2D"})"),
		 0},
		{"a derivative in a kernel",
		 With(With(Fountain, R"("in":{"Velocity":{"value":[1,0,0]}})", R"("in":{"Velocity":{"from":[18,"Out"]}})"), R"("settings":{"property":1}})",
			  R"("settings":{"property":1}},
		{"id":18,"def":"Math/DDX","x":0,"y":100})"),
		 18},
		{"a value no kernel supplies",
		 With(With(Fountain, R"("in":{"Velocity":{"value":[1,0,0]}})", R"("in":{"Velocity":{"from":[19,"Out"]}})"), R"("settings":{"property":1}})",
			  R"("settings":{"property":1}},
		{"id":19,"def":"Input/UV","x":0,"y":100})"),
		 19},
		{"a node shader", With(Fountain, R"("_type": "EffectGraph")", R"("_type": "NodeShader")"), 0},
	};
	for (const Case& refusal : cases) {
		GraphEffect effect;
		effect.systems.resize(1);
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		EXPECT_FALSE(Compile(refusal.graph, effect, diagnostics)) << refusal.what << " compiled";
		EXPECT_EQ(effect.systems.size(), 1u) << refusal.what;
		const bool named = std::any_of(diagnostics.begin(), diagnostics.end(), [&](const GraphDiagnostic& d) { return d.node == refusal.node; });
		EXPECT_TRUE(named) << refusal.what << ' ' << refusal.node << "; first said: " << (diagnostics.empty() ? str("nothing") : std::to_string(diagnostics[0].node) + " " + diagnostics[0].message);
	}
}

TEST(GraphEffectTest, TheSameGraphGivesTheSameKernels) {
	GraphEffect first;
	GraphEffect second;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Fountain, first, diagnostics));
	ASSERT_TRUE(Compile(Fountain, second, diagnostics));
	EXPECT_EQ(first.systems[0].initializeKernel, second.systems[0].initializeKernel);
	EXPECT_EQ(first.systems[0].updateKernel, second.systems[0].updateKernel);
	EXPECT_EQ(first.systems[0].outputs[0].shader, second.systems[0].outputs[0].shader);
}

TEST(GraphEffectTest, SplicedBlockReadsWhatBlockAboveWrote) {
	GraphProgram stack;
	GraphProgramBuilder above(stack);
	const GraphValueId written = above.Scalar(4.0f);

	GraphProgram block;
	GraphProgramBuilder builder(block);
	const GraphValueId current = builder.External(GraphValueType::Float, "Attribute/size", "sltAttr_size");
	const GraphValueId time = builder.External(GraphValueType::Float4, "TimeParameters", "SltParticleEffectTime()");
	block.roots.push_back({builder.Add(current, builder.Swizzle(time, "w")), GraphStageVertex});

	HashMap<str, GraphValueId> map{GetMemoryResource(MemTag::Editor)};
	map["Attribute/size"] = written;
	Vector<GraphValueId> roots{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectSplice::Append(stack, block, map, roots));
	ASSERT_EQ(roots.size(), 1u);
	const GraphValue& sum = stack.At(roots[0]);
	EXPECT_EQ(sum.op, GraphOp::Add);
	EXPECT_EQ(sum.operands[0], written);
	ASSERT_EQ(stack.externals.size(), 1u);
	EXPECT_EQ(stack.externals[0].name, "TimeParameters");
	EXPECT_EQ(stack.labels.size(), stack.values.size());
}

#ifndef SHALLTEAR_SHARED

TEST(GraphEffectTest, RandomSetDrawsBetweenAAndB) {
	constexpr std::string_view Blocks = R"({"_type":"EffectGraph","version":3,"contexts":[{"id":1,"def":"Context/Initialize","blocks":[
		{"id":2,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Random":"PerComponent"}},
		{"id":3,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime","Random":"PerComponent"}},
		{"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"size","Random":"Uniform","Source":"Source"}},
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"alive","Random":"Uniform"}}]}]})";
	GraphDocument document = Read(Blocks);
	GraphDefinitions definitions;
	const GraphContext& context = document.contexts[0];

	const GraphNodeDefinition* velocity = definitions.Find(document, context.blocks[0]);
	ASSERT_NE(velocity, nullptr);
	ASSERT_EQ(velocity->inputs.size(), 2u);
	EXPECT_EQ(velocity->inputs[0].name, "A");
	EXPECT_EQ(velocity->inputs[1].name, "B");
	EXPECT_EQ(velocity->inputs[1].type, GraphPortType::Float3);
	EXPECT_EQ(velocity->name, "Set Velocity Random (Per Component)");

	const GraphNodeDefinition* lifetime = definitions.Find(document, context.blocks[1]);
	ASSERT_NE(lifetime, nullptr);
	EXPECT_EQ(lifetime->name, "Set Lifetime Random (Uniform)");

	const GraphNodeDefinition* size = definitions.Find(document, context.blocks[2]);
	ASSERT_NE(size, nullptr);
	EXPECT_TRUE(size->inputs.empty());
	EXPECT_EQ(size->name, "Set Size from Source");

	str error;
	EXPECT_EQ(definitions.Find(document, context.blocks[3], &error), nullptr);
	EXPECT_NE(error.find("range"), str::npos) << error;
}

TEST(GraphEffectTest, DrawSaltedByNode) {
	str graph = With(Fountain, R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Composition":"Add"},"in":{"Velocity":{"value":[0,2,0]}}})",
					 R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Composition":"Add","Random":"PerComponent"},"in":{"A":{"value":[-1,0,-1]},"B":{"value":[1,2,1]}}})");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const GraphEffectSystem& system = effect.systems[0];
	for (u32 component = 0; component < 3; ++component) {
		EXPECT_NE(system.initializeKernel.find(std::format(", {}u)", 7 * 8 + component)), str::npos) << "component " << component;
	}
	EXPECT_NE(system.initializeKernel.find("SltParticleRandomKey(sltParticleId)"), str::npos);
	const Vector<str> unkept = StoredNames(system);
	EXPECT_EQ(std::find(unkept.begin(), unkept.end(), str("particleId")), unkept.end());

	const str updating = With(graph, R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]})",
							  R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[
			{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size","Random":"Uniform"}}]})");
	GraphEffect kept;
	ASSERT_TRUE(Compile(updating, kept, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const Vector<str> stored = StoredNames(kept.systems[0]);
	EXPECT_NE(std::find(stored.begin(), stored.end(), str("particleId")), stored.end());
	EXPECT_NE(kept.systems[0].initializeKernel.find("asfloat(sltParticleId)"), str::npos);
	EXPECT_NE(kept.systems[0].updateKernel.find("asuint(SltParticleLoad("), str::npos);

	constexpr std::string_view Numbers = R"({"_type":"EffectGraph","version":3,"nodes":[
		{"id":20,"def":"Random/RandomNumber"},{"id":21,"def":"Random/RandomNumber"},
		{"id":22,"def":"Random/RandomNumber","settings":{"Constant":false}},{"id":23,"def":"Random/RandomNumber","settings":{"Constant":false}},
		{"id":24,"def":"Math/Add","in":{"A":{"from":[20,"r"]},"B":{"from":[21,"r"]}}},
		{"id":25,"def":"Math/Add","in":{"A":{"from":[22,"r"]},"B":{"from":[23,"r"]}}}],
		"contexts":[{"id":1,"def":"Context/Initialize","blocks":[
		{"id":2,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[24,"Out"]}}},
		{"id":3,"def":"Block/SetAttribute","settings":{"Attribute":"mass"},"in":{"Mass":{"from":[25,"Out"]}}}]},
		{"id":4,"def":"Context/OutputParticlePrimitive","flow":[[1,"In"]],"blocks":[]}]})";
	GraphEffect numbers;
	ASSERT_TRUE(Compile(Numbers, numbers, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const str& kernel = numbers.systems[0].initializeKernel;
	size_t draws = 0;
	for (size_t at = kernel.find("ShalltearGraphRandom("); at != str::npos; at = kernel.find("ShalltearGraphRandom(", at + 1)) {
		++draws;
	}
	EXPECT_EQ(draws, 2u);
}

TEST(GraphEffectTest, GetAttributeReadsCurrentOrSource) {
	constexpr std::string_view Reads = R"({"_type":"EffectGraph","version":3,"nodes":[
		{"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnCount","Location":"Source"}},
		{"id":21,"def":"Attribute/GetAttribute","settings":{"Attribute":"velocity"}}],
		"contexts":[{"id":1,"def":"Context/Initialize","blocks":[
		{"id":2,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"spawnCount"]}}},
		{"id":3,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[21,"velocity"]}}}]},
		{"id":4,"def":"Context/OutputParticlePrimitive","flow":[[1,"In"]],"blocks":[]}]})";
	GraphDocument document = Read(Reads);
	GraphDefinitions definitions;
	const GraphNodeDefinition* count = definitions.Find(document, document.nodes[0]);
	ASSERT_NE(count, nullptr);
	EXPECT_EQ(count->name, "Get Attribute: spawnCount (Source)");
	const GraphNodeDefinition* velocity = definitions.Find(document, document.nodes[1]);
	ASSERT_NE(velocity, nullptr);
	ASSERT_EQ(velocity->outputs.size(), 1u);
	EXPECT_EQ(velocity->outputs[0].type, GraphPortType::Float3);

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Reads, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	EXPECT_TRUE(effect.systems[0].source.empty());
	EXPECT_NE(effect.systems[0].initializeKernel.find("float(SltParticleEffectWord(sltBatch))"), str::npos);

	str outside = With(Reads, R"({"id":4,"def":"Context/OutputParticlePrimitive","flow":[[1,"In"]],"blocks":[]})",
					   R"({"id":4,"def":"Context/OutputParticlePrimitive","flow":[[1,"In"]],"blocks":[
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[20,"spawnCount"]}}}]})");
	GraphEffect refused;
	EXPECT_FALSE(Compile(outside, refused, diagnostics));
	const bool named = std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& diagnostic) {
		return diagnostic.node == 20 && diagnostic.message.find("only an Initialize") != str::npos;
	});
	EXPECT_TRUE(named) << (diagnostics.empty() ? str("no diagnostic") : diagnostics[0].message);
}

TEST(GraphEffectTest, SpawnRangesAreCpuPrograms) {
	str graph = With(Fountain, R"({"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[)",
					 R"({"id":2,"def":"Context/Spawn","x":0,"y":0,"settings":{"Loop Duration":"Constant","Loop Count":"Random","Delay Before Loop":"Random"},
			"in":{"Loop Duration":{"value":[2]},"Loop Count Range":{"value":[2,5]}},"blocks":[)");
	graph = With(graph, R"({"id":3,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[10,"Out"]}}})",
				 R"({"id":3,"def":"Block/Burst","settings":{"Spawn Mode":"Random"},"in":{"Count Range":{"value":[4,9]}}})");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const GraphEffectSpawner& spawner = effect.spawners[0];
	ASSERT_EQ(spawner.loop.roots.size(), 4u);
	EXPECT_EQ(spawner.loop.ConstantOf(spawner.loop.roots[0].value)[0], 2.0f);
	EXPECT_EQ(spawner.loop.At(spawner.loop.roots[1].value).op, GraphOp::Truncate);
	EXPECT_EQ(spawner.loop.ConstantOf(spawner.loop.roots[3].value)[0], 0.0f);
	EXPECT_NE(spawner.loopAssembly.find("random 1 "), str::npos);
	EXPECT_NE(spawner.loopAssembly.find("input 1 RandomKey/Effect"), str::npos);
	ASSERT_EQ(spawner.blocks.size(), 1u);
	EXPECT_EQ(spawner.blocks[0].kind, GraphEffectSpawnBlock::Kind::SingleBurst);
	EXPECT_NE(spawner.blocks[0].assembly.find("random 1 "), str::npos);
}

TEST(GraphEffectTest, TheEffectsOwnNodesAreItsAlone) {
	for (std::string_view id : {GRAPH_EFFECT_GET_ATTRIBUTE, GRAPH_EFFECT_AGE_OVER_LIFETIME, GRAPH_EFFECT_RANDOM_NUMBER, GRAPH_EFFECT_TOTAL_TIME,
								GRAPH_EFFECT_DELTA_TIME, GRAPH_EFFECT_SET_SPAWN_EVENT, GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_SAMPLE_CURVE}) {
		EXPECT_TRUE(GraphEffectRules::IsEffectOnly(id)) << id;
	}
	EXPECT_FALSE(GraphEffectRules::IsEffectOnly("Math/Add"));
	EXPECT_FALSE(GraphEffectRules::IsEffectOnly("Input/Time"));
}

namespace {

	str CurveJson(std::string_view pre, std::string_view post) {
		return std::format(R"({{"keys":[{{"time":0,"value":0,"outTangent":2}},{{"time":0.5,"value":1,"inTangent":-1,"outTangent":3,"broken":true}},)"
						   R"({{"time":1,"value":0.5,"outTangentMode":"Constant"}},{{"time":2,"value":2,"inTangent":-1,"outTangent":1}}],)"
						   R"("preWrapMode":"{}","postWrapMode":"{}"}})",
						   pre, post);
	}

	str SampledRate(std::string_view curve, std::string_view time) {
		return std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[
			{{"id":20,"def":"Sampling/SampleCurve","in":{{"Curve":{{"value":{}}},"Time":{}}}}},
			{{"id":21,"def":"BuiltIn/TotalTime"}}],"contexts":[
			{{"id":1,"def":"Context/Spawn","blocks":[{{"id":2,"def":"Block/ConstantSpawnRate","in":{{"Rate":{{"from":[20,"s"]}}}}}}]}},
			{{"id":3,"def":"Context/Initialize","flow":[[1,"In"]],"blocks":[]}},
			{{"id":4,"def":"Context/OutputParticlePrimitive","flow":[[3,"In"]],"blocks":[]}}]}})",
						   curve, time);
	}

	constexpr std::string_view WRAPS[][2] = {{"Clamp", "Clamp"}, {"Loop", "Loop"}, {"PingPong", "PingPong"}, {"Loop", "PingPong"}, {"Clamp", "Loop"}};

} // anonymous namespace

TEST(GraphEffectTest, ConstantTimeCurveFolds) {
	for (const auto& wrap : WRAPS) {
		const str json = CurveJson(wrap[0], wrap[1]);
		AnimationCurve curve;
		str error;
		ASSERT_TRUE(GraphCurves::Parse(json, curve, error)) << error;
		for (i32 step = -12; step <= 20; ++step) {
			const f32 time = static_cast<f32>(step) * 0.25f;
			GraphEffect effect;
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			ASSERT_TRUE(Compile(SampledRate(json, std::format(R"({{"value":[{}]}})", time)), effect, diagnostics))
				<< (diagnostics.empty() ? str() : diagnostics[0].message);
			const GraphProgram& program = effect.spawners[0].blocks[0].program;
			const std::span<const f32> folded = program.ConstantOf(program.roots[0].value);
			ASSERT_EQ(folded.size(), 1u);
			EXPECT_NEAR(folded[0], curve.Evaluate(time), 1e-5f) << wrap[0] << "/" << wrap[1] << " at " << time;
		}
	}
}

TEST(GraphEffectTest, CurveKeyLimitEnforced) {
	AnimationCurve curve;
	str error;
	ASSERT_TRUE(GraphCurves::Parse(CurveJson("Loop", "PingPong"), curve, error)) << error;
	AnimationCurve again;
	ASSERT_TRUE(GraphCurves::Parse(GraphCurves::Write(curve), again, error)) << error;
	EXPECT_EQ(again, curve);
	EXPECT_EQ(curve.keys[2].outTangentMode, KeyframeTangentMode::Constant);
	EXPECT_TRUE(curve.keys[1].broken);
	EXPECT_EQ(curve.postWrapMode, AnimationCurveWrapMode::PingPong);

	AnimationCurve many;
	for (u32 index = 0; index <= GRAPH_CURVE_KEYS; ++index) {
		Keyframe key;
		key.time = static_cast<f32>(index);
		many.keys.push_back(key);
	}
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(SampledRate(GraphCurves::Write(many), R"({"value":[0]})"), effect, diagnostics));
	ASSERT_FALSE(diagnostics.empty());
	EXPECT_EQ(diagnostics[0].node, 20u);
	EXPECT_NE(diagnostics[0].message.find("input 'Curve' holds 65 keys"), str::npos) << diagnostics[0].message;

	const str unconnected = With(SampledRate("{}", R"({"value":[0.25]})"), R"("Curve":{"value":{}},)", "");
	diagnostics.clear();
	ASSERT_TRUE(Compile(unconnected, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const GraphProgram& program = effect.spawners[0].blocks[0].program;
	EXPECT_NEAR(program.ConstantOf(program.roots[0].value)[0], 0.25f, 1e-6f);
}

TEST_F(GraphEffectKernelFixture, DataGraphRunsAndReadsBack) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(Read(Fountain), "fountain.sleffect", effect, diagnostics));
	const GraphEffectSystem& compiled = effect.systems[0];
	if (!Import(_initialize, compiled.initializeKernel, "Initialize.hlsl") || !Import(_update, compiled.updateKernel, "Update.hlsl")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EXPECT_TRUE(Import(_output, compiled.outputs[0].shader, "Output.hlsl"));

	ParticleSystemDesc desc;
	desc.capacity = compiled.capacity;
	desc.attributeCount = compiled.attributeCount;
	desc.initialize = Handle<ShaderAsset>(&_initialize);
	desc.update = Handle<ShaderAsset>(&_update);
	ParticleSystem system;
	ASSERT_TRUE(system.Create(desc));

	Step(system, effect, 0.0f, 0.5f, 10);
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
	ASSERT_EQ(Alive(system, indices), 10u);
	for (u32 index : indices) {
		EXPECT_EQ(Attribute(system, 7, index), 2.0f);
		EXPECT_EQ(Attribute(system, 3, index), 1.0f);
		EXPECT_EQ(Attribute(system, 4, index), 2.0f);
		EXPECT_EQ(Attribute(system, 0, index), 0.0f);
		EXPECT_EQ(Attribute(system, 6, index), 0.0f);
	}

	Step(system, effect, 0.5f, 0.5f, 0);
	ASSERT_EQ(Alive(system, indices), 10u);
	for (u32 index : indices) {
		EXPECT_EQ(Attribute(system, 0, index), 0.5f);
		EXPECT_EQ(Attribute(system, 1, index), 1.0f);
		EXPECT_EQ(Attribute(system, 6, index), 0.5f);
	}

	for (u32 frame = 0; frame < 3; ++frame) {
		Step(system, effect, 1.0f + 0.5f * frame, 0.5f, 0);
		EXPECT_EQ(Alive(system, indices), 10u);
	}
	Step(system, effect, 2.5f, 0.5f, 0);
	EXPECT_EQ(Alive(system, indices), 0u);
}

TEST_F(GraphEffectBlockFixture, BlocksReadBackComputed) {
	{
		SCOPED_TRACE("Set Lifetime Random (Uniform)");
		BlockRun run;
		if (!Begin(run, EffectWith("", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime","Random":"Uniform"},"in":{"A":{"value":[1]},"B":{"value":[3]}}})", ""),
				   "LifetimeRandom")) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {32}, 7);
		const Vector<f32> lifetimes = Values(run, "lifetime");
		ASSERT_EQ(lifetimes.size(), 32u);
		for (f32 lifetime : lifetimes) {
			EXPECT_TRUE(lifetime >= 1.0f && lifetime <= 3.0f) << lifetime;
		}
		EXPECT_GE(Distinct(lifetimes), 28u);
	}
	{
		SCOPED_TRACE("Set Velocity Random (Per Component)");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith("", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Random":"PerComponent"},"in":{"A":{"value":[-1,-1,-1]},"B":{"value":[1,1,1]}}})", ""),
						  "VelocityRandom"));
		Advance(run, 0.0f, 0.1f, {32}, 7);
		const Vector<f32> x = Values(run, "velocity", 0);
		const Vector<f32> y = Values(run, "velocity", 1);
		ASSERT_EQ(x.size(), 32u);
		u32 apart = 0;
		for (size_t i = 0; i < x.size(); ++i) {
			EXPECT_TRUE(x[i] >= -1.0f && x[i] <= 1.0f && y[i] >= -1.0f && y[i] <= 1.0f);
			apart += x[i] != y[i] ? 1u : 0u;
		}
		EXPECT_GE(apart, 30u);
	}
	{
		SCOPED_TRACE("Set Color Random (Uniform)");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith("", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"color","Random":"Uniform"},"in":{"A":{"value":[0,0,0]},"B":{"value":[1,1,1]}}})", ""),
						  "ColorRandom"));
		Advance(run, 0.0f, 0.1f, {32}, 7);
		const Vector<f32> red = Values(run, "color", 0);
		const Vector<f32> blue = Values(run, "color", 2);
		ASSERT_EQ(red.size(), 32u);
		for (size_t i = 0; i < red.size(); ++i) {
			EXPECT_EQ(red[i], blue[i]);
		}
		EXPECT_GE(Distinct(red), 28u);
	}
	{
		SCOPED_TRACE("Get Attribute: particleId");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith(R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleId"}})", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"particleId"]}}})", ""),
						  "ParticleId"));
		Advance(run, 0.0f, 0.1f, {8}, 0, 0);
		Advance(run, 0.1f, 0.1f, {4}, 0, 8);
		const Vector<f32> ids = Sorted(Values(run, "size"));
		ASSERT_EQ(ids.size(), 12u);
		for (u32 i = 0; i < ids.size(); ++i) {
			EXPECT_EQ(ids[i], static_cast<f32>(i));
		}
	}
	{
		SCOPED_TRACE("Get Attribute: spawnIndex, spawnCount (Source)");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith(R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
				{"id":21,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnCount","Location":"Source"}})",
										  R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"spawnIndex"]}}},
				{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[21,"spawnCount"]}}})",
										  ""),
						  "SpawnIndex"));
		Advance(run, 0.0f, 0.1f, {5, 3});
		const Vector<f32> index = Values(run, "size");
		const Vector<f32> count = Values(run, "alpha");
		ASSERT_EQ(index.size(), 8u);
		Vector<f32> fives{GetMemoryResource(MemTag::Graphics)};
		Vector<f32> threes{GetMemoryResource(MemTag::Graphics)};
		for (size_t i = 0; i < index.size(); ++i) {
			(count[i] == 5.0f ? fives : threes).push_back(index[i]);
			EXPECT_TRUE(count[i] == 5.0f || count[i] == 3.0f) << count[i];
		}
		EXPECT_EQ(Sorted(fives), (Vector<f32>{{0.0f, 1.0f, 2.0f, 3.0f, 4.0f}, GetMemoryResource(MemTag::Graphics)}));
		EXPECT_EQ(Sorted(threes), (Vector<f32>{{0.0f, 1.0f, 2.0f}, GetMemoryResource(MemTag::Graphics)}));
	}
	{
		SCOPED_TRACE("Age Over Lifetime");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith(R"({"id":20,"def":"Attribute/AgeOverLifetime"})", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}})", R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"t"]}}})"),
						  "AgeOverLifetime"));
		Advance(run, 0.0f, 0.5f, {4});
		Advance(run, 0.5f, 0.5f, {});
		Advance(run, 1.0f, 0.5f, {});
		for (f32 size : Values(run, "size")) {
			EXPECT_EQ(size, 0.25f);
		}
	}
	for (const bool constant : {true, false}) {
		SCOPED_TRACE(constant ? "Random Number (Constant)" : "Random Number (not Constant)");
		BlockRun run;
		const str node = std::format(R"({{"id":20,"def":"Random/RandomNumber","settings":{{"Constant":{}}}}})", constant ? "true" : "false");
		ASSERT_TRUE(Begin(run, EffectWith(node, "", R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"r"]}}})"),
						  constant ? "RandomConstant" : "RandomVarying"));
		Advance(run, 0.0f, 0.1f, {32}, 3, 1u << 24);
		Advance(run, 0.1f, 0.1f, {}, 3);
		const Vector<f32> first = Sorted(Values(run, "size"));
		Advance(run, 0.2f, 0.1f, {}, 3);
		const Vector<f32> second = Sorted(Values(run, "size"));
		ASSERT_EQ(first.size(), 32u);
		EXPECT_GE(Distinct(first), 28u);
		if (constant) {
			EXPECT_EQ(first, second);
		} else {
			EXPECT_NE(first, second);
		}
	}
}

TEST_F(GraphEffectBlockFixture, OldPositionIsStartPosition) {
	BlockRun run;
	if (!Begin(run, EffectWith(R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"oldPosition"}})", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3]}}})",
							   R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position","Composition":"Add"},"in":{"Position":{"value":[0,1,0]}}},
				{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[20,"oldPosition"]}}})"),
			   "OldPosition")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, 0.1f, {4});
	for (u32 frame = 1; frame <= 3; ++frame) {
		SCOPED_TRACE(std::format("update {}", frame));
		Advance(run, 0.1f * frame, 0.1f, {});
		const Vector<f32> y = Values(run, "position", 1);
		const Vector<f32> before = Values(run, "color", 1);
		const Vector<f32> x = Values(run, "color", 0);
		ASSERT_EQ(y.size(), 4u);
		ASSERT_EQ(before.size(), 4u);
		for (size_t i = 0; i < y.size(); ++i) {
			EXPECT_EQ(y[i], 2.0f + static_cast<f32>(frame));
			EXPECT_EQ(before[i], y[i] - 1.0f);
			EXPECT_EQ(x[i], 1.0f);
		}
	}
}

TEST_F(GraphEffectBlockFixture, SeedsDifferAndReproduce) {
	const str graph = EffectWith("", R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime","Random":"Uniform"},"in":{"A":{"value":[1]},"B":{"value":[3]}}})", "");
	Vector<f32> drawn[3];
	const u32 seeds[3] = {7, 7, 8};
	for (u32 i = 0; i < 3; ++i) {
		BlockRun run;
		if (!Begin(run, graph, std::format("Seed{}", i))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {16}, seeds[i]);
		drawn[i] = Sorted(Values(run, "lifetime"));
		ASSERT_EQ(drawn[i].size(), 16u);
	}
	EXPECT_EQ(drawn[0], drawn[1]);
	EXPECT_NE(drawn[0], drawn[2]);
}

TEST_F(GraphEffectBlockFixture, KernelsDrawIsMachinesBitForBit) {
	const str graph = EffectWith(R"({"id":20,"def":"Random/RandomNumber","settings":{"Type":"Vector3","Seed":"PerEffectComponent"},"in":{"Seed":{"value":[5]}}})",
								 R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[20,"r"]}}})", "");
	BlockRun run;
	if (!Begin(run, graph, "Parity")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	constexpr u32 SEED = 1234;
	Advance(run, 0.0f, 0.1f, {4}, SEED);

	auto hash = [](u32 seed) {
		seed = (seed ^ 61u) ^ (seed >> 16);
		seed += seed << 3;
		seed = seed ^ (seed >> 4);
		seed *= 0x27d4eb2du;
		return seed ^ (seed >> 15);
	};
	const f32 key = std::bit_cast<f32>(hash(SEED) ^ 0x9e3779b9u);
	for (u32 component = 0; component < 3; ++component) {
		VisualEffectsImpl::EffectProgram program;
		str error;
		ASSERT_TRUE(VisualEffectsImpl::EffectPrograms::Assemble(std::format("input 1 key\nconst 1 0x40A00000\nrandom 1 {} 0 1\nroot 2\n", component), program,
																error))
			<< error;
		VisualEffectsImpl::EffectMachine machine;
		const VisualEffectsImpl::EffectValue slots[] = {{{key, 0.0f, 0.0f, 0.0f}}};
		ASSERT_TRUE(machine.Run(program, slots));
		const u32 expected = std::bit_cast<u32>(machine.Root(program, 0).v[0]);
		for (f32 value : Values(run, "color", component)) {
			EXPECT_EQ(std::bit_cast<u32>(value), expected) << "component " << component << ' ' << value << ", the VM " << machine.Root(program, 0).v[0];
		}
	}
}

TEST(GraphEffectTest, SampledCurveInCpuProgramIsCurve) {
	for (const auto& wrap : WRAPS) {
		const str json = CurveJson(wrap[0], wrap[1]);
		AnimationCurve curve;
		str error;
		ASSERT_TRUE(GraphCurves::Parse(json, curve, error)) << error;
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(Compile(SampledRate(json, R"({"from":[21,"totalTime"]})"), effect, diagnostics))
			<< (diagnostics.empty() ? str() : diagnostics[0].message);
		VisualEffectsImpl::EffectProgram program;
		ASSERT_TRUE(VisualEffectsImpl::EffectPrograms::Assemble(effect.spawners[0].blocks[0].assembly, program, error)) << error;
		ASSERT_EQ(program.inputs.size(), 1u);
		EXPECT_EQ(program.inputs[0].name, "TimeParameters");
		VisualEffectsImpl::EffectMachine machine;
		for (i32 step = -24; step <= 40; ++step) {
			const f32 time = static_cast<f32>(step) * 0.125f;
			const VisualEffectsImpl::EffectValue slots[] = {{{time, std::sin(time), std::cos(time), 0.1f}}};
			ASSERT_TRUE(machine.Run(program, slots));
			EXPECT_NEAR(machine.Root(program, 0).v[0], curve.Evaluate(time), 1e-5f) << wrap[0] << "/" << wrap[1] << " at " << time;
		}
	}
}

TEST(GraphEffectTest, SampledGradientInCpuProgramIsGradient) {
	f32 colors[8][4] = {};
	f32 alphas[8][2] = {};
	str colorJson;
	str alphaJson;
	for (u32 key = 0; key < 8; ++key) {
		const f32 time = static_cast<f32>(key) / 7.0f;
		const f32 k = static_cast<f32>(key);
		colors[key][0] = 0.1f + 0.1f * k;
		colors[key][1] = 0.9f - 0.1f * k;
		colors[key][2] = 0.03f * k * k;
		colors[key][3] = time;
		alphas[key][0] = 1.0f - 0.12f * k;
		alphas[key][1] = time;
		colorJson += std::format("{}[{},{},{},{}]", key == 0 ? "" : ",", colors[key][0], colors[key][1], colors[key][2], time);
		alphaJson += std::format("{}[{},{}]", key == 0 ? "" : ",", alphas[key][0], time);
	}
	const str gradient = std::format(R"({{"alphas":[{}],"colors":[{}],"mode":"Blend"}})", alphaJson, colorJson);
	const auto expected = [&](u32 channel, f32 time) {
		const bool alpha = channel == 3;
		const auto value = [&](u32 key) { return alpha ? alphas[key][0] : colors[key][channel]; };
		const auto at = [&](u32 key) { return alpha ? alphas[key][1] : colors[key][3]; };
		f32 out = value(0);
		for (u32 key = 1; key < 8; ++key) {
			const f32 share = std::clamp((time - at(key - 1)) / (at(key) - at(key - 1)), 0.0f, 1.0f);
			out = out + (value(key) - out) * share;
		}
		return out;
	};
	constexpr std::string_view CHANNELS[] = {"R", "G", "B", "A"};
	for (u32 channel = 0; channel < 4; ++channel) {
		const str graph = std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[
			{{"id":20,"def":"Input/SampleGradient","in":{{"Gradient":{{"value":{}}},"Time":{{"from":[21,"totalTime"]}}}}}},
			{{"id":21,"def":"BuiltIn/TotalTime"}},
			{{"id":22,"def":"Channel/Split","in":{{"In":{{"from":[20,"Out"]}}}}}}],"contexts":[
			{{"id":1,"def":"Context/Spawn","blocks":[{{"id":2,"def":"Block/ConstantSpawnRate","in":{{"Rate":{{"from":[22,"{}"]}}}}}}]}},
			{{"id":3,"def":"Context/Initialize","flow":[[1,"In"]],"blocks":[]}},
			{{"id":4,"def":"Context/OutputParticlePrimitive","flow":[[3,"In"]],"blocks":[]}}]}})",
									  gradient, CHANNELS[channel]);
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
		VisualEffectsImpl::EffectProgram program;
		str error;
		ASSERT_TRUE(VisualEffectsImpl::EffectPrograms::Assemble(effect.spawners[0].blocks[0].assembly, program, error)) << error;
		VisualEffectsImpl::EffectMachine machine;
		for (u32 step = 0; step <= 32; ++step) {
			const f32 time = static_cast<f32>(step) / 32.0f;
			const VisualEffectsImpl::EffectValue slots[] = {{{time, std::sin(time), std::cos(time), 0.1f}}};
			ASSERT_TRUE(machine.Run(program, slots));
			EXPECT_NEAR(machine.Root(program, 0).v[0], expected(channel, time), 1e-5f) << CHANNELS[channel] << " at " << time;
		}
	}
}

namespace {

	constexpr std::string_view EASE = R"({"keys":[{"time":0,"value":1},{"time":1,"value":3}]})";

	constexpr std::string_view RAMP = R"({"alphas":[[0.2,0],[1,0.5],[0.4,1]],"colors":[[0.5,0.2,0.1,0],[0,1,1,1]],"mode":"Blend"})";

	str FromCurve(u32 id, std::string_view settings, std::string_view inputs) {
		return std::format(R"({{"id":{},"def":"Block/AttributeFromCurve","settings":{{{}}},"in":{{{}}}}})", id, settings, inputs);
	}

} // anonymous namespace

TEST_F(GraphEffectBlockFixture, CurveAttributeSampleModeTimes) {
	AnimationCurve ease;
	str error;
	ASSERT_TRUE(GraphCurves::Parse(EASE, ease, error)) << error;
	std::array<f32, GRAPH_GRADIENT_COMPONENTS> ramp{};
	ASSERT_TRUE(GraphGradients::Read(std::string_view(RAMP), ramp, error)) << error;
	const str curveIn = std::format(R"("Size":{{"value":{}}})", EASE);
	const str lifetime = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}})";
	const auto overLife = [](BlockRun& run, u32 spawn) {
		Advance(run, 0.0f, 0.5f, {spawn});
		Advance(run, 0.5f, 0.5f, {});
		Advance(run, 1.0f, 0.5f, {});
	};

	{
		SCOPED_TRACE("Set Size Over Life");
		BlockRun run;
		if (!Begin(run, EffectWith("", lifetime, FromCurve(11, R"("Attribute":"size")", curveIn)), "FromCurveOverLife")) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		overLife(run, 4);
		const Vector<f32> sizes = Values(run, "size");
		ASSERT_EQ(sizes.size(), 4u);
		for (f32 size : sizes) {
			EXPECT_NEAR(size, ease.Evaluate(0.25f), 1e-5f);
		}
	}
	{
		SCOPED_TRACE("Over Life, no lifetime");
		BlockRun run;
		const str looped = R"("Size":{"value":{"keys":[{"time":0,"value":1},{"time":1,"value":3}],"preWrapMode":"Loop","postWrapMode":"PingPong"}})";
		ASSERT_TRUE(Begin(run, EffectWith("", FromCurve(10, R"("Attribute":"size")", looped), ""), "FromCurveNoLifetime"));
		Advance(run, 0.0f, 0.1f, {4});
		for (f32 size : Values(run, "size")) {
			EXPECT_EQ(size, 1.0f);
		}
	}
	{
		SCOPED_TRACE("Set Size By Speed");
		BlockRun run;
		const str nodes = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleId"}},
			{"id":21,"def":"Math/Multiply","in":{"A":{"from":[20,"particleId"]},"B":{"value":[0.25,0,0]}}})";
		const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"from":[21,"Out"]}}},)" +
						   FromCurve(11, R"("Attribute":"size","Sample Mode":"BySpeed")", curveIn + R"(,"Speed Range":{"value":[0,2]})");
		ASSERT_TRUE(Begin(run, EffectWith(nodes, blocks, ""), "FromCurveBySpeed"));
		Advance(run, 0.0f, 0.1f, {12});
		const Vector<f32> sizes = Values(run, "size");
		const Vector<f32> x = Values(run, "velocity", 0);
		const Vector<f32> y = Values(run, "velocity", 1);
		const Vector<f32> z = Values(run, "velocity", 2);
		ASSERT_EQ(sizes.size(), 12u);
		for (size_t i = 0; i < sizes.size(); ++i) {
			const f32 speed = std::sqrt(x[i] * x[i] + y[i] * y[i] + z[i] * z[i]);
			EXPECT_NEAR(sizes[i], ease.Evaluate(std::clamp(speed / 2.0f, 0.0f, 1.0f)), 1e-5f) << "at speed " << speed;
		}
	}
	{
		SCOPED_TRACE("Set Size Random from Curve");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith("", FromCurve(10, R"("Attribute":"size","Sample Mode":"Random")", curveIn), ""), "FromCurveRandom"));
		Advance(run, 0.0f, 0.1f, {32}, 7);
		const Vector<f32> sizes = Values(run, "size");
		ASSERT_EQ(sizes.size(), 32u);
		for (f32 size : sizes) {
			EXPECT_TRUE(size >= 1.0f && size <= 3.0f) << size;
		}
		EXPECT_GE(Distinct(sizes), 28u);
	}
	{
		SCOPED_TRACE("Set Size Random Constant/Particle");
		BlockRun run;
		const str nodes = R"({"id":20,"def":"Random/RandomNumber","in":{"Seed":{"value":[5]}}})";
		const str blocks = FromCurve(11, R"("Attribute":"size","Sample Mode":"RandomConstantPerParticle")", curveIn + R"(,"Seed":{"value":[5]})") +
						   R"(,{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[20,"r"]}}})";
		ASSERT_TRUE(Begin(run, EffectWith(nodes, "", blocks), "FromCurveRandomConstant"));
		Advance(run, 0.0f, 0.1f, {16}, 3);
		Advance(run, 0.1f, 0.1f, {}, 3);
		const Vector<f32> sizes = Values(run, "size");
		const Vector<f32> draws = Values(run, "alpha");
		ASSERT_EQ(sizes.size(), 16u);
		for (size_t i = 0; i < sizes.size(); ++i) {
			EXPECT_NEAR(sizes[i], ease.Evaluate(draws[i]), 1e-5f);
		}
		Advance(run, 0.2f, 0.1f, {}, 3);
		EXPECT_EQ(Sorted(Values(run, "size")), Sorted(sizes));
		EXPECT_GE(Distinct(sizes), 14u);
	}
	{
		SCOPED_TRACE("Set Size Custom");
		BlockRun run;
		const str nodes = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleId"}},
			{"id":21,"def":"Math/Multiply","in":{"A":{"from":[20,"particleId"]},"B":{"value":[0.1]}}})";
		const str blocks = FromCurve(10, R"("Attribute":"size","Sample Mode":"Custom")", curveIn + R"(,"Sample Time":{"from":[21,"Out"]})") +
						   R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[20,"particleId"]}}})";
		ASSERT_TRUE(Begin(run, EffectWith(nodes, blocks, ""), "FromCurveCustom"));
		Advance(run, 0.0f, 0.1f, {11});
		const Vector<f32> sizes = Values(run, "size");
		const Vector<f32> ids = Values(run, "alpha");
		ASSERT_EQ(sizes.size(), 11u);
		for (size_t i = 0; i < sizes.size(); ++i) {
			EXPECT_NEAR(sizes[i], ease.Evaluate(ids[i] * 0.1f), 1e-5f) << "at id " << ids[i];
		}
	}
	{
		SCOPED_TRACE("Multiply Color Over Life");
		BlockRun run;
		const str initialize = lifetime + R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"value":[0.5,0.5,0.5]}}},
			{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"value":[0.3]}}})";
		const str update = FromCurve(13, R"("Attribute":"color","Composition":"Multiply")", std::format(R"("Color":{{"value":{}}})", RAMP));
		ASSERT_TRUE(Begin(run, EffectWith("", initialize, update), "FromCurveColor"));
		overLife(run, 4);
		f32 first[4];
		f32 second[4];
		GraphGradients::Evaluate(ramp, 0.0f, first);
		GraphGradients::Evaluate(ramp, 0.25f, second);
		for (u32 channel = 0; channel < 3; ++channel) {
			for (f32 value : Values(run, "color", channel)) {
				EXPECT_NEAR(value, 0.5f * first[channel] * second[channel], 1e-5f) << "channel " << channel;
			}
		}
		for (f32 value : Values(run, "alpha")) {
			EXPECT_NEAR(value, second[3], 1e-5f);
		}
	}
	{
		SCOPED_TRACE("Set Color Over Life, Alpha, Uniform");
		BlockRun run;
		const str initialize = lifetime + R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"value":[0.5,0.5,0.5]}}})";
		const str update = FromCurve(13, R"("Attribute":"color","Color Mode":"Alpha","Mode":"Uniform")", std::format(R"("Color":{{"value":{}}})", EASE));
		ASSERT_TRUE(Begin(run, EffectWith("", initialize, update), "FromCurveAlpha"));
		overLife(run, 4);
		for (f32 value : Values(run, "alpha")) {
			EXPECT_NEAR(value, ease.Evaluate(0.25f), 1e-5f);
		}
		for (f32 value : Values(run, "color", 1)) {
			EXPECT_EQ(value, 0.5f);
		}
	}
	{
		SCOPED_TRACE("Blend Size Over Life");
		BlockRun run;
		const str initialize = lifetime + R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[2]}}})";
		ASSERT_TRUE(Begin(run, EffectWith("", initialize, FromCurve(12, R"("Attribute":"size","Composition":"Blend")", curveIn + R"(,"Blend":{"value":[0.25]})")), "FromCurveBlend"));
		overLife(run, 4);
		const f32 once = 2.0f + (ease.Evaluate(0.0f) - 2.0f) * 0.25f;
		const f32 twice = once + (ease.Evaluate(0.25f) - once) * 0.25f;
		for (f32 size : Values(run, "size")) {
			EXPECT_NEAR(size, twice, 1e-5f);
		}
	}
	{
		SCOPED_TRACE("Set Scale and Angle Over Life");
		BlockRun run;
		const str flat = R"({"keys":[{"time":0,"value":7}]})";
		const str update = FromCurve(12, R"("Attribute":"scale","Mode":"Uniform")", std::format(R"("Scale":{{"value":{}}})", EASE)) + "," +
						   FromCurve(13, R"("Attribute":"angle")", std::format(R"("Angle_x":{{"value":{}}},"Angle_z":{{"value":{}}})", EASE, flat));
		ASSERT_TRUE(Begin(run, EffectWith("", lifetime, update), "FromCurveVector"));
		overLife(run, 4);
		for (u32 axis = 0; axis < 3; ++axis) {
			for (f32 value : Values(run, "scale", axis)) {
				EXPECT_NEAR(value, ease.Evaluate(0.25f), 1e-5f) << axis;
			}
		}
		const f32 angles[3] = {ease.Evaluate(0.25f), 0.25f, 7.0f};
		for (u32 axis = 0; axis < 3; ++axis) {
			for (f32 value : Values(run, "angle", axis)) {
				EXPECT_NEAR(value, angles[axis], 1e-5f) << "angle axis " << axis;
			}
		}
	}
}

TEST_F(GraphEffectBlockFixture, SampledCurveInKernelIsCurve) {
	for (const auto& wrap : WRAPS) {
		SCOPED_TRACE(std::format("{}/{}", wrap[0], wrap[1]));
		const str json = CurveJson(wrap[0], wrap[1]);
		AnimationCurve curve;
		str error;
		ASSERT_TRUE(GraphCurves::Parse(json, curve, error)) << error;
		const str nodes = std::format(R"({{"id":20,"def":"Attribute/GetAttribute","settings":{{"Attribute":"particleId"}}}},
			{{"id":21,"def":"Math/Multiply","in":{{"A":{{"from":[20,"particleId"]}},"B":{{"value":[0.25]}}}}}},
			{{"id":22,"def":"Math/Subtract","in":{{"A":{{"from":[21,"Out"]}},"B":{{"value":[3]}}}}}},
			{{"id":23,"def":"Sampling/SampleCurve","in":{{"Curve":{{"value":{}}},"Time":{{"from":[22,"Out"]}}}}}})",
									  json);
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[23,"s"]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[20,"particleId"]}}})";
		BlockRun run;
		if (!Begin(run, EffectWith(nodes, initialize, ""), std::format("SampleCurve{}{}", wrap[0], wrap[1]))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {33});
		const Vector<f32> sizes = Values(run, "size");
		const Vector<f32> ids = Values(run, "alpha");
		ASSERT_EQ(sizes.size(), 33u);
		ASSERT_EQ(ids.size(), 33u);
		for (size_t i = 0; i < sizes.size(); ++i) {
			const f32 time = ids[i] * 0.25f - 3.0f;
			EXPECT_NEAR(sizes[i], curve.Evaluate(time), 1e-5f) << "at " << time;
		}
	}
}

#endif

TEST(GraphEffectTest, UpdatelessSystemKeepsKernel) {
	str graph = With(Fountain, R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},)", "");
	graph = With(graph, R"("flow":[[8,"In"]])", R"("flow":[[4,"In"]])");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_EQ(system.update, 0u);
	EXPECT_NE(system.updateKernel.find("SltParticleAppendAlive"), str::npos);
	EXPECT_EQ(system.updateKernel.find("SltParticleStore"), str::npos);
	EXPECT_TRUE(system.stored.empty());
	EXPECT_EQ(system.outputs.size(), 1u);
}

TEST(GraphEffectTest, UntickedBlockIsInert) {
	str graph = With(Fountain, R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},)",
					 R"({"id":5,"def":"Block/SetAttribute","enabled":false,"settings":{"Attribute":"lifetime"},)");
	graph = With(graph, R"("version": 2,)", R"("version": 3,)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	const Vector<str> expected{{"position", "velocity"}, GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(StoredNames(effect.systems[0]), expected);

	str spawning = With(Fountain, R"({"id":3,"def":"Block/ConstantSpawnRate",)", R"({"id":3,"def":"Block/ConstantSpawnRate","enabled":false,)");
	spawning = With(spawning, R"("version": 2,)", R"("version": 3,)");
	GraphEffect quiet;
	ASSERT_TRUE(Compile(spawning, quiet, diagnostics));
	ASSERT_EQ(quiet.spawners.size(), 1u);
	EXPECT_TRUE(quiet.spawners[0].blocks.empty());

	str broken = With(Fountain, R"({"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Composition":"Add"},)",
					  R"({"id":7,"def":"Block/SetAttribute","enabled":false,"settings":{"Attribute":"alive","Composition":"Add"},)");
	broken = With(broken, R"("version": 2,)", R"("version": 3,)");
	GraphEffect off;
	diagnostics.clear();
	EXPECT_TRUE(Compile(broken, off, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
}
