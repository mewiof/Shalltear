#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cmath>
#include <format>

#ifndef SHALLTEAR_SHARED
#include "EffectKernelHarness.h"

#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#endif

using namespace Shalltear;

namespace {

	GraphDocument ReadGraph(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	str OneSystem(std::string_view nodes, std::string_view blocks) {
		return std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}}]}})",
						   nodes, blocks);
	}

	const GraphNodeDefinition* Resolve(GraphDefinitions& definitions, const GraphDocument& document, u32 id) {
		for (const GraphNode& node : document.nodes) {
			if (node.id == id) {
				str error;
				const GraphNodeDefinition* definition = definitions.Find(document, node, &error);
				EXPECT_NE(definition, nullptr) << error;
				return definition;
			}
		}
		ADD_FAILURE() << "no node " << id;
		return nullptr;
	}

	size_t Count(std::string_view text, std::string_view what) {
		size_t count = 0;
		for (size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + 1)) {
			++count;
		}
		return count;
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, ANoisesPortsFollowItsDimensions) {
	const GraphDocument document = ReadGraph(OneSystem(R"({"id":20,"def":"Noise/Noise","settings":{"Type":"Cellular","Dimensions":"Three"}},
		{"id":21,"def":"Noise/Noise","settings":{"Type":"Value","Dimensions":"One"}},
		{"id":22,"def":"Noise/CurlNoise","settings":{"Type":"Perlin","Dimensions":"Three"}})",
													   ""));
	GraphDefinitions definitions;
	const GraphNodeDefinition* cellular = Resolve(definitions, document, 20);
	ASSERT_NE(cellular, nullptr);
	EXPECT_EQ(cellular->name, "Cellular Noise 3D");
	ASSERT_EQ(cellular->inputs.size(), 6u);
	EXPECT_EQ(cellular->inputs[0].type, GraphPortType::Float3);
	EXPECT_EQ(cellular->inputs[5].name, "Range");
	ASSERT_EQ(cellular->outputs.size(), 2u);
	EXPECT_EQ(cellular->outputs[1].type, GraphPortType::Float3);

	const GraphNodeDefinition* value = Resolve(definitions, document, 21);
	ASSERT_NE(value, nullptr);
	EXPECT_EQ(value->inputs[0].type, GraphPortType::Float);
	EXPECT_EQ(value->outputs[1].type, GraphPortType::Float);

	const GraphNodeDefinition* curl = Resolve(definitions, document, 22);
	ASSERT_NE(curl, nullptr);
	EXPECT_EQ(curl->name, "Perlin Curl Noise 3D");
	EXPECT_EQ(curl->inputs[5].name, "Amplitude");
	ASSERT_EQ(curl->outputs.size(), 1u);
	EXPECT_EQ(curl->outputs[0].type, GraphPortType::Float3);
}

TEST(GraphEffectLibraryTest, NoiseRunsInKernelAndOnCpu) {
	const str spawned = R"({"_type":"EffectGraph","version":3,"nextId":100,"nodes":[
		{"id":20,"def":"Noise/Noise","settings":{"Type":"Perlin","Dimensions":"One"}}],"contexts":[
		{"id":4,"def":"Context/Spawn","blocks":[{"id":5,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[20,"Noise"]}}}]},
		{"id":1,"def":"Context/Initialize","flow":[[4,"In"]],"blocks":[]}]})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(spawned), "noise.sleffect", effect, diagnostics)) << diagnostics[0].message;
	const str& rate = effect.spawners[0].blocks[0].assembly;
	EXPECT_EQ(Count(rate, "\nfractalnoise 2 1 "), 1u) << rate;

	const str kernels = OneSystem(R"({"id":20,"def":"Noise/Noise","settings":{"Type":"Perlin","Dimensions":"Three"}},
		{"id":21,"def":"Noise/Noise","settings":{"Type":"Perlin","Dimensions":"Three"},"in":{"Coordinate":{"value":[1,2,3]}}},
		{"id":22,"def":"Noise/CurlNoise","settings":{"Type":"Perlin","Dimensions":"Three"}})",
								  R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"Noise"]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[21,"Noise"]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[22,"Noise"]}}})");
	diagnostics.clear();
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(kernels), "noise.sleffect", effect, diagnostics)) << diagnostics[0].message;
	const str& text = effect.systems[0].initializeKernel;
	EXPECT_EQ(Count(text, "float4 ShalltearGraphPerlinNoise3D("), 1u);
	EXPECT_EQ(Count(text, "float3 ShalltearGraphPerlinNoise2D("), 1u);
	EXPECT_EQ(Count(text, "ShalltearGraphPerlinFractal3D("), 3u);
	EXPECT_EQ(Count(text, "float3 ShalltearGraphPerlinCurl3D("), 1u);
	EXPECT_EQ(Count(text, "ShalltearGraphValueNoise"), 0u);
}

namespace {

	str OneSystemIn(std::string_view space, std::string_view nodes, std::string_view blocks) {
		str text = OneSystem(nodes, blocks);
		const std::string_view from = R"("settings":{"Capacity":64})";
		text.replace(text.find(from), from.size(), std::format(R"("settings":{{"Capacity":64,"Space":"{}"}})", space));
		return text;
	}

	constexpr std::string_view TO_WORLD = "SltParticleEffectLocalToWorldRow";
	constexpr std::string_view TO_LOCAL = "SltParticleEffectWorldToLocalRow";

} // anonymous namespace

TEST(GraphEffectLibraryTest, OtherSpaceInputConverted) {
	const str localPosition = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3]}}})";
	const str worldPosition = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3],"space":"World"}}})";
	struct Case {
		std::string_view system;
		const str* block;
		std::string_view reads;
	};
	const Case cases[] = {{"World", &localPosition, TO_WORLD}, {"Local", &worldPosition, TO_LOCAL}, {"Local", &localPosition, {}}, {"World", &worldPosition, {}}};
	for (const Case& entry : cases) {
		SCOPED_TRACE(std::format("{} system, {}", entry.system, entry.block == &localPosition ? "Local input" : "World input"));
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(OneSystemIn(entry.system, "", *entry.block)), "space.sleffect", effect, diagnostics))
			<< diagnostics[0].message;
		const str& text = effect.systems[0].initializeKernel;
		EXPECT_EQ(text.find(entry.reads.empty() ? TO_WORLD : (entry.reads == TO_WORLD ? TO_LOCAL : TO_WORLD)), str::npos);
		if (entry.reads.empty()) {
			EXPECT_EQ(text.find(TO_LOCAL), str::npos);
		} else {
			EXPECT_NE(text.find(entry.reads), str::npos);
		}
	}

	const GraphDocument spawn = ReadGraph(R"({"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
		{"id":4,"def":"Context/Spawn","blocks":[{"id":5,"def":"Block/SetSpawnEvent","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3],"space":"World"}}}]},
		{"id":1,"def":"Context/Initialize","flow":[[4,"In"]],"blocks":[]}]})");
	GraphDefinitions definitions;
	const GraphNodeDefinition* setSpawn = definitions.Find(spawn, spawn.contexts[0].blocks[0]);
	ASSERT_NE(setSpawn, nullptr);
	EXPECT_EQ(setSpawn->inputs[0].space, GraphPortSpace::None);
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_TRUE(GraphEffectTarget::Compile(spawn, "spawn.sleffect", effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
}

namespace {

	const GraphNodeDefinition* BlockShape(GraphDefinitions& definitions, GraphDocument& holder, std::string_view def, std::string_view settings) {
		holder = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
			{{"id":1,"def":"Context/Initialize","blocks":[{{"id":10,"def":"{}","settings":{}}}]}}]}})",
									   def, settings));
		str error;
		const GraphNodeDefinition* definition = definitions.Find(holder, holder.contexts[0].blocks[0], &error);
		EXPECT_NE(definition, nullptr) << error;
		return definition;
	}

	Vector<str> InputNames(const GraphNodeDefinition& definition) {
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		for (const GraphPortDefinition& input : definition.inputs) {
			names.push_back(str(input.name));
		}
		return names;
	}

	Vector<str> Names(std::initializer_list<std::string_view> names) {
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (std::string_view name : names) {
			out.push_back(str(name));
		}
		return out;
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, SetPositionShapesPortsAreItsShapes) {
	GraphDefinitions definitions;
	GraphDocument holder;
	const GraphNodeDefinition* sphere = BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_SHAPE, R"({"Shape":"Sphere"})");
	ASSERT_NE(sphere, nullptr);
	EXPECT_EQ(sphere->name, "Set Position Shape Sphere");
	EXPECT_EQ(InputNames(*sphere), Names({"Position", "Angles", "Scale", "Radius", "Arc"}));
	EXPECT_EQ(sphere->inputs[0].space, GraphPortSpace::Matrix);
	EXPECT_EQ(sphere->inputs[4].defaultValue[0], 360.0f);
	ASSERT_EQ(sphere->outputs.size(), 2u);
	EXPECT_EQ(sphere->outputs[1].name, "direction");

	const GraphNodeDefinition* torus = BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_SHAPE,
												  R"({"Shape":"Torus","Spawn Mode":"Custom","Position Mode":"ThicknessRelative","Apply Orientation":"Axes","Composition Position":"Blend"})");
	ASSERT_NE(torus, nullptr);
	EXPECT_EQ(InputNames(*torus), Names({"Position", "Angles", "Scale", "Major Radius", "Minor Radius", "Arc", "Height Sequencer", "Arc Sequencer", "Thickness",
										 "Blend Position"}));
	EXPECT_EQ(torus->name, "Blend Position Shape Torus");
	ASSERT_EQ(torus->outputs.size(), 4u);
	EXPECT_EQ(torus->outputs[3].name, "axisZ");

	const GraphNodeDefinition* box = BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_SHAPE, R"({"Shape":"OrientedBox","Spawn Mode":"Custom"})");
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(InputNames(*box), Names({"Center", "Angles", "Size"}));

	const GraphNodeDefinition* line = BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_SHAPE, R"({"Shape":"Line","Position Mode":"Volume"})");
	ASSERT_NE(line, nullptr);
	EXPECT_EQ(InputNames(*line), Names({"Start", "End"}));
	EXPECT_EQ(line->inputs[0].space, GraphPortSpace::Position);
	EXPECT_EQ(line->inputs[1].defaultValue[0], 1.0f);

	const GraphNodeDefinition* kill = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_SHAPE, R"({"Behavior":"Kill","Shape":"Sphere","Radius Mode":"Custom"})");
	ASSERT_NE(kill, nullptr);
	EXPECT_EQ(kill->name, "Kill Shape Sphere");
	EXPECT_EQ(InputNames(*kill), Names({"Position", "Angles", "Scale", "Radius", "Particle Radius"}));
	ASSERT_EQ(kill->outputs.size(), 1u);
	EXPECT_EQ(kill->outputs[0].name, "alive");
	const GraphNodeDefinition* plane = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_SHAPE, R"({"Behavior":"Kill","Shape":"Plane","Radius Mode":"Custom"})");
	ASSERT_NE(plane, nullptr);
	EXPECT_EQ(GraphPortLabel(plane->inputs.back()), "Radius");
	EXPECT_EQ(plane->inputs[1].space, GraphPortSpace::Direction);
}

TEST(GraphEffectLibraryTest, ForceIsUpdateOnly) {
	for (std::string_view force : {GRAPH_EFFECT_FORCE, GRAPH_EFFECT_GRAVITY, GRAPH_EFFECT_LINEAR_DRAG, GRAPH_EFFECT_TURBULENCE, GRAPH_EFFECT_CONFORM_TO_SPHERE}) {
		EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, force)) << force;
		EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_INITIALIZE, force)) << force;
		EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT, force)) << force;
	}
	for (std::string_view context : {GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_OUTPUT}) {
		EXPECT_TRUE(GraphEffectRules::Holds(context, GRAPH_EFFECT_POSITION_SHAPE)) << context;
		EXPECT_TRUE(GraphEffectRules::Holds(context, GRAPH_EFFECT_COLLISION_SHAPE)) << context;
	}
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_POSITION_SHAPE));

	const auto offered = [](std::string_view context) {
		GraphDocument document = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[{{"id":1,"def":"{}"}}]}})", context));
		Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		EffectGraphEditOps::BlockChoices(document, 1, choices);
		return choices;
	};
	const auto find = [](const Vector<EffectGraphEditOps::BlockChoice>& choices, std::string_view name) -> const EffectGraphEditOps::BlockChoice* {
		for (const EffectGraphEditOps::BlockChoice& choice : choices) {
			if (choice.name == name) {
				return &choice;
			}
		}
		return nullptr;
	};
	const Vector<EffectGraphEditOps::BlockChoice> update = offered(GRAPH_EFFECT_UPDATE);
	for (std::string_view name : {"Force", "Gravity", "Linear Drag", "Turbulence", "Attractor Shape Sphere", "Set Position Shape Sphere", "Set Position Shape Cone / Cylinder"}) {
		EXPECT_NE(find(update, name), nullptr) << name;
	}
	const EffectGraphEditOps::BlockChoice* plane = find(update, "Kill Shape Plane");
	ASSERT_NE(plane, nullptr);
	EXPECT_TRUE(std::any_of(plane->settings.begin(), plane->settings.end(), [](const GraphSetting& setting) { return setting.key == "Behavior" && setting.value == "\"Kill\""; }));
	EXPECT_EQ(find(offered(GRAPH_EFFECT_INITIALIZE), "Gravity"), nullptr);

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphEffectTarget::Compile(ReadGraph(OneSystem("", R"({"id":10,"def":"Block/Gravity"})")), "force.sleffect", effect, diagnostics));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& diagnostic) { return diagnostic.node == 10; }));
}

TEST(GraphEffectLibraryTest, CollisionShapesPortsAndOutputsFollowBehavior) {
	GraphDefinitions definitions;
	GraphDocument holder;
	const auto outputNames = [](const GraphNodeDefinition& definition) {
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		for (const GraphPortDefinition& output : definition.outputs) {
			names.push_back(str(output.name));
		}
		return names;
	};
	const GraphNodeDefinition* plain = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_SHAPE, "{}");
	ASSERT_NE(plain, nullptr);
	EXPECT_EQ(plain->name, "Collision Shape Plane");
	EXPECT_EQ(InputNames(*plain), Names({"Position", "Normal", "Bounce", "Friction", "Lifetime Loss"}));
	EXPECT_EQ(plain->inputs[2].defaultValue[0], 0.1f);
	EXPECT_EQ(outputNames(*plain), Names({"position", "velocity", "age"}));

	const GraphNodeDefinition* full =
		BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_SHAPE,
				   R"({"Shape":"Sphere","Radius Mode":"Custom","Rough Surface":true,"Override Bounce Threshold":true,"Collision Attributes":"WriteAlways"})");
	ASSERT_NE(full, nullptr);
	EXPECT_EQ(InputNames(*full), Names({"Position", "Angles", "Scale", "Radius", "Bounce", "Friction", "Bounce Speed Threshold", "Lifetime Loss", "Particle Radius",
										"Roughness"}));
	EXPECT_EQ(outputNames(*full),
			  Names({"position", "velocity", "age", "hasCollisionEvent", "collisionEventNormal", "collisionEventPosition", "collisionEventCount"}));

	const GraphNodeDefinition* silent = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_SHAPE, R"({"Behavior":"None","Shape":"OrientedBox"})");
	ASSERT_NE(silent, nullptr);
	EXPECT_EQ(silent->name, "Trigger Shape Box");
	EXPECT_EQ(InputNames(*silent), Names({"Center", "Angles", "Size"}));
	EXPECT_TRUE(silent->outputs.empty());
	const GraphNodeDefinition* trigger = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_SHAPE, R"({"Behavior":"None","Collision Attributes":"WriteAlways"})");
	ASSERT_NE(trigger, nullptr);
	EXPECT_EQ(outputNames(*trigger), Names({"hasCollisionEvent", "collisionEventNormal", "collisionEventPosition", "collisionEventCount"}));

	const GraphNodeDefinition* table = GraphNodeLibrary::Find(GRAPH_EFFECT_COLLISION_SHAPE);
	ASSERT_NE(table, nullptr);
	const auto shown = [&](std::string_view settings, std::string_view key) {
		const GraphDocument document = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
			{{"id":1,"def":"Context/Update","blocks":[{{"id":10,"def":"Block/CollisionShape","settings":{}}}]}}]}})",
															 settings));
		for (u32 i = 0; i < table->settings.size(); ++i) {
			if (table->settings[i].key == key) {
				return GraphNodeLibrary::SettingActive(table->settings, i, document.contexts[0].blocks[0]);
			}
		}
		ADD_FAILURE() << "no setting " << key;
		return false;
	};
	EXPECT_TRUE(shown("{}", "Rough Surface"));
	EXPECT_TRUE(shown("{}", "Override Bounce Threshold"));
	EXPECT_FALSE(shown(R"({"Behavior":"Kill"})", "Rough Surface"));
	EXPECT_FALSE(shown("{}", "Write Rough Normal"));
	EXPECT_TRUE(shown(R"({"Rough Surface":true})", "Write Rough Normal"));

	for (std::string_view name : {"oldVelocity", "hasCollisionEvent", "collisionEventNormal", "collisionEventPosition", "collisionEventCount"}) {
		const GraphEffectAttributeInfo* attribute = GraphEffectAttributes::Find(name);
		ASSERT_NE(attribute, nullptr) << name;
		EXPECT_TRUE(attribute->readOnly) << name;
	}
}

TEST(GraphEffectLibraryTest, BouncingCollisionIsInitializesOrUpdates) {
	const GraphDocument blocks = ReadGraph(R"({"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
		{"id":1,"def":"Context/Update","blocks":[{"id":10,"def":"Block/CollisionShape"},{"id":11,"def":"Block/CollisionShape","settings":{"Behavior":"Kill"}}]}]})");
	const GraphNode& bounce = blocks.contexts[0].blocks[0];
	const GraphNode& kill = blocks.contexts[0].blocks[1];
	EXPECT_TRUE(GraphEffectRules::HoldsBlock(GRAPH_EFFECT_INITIALIZE, bounce));
	EXPECT_TRUE(GraphEffectRules::HoldsBlock(GRAPH_EFFECT_UPDATE, bounce));
	EXPECT_FALSE(GraphEffectRules::HoldsBlock(GRAPH_EFFECT_OUTPUT, bounce));
	EXPECT_TRUE(GraphEffectRules::HoldsBlock(GRAPH_EFFECT_OUTPUT, kill));

	str graph = OneSystem("", "");
	const std::string_view emptyOutput = R"("def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[])";
	graph.replace(graph.find(emptyOutput), emptyOutput.size(),
				  R"("def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[{"id":10,"def":"Block/CollisionShape","settings":{"Shape":"Sphere"}}])");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphEffectTarget::Compile(ReadGraph(graph), "bounce.sleffect", effect, diagnostics));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& diagnostic) {
		return diagnostic.node == 10 && diagnostic.message.find("Collision Shape Sphere") != str::npos;
	}));

	GraphDocument moved = ReadGraph(graph);
	moved.contexts.back().blocks.clear();
	moved.contexts[1].blocks.push_back(bounce);
	const u32 ids[] = {10};
	EXPECT_FALSE(EffectGraphEditOps::CanMoveBlocks(moved, ids, 3));
	moved.contexts[1].blocks.back().settings = kill.settings;
	EXPECT_TRUE(EffectGraphEditOps::CanMoveBlocks(moved, ids, 3));

	const auto names = [](std::string_view context) {
		GraphDocument document = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[{{"id":1,"def":"{}"}}]}})", context));
		Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		EffectGraphEditOps::BlockChoices(document, 1, choices);
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (const EffectGraphEditOps::BlockChoice& choice : choices) {
			if (choice.def == GRAPH_EFFECT_COLLISION_SHAPE) {
				const bool stores = std::any_of(choice.settings.begin(), choice.settings.end(), [](const GraphSetting& setting) { return setting.key == "Behavior"; }) &&
									std::any_of(choice.settings.begin(), choice.settings.end(), [](const GraphSetting& setting) { return setting.key == "Shape"; });
				EXPECT_TRUE(stores) << choice.name;
				out.push_back(choice.name);
			}
		}
		return out;
	};
	EXPECT_EQ(names(GRAPH_EFFECT_UPDATE),
			  Names({"Collision Shape Plane", "Collision Shape Sphere", "Collision Shape Box", "Collision Shape Cone / Cylinder", "Kill Shape Plane", "Kill Shape Sphere",
					 "Kill Shape Box", "Kill Shape Cone / Cylinder", "Trigger Shape Box", "Trigger Shape Sphere", "Trigger Shape Cone / Cylinder", "Trigger Shape Plane"}));
	EXPECT_EQ(names(GRAPH_EFFECT_OUTPUT), Names({"Kill Shape Plane", "Kill Shape Sphere", "Kill Shape Box", "Kill Shape Cone / Cylinder", "Trigger Shape Box",
												 "Trigger Shape Sphere", "Trigger Shape Cone / Cylinder", "Trigger Shape Plane"}));
}

TEST(GraphEffectLibraryTest, ADepthCollisionsPortsFollowItsSettings) {
	GraphDefinitions definitions;
	GraphDocument holder;
	const GraphNodeDefinition* plain = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_DEPTH, "{}");
	ASSERT_NE(plain, nullptr);
	EXPECT_EQ(plain->name, "Collision Depth Buffer");
	EXPECT_EQ(InputNames(*plain), Names({"Bounce", "Friction", "Lifetime Loss"}));
	ASSERT_EQ(plain->outputs.size(), 3u);

	const GraphNodeDefinition* thick = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_DEPTH, R"({"Surface Thickness":"Custom","Radius Mode":"Custom"})");
	ASSERT_NE(thick, nullptr);
	EXPECT_EQ(InputNames(*thick), Names({"Bounce", "Friction", "Lifetime Loss", "Particle Radius", "Surface Thickness"}));
	EXPECT_EQ(thick->inputs[3].label, "Radius");

	const GraphNodeDefinition* kill = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_DEPTH, R"({"Behavior":"Kill"})");
	ASSERT_NE(kill, nullptr);
	EXPECT_EQ(kill->name, "Kill Depth Buffer");
	ASSERT_EQ(kill->outputs.size(), 1u);
	EXPECT_EQ(kill->outputs[0].name, "alive");
	const GraphNodeDefinition* trigger = BlockShape(definitions, holder, GRAPH_EFFECT_COLLISION_DEPTH, R"({"Behavior":"None"})");
	ASSERT_NE(trigger, nullptr);
	EXPECT_EQ(trigger->name, "Trigger Depth Buffer");
	EXPECT_TRUE(trigger->outputs.empty());
}

TEST(GraphEffectLibraryTest, DepthCollisionReadsCameraDepth) {
	const auto compile = [](std::string_view initialize, std::string_view update, GraphEffect& effect, Vector<GraphDiagnostic>& diagnostics) {
		str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"properties":[{{"id":90,"name":"Speed","reference":"speed","type":"Float"}}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":8}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}}]}})",
								initialize, update);
		return GraphEffectTarget::Compile(ReadGraph(graph), "depth.sleffect", effect, diagnostics);
	};
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	GraphEffect none;
	ASSERT_TRUE(compile("", "", none, diagnostics));
	EXPECT_EQ(none.cameraOffset, 0u);

	GraphEffect effect;
	ASSERT_TRUE(compile("", R"({"id":20,"def":"Block/VectorFieldForce"},{"id":21,"def":"Block/CollisionDepth"})", effect, diagnostics))
		<< (diagnostics.empty() ? str() : diagnostics[0].message);
	EXPECT_EQ(effect.cameraOffset, none.batchesOffset);
	EXPECT_EQ(effect.batchesOffset, effect.cameraOffset + GRAPH_EFFECT_CAMERA_RECORD_BYTES);
	ASSERT_EQ(effect.systems.size(), 1u);
	const GraphEffectSystem& system = effect.systems[0];
	ASSERT_EQ(system.updateTextures.size(), 1u);
	EXPECT_EQ(system.updateDepthBinding, system.updateTextures[0].samplerBinding + 1);
	EXPECT_NE(system.updateKernel.find(std::format("[[vk::binding({}, 0)]] Texture2D sltCameraDepth;", system.updateDepthBinding)), str::npos);
	EXPECT_EQ(Count(system.updateKernel, "sltCameraDepth.Load("), 3u);
	EXPECT_EQ(system.initializeDepthBinding, GRAPH_EFFECT_NO_BINDING);
	EXPECT_EQ(system.initializeKernel.find("sltCameraDepth"), str::npos);

	GraphEffect inInitialize;
	ASSERT_TRUE(compile(R"({"id":21,"def":"Block/CollisionDepth","settings":{"Behavior":"Kill"}})", "", inInitialize, diagnostics));
	EXPECT_EQ(inInitialize.systems[0].initializeDepthBinding, GRAPH_EFFECT_KERNEL_TEXTURES);

	GraphEffect silent;
	ASSERT_TRUE(compile("", R"({"id":21,"def":"Block/CollisionDepth","settings":{"Behavior":"None"}})", silent, diagnostics));
	EXPECT_EQ(silent.systems[0].updateDepthBinding, GRAPH_EFFECT_NO_BINDING);

	str output = OneSystem("", "");
	const std::string_view emptyOutput = R"("def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[])";
	output.replace(output.find(emptyOutput), emptyOutput.size(),
				   R"("def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[{"id":10,"def":"Block/CollisionDepth","settings":{"Behavior":"Kill"}}])");
	GraphEffect refused;
	diagnostics.clear();
	EXPECT_FALSE(GraphEffectTarget::Compile(ReadGraph(output), "depth.sleffect", refused, diagnostics));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& diagnostic) {
		return diagnostic.node == 10 && diagnostic.message.find("Depth Buffer") != str::npos;
	}));

	const auto offered = [](std::string_view context) {
		GraphDocument document = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[{{"id":1,"def":"{}"}}]}})", context));
		Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		EffectGraphEditOps::BlockChoices(document, 1, choices);
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (const EffectGraphEditOps::BlockChoice& choice : choices) {
			if (choice.def == GRAPH_EFFECT_COLLISION_DEPTH) {
				out.push_back(choice.name);
			}
		}
		return out;
	};
	EXPECT_EQ(offered(GRAPH_EFFECT_UPDATE), Names({"Collision Depth Buffer", "Kill Depth Buffer", "Trigger Depth Buffer"}));
	EXPECT_EQ(offered(GRAPH_EFFECT_INITIALIZE), Names({"Collision Depth Buffer", "Kill Depth Buffer", "Trigger Depth Buffer"}));
	EXPECT_TRUE(offered(GRAPH_EFFECT_OUTPUT).empty());
}

TEST(GraphEffectLibraryTest, SetPositionDepthsPortsFollowSettings) {
	GraphDefinitions definitions;
	GraphDocument holder;
	const GraphNodeDefinition* plain = BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_DEPTH, "{}");
	ASSERT_NE(plain, nullptr);
	EXPECT_EQ(plain->name, "Set Position (Depth)");
	EXPECT_EQ(InputNames(*plain), Names({"Z Multiplier"}));
	ASSERT_EQ(plain->outputs.size(), 1u);
	EXPECT_EQ(plain->outputs[0].name, "position");

	const GraphNodeDefinition* sequential = BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_DEPTH, R"({"Mode":"Sequential"})");
	ASSERT_NE(sequential, nullptr);
	EXPECT_EQ(InputNames(*sequential), Names({"Z Multiplier", "Grid Step"}));

	const GraphNodeDefinition* full =
		BlockShape(definitions, holder, GRAPH_EFFECT_POSITION_DEPTH, R"({"Mode":"Custom","Cull Mode":"Range","Composition Position":"Blend"})");
	ASSERT_NE(full, nullptr);
	EXPECT_EQ(full->name, "Blend Position (Depth)");
	ASSERT_EQ(InputNames(*full), Names({"Z Multiplier", "UV Spawn", "Depth Range", "Blend Position"}));
	EXPECT_EQ(full->inputs[2].defaultValue[1], 1.0f);
	EXPECT_EQ(full->inputs[3].defaultValue[0], 0.0f);
	ASSERT_EQ(full->outputs.size(), 2u);
	EXPECT_EQ(full->outputs[1].name, "alive");
}

TEST(GraphEffectLibraryTest, SetPositionDepthReadsInInitialize) {
	const auto compile = [](std::string_view target, std::string_view initialize, std::string_view update, GraphEffect& effect,
							Vector<GraphDiagnostic>& diagnostics) {
		str graph = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":8{}}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}}]}})",
								target, initialize, update);
		diagnostics.clear();
		return GraphEffectTarget::Compile(ReadGraph(graph), "depth.sleffect", effect, diagnostics);
	};
	const str block = R"({"id":20,"def":"Block/PositionDepth","settings":{"Mode":"Custom"}})";
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	GraphEffect placed;
	ASSERT_TRUE(compile("", block, "", placed, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	EXPECT_NE(placed.cameraOffset, 0u);
	ASSERT_EQ(placed.systems.size(), 1u);
	EXPECT_EQ(placed.systems[0].initializeDepthBinding, GRAPH_EFFECT_KERNEL_TEXTURES);
	EXPECT_EQ(Count(placed.systems[0].initializeKernel, "sltCameraDepth.Load("), 1u);

	GraphEffect updated;
	EXPECT_FALSE(compile("", "", block, updated, diagnostics));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& diagnostic) { return diagnostic.node == 20; }));

	GraphEffect cpu;
	EXPECT_FALSE(compile(R"(,"Simulation Target":"CPU")", block, "", cpu, diagnostics));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& diagnostic) {
		return diagnostic.node == 20 && diagnostic.message.find("camera's depth") != str::npos;
	}));

	const auto offered = [](std::string_view context) {
		GraphDocument document = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[{{"id":1,"def":"{}"}}]}})", context));
		Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		EffectGraphEditOps::BlockChoices(document, 1, choices);
		return std::any_of(choices.begin(), choices.end(), [](const EffectGraphEditOps::BlockChoice& choice) {
			return choice.def == GRAPH_EFFECT_POSITION_DEPTH && choice.name == "Set Position (Depth)" && choice.category == "Position Shape";
		});
	};
	EXPECT_TRUE(offered(GRAPH_EFFECT_INITIALIZE));
	EXPECT_FALSE(offered(GRAPH_EFFECT_UPDATE));
	EXPECT_FALSE(offered(GRAPH_EFFECT_OUTPUT));
}

TEST(GraphEffectLibraryTest, OrientIsOutputsAndPortsAreModes) {
	GraphDefinitions definitions;
	GraphDocument holder;
	const auto ports = [&](std::string_view settings) {
		const GraphNodeDefinition* definition = BlockShape(definitions, holder, GRAPH_EFFECT_ORIENT, settings);
		EXPECT_NE(definition, nullptr) << settings;
		return definition ? InputNames(*definition) : Vector<str>{GetMemoryResource(MemTag::Editor)};
	};
	EXPECT_TRUE(ports("{}").empty());
	EXPECT_EQ(ports(R"({"Mode":"LookAtPosition"})"), Names({"Position"}));
	EXPECT_EQ(ports(R"({"Mode":"LookAtLine"})"), Names({"Start", "End"}));
	EXPECT_EQ(ports(R"({"Mode":"Advanced"})"), Names({"Axis Z", "Axis Y"}));
	EXPECT_EQ(ports(R"({"Mode":"Advanced","Axes":"XY"})"), Names({"Axis X", "Axis Y"}));
	EXPECT_EQ(ports(R"({"Mode":"FixedAxis"})"), Names({"Up"}));
	EXPECT_EQ(ports(R"({"Mode":"CustomZ"})"), Names({"Front"}));
	EXPECT_EQ(ports(R"({"Mode":"CustomY"})"), Names({"Up"}));
	const GraphNodeDefinition* line = BlockShape(definitions, holder, GRAPH_EFFECT_ORIENT, R"({"Mode":"LookAtLine"})");
	ASSERT_NE(line, nullptr);
	EXPECT_EQ(line->name, "Orient: Look At Line");
	EXPECT_EQ(line->inputs[1].defaultValue[0], -1.0f);
	ASSERT_EQ(line->outputs.size(), 3u);
	EXPECT_EQ(line->outputs[2].name, "axisZ");

	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_ORIENT));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_ORIENT));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_ORIENT));
	for (const GraphEffectKernel kernel : {GraphEffectKernel::Initialize, GraphEffectKernel::Update, GraphEffectKernel::Output}) {
		Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
		GraphEffectTarget::KernelBindings(kernel, bindings);
		const auto found = std::find_if(bindings.begin(), bindings.end(), [](const GraphBinding& binding) { return binding.name == GRAPH_EFFECT_CAMERA_FORWARD; });
		ASSERT_NE(found, bindings.end());
		EXPECT_EQ(found->unavailable.empty(), kernel == GraphEffectKernel::Output);
	}

	GraphDocument document = ReadGraph(R"({"_type":"EffectGraph","version":4,"nextId":100,"contexts":[{"id":1,"def":"Context/OutputParticlePrimitive"}]})");
	Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	EffectGraphEditOps::BlockChoices(document, 1, choices);
	Vector<str> orients{GetMemoryResource(MemTag::Editor)};
	for (const EffectGraphEditOps::BlockChoice& choice : choices) {
		if (choice.def == GRAPH_EFFECT_ORIENT) {
			orients.push_back(choice.name);
		}
	}
	EXPECT_EQ(orients, Names({"Orient: Face Camera Plane", "Orient: Along Velocity", "Orient: Advanced", "Orient: Face Camera Position", "Orient: Look At Position",
							  "Orient: Look At Line", "Orient: Fixed Axis", "Orient: Custom Z", "Orient: Custom Y"}));

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(OneSystem("", "")), "quad.sleffect", effect, diagnostics)) << diagnostics[0].message;
	const str& shader = effect.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("SltParticleElementPosition("), str::npos);
	EXPECT_EQ(shader.find("inverseView"), str::npos);
	EXPECT_NE(shader.find("SltParticleEffectLocalToWorld()"), str::npos);
}

namespace {

	str OneSystemShown(std::string_view nodes, std::string_view initialize, std::string_view update, std::string_view settings, std::string_view in) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"settings":{},"in":{},"blocks":[]}}]}})",
						   nodes, initialize, update, settings.empty() ? "{}" : settings, in.empty() ? "{}" : in);
	}

	Vector<str> ShownInputs(const GraphNodeDefinition& definition, const GraphNode& node) {
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		for (u32 i = 0; i < definition.inputs.size(); ++i) {
			if (GraphNodeLibrary::InputActive(definition, i, node)) {
				names.push_back(str(definition.inputs[i].name));
			}
		}
		return names;
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, OutputSamplesMainTextureAsSettingsSay) {
	const GraphNodeDefinition* output = GraphNodeLibrary::Find(GRAPH_EFFECT_OUTPUT);
	ASSERT_NE(output, nullptr);
	const auto shown = [&](std::string_view settings) {
		const GraphDocument document = ReadGraph(OneSystemShown("", "", "", settings, ""));
		return ShownInputs(*output, document.contexts[2].node);
	};
	EXPECT_EQ(shown(""), Names({"Main Texture"}));
	EXPECT_EQ(shown(R"({"UV Mode":"Flipbook"})"), Names({"Main Texture", "Flip Book Size"}));
	EXPECT_EQ(shown(R"({"UV Mode":"ScaleAndBias"})"), Names({"Main Texture", "UV Scale", "UV Bias"}));
	EXPECT_TRUE(shown(R"({"Use Base Color Map":"None","UV Mode":"Flipbook"})").empty());
	EXPECT_EQ(output->inputs[1].defaultValue[0], 4.0f);

	const str guid = "0123456789abcdef-0123456789abcdef";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(OneSystemShown("", "", "", R"({"UV Mode":"Flipbook","Flipbook Blend Frames":true})",
																	std::format(R"({{"Main Texture":{{"value":"{}"}}}})", guid))),
										   "texture.sleffect", effect, diagnostics))
		<< diagnostics[0].message;
	const GraphEffectOutput& sampled = effect.systems[0].outputs[0];
	ASSERT_EQ(sampled.textures.size(), 1u);
	EXPECT_EQ(sampled.textures[0].slot, "mainTexture");
	EXPECT_EQ(sampled.textures[0].texture.ToString(), guid);
	EXPECT_NE(sampled.shader.find("Texture2D mainTexture"), str::npos);
	EXPECT_NE(sampled.shader.find("SltParticleFlipbookUVs("), str::npos);
	EXPECT_NE(sampled.shader.find("sltIn.blend"), str::npos);
	bool storesFrame = false;
	for (const GraphEffectStored& stored : effect.systems[0].stored) {
		storesFrame = storesFrame || stored.attribute == "texIndex";
	}
	EXPECT_FALSE(storesFrame);

	diagnostics.clear();
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(OneSystemShown("", "", "", R"({"Use Base Color Map":"None"})",
																	std::format(R"({{"Main Texture":{{"value":"{}"}}}})", guid))),
										   "plain.sleffect", effect, diagnostics));
	EXPECT_TRUE(effect.systems[0].outputs[0].textures.empty());
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("mainTexture"), str::npos);
}

namespace {

	str Drawn(std::string_view initialize, std::string_view outputs) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[
				{{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[0.5]}}}}}}{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[]}},{}]}})",
						   initialize, outputs);
	}

	str Output(u32 id, std::string_view def, std::string_view settings = "{}") {
		return std::format(R"({{"id":{},"def":"{}","flow":[[2,"In"]],"settings":{},"blocks":[]}})", id, def, settings);
	}

	bool Stores(const GraphEffectSystem& system, std::string_view attribute) {
		return std::any_of(system.stored.begin(), system.stored.end(), [&](const GraphEffectStored& stored) { return stored.attribute == attribute; });
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, EachOutputKindCompilesToDraw) {
	struct Case {
		const char* name;
		std::string_view def;
		const char* settings;
		u32 vertices;
		const char* says;
	};
	const Case cases[] = {
		{"Triangle", GRAPH_EFFECT_OUTPUT, R"({"Primitive Type":"Triangle"})", 3, "SltParticleTriangleCorner("},
		{"Quad", GRAPH_EFFECT_OUTPUT, "{}", 6, "SltParticleQuadCorner("},
		{"Octagon", GRAPH_EFFECT_OUTPUT, R"({"Primitive Type":"Octagon"})", 24, "SltParticleOctagonCorner("},
		{"Point", GRAPH_EFFECT_OUTPUT_POINT, "{}", 1, "#pragma shalltear_topology point"},
		{"Line", GRAPH_EFFECT_OUTPUT_LINE, "{}", 6, "SltParticleLineCorner("},
		{"NativeLine", GRAPH_EFFECT_OUTPUT_LINE, R"({"Use Native Lines":true})", 2, "#pragma shalltear_topology line"},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(Drawn("", Output(3, entry.def, entry.settings))), "kind.sleffect", effect, diagnostics))
			<< diagnostics[0].message;
		const GraphEffectOutput& output = effect.systems[0].outputs[0];
		EXPECT_EQ(output.vertices, entry.vertices);
		EXPECT_FALSE(output.instanced);
		EXPECT_EQ(output.mesh, GRAPH_EFFECT_NO_BINDING);
		EXPECT_NE(output.shader.find(entry.says), str::npos) << output.shader;
		EXPECT_NE(output.shader.find("#pragma shalltear_cull off"), str::npos);
	}

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const str four = Output(3, GRAPH_EFFECT_OUTPUT_POINT) + "," + Output(4, GRAPH_EFFECT_OUTPUT_MESH) + "," + Output(5, GRAPH_EFFECT_OUTPUT) + "," +
					 Output(6, GRAPH_EFFECT_OUTPUT_LINE);
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(Drawn("", four)), "four.sleffect", effect, diagnostics)) << diagnostics[0].message;
	const Vector<GraphEffectOutput>& outputs = effect.systems[0].outputs;
	ASSERT_EQ(outputs.size(), 4u);
	EXPECT_EQ(outputs[0].vertices, 1u);
	EXPECT_EQ(outputs[2].vertices, 6u);
	EXPECT_EQ(outputs[3].vertices, 6u);
	const GraphEffectOutput& mesh = outputs[1];
	EXPECT_TRUE(mesh.instanced);
	ASSERT_LT(mesh.mesh, effect.meshes.size());
	EXPECT_EQ(effect.meshes[mesh.mesh].binding, GraphCompiler::PortMeshBinding(4, "Mesh"));
	EXPECT_NE(mesh.shader.find(std::format("SltParticleMeshIndex({}u", effect.meshes[mesh.mesh].offset)), str::npos);
	EXPECT_NE(mesh.shader.find("SV_InstanceID"), str::npos);
	EXPECT_NE(mesh.shader.find("#pragma shalltear_cull back"), str::npos);
}

TEST(GraphEffectLibraryTest, AnOutputReadsWhatItsKindDraws) {
	const auto compiled = [](std::string_view initialize, std::string_view outputs) {
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		EXPECT_TRUE(GraphEffectTarget::Compile(ReadGraph(Drawn(initialize, outputs)), "reads.sleffect", effect, diagnostics))
			<< (diagnostics.empty() ? str() : diagnostics[0].message);
		return effect;
	};
	EXPECT_TRUE(Stores(compiled("", Output(3, GRAPH_EFFECT_OUTPUT)).systems[0], "size"));
	EXPECT_FALSE(Stores(compiled("", Output(3, GRAPH_EFFECT_OUTPUT_POINT)).systems[0], "size"));
	const std::string_view target = R"(,{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"targetPosition"},"in":{"TargetPosition":{"value":[0,1,0]}}})";
	const GraphEffect toTarget = compiled(target, Output(3, GRAPH_EFFECT_OUTPUT_LINE, R"({"Use Target Offset":false})"));
	EXPECT_TRUE(Stores(toTarget.systems[0], "targetPosition"));
	EXPECT_FALSE(Stores(toTarget.systems[0], "size"));
	const GraphEffect toOffset = compiled(target, Output(3, GRAPH_EFFECT_OUTPUT_LINE));
	EXPECT_FALSE(Stores(toOffset.systems[0], "targetPosition"));
	EXPECT_TRUE(Stores(toOffset.systems[0], "size"));
	EXPECT_TRUE(compiled("", Output(3, GRAPH_EFFECT_OUTPUT_POINT)).systems[0].outputs[0].textures.empty());
}

TEST(GraphEffectLibraryTest, OutputShowsPortsSettingsUse) {
	const auto shown = [](std::string_view def, std::string_view settings) {
		const GraphDocument document = ReadGraph(Drawn("", Output(3, def, settings)));
		return ShownInputs(*GraphNodeLibrary::Find(def), document.contexts[2].node);
	};
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT, "{}"), Names({"Main Texture"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT, R"({"Primitive Type":"Octagon"})"), Names({"Main Texture", "Crop Factor"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LINE, "{}"), Names({"Target Offset"}));
	EXPECT_TRUE(shown(GRAPH_EFFECT_OUTPUT_LINE, R"({"Use Target Offset":false})").empty());
	EXPECT_TRUE(shown(GRAPH_EFFECT_OUTPUT_POINT, "{}").empty());
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_MESH, "{}"), Names({"Mesh", "Main Texture"}));
	const GraphNodeDefinition* primitive = GraphNodeLibrary::Find(GRAPH_EFFECT_OUTPUT);
	EXPECT_EQ(primitive->settings[0].options[primitive->settings[0].defaultIndex], "Quad");
}

namespace {

	str SortedBy(std::string_view def, std::string_view settings, std::string_view in = "{}", std::string_view blocks = "", std::string_view nodes = "") {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[
				{{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[0.5]}}}}}},
				{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[0,0,1]}}}}}}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[]}},
			{{"id":3,"def":"{}","flow":[[2,"In"]],"settings":{},"in":{},"blocks":[{}]}}]}})",
						   nodes, def, settings, in, blocks);
	}

	bool CompileSorted(const str& text, GraphEffect& effect, str& error) {
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		const bool compiled = GraphEffectTarget::Compile(ReadGraph(text), "sorted.sleffect", effect, diagnostics);
		error = diagnostics.empty() ? str() : diagnostics[0].message;
		return compiled;
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, OutputSortsAsHasSortingSays) {
	struct Case {
		const char* settings;
		GraphEffectSort sort;
		const char* keys;
	};
	const Case cases[] = {
		{"{}", GraphEffectSort::DistanceToCamera, "SltParticleSortDistance("},
		{R"({"Blend Mode":"AlphaPremultiplied"})", GraphEffectSort::DistanceToCamera, "SltParticleSortDistance("},
		{R"({"Blend Mode":"Additive"})", GraphEffectSort::None, nullptr},
		{R"({"Blend Mode":"Opaque"})", GraphEffectSort::None, nullptr},
		{R"({"Blend Mode":"Additive","Sort":"On"})", GraphEffectSort::DistanceToCamera, "SltParticleSortDistance("},
		{R"({"Sort":"Off"})", GraphEffectSort::None, nullptr},
		{R"({"Sort Mode":"CameraDepth"})", GraphEffectSort::CameraDepth, "SltParticleSortDepth("},
		{R"({"Sort Mode":"YoungestInFront"})", GraphEffectSort::YoungestInFront, "SltParticleSortStore(sltSlot, sltIndex, sltAttr_age)"},
		{R"({"Sort Mode":"Custom"})", GraphEffectSort::Custom, "SltParticleSortStore(sltSlot, sltIndex, -("},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.settings);
		GraphEffect effect;
		str error;
		ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, entry.settings), effect, error)) << error;
		const GraphEffectOutput& output = effect.systems[0].outputs[0];
		EXPECT_EQ(output.sort, entry.sort);
		EXPECT_FALSE(output.revertSorting);
		if (!entry.keys) {
			EXPECT_TRUE(output.sortKernel.empty());
			continue;
		}
		EXPECT_NE(output.sortKernel.find("#pragma shalltear_kernel"), str::npos);
		EXPECT_NE(output.sortKernel.find("#include \"VisualEffects/ParticleSort.hlsli\""), str::npos);
		EXPECT_NE(output.sortKernel.find(entry.keys), str::npos) << output.sortKernel;
	}
	GraphEffect reverted;
	str error;
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_POINT, R"({"Sort":"On","Revert Sorting":true})"), reverted, error)) << error;
	EXPECT_TRUE(reverted.systems[0].outputs[0].revertSorting);
	EXPECT_EQ(reverted.systems[0].outputs[0].sort, GraphEffectSort::DistanceToCamera);
}

TEST(GraphEffectLibraryTest, SortReadsParticleAsSimulationStoredIt) {
	GraphEffect effect;
	str error;
	const std::string_view moved = R"({"id":30,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,9,0]}}})";
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, "{}", "{}", moved), effect, error)) << error;
	const str& kernel = effect.systems[0].outputs[0].sortKernel;
	EXPECT_NE(kernel.find("SltParticleLoad("), str::npos);
	EXPECT_EQ(kernel.find("9.0"), str::npos);
	EXPECT_NE(kernel.find("mul(SltParticleEffectLocalToWorld(), float4("), str::npos);

	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, R"({"Sort Mode":"YoungestInFront"})"), effect, error)) << error;
	EXPECT_TRUE(Stores(effect.systems[0], "age"));
	EXPECT_NE(effect.systems[0].updateKernel.find("sltAttr_age"), str::npos);
}

TEST(GraphEffectLibraryTest, SortKeySamplesNoTexture) {
	GraphEffect effect;
	str error;
	const str custom = R"({"Sort Mode":"Custom"})";
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, custom, R"({"Sort Key":{"from":[20,"size"]}})", "",
									   R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"size"}})"),
							  effect, error))
		<< error;
	EXPECT_NE(effect.systems[0].outputs[0].sortKernel.find("-(sltAttr_size)"), str::npos) << effect.systems[0].outputs[0].sortKernel;

	const str sampled = R"({"id":20,"def":"Sampling/SampleTexture2D","in":{"Texture":{"value":"00000000000000a1-00000000000000b1"},"UV":{"value":[0.5,0.5]}}})";
	EXPECT_FALSE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, custom, R"({"Sort Key":{"from":[20,"s"]}})", "", sampled), effect, error));
	EXPECT_NE(error.find("samples no texture"), str::npos) << error;
}

TEST(GraphEffectLibraryTest, AnOutputShowsItsSharedSettingsPorts) {
	const auto shown = [](std::string_view def, std::string_view settings) {
		const GraphDocument document = ReadGraph(SortedBy(def, settings));
		return ShownInputs(*GraphNodeLibrary::Find(def), document.contexts[2].node);
	};
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, "{}"), Names({}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, R"({"Use Alpha Clipping":true})"), Names({"Alpha Threshold"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, R"({"Use Soft Particle":true})"), Names({"Soft Particle Fade Distance"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, R"({"Use Soft Particle":true,"Blend Mode":"Opaque"})"), Names({}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LINE, R"({"Sort Mode":"Custom"})"), Names({"Target Offset", "Sort Key"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_MESH, R"({"Sort Mode":"Custom","Sort":"Off"})"), Names({"Mesh", "Main Texture"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT, R"({"Use Alpha Clipping":true,"Use Soft Particle":true,"Sort Mode":"Custom"})"),
			  Names({"Main Texture", "Alpha Threshold", "Soft Particle Fade Distance", "Sort Key"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, R"({"Cast Shadows":true})"), Names({"Alpha Threshold"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, R"({"Cast Shadows":true,"Blend Mode":"Opaque"})"), Names({}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_POINT, R"({"Cast Shadows":true,"Blend Mode":"Opaque","Use Alpha Clipping":true})"), Names({"Alpha Threshold"}));

	for (std::string_view def : {GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_OUTPUT_MESH, GRAPH_EFFECT_OUTPUT_POINT, GRAPH_EFFECT_OUTPUT_LINE, GRAPH_EFFECT_OUTPUT_LIT,
								 GRAPH_EFFECT_OUTPUT_LIT_MESH}) {
		SCOPED_TRACE(def);
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
		ASSERT_NE(definition, nullptr);
		u32 mode = 0;
		for (u32 i = 0; i < definition->settings.size(); ++i) {
			mode = definition->settings[i].key == "Sort Mode" ? i : mode;
		}
		ASSERT_NE(mode, 0u);
		const GraphDocument off = ReadGraph(SortedBy(def, R"({"Sort":"Off"})"));
		const GraphDocument automatic = ReadGraph(SortedBy(def, "{}"));
		EXPECT_FALSE(GraphNodeLibrary::SettingActive(definition->settings, mode, off.contexts[2].node));
		EXPECT_TRUE(GraphNodeLibrary::SettingActive(definition->settings, mode, automatic.contexts[2].node));
		EXPECT_EQ(definition->settings[mode].options[definition->settings[mode].defaultIndex], "DistanceToCamera");
		const GraphDocument additive = ReadGraph(SortedBy(def, R"({"Blend Mode":"Additive"})"));
		const GraphDocument opaque = ReadGraph(SortedBy(def, R"({"Blend Mode":"Opaque"})"));
		const GraphDocument forced = ReadGraph(SortedBy(def, R"({"Blend Mode":"Additive","Sort":"On"})"));
		EXPECT_FALSE(GraphNodeLibrary::SettingActive(definition->settings, mode, additive.contexts[2].node));
		EXPECT_FALSE(GraphNodeLibrary::SettingActive(definition->settings, mode + 1, opaque.contexts[2].node));
		EXPECT_TRUE(GraphNodeLibrary::SettingActive(definition->settings, mode, forced.contexts[2].node));
		EXPECT_TRUE(GraphNodeLibrary::SettingActive(definition->settings, mode + 1, forced.contexts[2].node));
		EXPECT_EQ(definition->settings[mode + 1].key, "Revert Sorting");
	}
}

TEST(GraphEffectLibraryTest, SoftParticleFadesClippedDiscards) {
	GraphEffect effect;
	str error;
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, R"({"Use Soft Particle":true,"Use Alpha Clipping":true})",
									   R"({"Soft Particle Fade Distance":{"value":[0.25]},"Alpha Threshold":{"value":[0.3]}})"),
							  effect, error))
		<< error;
	const str& shader = effect.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("[[vk::binding(1, 2)]] Texture2D sltSceneDepth;"), str::npos);
	const size_t fade = shader.find("SltParticleSoftFade(");
	const size_t clip = shader.rfind("clip(sltColor.a - sltIn.threshold);");
	ASSERT_NE(fade, str::npos);
	ASSERT_NE(clip, str::npos);
	EXPECT_LT(fade, clip);
	EXPECT_NE(shader.find("sltOut.softDistance = "), str::npos);
	EXPECT_NE(shader.find("sltOut.threshold = "), str::npos);

	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_MESH, R"({"Use Soft Particle":true,"Blend Mode":"Opaque","Use Alpha Clipping":true})"), effect, error))
		<< error;
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("sltSceneDepth"), str::npos);
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("clip(sltColor.a"), str::npos);
}

TEST(GraphEffectLibraryTest, CameraFadeUsesClipW) {
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_CAMERA_FADE));
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT_LINE, GRAPH_EFFECT_CAMERA_FADE));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_CAMERA_FADE));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_CAMERA_FADE));
	const GraphNodeDefinition* fade = GraphNodeLibrary::Find(GRAPH_EFFECT_CAMERA_FADE);
	ASSERT_NE(fade, nullptr);
	EXPECT_EQ(fade->settings[0].options[fade->settings[0].defaultIndex], "Alpha");
	EXPECT_EQ(fade->settings[1].defaultIndex, 1u);
	EXPECT_EQ(fade->settings[2].key, "Affect Shadows");
	EXPECT_EQ(fade->settings[2].defaultIndex, 0u);
	EXPECT_FLOAT_EQ(fade->inputs[0].defaultValue[0], 0.5f);
	EXPECT_FLOAT_EQ(fade->inputs[1].defaultValue[0], 2.0f);

	GraphEffect effect;
	str error;
	const std::string_view block = R"({"id":30,"def":"Block/CameraFade","settings":{"Fade Mode":"ColorAndAlpha"}})";
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, "{}", "{}", block), effect, error)) << error;
	const str& shader = effect.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("#include \"VisualEffects/ParticleCamera.hlsli\""), str::npos);
	EXPECT_NE(shader.find("mul(SltParticleCameraClipWRow(), SltParticleEffectLocalToWorld())"), str::npos);
	EXPECT_NE(shader.find("SltParticleShadowPass()"), str::npos);
	const std::string_view shadows = R"({"id":30,"def":"Block/CameraFade","settings":{"Affect Shadows":true}})";
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT, "{}", "{}", shadows), effect, error)) << error;
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("SltParticleShadowPass()"), str::npos);

	const std::string_view inUpdate = R"({"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
		{"id":1,"def":"Context/Initialize","settings":{"Capacity":64},"blocks":[]},
		{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{"id":30,"def":"Block/CameraFade"}]}]})";
	EXPECT_FALSE(CompileSorted(str(inUpdate), effect, error));
}

TEST(GraphEffectLibraryTest, TheFlipbookPlayersPortsAreItsModes) {
	GraphDefinitions definitions;
	GraphDocument holder;
	const auto ports = [&](std::string_view settings) {
		const GraphNodeDefinition* definition = BlockShape(definitions, holder, GRAPH_EFFECT_FLIPBOOK_PLAYER, settings);
		EXPECT_NE(definition, nullptr) << settings;
		return definition ? InputNames(*definition) : Vector<str>{GetMemoryResource(MemTag::Editor)};
	};
	EXPECT_EQ(ports("{}"), Names({"Frame Rate"}));
	EXPECT_EQ(ports(R"({"Frame Rate Mode":"Random"})"), Names({"Frame Rate Range", "Seed"}));
	EXPECT_EQ(ports(R"({"Frame Rate Mode":"BySpeed"})"), Names({"Frame Rate Curve", "Speed Range"}));
	EXPECT_EQ(ports(R"({"Frame Rate Mode":"Custom"})"), Names({"Frame Rate Curve", "Sample Time"}));
	EXPECT_EQ(ports(R"({"Mode":"Cycles"})"), Names({"Cycles", "Start Offset"}));
	EXPECT_EQ(ports(R"({"Mode":"Cycles","Cycles Mode":"RandomFullCycle","Custom Curve":true,"Animation Range":"FlipbookRow"})"),
			  Names({"Cycles Range", "Seed", "Start Offset", "Custom Curve", "Index"}));
	EXPECT_EQ(ports(R"({"Use Custom Range":true,"Animation Range":"StartEndFrames"})"), Names({"Frame Rate", "Frame Range"}));
	EXPECT_EQ(ports(R"({"Animation Range":"StartEndFrames"})"), Names({"Frame Rate"}));

	const GraphNodeDefinition* curve = BlockShape(definitions, holder, GRAPH_EFFECT_FLIPBOOK_PLAYER, R"({"Frame Rate Mode":"OverLifetime"})");
	ASSERT_NE(curve, nullptr);
	EXPECT_EQ(GraphPortLabel(curve->inputs[0]), "Frame Rate");
	EXPECT_NE(curve->inputs[0].defaultJson.find("24"), std::string_view::npos);
	const GraphNodeDefinition* row = BlockShape(definitions, holder, GRAPH_EFFECT_FLIPBOOK_PLAYER, R"({"Mode":"Cycles","Animation Range":"FlipbookRow"})");
	ASSERT_NE(row, nullptr);
	ASSERT_EQ(row->outputs.size(), 2u);
	EXPECT_EQ(row->outputs[1].name, "texIndexBlend");

	const GraphNodeDefinition* table = GraphNodeLibrary::Find(GRAPH_EFFECT_FLIPBOOK_PLAYER);
	ASSERT_NE(table, nullptr);
	const auto rangeShown = [&](std::string_view settings) {
		const GraphDocument document = ReadGraph(std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"contexts":[
			{{"id":1,"def":"Context/Update","blocks":[{{"id":10,"def":"Block/FlipbookPlayer","settings":{}}}]}}]}})",
															 settings));
		return GraphNodeLibrary::SettingActive(table->settings, 4, document.contexts[0].blocks[0]);
	};
	EXPECT_FALSE(rangeShown("{}"));
	EXPECT_TRUE(rangeShown(R"({"Mode":"Cycles"})"));
	EXPECT_TRUE(rangeShown(R"({"Use Custom Range":true})"));

	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_FLIPBOOK_PLAYER));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_FLIPBOOK_PLAYER));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_FLIPBOOK_PLAYER));
}

namespace {

	constexpr std::string_view IMAGE_GUID = "00000000000000a1-00000000000000b1";
	constexpr std::string_view FIELD_GUID = "00000000000000a2-00000000000000b2";
	constexpr std::string_view VOLUME_GUID = "00000000000000a3-00000000000000b3";

	str SampledEverywhere() {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[
			{{"id":20,"def":"Sampling/SampleTexture2D","in":{{"Texture":{{"value":"{0}"}},"UV":{{"value":[0.5,0.5]}}}}}},
			{{"id":30,"def":"Sampling/SampleTexture3D","in":{{"Texture":{{"value":"{2}"}}}}}},
			{{"id":31,"def":"Sampling/SampleTexture3D","in":{{"Texture":{{"value":"{1}"}}}}}}],"contexts":[
			{{"id":1,"def":"Context/Initialize","blocks":[{{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"from":[20,"s"]}}}}}}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"from":[31,"s"]}}}}}}]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"alpha"}},"in":{{"Alpha":{{"from":[30,"s"]}}}}}}]}}]}})",
						   IMAGE_GUID, FIELD_GUID, VOLUME_GUID);
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, TextureDeclaredWhereSampled) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(SampledEverywhere()), "sampled.sleffect", effect, diagnostics)) << diagnostics[0].message;
	const GraphEffectSystem& system = effect.systems[0];

	ASSERT_EQ(system.initializeTextures.size(), 1u);
	EXPECT_EQ(system.initializeTextures[0].binding, 4u);
	EXPECT_EQ(system.initializeTextures[0].samplerBinding, 5u);
	EXPECT_EQ(system.initializeTextures[0].texture.ToString(), IMAGE_GUID);
	EXPECT_NE(system.initializeKernel.find("[[vk::binding(4, 0)]] Texture2D sltTexture20_0;"), str::npos);
	EXPECT_NE(system.initializeKernel.find("[[vk::binding(5, 0)]] SamplerState sltTexture20_0Sampler;"), str::npos);
	EXPECT_EQ(system.initializeKernel.find("sltTexture31_0"), str::npos);

	ASSERT_EQ(system.updateTextures.size(), 1u);
	EXPECT_EQ(system.updateTextures[0].binding, 4u);
	EXPECT_EQ(system.updateTextures[0].texture.ToString(), FIELD_GUID);
	EXPECT_NE(system.updateKernel.find("[[vk::binding(4, 0)]] Texture3D sltTexture31_0;"), str::npos);

	const GraphEffectOutput& output = system.outputs[0];
	ASSERT_EQ(output.textures.size(), 2u);
	EXPECT_EQ(output.textures[0].slot, "mainTexture");
	EXPECT_EQ(output.textures[1].slot, "sltTexture30_0");
	EXPECT_EQ(output.textures[1].texture.ToString(), VOLUME_GUID);
	EXPECT_NE(output.shader.find("[[vk::binding(2, 1)]] Texture3D sltTexture30_0;"), str::npos);
	EXPECT_NE(output.shader.find("[[vk::binding(3, 1)]] SamplerState sltTexture30_0Sampler;"), str::npos);

	str empty = SampledEverywhere();
	const str held = std::format(R"("Texture":{{"value":"{}"}})", FIELD_GUID);
	empty.replace(empty.find(held), held.size(), R"("Texture":{"value":null})");
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(empty), "unheld.sleffect", effect, diagnostics));
	ASSERT_EQ(effect.systems[0].updateTextures.size(), 1u);
	EXPECT_FALSE(effect.systems[0].updateTextures[0].texture.IsValid());
}

TEST(GraphEffectLibraryTest, TextureInSpawnOrBoundRefused) {
	const str sample = std::format(R"({{"id":20,"def":"Sampling/SampleTexture2D","in":{{"Texture":{{"value":"{}"}}}}}})", IMAGE_GUID);
	const str rated = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
		{{"id":5,"def":"Context/Spawn","blocks":[{{"id":6,"def":"Block/ConstantSpawnRate","in":{{"Rate":{{"from":[20,"s"]}}}}}}]}},
		{{"id":1,"def":"Context/Initialize","flow":[[5,"In"]],"blocks":[]}}]}})",
								  sample);
	const str bounded = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
		{{"id":1,"def":"Context/Initialize","in":{{"Bounds Size":{{"from":[20,"s"]}}}},"blocks":[]}}]}})",
									sample);
	for (const str& text : {rated, bounded}) {
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		EXPECT_FALSE(GraphEffectTarget::Compile(ReadGraph(text), "cpu.sleffect", effect, diagnostics));
		bool named = false;
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			named = named || (diagnostic.node == 20 && diagnostic.message.find("only a particle context samples a texture") != str::npos);
		}
		EXPECT_TRUE(named) << (diagnostics.empty() ? str() : diagnostics[0].message);
	}
}

TEST(GraphEffectLibraryTest, SearcherOffersEffectsSamplingOperatorsAlone) {
	Vector<str> offered{GetMemoryResource(MemTag::Editor)};
	for (const GraphNodeDefinition* definition : GraphNodeLibrary::All()) {
		if (EffectGraphEditOps::IsOperator(*definition)) {
			offered.push_back(str(definition->id));
		}
	}
	for (std::string_view own : {GRAPH_EFFECT_SAMPLE_TEXTURE_2D, GRAPH_EFFECT_SAMPLE_TEXTURE_2D_ARRAY, GRAPH_EFFECT_SAMPLE_TEXTURE_3D,
								 GRAPH_EFFECT_SAMPLE_TEXTURE_CUBE}) {
		EXPECT_NE(std::find(offered.begin(), offered.end(), own), offered.end()) << own;
	}
	EXPECT_EQ(std::find(offered.begin(), offered.end(), "Input/TextureSize"), offered.end());
}

TEST(GraphEffectLibraryTest, FieldsAreUpdatesAndShapesOfferField) {
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_VECTOR_FIELD_FORCE));
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_CONFORM_TO_SDF));
	for (std::string_view context : {GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_OUTPUT}) {
		EXPECT_FALSE(GraphEffectRules::Holds(context, GRAPH_EFFECT_VECTOR_FIELD_FORCE)) << context;
		EXPECT_FALSE(GraphEffectRules::Holds(context, GRAPH_EFFECT_CONFORM_TO_SDF)) << context;
	}

	GraphDefinitions definitions;
	const auto built = [&](std::string_view block) {
		const GraphDocument document = ReadGraph(OneSystem("", block));
		str error;
		const GraphNodeDefinition* definition = definitions.Find(document, document.contexts[0].blocks[0], &error);
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		if (!definition) {
			names.push_back("error: " + error);
			return names;
		}
		for (const GraphPortDefinition& port : definition->inputs) {
			names.push_back(str(port.name));
		}
		names.push_back("->");
		for (const GraphPortDefinition& port : definition->outputs) {
			names.push_back(str(port.name));
		}
		return names;
	};
	const auto list = [](std::initializer_list<std::string_view> names) {
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (std::string_view name : names) {
			out.push_back(str(name));
		}
		return out;
	};
	EXPECT_EQ(built(R"({"id":10,"def":"Block/PositionShape","settings":{"Shape":"SignedDistanceField","Spawn Mode":"Custom","Position Mode":"ThicknessRelative"}})"),
			  list({"SDF", "Center", "Angles", "Size", "Arc Sequencer", "Thickness", "->", "position", "direction"}));
	EXPECT_EQ(built(R"({"id":10,"def":"Block/PositionShape","settings":{"Shape":"SignedDistanceField","Kill Outliers":true}})"),
			  list({"SDF", "Center", "Angles", "Size", "->", "position", "direction", "alive"}));
	const Vector<str> refused = built(R"({"id":10,"def":"Block/PositionShape","settings":{"Shape":"SignedDistanceField","Projection Steps":40}})");
	ASSERT_EQ(refused.size(), 1u);
	EXPECT_NE(refused[0].find("Projection Steps of 40"), str::npos) << refused[0];
	EXPECT_EQ(built(R"({"id":10,"def":"Block/CollisionShape","settings":{"Shape":"SignedDistanceField","Behavior":"Kill"}})"),
			  list({"Distance Field", "Center", "Angles", "Size", "->", "alive"}));

	const GraphNodeDefinition* sample = GraphNodeLibrary::Find(GRAPH_EFFECT_SAMPLE_SDF);
	ASSERT_NE(sample, nullptr);
	EXPECT_TRUE(EffectGraphEditOps::IsOperator(*sample));
}

namespace {

	constexpr std::string_view MESH_GUID = "00000000000000a5-00000000000000b5";

	str MeshedEverywhere() {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[
			{{"id":20,"def":"Sampling/MeshVertexCount","in":{{"Mesh":{{"value":null}}}}}},
			{{"id":21,"def":"Sampling/SampleMesh"}}],"contexts":[
			{{"id":1,"def":"Context/Initialize","blocks":[{{"id":10,"def":"Block/PositionMesh","in":{{"Mesh":{{"value":"{0}"}}}}}}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"from":[20,"Count"]}}}}}}]}},
			{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"alpha"}},"in":{{"Alpha":{{"from":[21,"Position"]}}}}}}]}}]}})",
						   MESH_GUID);
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, MeshPortReservedAndCarried) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(MeshedEverywhere()), "meshed.sleffect", effect, diagnostics)) << diagnostics[0].message;
	ASSERT_EQ(effect.meshes.size(), 3u);
	EXPECT_EQ(effect.meshes[0].binding, "PortMesh/20/Mesh");
	EXPECT_EQ(effect.meshes[0].offset, GraphEffectTarget::FRAME_HEADER_BYTES);
	EXPECT_FALSE(effect.meshes[0].mesh.IsValid());
	EXPECT_EQ(effect.meshes[1].binding, "PortMesh/21/Mesh");
	EXPECT_EQ(effect.meshes[1].offset, GraphEffectTarget::FRAME_HEADER_BYTES + GRAPH_EFFECT_MESH_RECORD_BYTES);
	EXPECT_EQ(effect.meshes[2].binding, "PortMesh/10/Mesh");
	EXPECT_EQ(effect.meshes[2].offset, GraphEffectTarget::FRAME_HEADER_BYTES + 2 * GRAPH_EFFECT_MESH_RECORD_BYTES);
	EXPECT_EQ(effect.meshes[2].mesh.ToString(), MESH_GUID);
	EXPECT_EQ(effect.batchesOffset, GraphEffectTarget::FRAME_HEADER_BYTES + 3 * GRAPH_EFFECT_MESH_RECORD_BYTES);

	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_NE(system.initializeKernel.find("SltParticleMeshVertex((160u),"), str::npos);
	EXPECT_NE(system.initializeKernel.find("SltParticleMeshCount((160u), 0u)"), str::npos);
	EXPECT_EQ(system.initializeKernel.find("(96u)"), str::npos);
	EXPECT_NE(system.updateKernel.find("float(SltParticleMeshCount((96u), 0u))"), str::npos);
	EXPECT_NE(system.outputs[0].shader.find("SltParticleMeshVertex((128u),"), str::npos);

	str depth = MeshedEverywhere();
	const std::string_view update = R"("blocks":[{"id":11,)";
	depth.replace(depth.find(update), update.size(), R"("blocks":[{"id":13,"def":"Block/CollisionDepth"},{"id":11,)");
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(depth), "depth.sleffect", effect, diagnostics)) << diagnostics[0].message;
	EXPECT_EQ(effect.cameraOffset, GraphEffectTarget::FRAME_HEADER_BYTES);
	ASSERT_EQ(effect.meshes.size(), 3u);
	EXPECT_EQ(effect.meshes[0].offset, GraphEffectTarget::FRAME_HEADER_BYTES + GRAPH_EFFECT_CAMERA_RECORD_BYTES);
	EXPECT_EQ(effect.batchesOffset, effect.meshes[2].offset + GRAPH_EFFECT_MESH_RECORD_BYTES);
}

TEST(GraphEffectLibraryTest, MeshsCountsAndSamplesReachCpu) {
	const str counted = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[
		{{"id":20,"def":"Sampling/MeshTriangleCount","in":{{"Mesh":{{"value":"{}"}}}}}}],"contexts":[
		{{"id":5,"def":"Context/Spawn","blocks":[{{"id":6,"def":"Block/Burst","in":{{"Count":{{"from":[20,"Count"]}}}}}}]}},
		{{"id":1,"def":"Context/Initialize","flow":[[5,"In"]],"blocks":[]}}]}})",
									MESH_GUID);
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(counted), "counted.sleffect", effect, diagnostics)) << diagnostics[0].message;
	const str& assembly = effect.spawners[0].blocks[0].assembly;
	EXPECT_NE(assembly.find("input 1 PortMesh/20/Mesh/IndexCount"), str::npos) << assembly;
	EXPECT_NE(assembly.find("floor"), str::npos);

	const str sample = R"({"id":21,"def":"Sampling/SampleMesh"})";
	const str rated = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
		{{"id":5,"def":"Context/Spawn","blocks":[{{"id":6,"def":"Block/ConstantSpawnRate","in":{{"Rate":{{"from":[21,"Position"]}}}}}}]}},
		{{"id":1,"def":"Context/Initialize","flow":[[5,"In"]],"blocks":[]}}]}})",
								  sample);
	const str bounded = std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
		{{"id":1,"def":"Context/Initialize","in":{{"Bounds Size":{{"from":[21,"Position"]}}}},"blocks":[]}}]}})",
									sample);
	for (const bool bounds : {false, true}) {
		SCOPED_TRACE(bounds ? "system bounds" : "Spawn rate");
		diagnostics.clear();
		ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(bounds ? bounded : rated), "cpu.sleffect", effect, diagnostics))
			<< (diagnostics.empty() ? str() : diagnostics[0].message);
		const str& program = bounds ? effect.systems[0].boundsAssembly : effect.spawners[0].blocks[0].assembly;
		EXPECT_NE(program.find("mesh PortMesh/21/Mesh\n"), str::npos) << program;
		EXPECT_NE(program.find("meshvertex 3 0 "), str::npos) << program;
	}
}

TEST(GraphEffectLibraryTest, MeshPortsFollowSettings) {
	GraphDefinitions definitions;
	const auto shape = [&](std::string_view node, bool block) {
		const GraphDocument document = ReadGraph(block ? OneSystem("", node) : OneSystem(node, ""));
		str error;
		const GraphNode& held = block ? document.contexts[0].blocks[0] : document.nodes[0];
		const GraphNodeDefinition* definition = definitions.Find(document, held, &error);
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		if (!definition) {
			names.push_back("error: " + error);
			return names;
		}
		names.push_back(str(definition->name));
		for (const GraphPortDefinition& port : definition->inputs) {
			names.push_back(str(port.name));
		}
		names.push_back("->");
		for (const GraphPortDefinition& port : definition->outputs) {
			names.push_back(str(port.name));
		}
		return names;
	};
	const auto list = [](std::initializer_list<std::string_view> names) {
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (std::string_view name : names) {
			out.push_back(str(name));
		}
		return out;
	};
	EXPECT_EQ(shape(R"({"id":20,"def":"Sampling/SampleMesh"})", false),
			  list({"Sample Mesh", "Mesh", "Vertex", "Transform Position", "Transform Angles", "Transform Scale", "->", "Position", "Color", "Tex Coord 0"}));
	EXPECT_EQ(shape(R"({"id":20,"def":"Sampling/SampleMesh","settings":{"Placement Mode":"Edge","Output Color":false,"Output Normal":true}})", false),
			  list({"Sample Mesh", "Mesh", "Index", "Edge", "Transform Position", "Transform Angles", "Transform Scale", "->", "Position", "Normal",
					"Tex Coord 0"}));
	EXPECT_EQ(shape(R"({"id":20,"def":"Sampling/SampleMesh","settings":{"Placement Mode":"Surface","Surface Coordinates":"Barycentric","Output Position":false,
		"Output Tangent":true,"Output Bitangent":true,"Output Bitangent Sign":true,"Output Tex Coord 0":false}})",
					false),
			  list({"Sample Mesh", "Mesh", "Triangle", "Barycentric", "Transform Position", "Transform Angles", "Transform Scale", "->", "Tangent", "Bitangent",
					"Bitangent Sign", "Color"}));
	EXPECT_EQ(shape(R"({"id":10,"def":"Block/PositionMesh"})", true),
			  list({"Set Position (Mesh)", "Mesh", "Transform Position", "Transform Angles", "Transform Scale", "->", "position", "direction"}));
	EXPECT_EQ(shape(R"({"id":10,"def":"Block/PositionMesh","settings":{"Spawn Mode":"Custom","Placement Mode":"Surface","Apply Orientation":"Axes",
		"Composition Position":"Blend","Composition Axes":"Blend"}})",
					true),
			  list({"Blend Position (Mesh)", "Blend Position", "Blend Axes", "Mesh", "Triangle", "Square", "Transform Position", "Transform Angles", "Transform Scale",
					"->", "position", "axisX", "axisY", "axisZ"}));
	EXPECT_EQ(shape(R"({"id":10,"def":"Block/PositionMesh","settings":{"Spawn Mode":"Custom","Placement Mode":"Edge","Apply Orientation":"DirectionAndAxes",
		"Composition Position":"Add"}})",
					true),
			  list({"Add Position (Mesh)", "Mesh", "Index", "Edge", "Transform Position", "Transform Angles", "Transform Scale", "->", "position", "direction",
					"axisX", "axisY", "axisZ"}));
	const Vector<str> refused = shape(R"({"id":10,"def":"Block/PositionMesh","settings":{"Composition Axes":"Add"}})", true);
	ASSERT_EQ(refused.size(), 1u);
	EXPECT_NE(refused[0].find("isn't one of its options"), str::npos) << refused[0];
}

TEST(GraphEffectLibraryTest, MeshPortTakesOnlyMesh) {
	for (std::string_view context : {GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_OUTPUT}) {
		EXPECT_TRUE(GraphEffectRules::Holds(context, GRAPH_EFFECT_POSITION_MESH)) << context;
	}
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_POSITION_MESH));
	const GraphDocument document = ReadGraph(OneSystem("", ""));
	Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	EffectGraphEditOps::BlockChoices(document, 1, choices);
	const bool offered = std::any_of(choices.begin(), choices.end(), [](const EffectGraphEditOps::BlockChoice& choice) {
		return choice.def == GRAPH_EFFECT_POSITION_MESH && choice.name == "Set Position (Mesh)" && choice.category == "Position Shape";
	});
	EXPECT_TRUE(offered);

	for (std::string_view own : {GRAPH_EFFECT_SAMPLE_MESH, GRAPH_EFFECT_SAMPLE_MESH_INDEX, GRAPH_EFFECT_MESH_VERTEX_COUNT, GRAPH_EFFECT_MESH_INDEX_COUNT,
								 GRAPH_EFFECT_MESH_TRIANGLE_COUNT}) {
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(own);
		ASSERT_NE(definition, nullptr) << own;
		EXPECT_TRUE(EffectGraphEditOps::IsOperator(*definition)) << own;
		EXPECT_EQ(definition->category, "Sampling") << own;
	}

	const str wired = OneSystem(R"({"id":20,"def":"Sampling/MeshVertexCount","in":{"Mesh":{"from":[21,"Out"]}}},{"id":21,"def":"Input/Float"})",
								R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"Count"]}}})");
	const GraphDocument floatInto = ReadGraph(OneSystem(R"({"id":20,"def":"Sampling/MeshVertexCount"},{"id":21,"def":"Input/Float"})", ""));
	str why;
	EXPECT_FALSE(NodeShaderEditOps::CanConnect(floatInto, 21, "Out", 20, "Mesh", &why));
	EXPECT_NE(why.find("mesh"), str::npos) << why;
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphEffectTarget::Compile(ReadGraph(wired), "wired.sleffect", effect, diagnostics));
}

TEST(GraphEffectLibraryTest, SequentialPortsFollowSettings) {
	GraphDefinitions definitions;
	const auto shape = [&](std::string_view settings) {
		const GraphDocument document =
			ReadGraph(OneSystem("", std::format(R"({{"id":10,"def":"Block/PositionSequential","settings":{}}})", settings.empty() ? "{}" : settings)));
		str error;
		const GraphNodeDefinition* definition = definitions.Find(document, document.contexts[0].blocks[0], &error);
		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		if (!definition) {
			names.push_back("error: " + error);
			return names;
		}
		names.push_back(str(definition->name));
		for (const GraphPortDefinition& port : definition->inputs) {
			names.push_back(str(port.name));
		}
		names.push_back("->");
		for (const GraphPortDefinition& port : definition->outputs) {
			names.push_back(str(port.name));
		}
		return names;
	};
	const auto list = [](std::initializer_list<std::string_view> names) {
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (std::string_view name : names) {
			out.push_back(str(name));
		}
		return out;
	};
	EXPECT_EQ(shape(""), list({"Add Position Sequential Line", "Offset Index", "Count", "Start", "End", "->", "position", "direction"}));
	EXPECT_EQ(shape(R"({"Shape":"Circle","Index":"Custom","Composition Position":"Blend","Write Target Position":true,"Composition Target Position":"Blend"})"),
			  list({"Blend Position Sequential Circle", "Index", "Offset Index", "Blend Position", "Offset Target Index", "Blend Target Position", "Count", "Center",
					"Normal", "Up", "Radius", "->", "position", "direction", "targetPosition"}));
	EXPECT_EQ(shape(R"({"Shape":"ThreeDimensional","Write Position":false,"Write Target Position":true})"),
			  list({"Add Position Sequential ThreeDimensional", "Offset Target Index", "Count X", "Count Y", "Count Z", "Origin", "Axis X", "Axis Y", "Axis Z", "->",
					"targetPosition"}));
	const Vector<str> refused = shape(R"({"Shape":"Spiral"})");
	ASSERT_EQ(refused.size(), 1u);
	EXPECT_NE(refused[0].find("isn't one of its options"), str::npos) << refused[0];

	for (std::string_view context : {GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_OUTPUT}) {
		EXPECT_TRUE(GraphEffectRules::Holds(context, GRAPH_EFFECT_POSITION_SEQUENTIAL)) << context;
	}
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_POSITION_SEQUENTIAL));
	const GraphDocument document = ReadGraph(OneSystem("", ""));
	Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	EffectGraphEditOps::BlockChoices(document, 2, choices);
	Vector<str> offered{GetMemoryResource(MemTag::Editor)};
	for (const EffectGraphEditOps::BlockChoice& choice : choices) {
		if (choice.def == GRAPH_EFFECT_POSITION_SEQUENTIAL) {
			offered.push_back(choice.name);
			GraphNode made;
			made.def = choice.def;
			made.settings.assign(choice.settings.begin(), choice.settings.end());
			const GraphNodeDefinition* built = definitions.Find(document, made);
			ASSERT_NE(built, nullptr) << choice.name;
			EXPECT_EQ(built->name, choice.name);
		}
	}
	EXPECT_EQ(offered, list({"Set Position Sequential Circle", "Set Position Sequential Line", "Set Position Sequential ThreeDimensional"}));
}

#ifndef SHALLTEAR_SHARED

using namespace Shalltear::EffectKernelHarness;

namespace {

	struct Float3 {
		f32 x = 0.0f;
		f32 y = 0.0f;
		f32 z = 0.0f;
	};

	class GraphEffectLibraryFixture : public GraphEffectBlockFixture {
	protected:
		static f32 One(const BlockRun& run, std::string_view attribute, u32 component = 0) {
			const Vector<f32> values = Values(run, attribute, component);
			EXPECT_EQ(values.size(), 1u) << attribute;
			return values.empty() ? NAN : values[0];
		}

		static Vector<Float3> VectorsOf(const BlockRun& run, std::string_view attribute) {
			const Vector<f32> x = Values(run, attribute, 0);
			const Vector<f32> y = Values(run, attribute, 1);
			const Vector<f32> z = Values(run, attribute, 2);
			Vector<Float3> out{GetMemoryResource(MemTag::Graphics)};
			for (size_t i = 0; i < x.size(); ++i) {
				out.push_back({x[i], y[i], z[i]});
			}
			return out;
		}
	};

	constexpr std::string_view NOISE_TYPES[] = {"Value", "Perlin", "Cellular"};
	constexpr std::string_view NOISE_DIMENSIONS[] = {"One", "Two", "Three"};

	str JsonVector(std::span<const f32> components) {
		str out = "[";
		for (size_t i = 0; i < components.size(); ++i) {
			out += std::format("{}{}", i == 0 ? "" : ",", components[i]);
		}
		return out + "]";
	}

} // anonymous namespace

TEST_F(GraphEffectLibraryFixture, LatticeHashIsIEEEOnDevice) {
	const str nodes = R"({"id":60,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
		{"id":61,"def":"Input/Vector2","in":{"X":{"from":[60,"spawnIndex"]},"Y":{"value":[7]}}},
		{"id":62,"def":"Noise/Noise","settings":{"Type":"Value","Dimensions":"Two"},"in":{"Coordinate":{"from":[61,"Out"]},"Range":{"value":[0,1]}}},
		{"id":63,"def":"Noise/Noise","settings":{"Type":"Value","Dimensions":"One"},"in":{"Coordinate":{"from":[60,"spawnIndex"]},"Range":{"value":[0,1]}}},
		{"id":64,"def":"Input/Vector3","in":{"X":{"from":[60,"spawnIndex"]}}})";
	const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[62,"Noise"]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[63,"Noise"]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[64,"Out"]}}})";
	BlockRun run;
	if (!Begin(run, OneSystem(nodes, blocks), "LatticeHash")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, 0.1f, {64});
	const auto fraction = [](f32 x) { return x - std::floor(x); };
	const auto wrap = [](f32 cell) { return cell - std::floor(cell * static_cast<f32>(1.0 / 71.0)) * 71.0f; };
	const auto hash = [&](f32 x, f32 y) {
		const f32 px = (wrap(x) + 26.0f) * (wrap(x) + 26.0f);
		const f32 py = (wrap(y) + 161.0f) * (wrap(y) + 161.0f);
		return fraction(px * py * static_cast<f32>(1.0 / 951.135664));
	};
	const Vector<f32> cells = Values(run, "color");
	const Vector<f32> planar = Values(run, "size");
	const Vector<f32> linear = Values(run, "alpha");
	ASSERT_EQ(cells.size(), 64u);
	u32 moved = 0;
	for (size_t i = 0; i < cells.size(); ++i) {
		moved += std::bit_cast<u32>(planar[i]) == std::bit_cast<u32>(hash(cells[i], 7.0f)) ? 0u : 1u;
		moved += std::bit_cast<u32>(linear[i]) == std::bit_cast<u32>(hash(cells[i], 0.0f)) ? 0u : 1u;
	}
	EXPECT_EQ(moved, 0u);
	EXPECT_GT(Distinct(planar), 32u);
}

TEST_F(GraphEffectLibraryFixture, ValueNoiseIsIEEEOnDevice) {
	const str nodes = R"({"id":60,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
		{"id":61,"def":"Math/Subtract","in":{"A":{"from":[60,"spawnIndex"]},"B":{"value":[31.5]}}},
		{"id":62,"def":"Input/Vector2","in":{"X":{"from":[61,"Out"]},"Y":{"value":[-6.5]}}},
		{"id":63,"def":"Noise/Noise","settings":{"Type":"Value","Dimensions":"Two"},"in":{"Coordinate":{"from":[62,"Out"]},"Range":{"value":[0,1]}}},
		{"id":64,"def":"Noise/Noise","settings":{"Type":"Value","Dimensions":"One"},"in":{"Coordinate":{"from":[61,"Out"]},"Range":{"value":[0,1]}}},
		{"id":65,"def":"Input/Vector3","in":{"X":{"from":[61,"Out"]}}})";
	const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[63,"Noise"]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[63,"Derivatives"]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[64,"Noise"]}}},
		{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"pivot"},"in":{"Pivot":{"from":[64,"Derivatives"]}}},
		{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[65,"Out"]}}})";
	BlockRun run;
	if (!Begin(run, OneSystem(nodes, blocks), "MidpointHash")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, 0.1f, {64});
	const auto fraction = [](f32 x) { return x - std::floor(x); };
	const auto wrap = [](f32 cell) { return cell - std::floor(cell * static_cast<f32>(1.0 / 71.0)) * 71.0f; };
	const auto hash = [&](f32 x, f32 y) {
		const f32 px = (wrap(x) + 26.0f) * (wrap(x) + 26.0f);
		const f32 py = (wrap(y) + 161.0f) * (wrap(y) + 161.0f);
		return fraction(px * py * static_cast<f32>(1.0 / 951.135664));
	};
	const auto half = [](f32 a, f32 b) { return a + (b - a) * 0.5f; };
	const Vector<f32> coordinates = Values(run, "color");
	ASSERT_EQ(coordinates.size(), 64u);
	struct Read {
		std::string_view attribute;
		u32 component;
	};
	constexpr Read READS[] = {{"alpha", 0}, {"pivot", 0}, {"size", 0}, {"scale", 0}, {"scale", 1}};
	Vector<Vector<f32>> drawn{GetMemoryResource(MemTag::Graphics)};
	for (const Read& read : READS) {
		drawn.push_back(Values(run, read.attribute, read.component));
		ASSERT_EQ(drawn.back().size(), coordinates.size()) << read.attribute;
	}
	u32 moved = 0;
	str first;
	for (size_t i = 0; i < coordinates.size(); ++i) {
		const f32 x = std::floor(coordinates[i]);
		const f32 y = -7.0f;
		const f32 line[2] = {hash(x, 0.0f), hash(x + 1.0f, 0.0f)};
		const f32 corner[4] = {hash(x, y), hash(x + 1.0f, y), hash(x, y + 1.0f), hash(x + 1.0f, y + 1.0f)};
		const f32 alongY[2] = {half(corner[0], corner[2]), half(corner[1], corner[3])};
		const f32 alongX[2] = {half(corner[0], corner[1]), half(corner[2], corner[3])};
		const f32 want[] = {half(line[0], line[1]), (line[1] - line[0]) * 1.875f, half(alongY[0], alongY[1]), (alongY[1] - alongY[0]) * 1.875f,
							(alongX[1] - alongX[0]) * 1.875f};
		for (size_t r = 0; r < std::size(READS); ++r) {
			if (std::bit_cast<u32>(drawn[r][i]) != std::bit_cast<u32>(want[r])) {
				if (moved < 8) {
					first += std::format("  at {}, {}[{}]: drew {} where IEEE gives {}\n", coordinates[i], READS[r].attribute, READS[r].component, drawn[r][i], want[r]);
				}
				++moved;
			}
		}
	}
	EXPECT_EQ(moved, 0u);
	EXPECT_EQ(*std::ranges::min_element(coordinates), -31.5f);
	EXPECT_GT(Distinct(drawn[3]), 32u);
}

TEST_F(GraphEffectLibraryFixture, ANoisesDerivativesAreItsValuesSlope) {
	constexpr f32 STEP = 1.0e-3f;
	constexpr f32 BASE[3] = {0.37f, 1.61f, 2.29f};
	for (u32 type = 0; type < 3; ++type) {
		for (u32 width = 1; width <= 3; ++width) {
			SCOPED_TRACE(std::format("{} Noise {}D", NOISE_TYPES[type], width));
			str nodes;
			const std::string_view range = type == 1 ? "[-1,1]" : "[0,1]";
			const auto noise = [&](u32 id, i32 axis, f32 sign) {
				f32 at[3] = {BASE[0], BASE[1], BASE[2]};
				if (axis >= 0) {
					at[axis] += sign * STEP;
				}
				nodes += std::format(R"({}{{"id":{},"def":"Noise/Noise","settings":{{"Type":"{}","Dimensions":"{}"}},"in":{{"Coordinate":{{"value":{}}},"Range":{{"value":{}}}}}}})",
									 nodes.empty() ? "" : ",", id, NOISE_TYPES[type], NOISE_DIMENSIONS[width - 1], JsonVector({at, width}), range);
			};
			noise(20, -1, 0.0f);
			for (u32 axis = 0; axis < width; ++axis) {
				noise(21 + axis, static_cast<i32>(axis), 1.0f);
				noise(24 + axis, static_cast<i32>(axis), -1.0f);
			}
			for (const u32 first : {21u, 24u}) {
				str in;
				for (u32 axis = 0; axis < width; ++axis) {
					in += std::format(R"({}"{}":{{"from":[{},"Noise"]}})", axis == 0 ? "" : ",", "XYZ"[axis], first + axis);
				}
				nodes += std::format(R"(,{{"id":{},"def":"Input/Vector3","in":{{{}}}}})", first == 21 ? 30 : 31, in);
			}
			const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"Noise"]}}},
				{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[30,"Out"]}}},
				{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[31,"Out"]}}},
				{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[20,"Derivatives"]}}})";
			const str graph = OneSystem(nodes, blocks);
			BlockRun run;
			if (!Begin(run, graph, std::format("NoiseSlope{}{}", type, width))) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
			Advance(run, 0.0f, 0.1f, {1});
			const f32 scale = type == 1 ? 2.0f : 1.0f;
			for (u32 axis = 0; axis < width; ++axis) {
				const f32 ahead = One(run, "position", axis);
				const f32 behind = One(run, "scale", axis);
				const f32 slope = (ahead - behind) / (2.0f * STEP);
				EXPECT_NEAR(One(run, "color", axis) / scale, slope, 5.0e-3f) << "axis " << axis;
			}
			EXPECT_TRUE(std::isfinite(One(run, "size")));
		}
	}
}

TEST_F(GraphEffectLibraryFixture, TheOctavesAreAFractalSum) {
	for (u32 type = 0; type < 3; ++type) {
		SCOPED_TRACE(NOISE_TYPES[type]);
		const auto noise = [&](u32 id, f32 frequency, f32 octaves) {
			return std::format(R"({{"id":{},"def":"Noise/Noise","settings":{{"Type":"{}","Dimensions":"Three"}},"in":{{"Coordinate":{{"value":[0.41,1.73,2.9]}},"Frequency":{{"value":[{}]}},"Octaves":{{"value":[{}]}},"Roughness":{{"value":[0.4]}},"Lacunarity":{{"value":[2.1]}}}}}})",
							   id, NOISE_TYPES[type], frequency, octaves);
		};
		const str nodes = noise(20, 0.7f, 1.0f) + "," + noise(21, 0.7f * 2.1f, 1.0f) + "," + noise(22, 0.7f, 2.0f);
		const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"Noise"]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[21,"Noise"]}}},
			{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[22,"Noise"]}}})";
		BlockRun run;
		if (!Begin(run, OneSystem(nodes, blocks), std::format("NoiseOctaves{}", type))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {1});
		const f32 first = One(run, "size");
		const f32 second = One(run, "alpha");
		EXPECT_NEAR(One(run, "color"), (first + 0.4f * second) / 1.4f, 1.0e-5f);
		EXPECT_NE(first, second);
	}
}

TEST_F(GraphEffectLibraryFixture, CurlIs2DimensionalNoisesDerivativesCrossed) {
	constexpr f32 AT[3] = {0.53f, 1.27f, 2.11f};
	for (u32 type = 0; type < 3; ++type) {
		SCOPED_TRACE(NOISE_TYPES[type]);
		const auto plane = [&](u32 id, f32 x, f32 y) {
			return std::format(R"({{"id":{},"def":"Noise/Noise","settings":{{"Type":"{}","Dimensions":"Two"}},"in":{{"Coordinate":{{"value":[{},{}]}},"Range":{{"value":[0,1]}}}}}})",
							   id, NOISE_TYPES[type], x, y);
		};
		{
			const str nodes = std::format(R"({{"id":20,"def":"Noise/CurlNoise","settings":{{"Type":"{}","Dimensions":"Three"}},"in":{{"Coordinate":{{"value":[{},{},{}]}}}}}},)",
										  NOISE_TYPES[type], AT[0], AT[1], AT[2]) +
							  plane(21, AT[2], AT[1]) + "," + plane(22, AT[0] + 100.0f, AT[2] + 100.0f) + "," + plane(23, AT[1] + 200.0f, AT[0] + 200.0f) +
							  R"(,{"id":24,"def":"Channel/Split","in":{"In":{"from":[23,"Derivatives"]}}})";
			const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[20,"Noise"]}}},
				{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[21,"Derivatives"]}}},
				{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[22,"Derivatives"]}}},
				{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[23,"Derivatives"]}}},
				{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[24,"G"]}}})";
			BlockRun run;
			if (!Begin(run, OneSystem(nodes, blocks), std::format("Curl3D{}", type))) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
			Advance(run, 0.0f, 0.1f, {1});
			const f32 a[2] = {One(run, "color", 0), One(run, "color", 1)};
			const f32 b[2] = {One(run, "scale", 0), One(run, "scale", 1)};
			const f32 d[2] = {One(run, "size"), One(run, "alpha")};
			EXPECT_NEAR(One(run, "position", 0), d[0] - b[1], 1.0e-4f);
			EXPECT_NEAR(One(run, "position", 1), a[0] - d[1], 1.0e-4f);
			EXPECT_NEAR(One(run, "position", 2), b[0] - a[1], 1.0e-4f);
			EXPECT_GT(std::abs(One(run, "position", 0)) + std::abs(One(run, "position", 1)) + std::abs(One(run, "position", 2)), 0.0f);
		}
		{
			const str nodes = std::format(R"({{"id":20,"def":"Noise/CurlNoise","settings":{{"Type":"{}","Dimensions":"Two"}},"in":{{"Coordinate":{{"value":[{},{}]}}}}}},)",
										  NOISE_TYPES[type], AT[0], AT[1]) +
							  plane(21, AT[0], AT[1]);
			const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[20,"Noise"]}}},
				{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[21,"Derivatives"]}}})";
			BlockRun run;
			ASSERT_TRUE(Begin(run, OneSystem(nodes, blocks), std::format("Curl2D{}", type)));
			Advance(run, 0.0f, 0.1f, {1});
			EXPECT_NEAR(One(run, "position", 0), One(run, "color", 1), 1.0e-4f);
			EXPECT_NEAR(One(run, "position", 1), -One(run, "color", 0), 1.0e-4f);
		}
	}
}

TEST_F(GraphEffectLibraryFixture, ConvertedInputReadsBackThroughEffectsTransform) {
	constexpr f32 TURNED[12] = {0.0f, 0.0f, 2.0f, 10.0f, 0.0f, 2.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f};
	{
		SCOPED_TRACE("World system, Local inputs");
		const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3]}}},
			{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,0,2]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[20,"velocity"]}}})";
		const str nodes = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"velocity"}})";
		BlockRun run;
		if (!Begin(run, OneSystemIn("World", nodes, blocks), "SpaceToWorld")) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {1}, 0, 0, TURNED);
		EXPECT_NEAR(One(run, "position", 0), 16.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "position", 1), 4.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "position", 2), -2.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "scale", 0), 4.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "scale", 1), 0.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "scale", 2), 0.0f, 1.0e-4f);
	}
	{
		SCOPED_TRACE("Local system, a World input");
		const str blocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[16,4,-2],"space":"World"}}})";
		BlockRun run;
		ASSERT_TRUE(Begin(run, OneSystemIn("Local", "", blocks), "SpaceToLocal"));
		Advance(run, 0.0f, 0.1f, {1}, 0, 0, TURNED);
		EXPECT_NEAR(One(run, "position", 0), 1.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "position", 1), 2.0f, 1.0e-4f);
		EXPECT_NEAR(One(run, "position", 2), 3.0f, 1.0e-4f);
	}
}

namespace {

	f32 Length(Float3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

	str TwoContexts(std::string_view nodes, std::string_view initialize, std::string_view update) {
		return EffectWith(nodes, initialize, update);
	}

} // anonymous namespace

TEST_F(GraphEffectLibraryFixture, EachShapePlacesParticlesWhereFormulaSays) {
	const auto place = [&](std::string_view name, std::string_view settings, std::string_view in) {
		BlockRun run;
		const str block = std::format(R"({{"id":10,"def":"Block/PositionShape","settings":{},"in":{}}})", settings, in);
		if (!Begin(run, EffectWith("", block, ""), name)) {
			return Vector<Float3>{GetMemoryResource(MemTag::Graphics)};
		}
		Advance(run, 0.0f, 0.1f, {32}, 5);
		return VectorsOf(run, "position");
	};
	const Float3 center{1.0f, 2.0f, 3.0f};
	const auto from = [&](Float3 p) { return Float3{p.x - center.x, p.y - center.y, p.z - center.z}; };
	{
		SCOPED_TRACE("Sphere, surface");
		BlockRun run;
		ASSERT_TRUE(Begin(run, EffectWith(R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"direction"}})",
										  R"({"id":10,"def":"Block/PositionShape","settings":{"Shape":"Sphere"},"in":{"Position":{"value":[1,2,3]},"Radius":{"value":[2]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[20,"direction"]}}})",
										  ""),
						  "ShapeSphere"));
		Advance(run, 0.0f, 0.1f, {32}, 5);
		const Vector<Float3> positions = VectorsOf(run, "position");
		const Vector<Float3> directions = VectorsOf(run, "color");
		ASSERT_EQ(positions.size(), 32u);
		ASSERT_EQ(directions.size(), 32u);
		for (size_t i = 0; i < positions.size(); ++i) {
			const Float3 d = from(positions[i]);
			EXPECT_NEAR(Length(d), 2.0f, 1.0e-4f);
			EXPECT_NEAR(directions[i].x, d.x / 2.0f, 1.0e-4f);
			EXPECT_NEAR(directions[i].z, d.z / 2.0f, 1.0e-4f);
		}
	}
	{
		SCOPED_TRACE("Sphere, volume, 1/4 arc");
		const Vector<Float3> positions =
			place("ShapeSphereVolume", R"({"Shape":"Sphere","Position Mode":"Volume"})", R"({"Position":{"value":[1,2,3]},"Radius":{"value":[2]},"Arc":{"value":[90]}})");
		ASSERT_EQ(positions.size(), 32u);
		u32 inner = 0;
		for (const Float3 p : positions) {
			const Float3 d = from(p);
			EXPECT_LE(Length(d), 2.0f + 1.0e-4f);
			EXPECT_GE(d.x, -1.0e-4f);
			EXPECT_GE(d.y, -1.0e-4f);
			inner += Length(d) < 1.9f ? 1u : 0u;
		}
		EXPECT_GT(inner, 8u);
	}
	{
		SCOPED_TRACE("Circle, turned upright");
		const Vector<Float3> positions =
			place("ShapeCircle", R"({"Shape":"Circle"})", R"({"Position":{"value":[1,2,3]},"Angles":{"value":[90,0,0]},"Radius":{"value":[2]}})");
		ASSERT_EQ(positions.size(), 32u);
		for (const Float3 p : positions) {
			const Float3 d = from(p);
			EXPECT_NEAR(d.y, 0.0f, 1.0e-4f);
			EXPECT_NEAR(Length(d), 2.0f, 1.0e-4f);
		}
	}
	{
		SCOPED_TRACE("Box, volume and surface");
		const Vector<Float3> volume = place("ShapeBoxVolume", R"({"Shape":"OrientedBox","Position Mode":"Volume"})",
											R"({"Center":{"value":[1,2,3]},"Size":{"value":[2,4,6]}})");
		const Vector<Float3> surface = place("ShapeBoxSurface", R"({"Shape":"OrientedBox"})", R"({"Center":{"value":[1,2,3]},"Size":{"value":[2,4,6]}})");
		ASSERT_EQ(volume.size(), 32u);
		ASSERT_EQ(surface.size(), 32u);
		for (const Float3 p : volume) {
			const Float3 d = from(p);
			EXPECT_TRUE(std::abs(d.x) <= 1.0f + 1.0e-4f && std::abs(d.y) <= 2.0f + 1.0e-4f && std::abs(d.z) <= 3.0f + 1.0e-4f);
		}
		for (const Float3 p : surface) {
			const Float3 d = from(p);
			const bool onFace = std::abs(std::abs(d.x) - 1.0f) < 1.0e-4f || std::abs(std::abs(d.y) - 2.0f) < 1.0e-4f || std::abs(std::abs(d.z) - 3.0f) < 1.0e-4f;
			EXPECT_TRUE(onFace) << d.x << " " << d.y << " " << d.z;
		}
	}
	{
		SCOPED_TRACE("Cone, base and volume");
		const Vector<Float3> base = place("ShapeConeBase", R"({"Shape":"Cone","Height Mode":"Base","Position Mode":"Volume"})",
										  R"({"Position":{"value":[1,2,3]},"Base Radius":{"value":[2]},"Top Radius":{"value":[1]},"Height":{"value":[3]}})");
		const Vector<Float3> volume = place("ShapeConeVolume", R"({"Shape":"Cone","Position Mode":"Volume"})",
											R"({"Position":{"value":[1,2,3]},"Base Radius":{"value":[2]},"Top Radius":{"value":[1]},"Height":{"value":[3]}})");
		ASSERT_EQ(base.size(), 32u);
		ASSERT_EQ(volume.size(), 32u);
		for (const Float3 p : base) {
			const Float3 d = from(p);
			EXPECT_NEAR(d.y, 0.0f, 1.0e-4f);
			EXPECT_LE(std::sqrt(d.x * d.x + d.z * d.z), 2.0f + 1.0e-4f);
		}
		u32 high = 0;
		for (const Float3 p : volume) {
			const Float3 d = from(p);
			EXPECT_TRUE(d.y >= -1.0e-4f && d.y <= 3.0f + 1.0e-4f);
			EXPECT_LE(std::sqrt(d.x * d.x + d.z * d.z), 2.0f - d.y / 3.0f + 1.0e-4f);
			high += d.y > 1.5f ? 1u : 0u;
		}
		EXPECT_GT(high, 4u);
	}
	{
		SCOPED_TRACE("Torus, surface");
		const Vector<Float3> positions =
			place("ShapeTorus", R"({"Shape":"Torus"})", R"({"Position":{"value":[1,2,3]},"Major Radius":{"value":[2]},"Minor Radius":{"value":[0.5]}})");
		ASSERT_EQ(positions.size(), 32u);
		for (const Float3 p : positions) {
			const Float3 d = from(p);
			const f32 ring = std::sqrt(d.x * d.x + d.y * d.y) - 2.0f;
			EXPECT_NEAR(std::sqrt(ring * ring + d.z * d.z), 0.5f, 1.0e-3f);
		}
	}
	{
		SCOPED_TRACE("Line, random and custom");
		const Vector<Float3> random = place("ShapeLine", R"({"Shape":"Line"})", R"({"Start":{"value":[1,2,3]},"End":{"value":[5,2,3]}})");
		ASSERT_EQ(random.size(), 32u);
		for (const Float3 p : random) {
			EXPECT_TRUE(p.x >= 1.0f - 1.0e-4f && p.x <= 5.0f + 1.0e-4f && std::abs(p.y - 2.0f) < 1.0e-5f && std::abs(p.z - 3.0f) < 1.0e-5f);
		}
		const Vector<Float3> custom = place("ShapeLineCustom", R"({"Shape":"Line","Spawn Mode":"Custom"})",
											R"({"Start":{"value":[1,2,3]},"End":{"value":[5,2,3]},"Line Sequencer":{"value":[0.25]}})");
		ASSERT_EQ(custom.size(), 32u);
		for (const Float3 p : custom) {
			EXPECT_NEAR(p.x, 2.0f, 1.0e-5f);
		}
	}
}

TEST_F(GraphEffectLibraryFixture, OtherSpaceShapeMovesWhole) {
	constexpr f32 MOVED[12] = {2.0f, 0.0f, 0.0f, 10.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f};
	BlockRun run;
	const str blocks = R"({"id":10,"def":"Block/PositionShape","settings":{"Shape":"Sphere"},"in":{"Radius":{"value":[1]}}})";
	if (!Begin(run, OneSystemIn("World", "", blocks), "ShapeToWorld")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, 0.1f, {16}, 3, 0, MOVED);
	const Vector<Float3> positions = VectorsOf(run, "position");
	ASSERT_EQ(positions.size(), 16u);
	for (const Float3 p : positions) {
		EXPECT_NEAR(Length(Float3{p.x - 10.0f, p.y, p.z}), 2.0f, 1.0e-4f);
	}

	BlockRun added;
	const str two = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,100,0]}}},
		{"id":11,"def":"Block/PositionShape","settings":{"Shape":"Line","Composition Position":"Add","Spawn Mode":"Custom"},"in":{"End":{"value":[4,0,0]},"Line Sequencer":{"value":[0.5]}}})";
	ASSERT_TRUE(Begin(added, EffectWith("", two, ""), "ShapeAdd"));
	Advance(added, 0.0f, 0.1f, {4}, 3);
	for (const Float3 p : VectorsOf(added, "position")) {
		EXPECT_NEAR(p.x, 2.0f, 1.0e-5f);
		EXPECT_NEAR(p.y, 100.0f, 1.0e-5f);
	}
}

TEST_F(GraphEffectLibraryFixture, EachForceChangesVelocityAsFormulaSays) {
	constexpr f32 DELTA = 0.1f;
	constexpr f32 MASS = 2.0f;
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[1,2,3]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"mass"},"in":{"Mass":{"value":[2]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[0.5]}}},
		{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0.3,0.7,1.1]}}})";
	const auto after = [&](std::string_view name, std::string_view nodes, std::string_view update) {
		BlockRun run;
		if (!Begin(run, EffectWith(nodes, initialize, update), name)) {
			return Vector<Float3>{GetMemoryResource(MemTag::Graphics)};
		}
		Advance(run, 0.0f, DELTA, {2});
		Advance(run, DELTA, DELTA, {});
		return VectorsOf(run, "velocity");
	};
	const auto expect = [](const Vector<Float3>& velocities, Float3 expected, const char* what) {
		ASSERT_EQ(velocities.size(), 2u) << what;
		for (const Float3 v : velocities) {
			EXPECT_NEAR(v.x, expected.x, 1.0e-4f) << what;
			EXPECT_NEAR(v.y, expected.y, 1.0e-4f) << what;
			EXPECT_NEAR(v.z, expected.z, 1.0e-4f) << what;
		}
	};
	expect(after("ForceGravity", "", R"({"id":20,"def":"Block/Gravity"})"), {1.0f, 2.0f - 0.981f, 3.0f}, "gravity");
	expect(after("ForceAbsolute", "", R"({"id":20,"def":"Block/Force","in":{"Force":{"value":[4,0,-6]}}})"), {1.0f + 0.2f, 2.0f, 3.0f - 0.3f}, "an absolute force");
	expect(after("ForceRelative", "", R"({"id":20,"def":"Block/Force","settings":{"Mode":"Relative"},"in":{"Velocity":{"value":[5,2,-1]},"Drag":{"value":[5]}}})"),
		   {1.0f + 1.0f, 2.0f, 3.0f - 1.0f}, "a relative force");
	const f32 decay = std::exp(-(0.5f * DELTA) / MASS);
	expect(after("ForceDrag", "", R"({"id":20,"def":"Block/LinearDrag"})"), {1.0f * decay, 2.0f * decay, 3.0f * decay}, "linear drag");
	const f32 sized = std::exp(-(0.5f * 0.25f * DELTA) / MASS);
	expect(after("ForceDragSized", "", R"({"id":20,"def":"Block/LinearDrag","settings":{"Use Particle Size":true}})"), {1.0f * sized, 2.0f * sized, 3.0f * sized},
		   "linear drag by the particle's size");
	{
		const Float3 v{1.0f, 2.0f, 3.0f};
		const f32 normalSpeed = v.z;
		const f32 target = 1.0f * 5.0f * 1.0f;
		const f32 change = target - normalSpeed;
		const f32 step = std::copysign(std::min(std::abs(change), DELTA * 20.0f), change);
		expect(after("ForceAttractor", "", R"({"id":20,"def":"Block/ConformToSphere","in":{"Center":{"value":[0.3,0.7,5.1]}}})"),
			   {v.x, v.y, v.z + step / MASS}, "an attractor");
	}
	{
		SCOPED_TRACE("Turbulence");
		BlockRun run;
		const str nodes = R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"position"}},
			{"id":31,"def":"Math/Add","in":{"A":{"from":[30,"position"]},"B":{"value":[0.5,0.5,0.5]}}},
			{"id":32,"def":"Noise/CurlNoise","settings":{"Type":"Value","Dimensions":"Three"},"in":{"Coordinate":{"from":[31,"Out"]}}})";
		const str update = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[32,"Noise"]}}},
			{"id":21,"def":"Block/Turbulence","settings":{"Mode":"Absolute"},"in":{"Intensity":{"value":[3]}}})";
		ASSERT_TRUE(Begin(run, EffectWith(nodes, initialize, update), "ForceTurbulence"));
		Advance(run, 0.0f, DELTA, {2});
		Advance(run, DELTA, DELTA, {});
		const Vector<Float3> curls = VectorsOf(run, "color");
		const Vector<Float3> velocities = VectorsOf(run, "velocity");
		ASSERT_EQ(velocities.size(), 2u);
		for (size_t i = 0; i < velocities.size(); ++i) {
			EXPECT_NEAR(velocities[i].x, 1.0f + curls[i].x * 3.0f / MASS * DELTA, 1.0e-4f);
			EXPECT_NEAR(velocities[i].y, 2.0f + curls[i].y * 3.0f / MASS * DELTA, 1.0e-4f);
			EXPECT_NEAR(velocities[i].z, 3.0f + curls[i].z * 3.0f / MASS * DELTA, 1.0e-4f);
		}
		EXPECT_GT(std::abs(curls[0].x) + std::abs(curls[0].y) + std::abs(curls[0].z), 0.0f);
	}
}

TEST_F(GraphEffectLibraryFixture, EachKillShapeKillsWhatDetectionHits) {
	struct Case {
		const char* name;
		std::string_view shape;
		std::string_view settings;
		std::string_view in;
		Float3 position;
		Float3 velocity;
		bool dies;
	};
	const Case cases[] = {
		{"PlaneBelow", "Plane", "", R"("Position":{"value":[0,1,0]})", {0.0f, 0.5f, 0.0f}, {}, true},
		{"PlaneAbove", "Plane", "", R"("Position":{"value":[0,1,0]})", {0.0f, 2.0f, 0.0f}, {}, false},
		{"PlaneCrossed", "Plane", "", R"("Position":{"value":[0,1,0]})", {0.0f, 2.0f, 0.0f}, {0.0f, -20.0f, 0.0f}, true},
		{"PlaneInverted", "Plane", R"(,"Mode":"Inverted")", R"("Position":{"value":[0,1,0]})", {0.0f, 2.0f, 0.0f}, {}, true},
		{"SphereInside", "Sphere", "", R"("Position":{"value":[1,2,3]})", {1.2f, 2.0f, 3.0f}, {}, true},
		{"SphereOutside", "Sphere", "", R"("Position":{"value":[1,2,3]})", {3.0f, 2.0f, 3.0f}, {}, false},
		{"SphereCrossed", "Sphere", "", R"("Position":{"value":[1,2,3]})", {-2.0f, 2.0f, 3.0f}, {60.0f, 0.0f, 0.0f}, true},
		{"SphereInverted", "Sphere", R"(,"Mode":"Inverted")", R"("Position":{"value":[1,2,3]})", {3.0f, 2.0f, 3.0f}, {}, true},
		{"SphereBySize", "Sphere", R"(,"Radius Mode":"FromSize")", R"("Position":{"value":[1,2,3]})", {2.2f, 2.0f, 3.0f}, {}, true},
		{"BoxInside", "OrientedBox", "", R"("Center":{"value":[1,2,3]},"Size":{"value":[2,2,2]})", {1.5f, 2.5f, 3.5f}, {}, true},
		{"BoxOutside", "OrientedBox", "", R"("Center":{"value":[1,2,3]},"Size":{"value":[2,2,2]})", {3.0f, 2.0f, 3.0f}, {}, false},
		{"BoxInverted", "OrientedBox", R"(,"Mode":"Inverted")", R"("Center":{"value":[1,2,3]},"Size":{"value":[2,2,2]})", {1.5f, 2.5f, 3.5f}, {}, false},
		{"ConeInside", "Cone", "", R"("Position":{"value":[1,2,3]})", {1.1f, 2.3f, 3.0f}, {}, true},
		{"ConeAboveTop", "Cone", "", R"("Position":{"value":[1,2,3]})", {1.0f, 3.5f, 3.0f}, {}, false},
		{"ConeInverted", "Cone", R"(,"Mode":"Inverted")", R"("Position":{"value":[1,2,3]})", {1.0f, 3.5f, 3.0f}, {}, true},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		const str initialize = std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{},{},{}]}}}}}},
			{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":[{},{},{}]}}}}}},
			{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"value":[2.5]}}}}}})",
										   entry.position.x, entry.position.y, entry.position.z, entry.velocity.x, entry.velocity.y, entry.velocity.z);
		const str update = std::format(R"({{"id":20,"def":"Block/CollisionShape","settings":{{"Behavior":"Kill","Shape":"{}"{}}},"in":{{{}}}}})", entry.shape,
									   entry.settings, entry.in);
		BlockRun run;
		if (!Begin(run, EffectWith("", initialize, update), std::format("Kill{}", entry.name))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {8});
		Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
		ASSERT_EQ(Alive(run.system, indices), 8u);
		Advance(run, 0.1f, 0.1f, {});
		EXPECT_EQ(Alive(run.system, indices), entry.dies ? 0u : 8u) << (entry.dies ? "a particle the shape hits lived" : "a particle clear of the shape died");
	}

	BlockRun spawned;
	const str inside = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0.5,0]}}},
		{"id":11,"def":"Block/CollisionShape","settings":{"Behavior":"Kill","Shape":"Plane"},"in":{"Position":{"value":[0,1,0]}}})";
	ASSERT_TRUE(Begin(spawned, EffectWith("", inside, ""), "KillOnSpawn"));
	Advance(spawned, 0.0f, 0.1f, {8});
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
	EXPECT_EQ(Alive(spawned.system, indices), 0u);
}

TEST_F(GraphEffectLibraryFixture, EachCollisionShapeRespondsAsFormulaSays) {
	struct Case {
		const char* name;
		Float3 position;
		Float3 velocity;
		std::string_view block;
		Float3 expectedPosition;
		Float3 expectedVelocity;
	};
	constexpr f32 FRICTION = 0.39346934f;
	const Case cases[] = {
		{"PlaneBounce", {0.0f, 1.2f, 0.0f}, {1.0f, -4.0f, 0.0f}, R"("settings":{"Shape":"Plane"},"in":{"Position":{"value":[0,1,0]},"Bounce":{"value":[0.5]}})", {0.1f, 1.1f, 0.0f}, {1.0f, 2.0f, 0.0f}},
		{"PlaneFriction", {0.0f, 1.2f, 0.0f}, {2.0f, -4.0f, 0.0f}, R"("settings":{"Shape":"Plane"},"in":{"Position":{"value":[0,1,0]},"Bounce":{"value":[0]},"Friction":{"value":[5]}})", {0.1f + (2.0f - 2.0f * FRICTION) * 0.05f, 1.0f, 0.0f}, {2.0f - 2.0f * FRICTION, 0.0f, 0.0f}},
		{"PlaneUnderThreshold", {0.0f, 1.04f, 0.0f}, {0.0f, -0.5f, 0.0f}, R"("settings":{"Shape":"Plane"},"in":{"Position":{"value":[0,1,0]},"Bounce":{"value":[0.5]}})", {0.0f, 1.0f + 0.125f * 0.02f, 0.0f}, {0.0f, 0.125f, 0.0f}},
		{"SphereInside", {1.5f, 2.0f, 3.0f}, {}, R"("settings":{"Shape":"Sphere"},"in":{"Position":{"value":[1,2,3]}})", {2.0f, 2.0f, 3.0f}, {}},
		{"SphereCrossed", {-1.0f, 2.0f, 3.0f}, {20.0f, 0.0f, 0.0f}, R"("settings":{"Shape":"Sphere"},"in":{"Position":{"value":[1,2,3]},"Bounce":{"value":[1]}})", {-1.0f, 2.0f, 3.0f}, {-20.0f, 0.0f, 0.0f}},
		{"BoxInside", {0.9f, 0.2f, 0.1f}, {}, R"("settings":{"Shape":"OrientedBox"},"in":{"Size":{"value":[2,2,2]}})", {1.0f, 0.2f, 0.1f}, {}},
		{"BoxInverted", {1.5f, 0.3f, 0.0f}, {}, R"("settings":{"Shape":"OrientedBox","Mode":"Inverted"},"in":{"Size":{"value":[2,2,2]}})", {1.0f, 0.3f, 0.0f}, {}},
		{"CylinderInside", {0.9f, 1.0f, 0.0f}, {}, R"("settings":{"Shape":"Cone"},"in":{"Base Radius":{"value":[1]},"Top Radius":{"value":[1]},"Height":{"value":[2]}})", {1.0f, 1.0f, 0.0f}, {}},
		{"CylinderInverted", {1.5f, 1.0f, 0.0f}, {}, R"("settings":{"Shape":"Cone","Mode":"Inverted"},"in":{"Base Radius":{"value":[1]},"Top Radius":{"value":[1]},"Height":{"value":[2]}})", {1.0f, 1.0f, 0.0f}, {}},
	};
	const auto near = [](Float3 got, Float3 expected, const char* what) {
		EXPECT_NEAR(got.x, expected.x, 1.0e-4f) << what;
		EXPECT_NEAR(got.y, expected.y, 1.0e-4f) << what;
		EXPECT_NEAR(got.z, expected.z, 1.0e-4f) << what;
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		const str initialize = std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{},{},{}]}}}}}},
			{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":[{},{},{}]}}}}}})",
										   entry.position.x, entry.position.y, entry.position.z, entry.velocity.x, entry.velocity.y, entry.velocity.z);
		BlockRun run;
		if (!Begin(run, EffectWith("", initialize, std::format(R"({{"id":20,"def":"Block/CollisionShape",{}}})", entry.block)), std::format("Collide{}", entry.name))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {1});
		Advance(run, 0.1f, 0.1f, {});
		const Vector<Float3> positions = VectorsOf(run, "position");
		const Vector<Float3> velocities = VectorsOf(run, "velocity");
		ASSERT_EQ(positions.size(), 1u);
		ASSERT_EQ(velocities.size(), 1u);
		near(velocities[0], entry.expectedVelocity, "the velocity after the contact");
		near(positions[0], entry.expectedPosition, "the position after the contact and the integration");
	}

	{
		SCOPED_TRACE("LifetimeLoss");
		BlockRun run;
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}})";
		const str update = R"({"id":20,"def":"Block/CollisionShape","in":{"Position":{"value":[0,1,0]},"Lifetime Loss":{"value":[0.25]}}})";
		ASSERT_TRUE(Begin(run, EffectWith("", initialize, update), "CollideLifetimeLoss"));
		Advance(run, 0.0f, 0.1f, {1});
		Advance(run, 0.1f, 0.1f, {});
		EXPECT_NEAR(One(run, "age"), 0.25f * 2.0f + 0.1f, 1.0e-5f);
	}
	{
		SCOPED_TRACE("Ellipsoid");
		BlockRun run;
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[2,2,3.5]}}})";
		const str update = R"({"id":20,"def":"Block/CollisionShape","settings":{"Shape":"Sphere"},"in":{"Position":{"value":[1,2,3]},"Scale":{"value":[2,1,1]}}})";
		ASSERT_TRUE(Begin(run, EffectWith("", initialize, update), "CollideEllipsoid"));
		Advance(run, 0.0f, 0.1f, {1});
		Advance(run, 0.1f, 0.1f, {});
		const Vector<Float3> positions = VectorsOf(run, "position");
		ASSERT_EQ(positions.size(), 1u);
		const auto surface = [](f64 t) { return 4.0 / ((t + 4.0) * (t + 4.0)) + 0.25 / ((t + 1.0) * (t + 1.0)) - 1.0; };
		f64 low = -0.99;
		f64 high = 0.0;
		for (u32 i = 0; i < 60; ++i) {
			const f64 t = (low + high) * 0.5;
			(surface(t) > 0.0 ? low : high) = t;
		}
		const f64 t = (low + high) * 0.5;
		EXPECT_NEAR(positions[0].x, 1.0 + 4.0 / (t + 4.0), 5.0e-3);
		EXPECT_NEAR(positions[0].y, 2.0f, 1.0e-4f);
		EXPECT_NEAR(positions[0].z, 3.0 + 0.5 / (t + 1.0), 5.0e-3);
	}
}

TEST_F(GraphEffectLibraryFixture, CollisionAttributesAreWrittenAtContact) {
	const str nodes = R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"collisionEventNormal"}},
		{"id":31,"def":"Attribute/GetAttribute","settings":{"Attribute":"collisionEventPosition"}},
		{"id":32,"def":"Attribute/GetAttribute","settings":{"Attribute":"hasCollisionEvent"}},
		{"id":33,"def":"Utility/Branch","in":{"Predicate":{"from":[32,"hasCollisionEvent"]},"True":{"value":[1,1,1,1]},"False":{"value":[0,0,0,0]}}})";
	const str probes = R"({"id":21,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[30,"collisionEventNormal"]}}},
		{"id":22,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[31,"collisionEventPosition"]}}},
		{"id":23,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[33,"Out"]}}})";
	{
		SCOPED_TRACE("Trigger");
		BlockRun run;
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0.5,0]}}})";
		const str update = R"({"id":20,"def":"Block/CollisionShape","settings":{"Behavior":"None","Collision Attributes":"WriteAlways"},"in":{"Position":{"value":[0,1,0]}}},)" + probes;
		ASSERT_TRUE(Begin(run, EffectWith(nodes, initialize, update), "CollideTrigger"));
		for (const GraphEffectStored& stored : run.effect.systems[0].stored) {
			EXPECT_NE(stored.attribute, "hasCollisionEvent");
		}
		Advance(run, 0.0f, 0.1f, {1});
		Advance(run, 0.1f, 0.1f, {});
		EXPECT_EQ(One(run, "collisionEventCount"), 1.0f);
		EXPECT_NEAR(One(run, "position", 1), 0.5f, 1.0e-6f);
		EXPECT_NEAR(One(run, "color", 1), 1.0f, 1.0e-6f);
		EXPECT_NEAR(One(run, "scale", 1), 1.0f, 1.0e-6f);
		EXPECT_EQ(One(run, "alpha"), 1.0f);
		Advance(run, 0.2f, 0.1f, {});
		EXPECT_EQ(One(run, "collisionEventCount"), 2.0f);
	}
	{
		SCOPED_TRACE("PunctualOnly");
		BlockRun run;
		const str spawnNodes = R"({"id":40,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
			{"id":41,"def":"Math/Multiply","in":{"A":{"from":[40,"spawnIndex"]},"B":{"value":[-10,-10,-10,-10]}}},
			{"id":42,"def":"Input/Vector3","in":{"Y":{"from":[41,"Out"]}}},
			{"id":43,"def":"Math/Add","in":{"A":{"from":[40,"spawnIndex"]},"B":{"value":[0.5,0,0,0]}}},
			{"id":44,"def":"Input/Vector3","in":{"Y":{"from":[43,"Out"]}}})";
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[44,"Out"]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"from":[42,"Out"]}}},
			{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[40,"spawnIndex"]}}})";
		const str update = R"({"id":20,"def":"Block/CollisionShape","settings":{"Collision Attributes":"WritePunctualContactOnly"},"in":{"Position":{"value":[0,1,0]}}})";
		ASSERT_TRUE(Begin(run, EffectWith(spawnNodes, initialize, update), "CollidePunctual"));
		Advance(run, 0.0f, 0.1f, {2});
		Advance(run, 0.1f, 0.1f, {});
		const Vector<f32> counts = Values(run, "collisionEventCount");
		const Vector<f32> indices = Values(run, "size");
		ASSERT_EQ(counts.size(), 2u);
		ASSERT_EQ(indices.size(), 2u);
		for (size_t i = 0; i < counts.size(); ++i) {
			EXPECT_EQ(counts[i], indices[i] == 1.0f ? 1.0f : 0.0f);
		}
	}
	{
		SCOPED_TRACE("RoughSurface");
		BlockRun run;
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,1.5,0]}}},
			{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,-10,0]}}})";
		const str update = R"({"id":20,"def":"Block/CollisionShape","settings":{"Rough Surface":true,"Write Rough Normal":false,"Collision Attributes":"WriteAlways",
			"Override Bounce Threshold":true},"in":{"Position":{"value":[0,1,0]},"Bounce":{"value":[1]},"Bounce Speed Threshold":{"value":[0]},"Roughness":{"value":[1]}}},)" +
						   probes;
		ASSERT_TRUE(Begin(run, EffectWith(nodes, initialize, update), "CollideRough"));
		Advance(run, 0.0f, 0.1f, {32}, 7);
		Advance(run, 0.1f, 0.1f, {}, 7);
		const Vector<Float3> velocities = VectorsOf(run, "velocity");
		const Vector<Float3> normals = VectorsOf(run, "color");
		ASSERT_EQ(velocities.size(), 32u);
		Vector<f32> sideways{GetMemoryResource(MemTag::Graphics)};
		for (size_t i = 0; i < velocities.size(); ++i) {
			const Float3 v = velocities[i];
			EXPECT_NEAR(std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z), 10.0f, 1.0e-3f);
			EXPECT_NEAR(normals[i].y, 1.0f, 1.0e-5f);
			sideways.push_back(v.x);
		}
		EXPECT_GT(Distinct(sideways), 16u);
	}
}

TEST_F(GraphEffectLibraryFixture, FlipbookAdvancesTexIndex) {
	struct Case {
		const char* name;
		std::string_view settings;
		std::string_view in;
		std::string_view outputSettings;
		std::string_view outputIn;
		u32 updates;
		f32 texIndex;
		f32 blend;
	};
	constexpr std::string_view FLIPBOOK = R"({"UV Mode":"Flipbook"})";
	constexpr std::string_view BLENDING = R"({"UV Mode":"Flipbook","Flipbook Blend Frames":true})";
	constexpr std::string_view SHEET_4X1 = R"({"Flip Book Size":{"value":[4,1]}})";
	constexpr std::string_view SHEET_3X1 = R"({"Flip Book Size":{"value":[3,1]}})";
	constexpr std::string_view SHEET_4X2 = R"({"Flip Book Size":{"value":[4,2]}})";
	const Case cases[] = {
		{"Rate", "{}", "{}", "", "", 2, 4.8f, NAN},
		{"RateInRange", R"({"Use Custom Range":true})", "{}", FLIPBOOK, SHEET_3X1, 2, 1.8f, NAN},
		{"Reversed", R"({"Use Custom Range":true,"Reverse":true})", "{}", FLIPBOOK, SHEET_3X1, 1, 0.6f, NAN},
		{"BySpeed", R"({"Frame Rate Mode":"BySpeed"})",
		 R"({"Frame Rate Curve":{"value":{"keys":[{"time":0,"value":0,"outTangent":30},{"time":1,"value":30,"inTangent":30}]}}})", "", "", 1, 1.5f, NAN},
		{"Cycles", R"({"Mode":"Cycles"})", "{}", FLIPBOOK, SHEET_4X1, 2, 0.4f, NAN},
		{"CyclesOffset", R"({"Mode":"Cycles"})", R"({"Start Offset":{"value":[1]}})", FLIPBOOK, SHEET_4X1, 1, 1.2f, NAN},
		{"Row", R"({"Mode":"Cycles","Animation Range":"FlipbookRow"})", R"({"Index":{"value":[1]}})", FLIPBOOK, SHEET_4X2, 1, 4.2f, 1.0f},
		{"Column", R"({"Mode":"Cycles","Animation Range":"FlipbookColumn"})", R"({"Index":{"value":[1]}})", FLIPBOOK, SHEET_4X2, 1, 1.2f, 4.0f},
		{"StartEnd", R"({"Mode":"Cycles","Animation Range":"StartEndFrames"})", R"({"Frame Range":{"value":[2,5]}})", FLIPBOOK, SHEET_4X1, 1, 2.2f, 1.0f},
		{"ClampedBlending", R"({"Mode":"Cycles","Clamp Blending":true})", "{}", BLENDING, SHEET_4X1, 1, 0.0f, NAN},
		{"ClampedNotBlending", R"({"Mode":"Cycles","Clamp Blending":true})", "{}", FLIPBOOK, SHEET_4X1, 1, 0.2f, NAN},
		{"CustomCurve", R"({"Mode":"Cycles","Custom Curve":true})",
		 R"({"Custom Curve":{"value":{"keys":[{"time":0,"value":0,"outTangent":0},{"time":1,"value":1,"inTangent":2}]}}})", FLIPBOOK, SHEET_4X1, 1, 0.04f, NAN},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		const f32 life = std::string_view(entry.name) == "Column" ? 1.0f : 2.0f;
		const str initialize = std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"lifetime"}},"in":{{"Lifetime":{{"value":[{}]}}}}}},
			{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":[0.5,0,0]}}}}}})",
										   life);
		const str update = std::format(R"({{"id":20,"def":"Block/FlipbookPlayer","settings":{},"in":{}}})", entry.settings, entry.in);
		BlockRun run;
		if (!Begin(run, OneSystemShown("", initialize, update, entry.outputSettings, entry.outputIn), std::format("Flipbook{}", entry.name))) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {1});
		for (u32 i = 0; i < entry.updates; ++i) {
			Advance(run, 0.1f * static_cast<f32>(i + 1), 0.1f, {});
		}
		EXPECT_NEAR(One(run, "texIndex"), entry.texIndex, 1.0e-4f);
		if (!std::isnan(entry.blend)) {
			EXPECT_EQ(One(run, "texIndexBlend"), entry.blend);
		}
	}

	{
		SCOPED_TRACE("Random");
		BlockRun run;
		const str update = R"({"id":20,"def":"Block/FlipbookPlayer","settings":{"Frame Rate Mode":"Random"},"in":{"Frame Rate Range":{"value":[10,30]}}})";
		ASSERT_TRUE(Begin(run, EffectWith("", "", update), "FlipbookRandom"));
		Advance(run, 0.0f, 0.1f, {16}, 4);
		Advance(run, 0.1f, 0.1f, {}, 4);
		const Vector<f32> once = Values(run, "texIndex");
		Advance(run, 0.2f, 0.1f, {}, 4);
		const Vector<f32> twice = Values(run, "texIndex");
		ASSERT_EQ(once.size(), 16u);
		ASSERT_EQ(twice.size(), 16u);
		for (size_t i = 0; i < once.size(); ++i) {
			EXPECT_TRUE(once[i] >= 1.0f - 1.0e-4f && once[i] <= 3.0f + 1.0e-4f) << once[i] * 10.0f;
			EXPECT_NEAR(twice[i], 2.0f * once[i], 1.0e-4f);
		}
		EXPECT_GT(Distinct(once), 12u);
	}
}

TEST_F(GraphEffectLibraryFixture, TemplatesLaunchWithinRanges) {
	for (const u32 index : {1u, 2u}) {
		const bool burst = index == 2;
		SCOPED_TRACE(EffectGraphEditOps::TemplateLabel(index));
		BlockRun run;
		if (!Begin(run, EffectGraphEditOps::TemplateDocument(index), burst ? "TemplateBurst" : "TemplateLoop")) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, 0.1f, {32}, 9);
		const Vector<Float3> positions = VectorsOf(run, "position");
		const Vector<Float3> velocities = VectorsOf(run, "velocity");
		ASSERT_EQ(positions.size(), 32u);
		ASSERT_EQ(velocities.size(), 32u);
		const f32 radius = burst ? 0.15f : 0.03f;
		const f32 slowest = burst ? 0.5f : 4.8f;
		const f32 fastest = burst ? 4.5f : 7.5f;
		for (size_t i = 0; i < positions.size(); ++i) {
			const Float3 p = positions[i];
			const Float3 v = velocities[i];
			const f32 speed = Length(v);
			EXPECT_NEAR(Length(p), radius, 1.0e-5f);
			EXPECT_TRUE(speed >= slowest - 1.0e-4f && speed <= fastest + 1.0e-4f) << speed;
			if (burst) {
				EXPECT_NEAR((p.x * v.x + p.y * v.y + p.z * v.z) / (Length(p) * speed), 1.0f, 1.0e-4f);
			} else {
				EXPECT_GT(v.y / speed, 0.5f);
			}
		}
	}
}

TEST_F(GraphEffectLibraryFixture, TheSimpleTrailLeavesItsCircle) {
	BlockRun run;
	if (!Begin(run, EffectGraphEditOps::TemplateDocument(3), "TemplateTrail")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	constexpr f32 TURNED[12] = {0.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f};
	Advance(run, 0.0f, 0.1f, {8}, 0, 0, TURNED);
	const Vector<Float3> positions = VectorsOf(run, "position");
	const Vector<Float3> velocities = VectorsOf(run, "velocity");
	ASSERT_EQ(positions.size(), 8u);
	ASSERT_EQ(velocities.size(), 8u);
	const f32 chord = 2.0f * 0.01f * std::sin(2.5f * 3.14159265f / 180.0f);
	for (size_t i = 0; i < positions.size(); ++i) {
		const Float3 radial{positions[i].x - 2.0f, positions[i].y, positions[i].z};
		EXPECT_NEAR(Length(radial), 0.01f, 1.0e-5f);
		EXPECT_NEAR(radial.y, 0.0f, 1.0e-6f);
		f32 nearest = 1.0f;
		for (size_t j = 0; j < positions.size(); ++j) {
			const Float3 apart{positions[i].x - positions[j].x, positions[i].y - positions[j].y, positions[i].z - positions[j].z};
			nearest = j == i ? nearest : std::min(nearest, Length(apart));
		}
		EXPECT_NEAR(nearest, chord, 2.0e-6f);
		const f32 r = Length(radial);
		const Float3 lerped{0.25f * radial.x / r, 0.75f, 0.25f * radial.z / r};
		const f32 l = Length(lerped);
		EXPECT_NEAR(velocities[i].x, lerped.x / l, 1.0e-3f);
		EXPECT_NEAR(velocities[i].y, lerped.y / l, 1.0e-3f);
		EXPECT_NEAR(velocities[i].z, lerped.z / l, 1.0e-3f);
	}
	const StripWords strip = Strip(run.system, 0);
	EXPECT_EQ(strip.first, 0u);
	EXPECT_EQ(strip.count, 8u);
}

namespace {

	constexpr std::string_view LAYERS_GUID = "00000000000000a4-00000000000000b4";

	void PlanarField(LoadableTexture& texture) {
		texture.Volume(8, [](f32, f32, f32 z) { return std::array<f32, 4>{z, 0.0f, 0.0f, 1.0f}; });
	}

	void SphereField(LoadableTexture& texture, f32 radius) {
		texture.Volume(32, [radius](f32 x, f32 y, f32 z) { return std::array<f32, 4>{std::sqrt(x * x + y * y + z * z) - radius, 0.0f, 0.0f, 1.0f}; });
	}

	constexpr f32 ABOVE = 0.1875f;

	str Particle(std::string_view position, std::string_view velocity) {
		return std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":{}}}}}}},
			{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":{}}}}}}},
			{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"mass"}},"in":{{"Mass":{{"value":[2]}}}}}})",
						   position, velocity);
	}

} // anonymous namespace

TEST_F(GraphEffectLibraryFixture, SamplingOperatorsReadTexturesPortsHold) {
	LoadableTexture image;
	const u8 rows[] = {255, 0, 0, 255, 0, 255, 0, 255};
	image.Image(1, 2, rows);
	LoadableTexture layers;
	const u8 slices[] = {0, 0, 255, 255, 255, 255, 255, 255};
	layers.Image(1, 1, slices, 2);
	LoadableTexture volume;
	volume.Volume(2, [](f32 x, f32 y, f32 z) { return std::array<f32, 4>{x, y, z, 1.0f}; }, false);
	ASSERT_TRUE(image.texture.IsValid() && layers.texture.IsValid() && volume.texture.IsValid());
	const HeldTexture held[] = {{Guid::FromString(IMAGE_GUID), &image}, {Guid::FromString(LAYERS_GUID), &layers}, {Guid::FromString(VOLUME_GUID), &volume}};

	const str nodes = std::format(R"({{"id":20,"def":"Sampling/SampleTexture2D","in":{{"Texture":{{"value":"{0}"}},"UV":{{"value":[0.5,0.25]}}}}}},
		{{"id":21,"def":"Sampling/SampleTexture2D","in":{{"Texture":{{"value":"{0}"}},"UV":{{"value":[0.5,0.75]}}}}}},
		{{"id":22,"def":"Sampling/SampleTexture2DArray","in":{{"Texture":{{"value":"{1}"}},"Slice":{{"value":[1]}}}}}},
		{{"id":23,"def":"Sampling/SampleTexture3D","in":{{"Texture":{{"value":"{2}"}},"UVW":{{"value":[0.75,0.25,0.75]}}}}}},
		{{"id":24,"def":"Sampling/SampleTexture3D","in":{{"Texture":{{"value":null}}}}}})",
								  IMAGE_GUID, LAYERS_GUID, VOLUME_GUID);
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[20,"s"]}}},
		{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[21,"s"]}}},
		{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"pivot"},"in":{"Pivot":{"from":[22,"s"]}}},
		{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[23,"s"]}}},
		{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[24,"s"]}}})";
	BlockRun run;
	ASSERT_TRUE(Begin(run, EffectWith(nodes, initialize, ""), "SampleTextures", held));
	Advance(run, 0.0f, 0.1f, {1});
	EXPECT_FLOAT_EQ(One(run, "color", 0), 0.0f);
	EXPECT_FLOAT_EQ(One(run, "color", 1), 1.0f);
	EXPECT_FLOAT_EQ(One(run, "size"), 1.0f);
	EXPECT_FLOAT_EQ(One(run, "pivot", 0), 1.0f);
	EXPECT_FLOAT_EQ(One(run, "pivot", 2), 1.0f);
	EXPECT_FLOAT_EQ(One(run, "scale", 0), 0.25f);
	EXPECT_FLOAT_EQ(One(run, "scale", 1), -0.25f);
	EXPECT_FLOAT_EQ(One(run, "scale", 2), 0.25f);
	EXPECT_NEAR(One(run, "alpha"), 128.0f / 255.0f, 1.0e-3f);
}

TEST_F(GraphEffectLibraryFixture, VectorFieldPushesAsFormulaSays) {
	LoadableTexture signedField;
	signedField.Volume(2, [](f32, f32, f32) { return std::array<f32, 4>{2.0f, -1.0f, 0.5f, 1.0f}; });
	LoadableTexture unsignedField;
	unsignedField.Volume(2, [](f32, f32, f32) { return std::array<f32, 4>{1.0f, 0.0f, 0.75f, 1.0f}; });
	const HeldTexture held[] = {{Guid::FromString(FIELD_GUID), &signedField}, {Guid::FromString(VOLUME_GUID), &unsignedField}};
	const str initialize = Particle("[0.3,0.7,1.1]", "[1,2,3]");
	const auto after = [&](std::string_view name, std::string_view settings, std::string_view guid, std::string_view size) {
		BlockRun run;
		const str update = std::format(R"({{"id":20,"def":"Block/VectorFieldForce","settings":{},"in":{{"Vector Field":{{"value":"{}"}},"Size":{{"value":{}}},"Intensity":{{"value":[3]}}}}}})",
									   settings, guid, size);
		if (!Begin(run, EffectWith("", initialize, update), name, held)) {
			return Float3{NAN, NAN, NAN};
		}
		Advance(run, 0.0f, 0.1f, {1});
		Advance(run, 0.1f, 0.1f, {});
		const Vector<Float3> velocities = VectorsOf(run, "velocity");
		EXPECT_EQ(velocities.size(), 1u) << name;
		return velocities.empty() ? Float3{NAN, NAN, NAN} : velocities[0];
	};
	const auto expect = [](Float3 v, Float3 expected, const char* what) {
		EXPECT_NEAR(v.x, expected.x, 1.0e-4f) << what;
		EXPECT_NEAR(v.y, expected.y, 1.0e-4f) << what;
		EXPECT_NEAR(v.z, expected.z, 1.0e-4f) << what;
	};
	expect(after("FieldAbsolute", R"({"Mode":"Absolute","Data Encoding":"Signed"})", FIELD_GUID, "[4,4,4]"), {1.0f + 1.2f, 2.0f - 0.6f, 3.0f + 0.3f}, "an absolute field");
	expect(after("FieldConserved", R"({"Mode":"Absolute","Data Encoding":"Signed","Conserve Magnitude":true})", FIELD_GUID, "[4,4,4]"),
		   {1.0f + 0.3f, 2.0f - 0.15f, 3.0f + 0.075f}, "a field keeping its texel's length");
	expect(after("FieldUnsigned", R"({"Mode":"Absolute"})", VOLUME_GUID, "[4,4,4]"), {1.0f + 0.6f, 2.0f - 0.6f, 3.0f + 0.3f}, "an unsigned field");
	expect(after("FieldRelative", R"({"Data Encoding":"Signed"})", FIELD_GUID, "[4,4,4]"), {1.0f + 23.0f * 0.05f, 2.0f - 14.0f * 0.05f, 3.0f + 3.0f * 0.05f},
		   "a relative field");
	expect(after("FieldClosed", R"({"Mode":"Absolute","Data Encoding":"Signed","Closed Field":true})", FIELD_GUID, "[1,1,1]"), {1.0f, 2.0f, 3.0f},
		   "a closed field pushed a particle outside its box");
	expect(after("FieldOpen", R"({"Mode":"Absolute","Data Encoding":"Signed"})", FIELD_GUID, "[1,1,1]"), {1.0f + 0.3f, 2.0f - 0.15f, 3.0f + 0.075f},
		   "an open field read nothing at its edge");
}

TEST_F(GraphEffectLibraryFixture, AttractorPullsToSurface) {
	LoadableTexture plane;
	PlanarField(plane);
	const HeldTexture held[] = {{Guid::FromString(FIELD_GUID), &plane}};
	const auto pulled = [&](std::string_view name, f32 z) {
		BlockRun run;
		const str update = std::format(R"({{"id":20,"def":"Block/ConformToSignedDistanceField","in":{{"Distance Field":{{"value":"{}"}}}}}})", FIELD_GUID);
		if (!Begin(run, EffectWith("", Particle(std::format("[0,0,{}]", z), "[1,2,3]"), update), name, held)) {
			return Float3{NAN, NAN, NAN};
		}
		Advance(run, 0.0f, 0.1f, {1});
		Advance(run, 0.1f, 0.1f, {});
		const Vector<Float3> velocities = VectorsOf(run, "velocity");
		return velocities.size() == 1 ? velocities[0] : Float3{NAN, NAN, NAN};
	};
	const auto formula = [](f32 distance) {
		const f32 t = std::clamp(distance / 0.2f, 0.0f, 1.0f);
		const f32 ratio = t * t * (3.0f - 2.0f * t);
		const f32 change = 5.0f * ratio - (-3.0f);
		const f32 step = std::copysign(std::min(std::abs(change), 0.1f * (50.0f + (20.0f - 50.0f) * ratio)), change);
		return 3.0f - step / 2.0f;
	};
	const Float3 inside = pulled("AttractInside", ABOVE);
	EXPECT_NEAR(inside.x, 1.0f, 1.0e-4f);
	EXPECT_NEAR(inside.z, formula(ABOVE), 1.0e-3f);
	const Float3 outside = pulled("AttractOutside", 0.8f);
	EXPECT_NEAR(outside.z, formula(0.4375f + 0.3f), 1.0e-3f);
}

TEST_F(GraphEffectLibraryFixture, FieldCollisionMeetsWhereMarchDoes) {
	LoadableTexture plane;
	PlanarField(plane);
	const HeldTexture held[] = {{Guid::FromString(FIELD_GUID), &plane}};
	const auto run = [&](std::string_view name, f32 z, f32 speed, std::string_view settings, Vector<Float3>& positions, Vector<Float3>& velocities) {
		BlockRun effect;
		const str update = std::format(R"({{"id":20,"def":"Block/CollisionShape","settings":{},"in":{{"Distance Field":{{"value":"{}"}},"Bounce":{{"value":[1]}}}}}})",
									   settings, FIELD_GUID);
		if (!Begin(effect, EffectWith("", Particle(std::format("[0,0,{}]", z), std::format("[0,0,{}]", speed)), update), name, held)) {
			return;
		}
		Advance(effect, 0.0f, 0.1f, {1});
		Advance(effect, 0.1f, 0.1f, {});
		positions = VectorsOf(effect, "position");
		velocities = VectorsOf(effect, "velocity");
	};
	Vector<Float3> positions{GetMemoryResource(MemTag::Graphics)};
	Vector<Float3> velocities{GetMemoryResource(MemTag::Graphics)};
	run("CollideField", ABOVE, -4.0f, R"({"Shape":"SignedDistanceField"})", positions, velocities);
	ASSERT_EQ(velocities.size(), 1u);
	EXPECT_NEAR(velocities[0].z, 4.0f, 1.0e-3f);
	EXPECT_NEAR(positions[0].z, -ABOVE + 0.4f, 2.0e-3f);
	run("CollideFieldInverted", -ABOVE, 4.0f, R"({"Shape":"SignedDistanceField","Mode":"Inverted"})", positions, velocities);
	ASSERT_EQ(velocities.size(), 1u);
	EXPECT_NEAR(velocities[0].z, -4.0f, 1.0e-3f);
	EXPECT_NEAR(positions[0].z, ABOVE - 0.4f, 2.0e-3f);
	run("KillField", ABOVE, -4.0f, R"({"Shape":"SignedDistanceField","Behavior":"Kill"})", positions, velocities);
	EXPECT_TRUE(velocities.empty());
	run("KillFieldAway", ABOVE, 4.0f, R"({"Shape":"SignedDistanceField","Behavior":"Kill"})", positions, velocities);
	EXPECT_EQ(velocities.size(), 1u);
}

TEST_F(GraphEffectLibraryFixture, FieldPlacesOnSurfaceOrVolume) {
	LoadableTexture sphere;
	SphereField(sphere, 0.45f);
	const HeldTexture held[] = {{Guid::FromString(FIELD_GUID), &sphere}};
	const auto place = [&](std::string_view name, std::string_view mode) {
		BlockRun run;
		const str block = std::format(R"({{"id":10,"def":"Block/PositionShape","settings":{{"Shape":"SignedDistanceField","Position Mode":"{}","Projection Steps":4}},
			"in":{{"SDF":{{"value":"{}"}},"Center":{{"value":[1,2,3]}},"Size":{{"value":[2,2,2]}}}}}})",
									  mode, FIELD_GUID);
		if (!Begin(run, EffectWith("", block, ""), name, held)) {
			return Vector<f32>{GetMemoryResource(MemTag::Graphics)};
		}
		Advance(run, 0.0f, 0.1f, {32}, 7);
		Vector<f32> radii{GetMemoryResource(MemTag::Graphics)};
		for (const Float3 p : VectorsOf(run, "position")) {
			radii.push_back(Length({p.x - 1.0f, p.y - 2.0f, p.z - 3.0f}));
		}
		return radii;
	};
	const Vector<f32> surface = place("FieldSurface", "Surface");
	ASSERT_EQ(surface.size(), 32u);
	for (f32 radius : surface) {
		EXPECT_NEAR(radius, 0.9f, 0.03f);
	}
	const Vector<f32> volume = place("FieldVolume", "Volume");
	ASSERT_EQ(volume.size(), 32u);
	u32 inner = 0;
	for (f32 radius : volume) {
		EXPECT_LE(radius, 0.93f);
		inner += radius < 0.8f ? 1u : 0u;
	}
	EXPECT_GT(inner, 4u);
}

TEST_F(GraphEffectLibraryFixture, SdfSampleReadsDistanceAndDirection) {
	LoadableTexture plane;
	PlanarField(plane);
	const HeldTexture held[] = {{Guid::FromString(FIELD_GUID), &plane}};
	const auto sample = [&](std::string_view name, f32 z, std::string_view size, f32& distance, Float3& direction) {
		BlockRun run;
		const str nodes = std::format(R"({{"id":30,"def":"Attribute/GetAttribute","settings":{{"Attribute":"position"}}}},
			{{"id":31,"def":"Sampling/SampleSignedDistanceField","in":{{"Texture":{{"value":"{}"}},"Size":{{"value":{}}},"Position":{{"from":[30,"position"]}}}}}})",
									  FIELD_GUID, size);
		const str initialize = std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[0,0,{}]}}}}}},
			{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"from":[31,"distance"]}}}}}},
			{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"from":[31,"direction"]}}}}}})",
										   z);
		if (!Begin(run, EffectWith(nodes, initialize, ""), name, held)) {
			return;
		}
		Advance(run, 0.0f, 0.1f, {1});
		distance = One(run, "size");
		const Vector<Float3> directions = VectorsOf(run, "color");
		direction = directions.size() == 1 ? directions[0] : Float3{NAN, NAN, NAN};
	};
	f32 distance = NAN;
	Float3 direction;
	sample("SampleFieldInside", ABOVE, "[1,1,1]", distance, direction);
	EXPECT_NEAR(distance, ABOVE, 1.0e-4f);
	EXPECT_NEAR(direction.z, -1.0f, 1.0e-4f);
	sample("SampleFieldScaled", 2.0f * ABOVE, "[2,2,2]", distance, direction);
	EXPECT_NEAR(distance, 2.0f * ABOVE, 1.0e-4f);
	sample("SampleFieldOutside", 0.8f, "[1,1,1]", distance, direction);
	EXPECT_NEAR(distance, 0.4375f + 0.3f, 1.0e-3f);
	EXPECT_NEAR(direction.z, -1.0f, 1.0e-4f);
}

namespace {

	class DepthCollisionFixture : public GraphEffectLibraryFixture {
	protected:
		static constexpr f32 WALL_DISTANCE = 4.0f;
		static constexpr f32 DELTA = 0.1f;
		Handle<Scene> _scene;
		Handle<SceneObject> _wall;
		Handle<SceneObject> _cameraObject;
		Handle<Camera> _camera;
		ViewTargetId _target = INVALID_VIEW_TARGET;

		void SetUp() override {
			GraphEffectLibraryFixture::SetUp();
			if (IsSkipped()) {
				return;
			}
			Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!unlit || !quadMesh) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
			FileSystem::WriteTextFile(Path((_root / "Wall.mat").generic_string()), "{\n\t\"shader\": \"" + unlit->assetId.ToString() + "\"\n}\n");
			auto* surface = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Wall.mat")));
			ASSERT_NE(surface, nullptr);
			_scene = SceneSystem::CreateScene("DepthCollision");
			_wall = SceneSystem::CreateObject("Wall", _scene);
			_wall->SetLocalPosition(Vec3(0.0f, 0.0f, WALL_DISTANCE));
			_wall->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
			_wall->SetLocalScale(Vec3(20.0f, 20.0f, 1.0f));
			Handle<MeshRenderer> renderer = _wall->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(surface));
			_cameraObject = SceneSystem::CreateObject("Camera", _scene);
			_camera = _cameraObject->AddComponent<Camera>();
			SceneSystem::EndFrame();
			_target = RenderSystem::CreateViewTarget({64, 64});
		}

		void TearDown() override {
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			GraphEffectLibraryFixture::TearDown();
		}

		void Frame(BlockRun& run, u32 frame, std::initializer_list<u32> batches, bool drawn = true, std::span<const f32> localToWorld = {}) {
			if (drawn) {
				ViewDesc desc;
				desc.view = _cameraObject->GetWorldMatrix().Inverse();
				desc.projection = _camera->BuildProjection(1.0f);
				desc.cameraPosition = _cameraObject->GetWorldPosition();
				desc.camera = _camera;
				RenderSystem::SubmitView(_target, desc);
			}
			Advance(run, static_cast<f32>(frame) * DELTA, DELTA, batches, 0, 0, localToWorld);
		}

		bool Run(BlockRun& run, Float3 position, Float3 velocity, std::string_view update, std::string_view name, u32 frames, bool drawn = true,
				 std::span<const f32> localToWorld = {}) {
			const str initialize = std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[{},{},{}]}}}}}},
				{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":[{},{},{}]}}}}}})",
											   position.x, position.y, position.z, velocity.x, velocity.y, velocity.z);
			if (!Begin(run, EffectWith("", initialize, update), name)) {
				return false;
			}
			for (u32 frame = 0; frame < frames; ++frame) {
				if (frame == 0) {
					Frame(run, frame, {1}, drawn, localToWorld);
				} else {
					Frame(run, frame, {}, drawn, localToWorld);
				}
			}
			return true;
		}

		bool Placed(BlockRun& run, std::string_view block, std::string_view name, u32 count, bool drawn) {
			if (!Begin(run, EffectWith("", block, ""), name)) {
				return false;
			}
			Frame(run, 0, {}, drawn);
			Frame(run, 1, {}, drawn);
			Frame(run, 2, {count}, drawn);
			Frame(run, 3, {}, drawn);
			return true;
		}
	};

} // anonymous namespace

TEST_F(DepthCollisionFixture, ParticleStopsOnDepthMainCameraDrew) {
	const str update = R"({"id":20,"def":"Block/CollisionDepth","in":{"Bounce":{"value":[0]}}})";
	BlockRun undrawn;
	if (!Run(undrawn, {0.0f, 0.0f, 1.2f}, {0.0f, 0.0f, 5.0f}, update, "DepthUndrawn", 12, false)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const Vector<Float3> passed = VectorsOf(undrawn, "position");
	ASSERT_EQ(passed.size(), 1u);
	EXPECT_NEAR(passed[0].z, 1.2f + 11.0f * 0.5f, 1.0e-3f);

	BlockRun run;
	ASSERT_TRUE(Run(run, {0.0f, 0.0f, 1.2f}, {0.0f, 0.0f, 5.0f}, update, "DepthStop", 12));
	const Vector<Float3> positions = VectorsOf(run, "position");
	const Vector<Float3> velocities = VectorsOf(run, "velocity");
	ASSERT_EQ(positions.size(), 1u);
	EXPECT_NEAR(positions[0].z, WALL_DISTANCE, 1.0e-3f);
	EXPECT_NEAR(positions[0].x, 0.0f, 1.0e-4f);
	EXPECT_NEAR(velocities[0].z, 0.0f, 1.0e-4f);

	BlockRun bounced;
	ASSERT_TRUE(Run(bounced, {0.0f, 0.0f, 1.2f}, {0.0f, 0.0f, 5.0f}, R"({"id":20,"def":"Block/CollisionDepth","in":{"Bounce":{"value":[1]}}})",
					"DepthBounce", 8));
	const Vector<Float3> back = VectorsOf(bounced, "velocity");
	ASSERT_EQ(back.size(), 1u);
	EXPECT_NEAR(back[0].z, -5.0f, 1.0e-3f);
}

TEST_F(DepthCollisionFixture, ParticleStopsOnlyWhereWallWasDrawn) {
	const str update = R"({"id":20,"def":"Block/CollisionDepth","in":{"Bounce":{"value":[0]}}})";
	struct Case {
		const char* name;
		Vec3 wall;
		Float3 position;
		bool stops;
	};
	const Case cases[] = {
		{"DepthAbove", {0.0f, 10.0f, WALL_DISTANCE}, {0.0f, 0.4f, 1.2f}, true},
		{"DepthBelow", {0.0f, 10.0f, WALL_DISTANCE}, {0.0f, -0.4f, 1.2f}, false},
		{"DepthRight", {10.0f, 0.0f, WALL_DISTANCE}, {0.4f, 0.0f, 1.2f}, true},
		{"DepthLeft", {10.0f, 0.0f, WALL_DISTANCE}, {-0.4f, 0.0f, 1.2f}, false},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		_wall->SetLocalPosition(entry.wall);
		SceneSystem::EndFrame();
		BlockRun run;
		if (!Run(run, entry.position, {0.0f, 0.0f, 5.0f}, update, entry.name, 12)) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		const Vector<Float3> positions = VectorsOf(run, "position");
		ASSERT_EQ(positions.size(), 1u);
		if (entry.stops) {
			EXPECT_NEAR(positions[0].z, WALL_DISTANCE, 1.0e-3f);
		} else {
			EXPECT_NEAR(positions[0].z, 1.2f + 11.0f * 0.5f, 1.0e-3f);
		}
	}
}

TEST_F(DepthCollisionFixture, DepthKillEndsParticle) {
	const str update = R"({"id":20,"def":"Block/CollisionDepth","settings":{"Behavior":"Kill"}})";
	BlockRun undrawn;
	if (!Run(undrawn, {0.0f, 0.0f, 1.2f}, {0.0f, 0.0f, 5.0f}, update, "DepthKillUndrawn", 12, false)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
	EXPECT_EQ(Alive(undrawn.system, indices), 1u);
	BlockRun run;
	ASSERT_TRUE(Run(run, {0.0f, 0.0f, 1.2f}, {0.0f, 0.0f, 5.0f}, update, "DepthKill", 12));
	EXPECT_EQ(Alive(run.system, indices), 0u);
}

TEST_F(DepthCollisionFixture, InitializeKillReadsPriorDepth) {
	BlockRun run;
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,0,6]}}},
		{"id":11,"def":"Block/CollisionDepth","settings":{"Behavior":"Kill"}})";
	if (!Placed(run, initialize, "DepthInitializeKill", 1, true)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
	EXPECT_EQ(Alive(run.system, indices), 0u);
}

TEST_F(DepthCollisionFixture, LocalSystemStopsOnWallInSpace) {
	const str update = R"({"id":20,"def":"Block/CollisionDepth","in":{"Bounce":{"value":[0]}}})";
	const f32 moved[12] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
	BlockRun run;
	if (!Run(run, {0.0f, 0.0f, 0.2f}, {0.0f, 0.0f, 5.0f}, update, "DepthLocal", 12, true, moved)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const Vector<Float3> positions = VectorsOf(run, "position");
	ASSERT_EQ(positions.size(), 1u);
	EXPECT_NEAR(positions[0].z, WALL_DISTANCE - 1.0f, 1.0e-3f);
}

TEST_F(DepthCollisionFixture, SurfaceThicknessEndsSurfaceBehindIt) {
	BlockRun infinite;
	if (!Run(infinite, {0.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 1.0f}, R"({"id":20,"def":"Block/CollisionDepth","in":{"Bounce":{"value":[0]}}})",
			 "DepthInfinite", 4)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const Vector<Float3> held = VectorsOf(infinite, "position");
	ASSERT_EQ(held.size(), 1u);
	EXPECT_NEAR(held[0].z, WALL_DISTANCE, 1.0e-3f);

	BlockRun thin;
	ASSERT_TRUE(Run(thin, {0.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 1.0f},
					R"({"id":20,"def":"Block/CollisionDepth","settings":{"Surface Thickness":"Custom"},"in":{"Bounce":{"value":[0]},"Surface Thickness":{"value":[0.5]}}})",
					"DepthThin", 4));
	const Vector<Float3> passed = VectorsOf(thin, "position");
	ASSERT_EQ(passed.size(), 1u);
	EXPECT_NEAR(passed[0].z, 6.0f + 3.0f * 0.1f, 1.0e-3f);
}

TEST_F(DepthCollisionFixture, DepthPositionUsesCameraDepth) {
	const auto at = [&](f32 u, f32 v, std::string_view name, Float3& out) {
		BlockRun run;
		const str block = std::format(R"({{"id":10,"def":"Block/PositionDepth","settings":{{"Mode":"Custom"}},"in":{{"UV Spawn":{{"value":[{},{}]}}}}}})", u, v);
		if (!Placed(run, block, name, 1, true)) {
			return false;
		}
		const Vector<Float3> positions = VectorsOf(run, "position");
		EXPECT_EQ(positions.size(), 1u) << name;
		out = positions.empty() ? Float3{} : positions[0];
		return true;
	};
	Float3 middle{};
	if (!at(0.5f, 0.5f, "DepthPlaceMiddle", middle)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EXPECT_NEAR(middle.x, 0.0f, 1.0e-3f);
	EXPECT_NEAR(middle.y, 0.0f, 1.0e-3f);
	EXPECT_NEAR(middle.z, WALL_DISTANCE, 1.0e-3f);

	Float3 upperRight{};
	ASSERT_TRUE(at(0.75f, 0.75f, "DepthPlaceUpperRight", upperRight));
	EXPECT_NEAR(upperRight.z, WALL_DISTANCE, 1.0e-3f);
	EXPECT_GT(upperRight.x, 0.1f);
	EXPECT_GT(upperRight.y, 0.1f);
	EXPECT_NEAR(upperRight.x, upperRight.y, 1.0e-2f);

	Float3 lowerLeft{};
	ASSERT_TRUE(at(0.25f, 0.25f, "DepthPlaceLowerLeft", lowerLeft));
	EXPECT_NEAR(lowerLeft.x, -upperRight.x, 1.0e-2f);
	EXPECT_NEAR(lowerLeft.y, -upperRight.y, 1.0e-2f);
}

TEST_F(DepthCollisionFixture, FarPlaneCullKillsEmptyPixels) {
	_wall->SetLocalPosition(Vec3(0.0f, 10.0f, WALL_DISTANCE));
	SceneSystem::EndFrame();
	const auto alive = [&](f32 v, std::string_view cull, std::string_view name) -> i32 {
		BlockRun run;
		const str block =
			std::format(R"({{"id":10,"def":"Block/PositionDepth","settings":{{"Mode":"Custom","Cull Mode":"{}"}},"in":{{"UV Spawn":{{"value":[0.5,{}]}}}}}})", cull, v);
		if (!Placed(run, block, name, 1, true)) {
			return -1;
		}
		Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
		return static_cast<i32>(Alive(run.system, indices));
	};
	const i32 below = alive(0.25f, "Far Plane", "DepthCullBelow");
	if (below < 0) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EXPECT_EQ(below, 0);
	EXPECT_EQ(alive(0.75f, "Far Plane", "DepthCullAbove"), 1);
	EXPECT_EQ(alive(0.25f, "None", "DepthCullNone"), 1);
}

TEST_F(DepthCollisionFixture, RandomDepthPositionHitsWall) {
	BlockRun run;
	if (!Placed(run, R"({"id":10,"def":"Block/PositionDepth"})", "DepthRandom", 16, true)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const Vector<Float3> positions = VectorsOf(run, "position");
	ASSERT_EQ(positions.size(), 16u);
	f32 spread = 0.0f;
	for (const Float3& position : positions) {
		EXPECT_NEAR(position.z, WALL_DISTANCE, 1.0e-3f);
		EXPECT_LT(std::abs(position.x), WALL_DISTANCE);
		spread = std::max(spread, std::abs(position.x - positions[0].x) + std::abs(position.y - positions[0].y));
	}
	EXPECT_GT(spread, 0.1f);
}

namespace {

	struct MeshCorner {
		f32 position[3];
		f32 normal[3];
		f32 tangent[3];
		f32 sign;
		u8 color[4];
		f32 uv[2];
	};
	constexpr MeshCorner CORNERS[] = {
		{{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 2.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, {255, 0, 0, 255}, {0.25f, 0.5f}},
		{{4.0f, -5.0f, 6.0f}, {0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, -1.0f, {0, 255, 0, 128}, {0.75f, 0.125f}},
		{{-7.0f, 8.0f, 9.0f}, {4.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f, {0, 0, 255, 255}, {0.5f, 1.0f}},
		{{-1.0f, -2.0f, -3.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, 1.0f, {255, 255, 255, 0}, {0.0f, 0.0f}},
	};
	constexpr u32 CORNER_INDICES[] = {2, 0, 1, 3, 2, 1};

	void FillCorners(LoadableMesh& mesh) {
		for (const MeshCorner& corner : CORNERS) {
			MeshVertex vertex;
			vertex.position = Vec3(corner.position[0], corner.position[1], corner.position[2]);
			vertex.normal = Vec3(corner.normal[0], corner.normal[1], corner.normal[2]);
			vertex.tangent = Vec3(corner.tangent[0], corner.tangent[1], corner.tangent[2]);
			vertex.tangentSign = corner.sign;
			vertex.color = Color32{corner.color[0], corner.color[1], corner.color[2], corner.color[3]};
			vertex.u = corner.uv[0];
			vertex.v = corner.uv[1];
			mesh.vertices.push_back(vertex);
		}
		mesh.indices.assign(std::begin(CORNER_INDICES), std::end(CORNER_INDICES));
		mesh.submeshes.push_back({0, 6, 0, AABB::Empty()});
		mesh.OnLoad();
	}

	Float3 Corner(u32 vertex) { return {CORNERS[vertex].position[0], CORNERS[vertex].position[1], CORNERS[vertex].position[2]}; }

	Float3 Mix(Float3 a, Float3 b, f32 t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

	Float3 Weigh(Float3 a, Float3 b, Float3 c, f32 x, f32 y, f32 z) {
		return {a.x * x + b.x * y + c.x * z, a.y * x + b.y * y + c.y * z, a.z * x + b.z * y + c.z * z};
	}

	class MeshSamplingFixture : public GraphEffectLibraryFixture {
	protected:
		LoadableMesh _mesh;
		StagedMesh _staged[1];

		void SetUp() override {
			GraphEffectLibraryFixture::SetUp();
			if (IsSkipped()) {
				return;
			}
			FillCorners(_mesh);
			_staged[0] = {Guid::FromString(MESH_GUID), &_mesh};
		}

		void TearDown() override {
			_mesh.OnUnload();
			GraphEffectLibraryFixture::TearDown();
		}

		bool Spawn(BlockRun& run, const str& nodes, std::string_view initialize, const char* name, u32 spawn = 1) {
			if (!Begin(run, EffectWith(nodes, initialize, ""), name, {}, _staged)) {
				return false;
			}
			Advance(run, 0.0f, 0.1f, {spawn});
			return true;
		}

		static void ExpectNear(Float3 got, Float3 want, const char* what, f32 tolerance = 1.0e-5f) {
			EXPECT_NEAR(got.x, want.x, tolerance) << what;
			EXPECT_NEAR(got.y, want.y, tolerance) << what;
			EXPECT_NEAR(got.z, want.z, tolerance) << what;
		}

		static Float3 Only(const BlockRun& run, std::string_view attribute) {
			const Vector<Float3> values = VectorsOf(run, attribute);
			EXPECT_EQ(values.size(), 1u) << attribute;
			return values.empty() ? Float3{NAN, NAN, NAN} : values[0];
		}
	};

	str SampleNode(u32 id, std::string_view settings, std::string_view inputs) {
		return std::format(R"({{"id":{},"def":"Sampling/SampleMesh","settings":{{{}}},"in":{{"Mesh":{{"value":"{}"}}{}{}}}}})", id, settings, MESH_GUID,
						   inputs.empty() ? "" : ",", inputs);
	}

	str SetFrom(u32 id, std::string_view attribute, u32 from, std::string_view port) {
		const str capitalized = str(1, static_cast<char>(std::toupper(static_cast<unsigned char>(attribute[0])))) + str(attribute.substr(1));
		return std::format(R"({{"id":{},"def":"Block/SetAttribute","settings":{{"Attribute":"{}"}},"in":{{"{}":{{"from":[{},"{}"]}}}}}})", id, attribute,
						   capitalized, from, port);
	}

} // anonymous namespace

TEST_F(MeshSamplingFixture, SampleMeshFollowsFormula) {
	const str nodes = SampleNode(20, "", R"("Vertex":{"value":[1]})") + "," +
					  SampleNode(21, R"("Placement Mode":"Edge")", R"("Index":{"value":[1]},"Edge":{"value":[0.25]})") + "," +
					  SampleNode(22, R"("Placement Mode":"Edge")", R"("Index":{"value":[2]},"Edge":{"value":[0.5]})") + "," +
					  SampleNode(23, R"("Placement Mode":"Surface","Surface Coordinates":"Barycentric")", R"("Triangle":{"value":[1]},"Barycentric":{"value":[0.2,0.3]})") +
					  "," + SampleNode(24, R"("Placement Mode":"Surface")", R"("Triangle":{"value":[1]},"Square":{"value":[0.2,0.8]})") + "," +
					  SampleNode(25, R"("Placement Mode":"Surface")", R"("Triangle":{"value":[1]},"Square":{"value":[0.5,0.5]})");
	const str initialize = SetFrom(10, "position", 20, "Position") + "," + SetFrom(11, "scale", 21, "Position") + "," + SetFrom(12, "pivot", 22, "Position") +
						   "," + SetFrom(13, "axisX", 23, "Position") + "," + SetFrom(14, "axisY", 24, "Position") + "," + SetFrom(15, "axisZ", 25, "Position");
	BlockRun run;
	if (!Spawn(run, nodes, initialize, "MeshPlacements")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ExpectNear(Only(run, "position"), Corner(1), "a vertex is not the vertex its index names");
	ExpectNear(Only(run, "scale"), Mix(Corner(0), Corner(1), 0.25f), "an edge is not a quarter along from index 1's vertex to index 2's");
	ExpectNear(Only(run, "pivot"), Mix(Corner(1), Corner(2), 0.5f), "an edge at a triangle's last index does not wrap to its first");
	ExpectNear(Only(run, "axisX"), Weigh(Corner(3), Corner(2), Corner(1), 0.2f, 0.3f, 0.5f), "barycentric weights");
	ExpectNear(Only(run, "axisY"), Weigh(Corner(3), Corner(2), Corner(1), 0.1f, 0.7f, 0.2f), "a square's uniform map, its difference above 0");
	ExpectNear(Only(run, "axisZ"), Weigh(Corner(3), Corner(2), Corner(1), 0.25f, 0.25f, 0.5f), "a square's uniform map, its difference 0");
}

TEST_F(MeshSamplingFixture, EachOutputIsItsVertexAttribute) {
	const str every = R"("Output Normal":true,"Output Tangent":true,"Output Bitangent":true,"Output Bitangent Sign":true)";
	const str nodes = SampleNode(20, every, R"("Vertex":{"value":[1]})") + "," +
					  SampleNode(21, R"("Output Normal":true)", R"("Vertex":{"value":[0]},"Transform Position":{"value":[10,0,0]},"Transform Scale":{"value":[2,2,2]})") +
					  R"(,{"id":22,"def":"Channel/Split","in":{"In":{"from":[20,"Color"]}}})";
	const str initialize = SetFrom(10, "axisX", 20, "Normal") + "," + SetFrom(11, "axisY", 20, "Tangent") + "," + SetFrom(12, "axisZ", 20, "Bitangent") + "," +
						   SetFrom(13, "size", 20, "Bitangent Sign") + "," + SetFrom(14, "color", 20, "Color") + "," + SetFrom(15, "alpha", 22, "A") + "," +
						   SetFrom(16, "pivot", 20, "Tex Coord 0") + "," + SetFrom(17, "position", 21, "Position") + "," + SetFrom(18, "scale", 21, "Normal");
	BlockRun run;
	if (!Spawn(run, nodes, initialize, "MeshOutputs")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ExpectNear(Only(run, "axisX"), {0.0f, 1.0f, 0.0f}, "the normal is not normalized");
	ExpectNear(Only(run, "axisY"), {0.0f, 0.0f, 1.0f}, "the tangent");
	ExpectNear(Only(run, "axisZ"), {-3.0f, 0.0f, 0.0f}, "the bitangent is not the raw normal crossed with the tangent, times its sign");
	EXPECT_FLOAT_EQ(One(run, "size"), -1.0f);
	ExpectNear(Only(run, "color"), {0.0f, 1.0f, 0.0f}, "the color as stored");
	EXPECT_NEAR(One(run, "alpha"), 128.0f / 255.0f, 1.0e-6f);
	ExpectNear(Only(run, "pivot"), {0.75f, 0.125f, 0.0f}, "the UV");
	ExpectNear(Only(run, "position"), {12.0f, 4.0f, 6.0f}, "a position through the transform");
	ExpectNear(Only(run, "scale"), {0.0f, 0.0f, 2.0f}, "a normal through the transform is a vector, scaled and not renormalized");
}

TEST_F(MeshSamplingFixture, IndexWrapsByMode) {
	str nodes = R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}},
		{"id":31,"def":"Math/Add","in":{"A":{"from":[30,"spawnIndex"]},"B":{"value":[3,0,0,0]}}})";
	const char* modes[] = {"Wrap", "Clamp", "Mirror"};
	for (u32 i = 0; i < 3; ++i) {
		nodes += "," + SampleNode(20 + i, std::format(R"("Mode":"{}")", modes[i]), R"("Vertex":{"from":[31,"Out"]})");
	}
	const str initialize = SetFrom(10, "position", 20, "Position") + "," + SetFrom(11, "scale", 21, "Position") + "," + SetFrom(12, "pivot", 22, "Position") +
						   "," + SetFrom(13, "size", 30, "spawnIndex");
	BlockRun run;
	if (!Spawn(run, nodes, initialize, "MeshAddressing", 4)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const Vector<f32> spawns = Values(run, "size");
	const Vector<Float3> wrapped = VectorsOf(run, "position");
	const Vector<Float3> clamped = VectorsOf(run, "scale");
	const Vector<Float3> mirrored = VectorsOf(run, "pivot");
	ASSERT_EQ(spawns.size(), 4u);
	constexpr u32 WRAPPED[] = {3, 0, 1, 2};
	constexpr u32 MIRRORED[] = {3, 2, 1, 0};
	for (size_t i = 0; i < spawns.size(); ++i) {
		const u32 spawn = static_cast<u32>(spawns[i]);
		ASSERT_LT(spawn, 4u);
		ExpectNear(wrapped[i], Corner(WRAPPED[spawn]), "Wrap");
		ExpectNear(clamped[i], Corner(3), "Clamp");
		ExpectNear(mirrored[i], Corner(MIRRORED[spawn]), "Mirror");
	}
}

TEST_F(MeshSamplingFixture, CountsAndIndexReadMeshPortHolds) {
	const str nodes = std::format(R"({{"id":20,"def":"Sampling/MeshVertexCount","in":{{"Mesh":{{"value":"{0}"}}}}}},
		{{"id":21,"def":"Sampling/MeshIndexCount","in":{{"Mesh":{{"value":"{0}"}}}}}},
		{{"id":22,"def":"Sampling/MeshTriangleCount","in":{{"Mesh":{{"value":"{0}"}}}}}},
		{{"id":23,"def":"Sampling/SampleMeshIndex","in":{{"Mesh":{{"value":"{0}"}},"Index":{{"value":[4]}}}}}},
		{{"id":24,"def":"Sampling/SampleMeshIndex","in":{{"Mesh":{{"value":"{0}"}},"Index":{{"value":[7]}}}}}},
		{{"id":25,"def":"Sampling/SampleMeshIndex","in":{{"Mesh":{{"value":"{0}"}},"Index":{{"value":[11]}}}}}},
		{{"id":26,"def":"Sampling/MeshVertexCount","in":{{"Mesh":{{"value":null}}}}}},
		{{"id":27,"def":"Sampling/MeshIndexCount","in":{{"Mesh":{{"value":"00000000000000a6-00000000000000b6"}}}}}},
		{{"id":28,"def":"Sampling/SampleMesh","in":{{"Mesh":{{"value":null}}}}}},
		{{"id":40,"def":"Input/Vector3","in":{{"X":{{"from":[20,"Count"]}},"Y":{{"from":[21,"Count"]}},"Z":{{"from":[22,"Count"]}}}}}},
		{{"id":41,"def":"Input/Vector3","in":{{"X":{{"from":[23,"Index"]}},"Y":{{"from":[24,"Index"]}},"Z":{{"from":[25,"Index"]}}}}}},
		{{"id":42,"def":"Input/Vector3","in":{{"X":{{"from":[26,"Count"]}},"Y":{{"from":[27,"Count"]}}}}}})",
								  MESH_GUID);
	const str initialize = SetFrom(10, "axisX", 40, "Out") + "," + SetFrom(11, "axisY", 41, "Out") + "," + SetFrom(12, "axisZ", 42, "Out") + "," +
						   SetFrom(13, "position", 28, "Position");
	BlockRun run;
	if (!Spawn(run, nodes, initialize, "MeshCounts")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ExpectNear(Only(run, "axisX"), {4.0f, 6.0f, 2.0f}, "the vertex, index and triangle counts");
	ExpectNear(Only(run, "axisY"), {2.0f, 0.0f, 1.0f}, "Sample Mesh Index");
	ExpectNear(Only(run, "axisZ"), {0.0f, 0.0f, 0.0f}, "a cleared port or an unloaded mesh counted something");
	ExpectNear(Only(run, "position"), {0.0f, 0.0f, 0.0f}, "a cleared port sampled something");
}

TEST_F(MeshSamplingFixture, MeshPositionIsRandomOnMesh) {
	const auto run = [&](BlockRun& out, std::string_view placement, const char* name) {
		return Spawn(out, R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"direction"}})",
					 std::format(R"({{"id":10,"def":"Block/PositionMesh","settings":{{"Placement Mode":"{}"}},"in":{{"Mesh":{{"value":"{}"}}}}}},
						{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"scale"}},"in":{{"Scale":{{"from":[30,"direction"]}}}}}})",
								 placement, MESH_GUID),
					 name, 64);
	};
	BlockRun vertices;
	if (!run(vertices, "Vertex", "MeshRandomVertex")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	const Vector<Float3> placed = VectorsOf(vertices, "position");
	ASSERT_EQ(placed.size(), 64u);
	u32 met[4] = {};
	for (const Float3& p : placed) {
		bool on = false;
		for (u32 v = 0; v < 4; ++v) {
			const Float3 c = Corner(v);
			if (std::fabs(p.x - c.x) + std::fabs(p.y - c.y) + std::fabs(p.z - c.z) < 1.0e-5f) {
				on = true;
				++met[v];
			}
		}
		EXPECT_TRUE(on) << p.x << ", " << p.y << ", " << p.z;
	}
	for (u32 v = 0; v < 4; ++v) {
		EXPECT_GT(met[v], 0u) << "vertex " << v;
	}

	BlockRun edges;
	ASSERT_TRUE(run(edges, "Edge", "MeshRandomEdge"));
	for (const Float3& p : VectorsOf(edges, "position")) {
		bool on = false;
		for (u32 i = 0; i < 6; ++i) {
			const u32 next = (i + 1) % 3 == 0 ? i - 2 : i + 1;
			const Float3 a = Corner(CORNER_INDICES[i]);
			const Float3 b = Corner(CORNER_INDICES[next]);
			const Float3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
			const f32 t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y + (p.z - a.z) * ab.z) / (ab.x * ab.x + ab.y * ab.y + ab.z * ab.z);
			const Float3 q = Mix(a, b, t);
			on = on || (t >= -1.0e-5f && t <= 1.0f + 1.0e-5f && std::fabs(p.x - q.x) + std::fabs(p.y - q.y) + std::fabs(p.z - q.z) < 1.0e-4f);
		}
		EXPECT_TRUE(on) << p.x << ", " << p.y << ", " << p.z;
	}

	BlockRun surfaces;
	ASSERT_TRUE(run(surfaces, "Surface", "MeshRandomSurface"));
	u32 inTriangle[2] = {};
	for (const Float3& p : VectorsOf(surfaces, "position")) {
		bool inside = false;
		for (u32 triangle = 0; triangle < 2; ++triangle) {
			const Float3 a = Corner(CORNER_INDICES[triangle * 3]);
			const Float3 b = Corner(CORNER_INDICES[triangle * 3 + 1]);
			const Float3 c = Corner(CORNER_INDICES[triangle * 3 + 2]);
			const Vec3 e0(b.x - a.x, b.y - a.y, b.z - a.z);
			const Vec3 e1(c.x - a.x, c.y - a.y, c.z - a.z);
			const Vec3 ep(p.x - a.x, p.y - a.y, p.z - a.z);
			const f32 d00 = Vec3::Dot(e0, e0), d01 = Vec3::Dot(e0, e1), d11 = Vec3::Dot(e1, e1), d20 = Vec3::Dot(ep, e0), d21 = Vec3::Dot(ep, e1);
			const f32 denominator = d00 * d11 - d01 * d01;
			const f32 v = (d11 * d20 - d01 * d21) / denominator;
			const f32 w = (d00 * d21 - d01 * d20) / denominator;
			const Vec3 back = e0 * v + e1 * w;
			const bool hit = v >= -1.0e-4f && w >= -1.0e-4f && v + w <= 1.0f + 1.0e-4f && (back - ep).Length() < 1.0e-3f;
			inside = inside || hit;
			inTriangle[triangle] += hit ? 1u : 0u;
		}
		EXPECT_TRUE(inside) << p.x << ", " << p.y << ", " << p.z;
	}
	EXPECT_GT(inTriangle[0], 0u);
	EXPECT_GT(inTriangle[1], 0u);
	const Vector<Float3> directions = VectorsOf(surfaces, "scale");
	ASSERT_FALSE(directions.empty());
	for (const Float3& d : directions) {
		EXPECT_NEAR(d.x * d.x + d.y * d.y + d.z * d.z, 1.0f, 1.0e-4f);
	}
}

TEST_F(MeshSamplingFixture, MeshPositionOrientsAndComposes) {
	BlockRun axes;
	if (!Spawn(axes, "",
			   std::format(R"({{"id":10,"def":"Block/PositionMesh","settings":{{"Spawn Mode":"Custom","Apply Orientation":"Axes"}},
				"in":{{"Mesh":{{"value":"{}"}},"Vertex":{{"value":[0]}}}}}})",
						   MESH_GUID),
			   "MeshAxes")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ExpectNear(Only(axes, "position"), Corner(0), "the vertex's position");
	ExpectNear(Only(axes, "axisX"), {0.0f, 1.0f, 0.0f}, "axis X is not the normal crossed with the tangent");
	ExpectNear(Only(axes, "axisY"), {0.0f, 0.0f, 1.0f}, "axis Y is not the normal, normalized");
	ExpectNear(Only(axes, "axisZ"), {1.0f, 0.0f, 0.0f}, "axis Z is not the tangent made orthogonal");

	BlockRun added;
	ASSERT_TRUE(Spawn(added, R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"direction"}})",
					  std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[10,0,0]}}}}}},
				{{"id":11,"def":"Block/PositionMesh","settings":{{"Spawn Mode":"Custom","Placement Mode":"Edge","Composition Position":"Add"}},
				"in":{{"Mesh":{{"value":"{}"}},"Index":{{"value":[1]}},"Edge":{{"value":[0.25]}}}}}},
				{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"scale"}},"in":{{"Scale":{{"from":[30,"direction"]}}}}}})",
								  MESH_GUID),
					  "MeshAdded"));
	const Float3 on = Mix(Corner(0), Corner(1), 0.25f);
	ExpectNear(Only(added, "position"), {on.x + 10.0f, on.y, on.z}, "the edge's point was not added onto the position before it");
	ExpectNear(Only(added, "scale"), {0.0f, 0.4472136f, 0.8944272f}, "the direction is not the edge's normal, normalized", 1.0e-5f);
}

TEST_F(MeshSamplingFixture, UnloadedMeshReads0) {
	BlockRun run;
	if (!Begin(run, EffectWith(SampleNode(20, "", R"("Vertex":{"value":[1]})"), SetFrom(10, "position", 20, "Position"), ""), "MeshReload", {}, _staged)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	run.system.SetFrameData(Frame(run.effect, 0.0f, 0.1f, 1));
	const auto spawn = [&run]() {
		run.system.Advance(0.1f, 1);
		RenderSystem::RenderAll();
	};
	spawn();
	_mesh.OnUnload();
	spawn();
	_mesh.vertices[1].position = Vec3(40.0f, 50.0f, 60.0f);
	_mesh.OnLoad();
	spawn();
	Vector<Float3> placed = VectorsOf(run, "position");
	ASSERT_EQ(placed.size(), 3u);
	std::sort(placed.begin(), placed.end(), [](const Float3& a, const Float3& b) { return a.x < b.x; });
	ExpectNear(placed[0], {0.0f, 0.0f, 0.0f}, "a spawn while the mesh was unloaded read something");
	ExpectNear(placed[1], Corner(1), "the first spawn");
	ExpectNear(placed[2], {40.0f, 50.0f, 60.0f}, "a spawn after the reload read the mesh as it was");
}

TEST_F(GraphEffectLibraryFixture, SequentialPlacesEachId) {
	struct Placed {
		Float3 position;
		Float3 direction;
		Float3 target;
	};
	const auto place = [&](std::string_view name, std::string_view settings, std::string_view in, u32 count, Vector<Placed>& out,
						   std::string_view before = "") -> bool {
		BlockRun run;
		const str blocks = std::format(R"({}{{"id":10,"def":"Block/PositionSequential","settings":{},"in":{}}},
			{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"from":[20,"particleId"]}}}}}},
			{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"scale"}},"in":{{"Scale":{{"from":[21,"direction"]}}}}}},
			{{"id":13,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"from":[22,"targetPosition"]}}}}}})",
									   before, settings, in);
		const str nodes = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleId"}},
			{"id":21,"def":"Attribute/GetAttribute","settings":{"Attribute":"direction"}},
			{"id":22,"def":"Attribute/GetAttribute","settings":{"Attribute":"targetPosition"}})";
		if (!Begin(run, EffectWith(nodes, blocks, ""), name)) {
			return false;
		}
		Advance(run, 0.0f, 0.1f, {count});
		const Vector<f32> ids = Values(run, "size");
		const Vector<Float3> positions = VectorsOf(run, "position");
		const Vector<Float3> directions = VectorsOf(run, "scale");
		const Vector<Float3> targets = VectorsOf(run, "color");
		EXPECT_EQ(ids.size(), count) << name;
		out.assign(count, Placed{});
		for (size_t k = 0; k < ids.size() && k < positions.size() && k < directions.size() && k < targets.size(); ++k) {
			const u32 id = static_cast<u32>(ids[k]);
			if (id < count) {
				out[id] = {positions[k], directions[k], targets[k]};
			}
		}
		return ids.size() == count;
	};
	const auto expectNear = [](Float3 got, Float3 want, const char* what = "") {
		EXPECT_NEAR(got.x, want.x, 1.0e-4f) << what;
		EXPECT_NEAR(got.y, want.y, 1.0e-4f) << what;
		EXPECT_NEAR(got.z, want.z, 1.0e-4f) << what;
	};
	const auto line = [](f32 index, f32 last, Float3 start, Float3 end) {
		const f32 t = index / std::max(last, 1.0f);
		return Float3{start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t, start.z + (end.z - start.z) * t};
	};

	Vector<Placed> placed{GetMemoryResource(MemTag::Graphics)};
	{
		SCOPED_TRACE("Line, each addressing mode");
		constexpr f32 WRAP[] = {0, 1, 2, 3, 0, 1};
		constexpr f32 CLAMP[] = {0, 1, 2, 3, 3, 3};
		constexpr f32 MIRROR[] = {0, 1, 2, 1, 0, 1};
		const std::string_view modes[] = {"Wrap", "Clamp", "Mirror"};
		const f32* expected[] = {WRAP, CLAMP, MIRROR};
		const f32 counts[] = {4.0f, 4.0f, 3.0f};
		for (u32 mode = 0; mode < 3; ++mode) {
			SCOPED_TRACE(modes[mode]);
			const str settings = std::format(R"({{"Composition Position":"Set","Mode":"{}"}})", modes[mode]);
			const str in = std::format(R"({{"Count":{{"value":[{}]}},"Start":{{"value":[1,2,3]}},"End":{{"value":[4,2,3]}}}})", counts[mode]);
			if (!place(std::format("SequentialLine{}", modes[mode]), settings, in, 6, placed)) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
			for (u32 id = 0; id < 6; ++id) {
				expectNear(placed[id].position, line(expected[mode][id], counts[mode] - 1.0f, {1, 2, 3}, {4, 2, 3}), "a line's place");
				expectNear(placed[id].direction, {1.0f, 0.0f, 0.0f}, "a line's direction is not start to end");
			}
		}
	}
	{
		SCOPED_TRACE("Circle, wrapped");
		ASSERT_TRUE(place("SequentialCircle", R"({"Composition Position":"Set","Shape":"Circle","Mode":"Wrap"})",
						  R"({"Count":{"value":[8]},"Center":{"value":[1,2,3]},"Normal":{"value":[0,1,0]},"Up":{"value":[0,0,1]},"Radius":{"value":[2]}})", 10,
						  placed));
		for (u32 id = 0; id < 10; ++id) {
			const f64 turn = 2.0 * 3.14159265358979 * static_cast<f64>(id % 8) / 8.0;
			const Float3 around{static_cast<f32>(std::sin(turn)), 0.0f, static_cast<f32>(std::cos(turn))};
			expectNear(placed[id].position, {1.0f + 2.0f * around.x, 2.0f, 3.0f + 2.0f * around.z}, "a circle's place");
			expectNear(placed[id].direction, around, "a circle's direction is not away from its center");
		}
	}
	{
		SCOPED_TRACE("Circle, clamped");
		ASSERT_TRUE(place("SequentialCircleClamp", R"({"Composition Position":"Set","Shape":"Circle"})",
						  R"({"Count":{"value":[4]},"Normal":{"value":[0,1,0]},"Up":{"value":[0,0,1]},"Radius":{"value":[1]}})", 6, placed));
		expectNear(placed[4].position, placed[0].position, "a clamped circle's last place is not back at its first");
		expectNear(placed[5].position, placed[0].position, "a clamped circle went past its closing place");
		expectNear(placed[1].position, {1.0f, 0.0f, 0.0f}, "a quarter turn is not toward left");
	}
	{
		SCOPED_TRACE("ThreeDimensional, lone count at origin");
		ASSERT_TRUE(place("SequentialGrid", R"({"Composition Position":"Set","Shape":"ThreeDimensional","Mode":"Wrap"})",
						  R"({"Count X":{"value":[2]},"Count Y":{"value":[3]},"Count Z":{"value":[4]},"Origin":{"value":[1,1,1]}})", 24, placed));
		for (u32 id = 0; id < 24; ++id) {
			const Float3 t{static_cast<f32>(id / 12) * 2.0f - 1.0f, static_cast<f32>((id / 4) % 3) - 1.0f, static_cast<f32>(id % 4) / 3.0f * 2.0f - 1.0f};
			expectNear(placed[id].position, {1.0f + t.x, 1.0f + t.y, 1.0f + t.z}, "a grid's cell");
			const f32 length = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
			expectNear(placed[id].direction, {t.x / length, t.y / length, t.z / length}, "a grid's direction is not away from its origin");
		}
		ASSERT_TRUE(place("SequentialGridLone", R"({"Composition Position":"Set","Shape":"ThreeDimensional","Mode":"Wrap"})",
						  R"({"Count X":{"value":[1]},"Count Y":{"value":[2]},"Count Z":{"value":[1]}})", 2, placed));
		expectNear(placed[0].position, {0.0f, -1.0f, 0.0f}, "a lone count does not sit on its axis's origin");
		expectNear(placed[1].position, {0.0f, 1.0f, 0.0f});
	}
	{
		SCOPED_TRACE("Offset Index, target 1 place on");
		ASSERT_TRUE(place("SequentialOffset", R"({"Composition Position":"Set","Composition Target Position":"Set","Mode":"Wrap","Write Target Position":true})",
						  R"({"Count":{"value":[4]},"End":{"value":[3,0,0]},"Offset Index":{"value":[2]}})", 4, placed));
		for (u32 id = 0; id < 4; ++id) {
			expectNear(placed[id].position, {static_cast<f32>((id + 2) % 4), 0.0f, 0.0f}, "Offset Index did not move the place");
			expectNear(placed[id].target, {static_cast<f32>((id + 1) % 4), 0.0f, 0.0f}, "the target is not one place on");
		}
	}
	{
		SCOPED_TRACE("Add onto a set position");
		ASSERT_TRUE(place("SequentialAdd", "{}", R"({"Count":{"value":[4]},"End":{"value":[3,0,0]}})", 2, placed,
						  R"({"id":9,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,10,0]}}},)"));
		expectNear(placed[0].position, {0.0f, 10.0f, 0.0f}, "the place was not added onto the position before it");
		expectNear(placed[1].position, {1.0f, 10.0f, 0.0f});
	}
}

#endif

namespace {

	u32 Occurrences(const str& text, std::string_view needle) {
		u32 count = 0;
		for (size_t at = text.find(needle); at != str::npos; at = text.find(needle, at + needle.size())) {
			++count;
		}
		return count;
	}

} // anonymous namespace

TEST(GraphEffectLibraryTest, OutputCarriesDepthProgram) {
	for (std::string_view def : {GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_OUTPUT_MESH, GRAPH_EFFECT_OUTPUT_POINT, GRAPH_EFFECT_OUTPUT_LINE, GRAPH_EFFECT_OUTPUT_LIT,
								 GRAPH_EFFECT_OUTPUT_LIT_MESH}) {
		SCOPED_TRACE(def);
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
		ASSERT_NE(definition, nullptr);
		const GraphSettingDefinition* casts = nullptr;
		for (const GraphSettingDefinition& setting : definition->settings) {
			casts = setting.key == "Cast Shadows" ? &setting : casts;
		}
		ASSERT_NE(casts, nullptr);
		EXPECT_EQ(casts->defaultIndex, 0u);
		EXPECT_FALSE(casts->inGraph);
	}

	enum class Depth : u8 { Declared,
							Clips };
	struct Case {
		std::string_view def;
		const char* settings;
		bool casts;
		Depth depth;
		u32 clips;
	};
	const Case cases[] = {
		{GRAPH_EFFECT_OUTPUT, "{}", false, Depth::Declared, 0},
		{GRAPH_EFFECT_OUTPUT, R"({"Blend Mode":"Opaque"})", false, Depth::Declared, 0},
		{GRAPH_EFFECT_OUTPUT, R"({"Blend Mode":"Opaque","Use Alpha Clipping":true})", false, Depth::Clips, 4},
		{GRAPH_EFFECT_OUTPUT, R"({"Cast Shadows":true})", true, Depth::Clips, 3},
		{GRAPH_EFFECT_OUTPUT, R"({"Cast Shadows":true,"Use Alpha Clipping":true})", true, Depth::Clips, 4},
		{GRAPH_EFFECT_OUTPUT, R"({"Cast Shadows":true,"Blend Mode":"Opaque"})", true, Depth::Declared, 0},
		{GRAPH_EFFECT_OUTPUT_MESH, R"({"Cast Shadows":true,"Blend Mode":"Opaque"})", true, Depth::Declared, 0},
		{GRAPH_EFFECT_OUTPUT_POINT, R"({"Cast Shadows":true})", true, Depth::Clips, 3},
		{GRAPH_EFFECT_OUTPUT_LINE, R"({"Cast Shadows":true,"Blend Mode":"Opaque"})", true, Depth::Declared, 0},
		{GRAPH_EFFECT_OUTPUT_LIT, R"({"Cast Shadows":true})", true, Depth::Clips, 3},
		{GRAPH_EFFECT_OUTPUT_LIT_MESH, R"({"Blend Mode":"Opaque"})", false, Depth::Declared, 0},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(std::format("{} {}", entry.def, entry.settings));
		GraphEffect effect;
		str error;
		ASSERT_TRUE(CompileSorted(SortedBy(entry.def, entry.settings), effect, error)) << error;
		const GraphEffectOutput& output = effect.systems[0].outputs[0];
		EXPECT_EQ(output.castShadows, entry.casts);
		const bool declared = output.shader.find("#pragma shalltear_depth_program value\n") != str::npos;
		const bool clipping = output.shader.find("#pragma shalltear_depth_program clip value\n") != str::npos;
		EXPECT_EQ(declared, entry.depth == Depth::Declared);
		EXPECT_EQ(clipping, entry.depth == Depth::Clips);
		EXPECT_EQ(Occurrences(output.shader, "clip(sltColor.a - sltIn.threshold);"), entry.clips);
		EXPECT_NE(output.shader.find("#if defined(SHALLTEAR_DEPTH_PROGRAM)"), str::npos);
		EXPECT_NE(output.shader.find("return float4(sltNormal * 0.5 + 0.5, 1.0);"), str::npos);
		EXPECT_NE(output.shader.find("#if defined(SHALLTEAR_DEPTH_VALUE)"), str::npos);
		EXPECT_NE(output.shader.find("return sltParticleOutput.value;"), str::npos);
	}
}

TEST(GraphEffectLibraryTest, LitOutputUsesEngineIntegrator) {
	const GraphNodeDefinition* lit = GraphNodeLibrary::Find(GRAPH_EFFECT_OUTPUT_LIT);
	const GraphNodeDefinition* litMesh = GraphNodeLibrary::Find(GRAPH_EFFECT_OUTPUT_LIT_MESH);
	ASSERT_NE(lit, nullptr);
	ASSERT_NE(litMesh, nullptr);
	EXPECT_EQ(lit->name, "Output Particle Lit Primitive");
	EXPECT_EQ(litMesh->name, "Output Particle Lit Mesh");
	const auto setting = [](const GraphNodeDefinition& definition, std::string_view key) -> const GraphSettingDefinition* {
		for (const GraphSettingDefinition& candidate : definition.settings) {
			if (candidate.key == key) {
				return &candidate;
			}
		}
		return nullptr;
	};
	ASSERT_NE(setting(*lit, "Color Mode"), nullptr);
	EXPECT_EQ(setting(*lit, "Color Mode")->options[setting(*lit, "Color Mode")->defaultIndex], "BaseColor");
	ASSERT_NE(setting(*lit, "Receive Shadows"), nullptr);
	EXPECT_EQ(setting(*lit, "Receive Shadows")->defaultIndex, 1u);
	EXPECT_NE(setting(*lit, "Normal Bending"), nullptr);
	EXPECT_EQ(setting(*litMesh, "Normal Bending"), nullptr);
	for (const GraphPortDefinition& port : lit->inputs) {
		if (port.name == "Smoothness") {
			EXPECT_FLOAT_EQ(port.defaultValue[0], 0.5f);
		}
		if (port.name == "Normal Bending Factor") {
			EXPECT_FLOAT_EQ(port.defaultValue[0], 0.1f);
		}
	}

	const auto shown = [](std::string_view def, std::string_view settings) {
		const GraphDocument document = ReadGraph(SortedBy(def, settings));
		return ShownInputs(*GraphNodeLibrary::Find(def), document.contexts[2].node);
	};
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LIT, "{}"), Names({"Base Color Map", "Smoothness", "Metallic"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LIT, R"({"Color Mode":"None"})"), Names({"Base Color Map", "Smoothness", "Metallic", "Base Color"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LIT, R"({"Use Emissive":true})"), Names({"Base Color Map", "Smoothness", "Metallic", "Emissive Color"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LIT, R"({"Color Mode":"Emissive","Use Emissive":true})"),
			  Names({"Base Color Map", "Smoothness", "Metallic", "Base Color"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LIT, R"({"Use Base Color Map":"None","Use Normal Map":true,"Use Emissive Map":true,"Normal Bending":true})"),
			  Names({"Smoothness", "Metallic", "Normal Map", "Normal Scale", "Emissive Map", "Emissive Scale", "Normal Bending Factor"}));
	EXPECT_EQ(shown(GRAPH_EFFECT_OUTPUT_LIT_MESH, R"({"Use Occlusion Map":true,"Use Metallic Map":true})"),
			  Names({"Mesh", "Base Color Map", "Smoothness", "Metallic", "Occlusion Map", "Metallic Map"}));

	GraphEffect effect;
	str error;
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT, R"({"Use Normal Map":true,"Use Emissive Map":true})"), effect, error)) << error;
	const str& shader = effect.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("#include \"VisualEffects/ParticleLit.hlsli\""), str::npos);
	EXPECT_NE(shader.find("ShalltearShadeSurface(sltInput, sltSurface, sltNormal, SltParticleReceivesShadows(true, true))"), str::npos);
	EXPECT_NE(shader.find("[[vk::binding(2, 1)]] Texture2D normalMap;"), str::npos);
	EXPECT_NE(shader.find("[[vk::binding(4, 1)]] Texture2D emissiveMap;"), str::npos);
	EXPECT_NE(shader.find("#pragma shalltear_default normalMap normal"), str::npos);
	u32 slots = 0;
	for (const GraphEffectOutputTexture& texture : effect.systems[0].outputs[0].textures) {
		slots += texture.slot == "normalMap" || texture.slot == "emissiveMap" ? 1u : 0u;
	}
	EXPECT_EQ(slots, 2u);

	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT, R"({"Blend Mode":"Opaque","Receive Shadows":false})"), effect, error)) << error;
	const str& opaque = effect.systems[0].outputs[0].shader;
	EXPECT_NE(opaque.find("sltSurface.alpha = 1.0;"), str::npos);
	EXPECT_NE(opaque.find("SltParticleReceivesShadows(false, false)"), str::npos);
	const size_t depthInclude = opaque.find("#ifdef SHALLTEAR_DEPTH_PROGRAM\n#include \"Surface.hlsli\"\n#else\n#include \"VisualEffects/ParticleLit.hlsli\"");
	EXPECT_NE(depthInclude, str::npos);

	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT, R"({"Color Mode":"BaseColorAndEmissive"})"), effect, error)) << error;
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("sltSurface.emissive = sltEmission;"), str::npos);
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT, "{}"), effect, error)) << error;
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("sltEmission"), str::npos);

	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT_MESH, R"({"Double Sided":true})"), effect, error)) << error;
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("if (!sltFront) {"), str::npos);
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("#pragma shalltear_cull back"), str::npos);
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT_MESH, R"({"Double Sided":true,"Cull Mode":"Off"})"), effect, error)) << error;
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("#pragma shalltear_cull off"), str::npos);
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT_MESH, "{}"), effect, error)) << error;
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("#pragma shalltear_cull back"), str::npos);
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("if (!sltFront) {"), str::npos);

	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT, R"({"Use Base Color Map":"None","Smoothness Source":"AlbedoAlpha"})"), effect, error)) << error;
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("sltSmoothness *="), str::npos);
	ASSERT_TRUE(CompileSorted(SortedBy(GRAPH_EFFECT_OUTPUT_LIT, R"({"Smoothness Source":"AlbedoAlpha"})"), effect, error)) << error;
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("sltSmoothness *= sltTexel.a;"), str::npos);
}

TEST(GraphEffectLibraryTest, OutputsCarryRenderStatePragmas) {
	const std::string_view outputs[] = {GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_OUTPUT_MESH, GRAPH_EFFECT_OUTPUT_POINT, GRAPH_EFFECT_OUTPUT_LINE,
										GRAPH_EFFECT_OUTPUT_LIT, GRAPH_EFFECT_OUTPUT_LIT_MESH, GRAPH_EFFECT_OUTPUT_STRIP};
	const auto declares = [](std::string_view def, std::string_view key) {
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
		return definition && std::any_of(definition->settings.begin(), definition->settings.end(),
										 [key](const GraphSettingDefinition& setting) { return setting.key == key; });
	};
	for (const std::string_view def : outputs) {
		for (const std::string_view key : {GRAPH_EFFECT_CULL_MODE, GRAPH_EFFECT_Z_WRITE_MODE, GRAPH_EFFECT_Z_TEST_MODE}) {
			EXPECT_TRUE(declares(def, key)) << def << " has no " << key;
		}
	}
	for (const std::string_view def : {GRAPH_EFFECT_OUTPUT_NODE_SHADER, GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH, GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP}) {
		EXPECT_FALSE(declares(def, GRAPH_EFFECT_Z_TEST_MODE)) << def;
	}

	struct Row {
		std::string_view output;
		std::string_view settings;
		std::string_view depth;
		std::string_view compare;
		std::string_view cull;
	};
	const Row rows[] = {
		{GRAPH_EFFECT_OUTPUT, "", "test_only", "", "off"},
		{GRAPH_EFFECT_OUTPUT, R"("Blend Mode":"Opaque")", "test_write", "", "off"},
		{GRAPH_EFFECT_OUTPUT_MESH, "", "test_only", "", "back"},
		{GRAPH_EFFECT_OUTPUT, R"("Z Write Mode":"On","Z Test Mode":"Always")", "test_write", "always", "off"},
		{GRAPH_EFFECT_OUTPUT, R"("Blend Mode":"Opaque","Z Write Mode":"Off","Z Test Mode":"Greater")", "test_only", "farther", "off"},
		{GRAPH_EFFECT_OUTPUT, R"("Z Test Mode":"Less","Cull Mode":"Front")", "test_only", "nearer", "front"},
		{GRAPH_EFFECT_OUTPUT_MESH, R"("Z Test Mode":"GEqual","Cull Mode":"Off")", "test_only", "farther_or_equal", "off"},
		{GRAPH_EFFECT_OUTPUT_POINT, R"("Z Test Mode":"NotEqual","Cull Mode":"Back")", "test_only", "not_equal", "back"},
		{GRAPH_EFFECT_OUTPUT_LIT_MESH, R"("Double Sided":true)", "test_only", "", "back"},
		{GRAPH_EFFECT_OUTPUT, R"("Z Test Mode":"Equal")", "test_only", "equal", "off"},
	};
	for (const Row& row : rows) {
		SCOPED_TRACE(std::format("{} {{{}}}", row.output, row.settings));
		const str text = std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[]}},
			{{"id":3,"def":"{}","settings":{{{}}},"flow":[[2,"In"]],"blocks":[]}}]}})",
									 row.output, row.settings);
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(GraphEffectTarget::Compile(ReadGraph(text), "states.sleffect", effect, diagnostics));
		ASSERT_EQ(effect.systems.size(), 1u);
		const str& shader = effect.systems[0].outputs[0].shader;
		EXPECT_NE(shader.find(std::format("#pragma shalltear_depth {}\n", row.depth)), str::npos);
		EXPECT_NE(shader.find(std::format("#pragma shalltear_cull {}\n", row.cull)), str::npos);
		if (row.compare.empty()) {
			EXPECT_EQ(shader.find("#pragma shalltear_depth_compare"), str::npos);
		} else {
			EXPECT_NE(shader.find(std::format("#pragma shalltear_depth_compare {}\n", row.compare)), str::npos);
		}
	}

	constexpr std::string_view Unknown = R"({"_type":"EffectGraph","version":3,"nextId":100,"nodes":[],"contexts":[
		{"id":1,"def":"Context/Initialize","settings":{"Capacity":64},"blocks":[]},
		{"id":3,"def":"Context/OutputParticlePrimitive","settings":{"Z Test Mode":"Sometimes"},"flow":[[1,"In"]],"blocks":[]}]})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphEffectTarget::Compile(ReadGraph(Unknown), "states.sleffect", effect, diagnostics));
	ASSERT_FALSE(diagnostics.empty());
	EXPECT_EQ(diagnostics[0].node, 3u);
}
