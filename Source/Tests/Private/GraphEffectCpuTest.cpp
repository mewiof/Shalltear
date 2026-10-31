#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>

#ifndef SHALLTEAR_SHARED
#include "EffectDescription.h"
#include "EffectKernelHarness.h"
#include "ParticleCpuSystem.h"

#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Time.h"

#include <bit>
#include <cstdio>
#include <ranges>
#endif

using namespace Shalltear;

namespace {

	GraphDocument Parse(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	bool Compile(const str& text, GraphEffect& out, Vector<GraphDiagnostic>& diagnostics) {
		GraphDefinitions definitions;
		return GraphEffectTarget::Compile(Parse(text), "Cpu", out, diagnostics, &definitions);
	}

	str Said(const Vector<GraphDiagnostic>& diagnostics) {
		str all;
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			all += std::format("[node {}] {}\n", diagnostic.node, diagnostic.message);
		}
		return all;
	}

	bool SaidOn(const Vector<GraphDiagnostic>& diagnostics, u32 node, std::string_view words) {
		return std::any_of(diagnostics.begin(), diagnostics.end(),
						   [&](const GraphDiagnostic& diagnostic) { return diagnostic.node == node && diagnostic.message.find(words) != str::npos; });
	}

	str Effect(std::string_view settings, std::string_view initialize, std::string_view update = {}, std::string_view nodes = {}) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64{}}},"blocks":[{}]}},
{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}}]}})",
						   nodes, settings, initialize, update);
	}

	constexpr std::string_view CPU = R"(,"Simulation Target":"CPU")";

	str TriggeringGpuEvent(std::string_view settings) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[],"contexts":[
{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64{}}},"blocks":[]}},
{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{{"id":50,"def":"Block/TriggerEvent","settings":{{"Mode":"Always"}},"in":{{"Count":{{"value":[1]}}}}}}]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}},
{{"id":6,"def":"Context/GPUEvent","in":{{"Evt":{{"from":[50,"Evt"]}}}},"blocks":[]}},
{{"id":7,"def":"Context/Initialize","flow":[[6,"In"]],"settings":{{"Capacity":64}},"blocks":[]}},
{{"id":8,"def":"Context/OutputParticlePrimitive","flow":[[7,"In"]],"blocks":[]}}]}})",
						   settings);
	}

	str Sending(std::string_view settings, f32 from, f32 rise) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[],"contexts":[
{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":16{}}},"blocks":[
	{{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"lifetime"}},"in":{{"Lifetime":{{"value":[1]}}}}}},
	{{"id":11,"def":"Block/SetAttribute","settings":{{"Attribute":"position"}},"in":{{"Position":{{"value":[0,{},0]}}}}}},
	{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"value":[0,{},0]}}}}}}]}},
{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[
	{{"id":20,"def":"Block/CollisionShape","settings":{{"Collision Attributes":"WritePunctualContactOnly"}},"in":{{"Position":{{"value":[0,0,0]}}}}}},
	{{"id":21,"def":"Block/TriggerEvent","settings":{{"Mode":"OnDie"}},"in":{{"Count":{{"value":[1]}}}}}},
	{{"id":22,"def":"Block/TriggerEvent","settings":{{"Mode":"OnCollide"}},"in":{{"Count":{{"value":[3]}}}}}}]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}},
{{"id":4,"def":"Context/OutputEvent","settings":{{"Event Name":"Died"}},"in":{{"Evt":{{"from":[21,"Evt"]}}}},"blocks":[]}},
{{"id":5,"def":"Context/OutputEvent","settings":{{"Event Name":"Hit"}},"in":{{"Evt":{{"from":[22,"Evt"]}}}},"blocks":[]}}]}})",
						   settings, from, rise);
	}

	u32 Lines(std::string_view assembly, std::string_view prefix) {
		u32 count = 0;
		for (size_t at = 0; at < assembly.size();) {
			const size_t end = std::min(assembly.find('\n', at), assembly.size());
			count += assembly.substr(at, end - at).starts_with(prefix) ? 1u : 0u;
			at = end + 1;
		}
		return count;
	}

	constexpr std::string_view PARITY_INITIALIZE =
		R"({"id":10,"def":"Block/PositionShape","settings":{"Shape":"Sphere"},"in":{"Radius":{"value":[1]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity","Random":"PerComponent"},"in":{"A":{"value":[-1,0,-1]},"B":{"value":[1,2,1]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime","Random":"Uniform"},"in":{"A":{"value":[0.6]},"B":{"value":[1.4]}}},
{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"color","Random":"Uniform"},"in":{"A":{"value":[0,0,0]},"B":{"value":[1,1,1]}}})";
	constexpr std::string_view PARITY_UPDATE =
		R"({"id":20,"def":"Block/Gravity","in":{"Force":{"value":[0,-9.81,0],"space":"World"}}},
{"id":21,"def":"Block/LinearDrag"},
{"id":22,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[30,"r"]}}})";
	constexpr std::string_view PARITY_NODES = R"({"id":30,"def":"Random/RandomNumber","settings":{"Constant":false}})";

} // anonymous namespace

TEST(GraphEffectCpuTest, CpuSystemCompilesToPrograms) {
	const str update = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[30,"color"]}}})";
	const str nodes = R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"color"}})";
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,1,0]}}})";
	for (const bool cpu : {true, false}) {
		SCOPED_TRACE(cpu ? "CPU" : "GPU");
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(Compile(Effect(cpu ? CPU : "", initialize, update, nodes), effect, diagnostics)) << Said(diagnostics);
		ASSERT_EQ(effect.systems.size(), 1u);
		const GraphEffectSystem& system = effect.systems[0];
		ASSERT_EQ(system.outputs.size(), 1u);
		EXPECT_FALSE(system.outputs[0].shader.empty());
		EXPECT_EQ(system.cpu, cpu);
		if (!cpu) {
			EXPECT_FALSE(system.initializeKernel.empty());
			EXPECT_FALSE(system.updateKernel.empty());
			EXPECT_TRUE(system.initializeAssembly.empty() && system.updateAssembly.empty());
			continue;
		}
		EXPECT_TRUE(system.initializeKernel.empty() && system.updateKernel.empty());
		ASSERT_FALSE(system.stored.empty());
		for (const str* assembly : {&system.initializeAssembly, &system.updateAssembly}) {
			EXPECT_EQ(Lines(*assembly, "root "), system.stored.size() + 1) << *assembly;
		}
		const auto stores = [&](std::string_view name) {
			return std::any_of(system.stored.begin(), system.stored.end(), [name](const GraphEffectStored& stored) { return stored.attribute == name; });
		};
		EXPECT_TRUE(stores("position") && stores("velocity") && stores("lifetime") && stores("age"));
		EXPECT_FALSE(stores("color"));
		EXPECT_EQ(Lines(system.updateAssembly, "input 3 Attribute/position"), 1u) << system.updateAssembly;
		EXPECT_EQ(Lines(system.updateAssembly, "input 3 Attribute/color"), 0u) << system.updateAssembly;
		EXPECT_EQ(Lines(system.initializeAssembly, "input 3 Attribute/"), 0u) << system.initializeAssembly;
	}
}

TEST(GraphEffectCpuTest, CpuSystemRefusesKernelOnly) {
	struct Case {
		const char* what;
		str text;
		u32 node;
		const char* words;
	};
	const str customHLSL = R"({"id":50,"def":"Block/CustomHLSL","settings":{"HLSL Code":"void Lift(inout EffectAttributes attributes)\n{\n  attributes.size = 2;\n}"}})";
	const Case cases[] = {
		{"custom HLSL", Effect(CPU, customHLSL), 50, "simulates its system on the CPU"},
		{"the camera's depth", Effect(CPU, "", R"({"id":50,"def":"Block/CollisionDepth"})"), 50, "camera's depth"},
		{"a texture", Effect(CPU, R"({"id":50,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[60,"s"]}}})", "", R"({"id":60,"def":"Sampling/SampleTexture2D","in":{"Texture":{"value":"00000000000000a1-00000000000000b1"},"UV":{"value":[0.5,0.5]}}})"),
		 60, "samples a texture"},
		{"a matrix", Effect(CPU, "", R"({"id":50,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[62,"Out"]}}})",
							R"({"id":60,"def":"Random/RandomNumber","settings":{"Type":"Vector4","Constant":false}},
{"id":63,"def":"Random/RandomNumber","settings":{"Type":"Vector4","Constant":false}},
{"id":61,"def":"Math/MatrixConstruction","in":{"M0":{"from":[60,"r"]},"M1":{"from":[63,"r"]},"M2":{"from":[60,"r"]},"M3":{"from":[63,"r"]}}},
{"id":62,"def":"Math/MatrixDeterminant","in":{"In":{"from":[61,"4x4"]}}})"),
		 61, "can't hold"},
		{"a Trigger Event feeding a GPU Event", TriggeringGpuEvent(CPU), 50, "only a system simulated on the GPU triggers"},
		{"strips", Effect(R"(,"Data Type":"ParticleStrip","Simulation Target":"CPU")", ""), 1, "makes strips"},
	};
	for (const Case& refused : cases) {
		SCOPED_TRACE(refused.what);
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		EXPECT_FALSE(Compile(refused.text, effect, diagnostics));
		EXPECT_TRUE(SaidOn(diagnostics, refused.node, refused.words)) << Said(diagnostics);
		str gpu = refused.text;
		const size_t at = gpu.find(R"("Simulation Target":"CPU")");
		ASSERT_NE(at, str::npos);
		gpu.replace(at, std::string_view(R"("Simulation Target":"CPU")").size(), R"("Simulation Target":"GPU")");
		diagnostics.clear();
		Compile(gpu, effect, diagnostics);
		EXPECT_FALSE(SaidOn(diagnostics, refused.node, "CPU")) << Said(diagnostics);
	}

	const str family = R"({"_type":"EffectGraph","version":4,"nextId":100,"nodes":[],"contexts":[
{"id":7,"def":"Context/Spawn","blocks":[{"id":40,"def":"Block/ConstantSpawnRate","in":{"Rate":{"value":[10]}}}]},
{"id":1,"def":"Context/Initialize","flow":[[7,"In"]],"settings":{"Capacity":64},"blocks":[]},
{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"Always"},"in":{"Count":{"value":[1]}}}]},
{"id":6,"def":"Context/GPUEvent","in":{"Evt":{"from":[20,"Evt"]}},"blocks":[]},
{"id":3,"def":"Context/Initialize","flow":[[6,"In"]],"settings":{"Capacity":64,"Simulation Target":"CPU"},"blocks":[]},
{"id":4,"def":"Context/OutputParticlePrimitive","flow":[[3,"In"]],"blocks":[]}]})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(family, effect, diagnostics));
	EXPECT_TRUE(SaidOn(diagnostics, 3, "is fed by a GPU Event")) << Said(diagnostics);
}

TEST(GraphEffectCpuTest, NoisesAndMeshsSamplesCompileForCpu) {
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[60,"Noise"]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[61,"Noise"]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[62,"Out"]}}},
{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[63,"Out"]}}},
{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"pivot"},"in":{"Pivot":{"from":[64,"Out"]}}},
{"id":15,"def":"Block/PositionMesh","settings":{"Placement Mode":"Surface"},"in":{"Mesh":{"value":"00000000000000c6-00000000000000d6"}}},
{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"from":[65,"Position"]}}})";
	const str update = R"({"id":20,"def":"Block/Turbulence","settings":{"Noise Type":"Value"}})";
	const str nodes = R"({"id":59,"def":"Attribute/GetAttribute","settings":{"Attribute":"position"}},
{"id":60,"def":"Noise/Noise","settings":{"Type":"Cellular","Dimensions":"Three"},"in":{"Coordinate":{"from":[59,"position"]}}},
{"id":61,"def":"Noise/CurlNoise","settings":{"Type":"Perlin","Dimensions":"Two"}},
{"id":62,"def":"Procedural/GradientNoise","settings":{"Hash Type":"LegacyMod"},"in":{"UV":{"from":[59,"position"]}}},
{"id":63,"def":"Procedural/SimpleNoise","settings":{"Hash Type":"LegacySine"},"in":{"UV":{"from":[59,"position"]}}},
{"id":64,"def":"Math/RandomRange","in":{"Seed":{"from":[59,"position"]}}},
{"id":65,"def":"Sampling/SampleMesh","settings":{"Placement Mode":"Edge"},"in":{"Mesh":{"value":"00000000000000c6-00000000000000d6"}}})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(CPU, initialize, update, nodes), effect, diagnostics)) << Said(diagnostics);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_EQ(Lines(system.updateAssembly, "curlnoise 3 0 "), 1u) << system.updateAssembly;
	const std::string_view ops[] = {"fractalnoise 4 2 ", "curlnoise 2 1 ", "gradientnoise 1 2 ", "valuenoise 1 1 ", "sin ", "meshindex 1 0 ", "meshvertex 3 0 "};
	for (const std::string_view op : ops) {
		EXPECT_GE(Lines(system.initializeAssembly, op), 1u) << "no '" << op << "' line:\n"
															<< system.initializeAssembly;
	}
	EXPECT_EQ(Lines(system.initializeAssembly, "mesh "), 2u) << system.initializeAssembly;
}

TEST(GraphEffectCpuTest, CpuSystemsTriggerEventsSendOutputEvents) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Sending(CPU, 1.0f, 2.0f), effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 1u);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_TRUE(system.initializeOutputEvents.empty() && system.initializeCarried.empty());
	ASSERT_EQ(system.updateOutputEvents.size(), 2u);
	EXPECT_EQ(system.updateOutputEvents[0].name, "Died");
	EXPECT_EQ(system.updateOutputEvents[1].name, "Hit");
	const u32 row = static_cast<u32>(system.stored.size()) + 1;
	EXPECT_EQ(system.updateOutputEvents[0].root, row);
	EXPECT_EQ(system.updateOutputEvents[1].root, row + 1);
	const auto carries = [&](std::string_view name, u32& root) {
		for (const GraphEffectCarried& carried : system.updateCarried) {
			if (carried.attribute == name) {
				root = carried.root;
				return true;
			}
		}
		return false;
	};
	u32 root = 0;
	ASSERT_TRUE(carries("position", root));
	EXPECT_LT(root, row);
	EXPECT_TRUE(carries("collisionEventPosition", root) && carries("collisionEventNormal", root));
	ASSERT_TRUE(carries("hasCollisionEvent", root));
	EXPECT_GE(root, row + 2);
	EXPECT_FALSE(carries("color", root));
	u32 roots = 0;
	for (const GraphEffectCarried& carried : system.updateCarried) {
		roots = std::max(roots, carried.root + 1);
	}
	EXPECT_EQ(Lines(system.updateAssembly, "root "), roots) << system.updateAssembly;

	str gpu = Sending("", 1.0f, 2.0f);
	diagnostics.clear();
	EXPECT_FALSE(Compile(gpu, effect, diagnostics));
	EXPECT_TRUE(SaidOn(diagnostics, 4, "the GPU simulates that")) << Said(diagnostics);
	EXPECT_TRUE(SaidOn(diagnostics, 5, "the GPU simulates that")) << Said(diagnostics);

	str stray = Sending(CPU, 1.0f, 2.0f);
	const size_t at = stray.find(R"("from":[22,"Evt"])");
	ASSERT_NE(at, str::npos);
	stray.replace(at, std::string_view(R"("from":[22,"Evt"])").size(), R"("from":[12,"Evt"])");
	diagnostics.clear();
	EXPECT_FALSE(Compile(stray, effect, diagnostics));
	EXPECT_TRUE(SaidOn(diagnostics, 5, "isn't a Trigger Event")) << Said(diagnostics);
}

#ifndef SHALLTEAR_SHARED

namespace {

	using namespace VisualEffectsImpl;

	bool Describe(const GraphEffect& effect, EffectDescription& out) {
		out = EffectDescription{};
		out.batchesOffset = effect.batchesOffset;
		out.timing.cullSimulation = (effect.timing.cullingFlags & GRAPH_EFFECT_CULL_SIMULATION) != 0;
		out.timing.cullBoundsUpdate = (effect.timing.cullingFlags & GRAPH_EFFECT_CULL_BOUNDS_UPDATE) != 0;
		for (const GraphEffectSystem& compiled : effect.systems) {
			EffectSystem system;
			system.capacity = compiled.capacity;
			system.world = compiled.world;
			system.attributeCount = compiled.attributeCount;
			for (const GraphEffectStored& stored : compiled.stored) {
				system.stored.push_back({stored.attribute, stored.slot, stored.components});
			}
			for (const GraphEffectField& field : compiled.source) {
				EffectField source{field.name, field.offset, field.components};
				if (const GraphEffectAttributeInfo* attribute = GraphEffectAttributes::Find(field.name)) {
					std::copy(std::begin(attribute->defaultValue), std::end(attribute->defaultValue), source.defaultValue);
				}
				system.source.push_back(std::move(source));
			}
			system.batchStride = compiled.batchStride;
			system.cpu = compiled.cpu;
			system.initialize = EFFECT_NO_BINDING;
			system.update = EFFECT_NO_BINDING;
			system.boundsMode = static_cast<EffectBoundsMode>(compiled.boundsMode);
			system.measuresBounds = compiled.measuresBounds;
			system.initializeBoundsRoot = compiled.initializeBoundsRoot;
			system.updateBoundsRoot = compiled.updateBoundsRoot;
			for (const GraphEffectOutputEvent& event : compiled.initializeOutputEvents) {
				system.initializeOutputEvents.push_back({event.name, event.root});
			}
			for (const GraphEffectOutputEvent& event : compiled.updateOutputEvents) {
				system.updateOutputEvents.push_back({event.name, event.root});
			}
			for (const GraphEffectCarried& carried : compiled.initializeCarried) {
				system.initializeCarried.push_back({carried.attribute, carried.root, carried.components});
			}
			for (const GraphEffectCarried& carried : compiled.updateCarried) {
				system.updateCarried.push_back({carried.attribute, carried.root, carried.components});
			}
			str error;
			if (!EffectPrograms::Assemble(compiled.initializeAssembly, system.initializeProgram, error) ||
				!EffectPrograms::Assemble(compiled.updateAssembly, system.updateProgram, error) ||
				!EffectPrograms::Assemble(compiled.boundsAssembly, system.bounds, error)) {
				ADD_FAILURE() << "CPU program didn't assemble: " << error;
				return false;
			}
			out.systems.push_back(std::move(system));
		}
		for (const GraphEffectMesh& mesh : effect.meshes) {
			out.meshes.push_back({mesh.binding, mesh.offset, mesh.mesh});
		}
		str error;
		if (!EffectDescriptions::Validate(out, 0, error) || !EffectDescriptions::Bind(out, error)) {
			ADD_FAILURE() << "description refused: " << error;
			return false;
		}
		return true;
	}

	Vector<EffectValue> Slots(const EffectDescription& description, f32 time, f32 delta, std::span<const EffectMeshView> meshes = {}) {
		Vector<EffectValue> slots{GetMemoryResource(MemTag::Graphics)};
		slots.resize(EffectSlotCount(description));
		slots[EFFECT_SLOT_TIME] = EffectValue{{time, std::sin(time), std::cos(time), delta}};
		slots[EFFECT_SLOT_SMOOTH_DELTA] = EffectValue{{delta, 0.0f, 0.0f, 0.0f}};
		for (u32 i = 0; i < meshes.size() && i < description.meshes.size(); ++i) {
			slots[EffectMeshSlot(description, i, false)] = EffectValue{{static_cast<f32>(meshes[i].vertexCount), 0.0f, 0.0f, 0.0f}};
			slots[EffectMeshSlot(description, i, true)] = EffectValue{{static_cast<f32>(meshes[i].indexCount), 0.0f, 0.0f, 0.0f}};
		}
		return slots;
	}

	constexpr std::string_view PYRAMID_GUID = "00000000000000c6-00000000000000d6";

	constexpr f32 TURNED[12] = {0.0f, -1.0f, 0.0f, 1.0f, 2.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.5f, 3.0f};

	void FillPyramid(MeshAsset& mesh) {
		constexpr f32 POSITIONS[5][3] = {{0.0f, 1.5f, 0.0f}, {-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}, {-1.0f, 0.0f, 1.0f}};
		for (u32 i = 0; i < 5; ++i) {
			MeshVertex vertex;
			vertex.position = Vec3(POSITIONS[i][0], POSITIONS[i][1], POSITIONS[i][2]);
			vertex.normal = Vec3(POSITIONS[i][0], 1.0f, POSITIONS[i][2]).Normalized();
			vertex.tangent = Vec3(1.0f, 0.0f, -POSITIONS[i][0]).Normalized();
			vertex.tangentSign = i % 2 == 0 ? 1.0f : -1.0f;
			vertex.color = Color32{static_cast<u8>(40 * i), static_cast<u8>(255 - 30 * i), 128, static_cast<u8>(200 + i)};
			vertex.u = 0.2f * static_cast<f32>(i);
			vertex.v = 1.0f - 0.15f * static_cast<f32>(i);
			mesh.vertices.push_back(vertex);
		}
		const u32 indices[] = {0, 2, 1, 0, 3, 2, 0, 4, 3, 0, 1, 4, 1, 2, 3, 1, 3, 4};
		mesh.indices.assign(std::begin(indices), std::end(indices));
		mesh.submeshes.push_back({0, 18, 0, AABB::Empty()});
	}

	Vector<EffectMeshView> Views(const EffectDescription& description, std::span<const EffectKernelHarness::StagedMesh> staged) {
		Vector<EffectMeshView> views{GetMemoryResource(MemTag::Graphics)};
		views.resize(description.meshes.size());
		for (u32 i = 0; i < description.meshes.size(); ++i) {
			for (const EffectKernelHarness::StagedMesh& mesh : staged) {
				if (mesh.guid == description.meshes[i].mesh) {
					views[i] = {mesh.mesh->vertices.data(), static_cast<u32>(mesh.mesh->vertices.size()), mesh.mesh->indices.data(),
								static_cast<u32>(mesh.mesh->indices.size())};
				}
			}
		}
		return views;
	}

	Vector<u8> FrameFor(const GraphEffect& effect, f32 time, f32 delta, std::span<const u32> batches, u32 seed, u32 idBase,
						std::span<const f32> localToWorld) {
		const u32 stride = effect.systems[0].batchStride;
		Vector<u8> bytes{GetMemoryResource(MemTag::Graphics)};
		bytes.resize(effect.batchesOffset + std::max<size_t>(batches.size(), 1) * stride, 0);
		const f32 clock[4] = {time, std::sin(time), std::cos(time), delta};
		std::memcpy(bytes.data(), clock, sizeof(clock));
		const u32 count = static_cast<u32>(batches.size());
		std::memcpy(bytes.data() + 20, &count, 4);
		std::memcpy(bytes.data() + 24, &idBase, 4);
		std::memcpy(bytes.data() + 28, &seed, 4);
		f32 matrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
		if (localToWorld.size() == 12) {
			std::memcpy(matrix, localToWorld.data(), 12 * sizeof(f32));
		}
		std::memcpy(bytes.data() + 32, matrix, sizeof(matrix));
		for (u32 i = 0; i < count; ++i) {
			std::memcpy(bytes.data() + effect.batchesOffset + i * stride, &batches[i], 4);
		}
		return bytes;
	}

	struct Agreement {
		u32 compared = 0;
		u32 disagreed = 0;
		u32 particles = 0;
		f32 worst = 0.0f;
		str first;
	};

	struct Parity {
		std::span<const f32> localToWorld;
		u32 frames = 20;
		u32 every = 5;
		u32 first[2] = {20, 12};
		u32 trickle = 3;
		f32 delta = 0.05f;
		f32 tolerance = 1e-4f;
		u32 seed = 1234;
		std::span<const EffectKernelHarness::StagedMesh> meshes;
	};

	class GraphEffectCpuFixture : public EffectKernelHarness::GraphEffectBlockFixture {
	protected:
		static Vector<u32> Words(const ParticleSystem& system, u32 attributeCount) {
			RHI::Device& device = *RenderSystem::GetDevice();
			device.WaitIdle();
			Vector<u32> words{GetMemoryResource(MemTag::Graphics)};
			words.resize(static_cast<size_t>(system.GetCapacity()) * attributeCount);
			device.ReadbackBuffer(system.GetAttributesBuffer(), {reinterpret_cast<u8*>(words.data()), words.size() * 4}, system.GetAttributesOffset());
			return words;
		}

		bool ModuliDivideAsIEEE(u32& inexact) {
			constexpr f32 OFFSETS[3] = {635.298681f, 682.357502f, 668.926525f};
			constexpr f32 SCALES[3] = {48.500388f, 65.294118f, 63.934599f};
			str nodes = R"({"id":60,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndex"}})";
			for (u32 i = 0; i < 3; ++i) {
				nodes += std::format(R"(,{{"id":{},"def":"Math/Multiply","in":{{"A":{{"from":[60,"spawnIndex"]}},"B":{{"value":[{}]}}}}}})", 61 + 2 * i,
									 SCALES[i]);
				nodes += std::format(R"(,{{"id":{},"def":"Math/Add","in":{{"A":{{"from":[{},"Out"]}},"B":{{"value":[{}]}}}}}})", 62 + 2 * i, 61 + 2 * i,
									 OFFSETS[i]);
			}
			nodes += R"(,{"id":70,"def":"Input/Vector3","in":{"X":{"from":[62,"Out"]},"Y":{"from":[64,"Out"]},"Z":{"from":[66,"Out"]}}},
{"id":71,"def":"Math/Divide","in":{"A":{"value":[1,1,1]},"B":{"from":[70,"Out"]}}})";
			const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[70,"Out"]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"from":[71,"Out"]}}})";
			str text = Effect("", initialize, "", nodes);
			text.replace(text.find(R"("Capacity":64)"), std::string_view(R"("Capacity":64)").size(), R"("Capacity":128)");
			EffectKernelHarness::BlockRun run;
			if (!Begin(run, text, "Moduli")) {
				return false;
			}
			constexpr u32 CELLS = 69;
			Advance(run, 0.0f, 0.1f, {CELLS});
			inexact = 0;
			for (u32 c = 0; c < 3; ++c) {
				const Vector<f32> sums = Values(run, "position", c);
				const Vector<f32> quotients = Values(run, "velocity", c);
				EXPECT_EQ(sums.size(), CELLS);
				for (size_t i = 0; i < sums.size() && i < quotients.size(); ++i) {
					inexact += std::bit_cast<u32>(1.0f / sums[i]) == std::bit_cast<u32>(quotients[i]) ? 0u : 1u;
				}
			}
			return true;
		}

		bool BothTargets(std::string_view initialize, std::string_view update, std::string_view nodes, std::string_view name, const Parity& parity,
						 Agreement& out) {
			EffectKernelHarness::BlockRun gpu;
			if (!Begin(gpu, Effect("", initialize, update, nodes), str(name) + "Gpu", {}, parity.meshes)) {
				return false;
			}
			GraphEffect effect;
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			if (!Compile(Effect(CPU, initialize, update, nodes), effect, diagnostics)) {
				ADD_FAILURE() << name << " doesn't compile for the CPU:\n"
							  << Said(diagnostics);
				return true;
			}
			EffectDescription description;
			ParticleCpuSystem cpu;
			if (!Describe(effect, description) || !cpu.Build(description, 0)) {
				ADD_FAILURE() << name << ": no CPU system built";
				return true;
			}
			const Vector<EffectMeshView> views = Views(description, parity.meshes);
			const EffectSystem& layout = description.systems[0];
			const GraphEffectSystem& compiled = gpu.effect.systems[0];
			if (layout.attributeCount != compiled.attributeCount) {
				ADD_FAILURE() << name << ": targets store differently";
				return true;
			}
			u32 idSlot = ~0u;
			for (const EffectStored& stored : layout.stored) {
				idSlot = stored.attribute == "particleId" ? stored.slot : idSlot;
			}
			if (idSlot == ~0u) {
				ADD_FAILURE() << name << ": particles store no id";
				return true;
			}
			u32 idBase = 0;
			for (u32 frame = 0; frame < parity.frames; ++frame) {
				const f32 time = parity.delta * static_cast<f32>(frame);
				Vector<u32> batches{GetMemoryResource(MemTag::Graphics)};
				if (frame == 0) {
					batches = {parity.first[0], parity.first[1]};
				} else if (parity.trickle > 0) {
					batches = {parity.trickle};
				}
				u32 spawned = 0;
				for (u32 count : batches) {
					spawned += count;
				}
				const Vector<u8> block = FrameFor(gpu.effect, time, parity.delta, batches, parity.seed, idBase, parity.localToWorld);
				gpu.system.SetFrameData(block);
				gpu.system.Advance(parity.delta, spawned);
				RenderSystem::RenderAll();
				const Vector<u8> cpuBlock = FrameFor(effect, time, parity.delta, batches, parity.seed, idBase, parity.localToWorld);
				cpu.Advance(description, cpuBlock, Slots(description, time, parity.delta, views), spawned, views);
				idBase += spawned;
				if ((frame + 1) % parity.every != 0 && frame + 1 != parity.frames) {
					continue;
				}
				Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
				Alive(gpu.system, indices);
				const Vector<u32> words = Words(gpu.system, compiled.attributeCount);
				HashMap<u32, u32> byId{GetMemoryResource(MemTag::Graphics)};
				for (const u32 index : indices) {
					byId[words[static_cast<size_t>(idSlot) * compiled.capacity + index]] = index;
				}
				if (indices.size() != cpu.GetAlive().size()) {
					ADD_FAILURE() << name << ", frame " << frame << ": GPU alive " << indices.size() << ", CPU alive " << cpu.GetAlive().size();
					return true;
				}
				out.particles = std::max(out.particles, static_cast<u32>(indices.size()));
				for (const u32 index : cpu.GetAlive()) {
					const u32 id = std::bit_cast<u32>(cpu.Read(idSlot, index));
					const auto found = byId.find(id);
					if (found == byId.end()) {
						ADD_FAILURE();
						return true;
					}
					for (const EffectStored& stored : layout.stored) {
						for (u32 c = 0; c < stored.components; ++c) {
							const f32 mine = cpu.Read(stored.slot + c, index);
							const f32 theirs = std::bit_cast<f32>(words[static_cast<size_t>(stored.slot + c) * compiled.capacity + found->second]);
							++out.compared;
							const f32 difference = std::fabs(mine - theirs) / std::max(1.0f, std::fabs(theirs));
							out.worst = std::max(out.worst, difference);
							if (difference <= parity.tolerance) {
								continue;
							}
							if (out.disagreed++ < 8) {
								out.first += std::format("  frame {}, particle {}'s {}[{}]: the VM {}, the kernel {}\n", frame, id, stored.attribute, c, mine, theirs);
							}
						}
					}
				}
			}
			return true;
		}
	};

} // anonymous namespace

TEST(GraphEffectCpuVMTest, CpuSystemMatchesKernel) {
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[1]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,2,0]}}})";
	str text = Effect(CPU, initialize);
	text.replace(text.find(R"("Capacity":64)"), std::string_view(R"("Capacity":64)").size(), R"("Capacity":16)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(text, effect, diagnostics)) << Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	ParticleCpuSystem cpu;
	ASSERT_TRUE(cpu.Build(description, 0));
	u32 positionY = ~0u;
	for (const EffectStored& stored : description.systems[0].stored) {
		positionY = stored.attribute == "position" ? stored.slot + 1 : positionY;
	}
	ASSERT_NE(positionY, ~0u);

	const u32 twenty[] = {20};
	const Vector<u8> spawn = FrameFor(effect, 0.0f, 0.25f, twenty, 0, 0, {});
	cpu.Advance(description, spawn, Slots(description, 0.0f, 0.25f), 20);
	EXPECT_EQ(cpu.GetAlive().size(), 16u);
	EXPECT_EQ(cpu.GetDropped(), 4u);
	for (const u32 index : cpu.GetAlive()) {
		EXPECT_EQ(cpu.Read(positionY, index), 0.0f);
	}
	const Vector<u8> quiet = FrameFor(effect, 0.25f, 0.25f, {}, 0, 20, {});
	for (u32 frame = 1; frame <= 5; ++frame) {
		cpu.Advance(description, quiet, Slots(description, 0.25f * frame, 0.25f), 0);
		if (frame < 5) {
			ASSERT_EQ(cpu.GetAlive().size(), 16u);
			for (const u32 index : cpu.GetAlive()) {
				EXPECT_EQ(cpu.Read(positionY, index), 0.5f * frame) << "update " << frame;
			}
		}
	}
	EXPECT_EQ(cpu.GetAlive().size(), 0u);
	cpu.Advance(description, spawn, Slots(description, 1.5f, 0.25f), 20);
	EXPECT_EQ(cpu.GetAlive().size(), 16u);
	EXPECT_EQ(cpu.GetDropped(), 8u);
}

TEST(GraphEffectCpuVMTest, ChunkedVMMatches) {
	str text = Effect(CPU, PARITY_INITIALIZE, str(PARITY_UPDATE) + R"(,{"id":23,"def":"Block/TriggerEvent","settings":{"Mode":"OnDie"},"in":{"Count":{"value":[1]}}})",
					  PARITY_NODES);
	text.replace(text.find(R"("Capacity":64)"), std::string_view(R"("Capacity":64)").size(), R"("Capacity":32768)");
	constexpr std::string_view LastOutput = R"("def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]})";
	ASSERT_NE(text.find(LastOutput), str::npos);
	text.insert(text.find(LastOutput) + LastOutput.size(), R"(,{"id":4,"def":"Context/OutputEvent","in":{"Evt":{"from":[23,"Evt"]}},"blocks":[]})");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(text, effect, diagnostics)) << Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	ASSERT_EQ(JobSystem::GetWorkerCount(), 0u);

	constexpr u32 FILLING = 8;
	constexpr u32 FRAMES = 16;
	constexpr u32 SPAWNS = 4096;
	const auto hash = [](const ParticleCpuSystem& cpu) {
		u64 h = 1469598103934665603ull;
		const auto mix = [&h](std::span<const u32> words) {
			for (const u32 word : words) {
				h = (h ^ word) * 1099511628211ull;
			}
		};
		mix(cpu.GetAlive());
		mix(cpu.GetAttributes());
		for (const ParticleCpuEvent& event : cpu.GetEvents()) {
			const u32 words[] = {event.update ? 1u : 0u, event.event, event.count, event.values};
			mix(words);
		}
		for (const f32 value : cpu.GetEventValues()) {
			const u32 word = std::bit_cast<u32>(value);
			mix({&word, 1});
		}
		return h;
	};
	struct Run {
		ParticleCpuSystem cpu;
		u64 hashes[FRAMES] = {};
		f64 seconds = 0.0;
		u64 updated = 0;
		u32 lanes = 0;
		u32 sent = 0;
	};
	const auto run = [&](Run& out) {
		ASSERT_TRUE(out.cpu.Build(description, 0));
		u32 idBase = 0;
		f32 time = 0.0f;
		for (u32 frame = 0; frame < FRAMES; ++frame) {
			const u32 spawned = frame < FILLING ? SPAWNS : 0u;
			const u32 batch[] = {spawned};
			const f32 delta = 0.04f * static_cast<f32>(1 + frame % 3);
			time += delta;
			const Vector<u8> block = FrameFor(effect, time, delta, spawned > 0 ? std::span<const u32>(batch) : std::span<const u32>(), 77, idBase, {});
			const Vector<EffectValue> slots = Slots(description, time, delta);
			const u64 living = out.cpu.GetAlive().size();
			Stopwatch watch;
			watch.Start();
			out.cpu.Advance(description, block, slots, spawned);
			watch.Stop();
			if (frame >= FILLING) {
				out.seconds += watch.GetElapsedSeconds();
				out.updated += living;
				out.lanes = std::max(out.lanes, out.cpu.GetLanesUsed());
			}
			idBase += spawned;
			out.hashes[frame] = hash(out.cpu);
			out.sent += static_cast<u32>(out.cpu.GetEvents().size());
		}
	};
	Run alone;
	run(alone);
	Run chunked;
	JobSystem::Initialize();
	run(chunked);
	const u32 workers = JobSystem::GetWorkerCount();
	JobSystem::Terminate();

	EXPECT_EQ(alone.lanes, 1u);
	EXPECT_GT(chunked.lanes, 1u);
	ASSERT_GT(alone.cpu.GetAlive().size(), 4096u);
	ASSERT_GT(alone.sent, 1000u);
	EXPECT_EQ(alone.sent, chunked.sent);
	for (u32 frame = 0; frame < FRAMES; ++frame) {
		EXPECT_EQ(alone.hashes[frame], chunked.hashes[frame]) << "frame " << frame;
	}
	EXPECT_TRUE(std::ranges::equal(alone.cpu.GetAlive(), chunked.cpu.GetAlive()));
	EXPECT_TRUE(std::ranges::equal(alone.cpu.GetAttributes(), chunked.cpu.GetAttributes()));
	EXPECT_EQ(alone.cpu.GetDropped(), chunked.cpu.GetDropped());
	std::printf("[ THROUGHPUT ] the parity graph's VM: %.0f particles a second on the main thread; %.0f on %u lanes (%u workers)\n",
				static_cast<f64>(alone.updated) / std::max(alone.seconds, 1e-9), static_cast<f64>(chunked.updated) / std::max(chunked.seconds, 1e-9),
				chunked.lanes, workers);
}

TEST(GraphEffectCpuVMTest, CpuParticlesSendHitAndDeath) {
	for (const bool rising : {true, false}) {
		SCOPED_TRACE(rising ? "rising" : "falling");
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(Compile(Sending(CPU, 1.0f, rising ? 2.0f : -2.0f), effect, diagnostics)) << Said(diagnostics);
		EffectDescription description;
		ASSERT_TRUE(Describe(effect, description));
		const EffectSystem& system = description.systems[0];
		const auto offsetOf = [&](std::string_view name) {
			u32 offset = 0;
			for (const EffectCarried& carried : system.updateCarried) {
				if (carried.attribute == name) {
					return offset;
				}
				offset += carried.components;
			}
			ADD_FAILURE() << name << " isn't carried";
			return 0u;
		};
		const u32 position = offsetOf("position");
		const u32 contact = offsetOf("collisionEventPosition");
		const u32 normal = offsetOf("collisionEventNormal");
		ParticleCpuSystem cpu;
		ASSERT_TRUE(cpu.Build(description, 0));

		const u32 four[] = {4};
		cpu.Advance(description, FrameFor(effect, 0.0f, 0.25f, four, 0, 0, {}), Slots(description, 0.0f, 0.25f), 4);
		EXPECT_TRUE(cpu.GetEvents().empty());
		u32 hits = 0;
		u32 deaths = 0;
		for (u32 frame = 1; frame <= 6; ++frame) {
			SCOPED_TRACE(std::format("frame {}", frame));
			const f32 time = 0.25f * static_cast<f32>(frame);
			cpu.Advance(description, FrameFor(effect, time, 0.25f, {}, 0, 4, {}), Slots(description, time, 0.25f), 0);
			const std::span<const f32> values = cpu.GetEventValues();
			for (const ParticleCpuEvent& event : cpu.GetEvents()) {
				ASSERT_TRUE(event.update);
				const f32* carried = values.data() + event.values;
				if (system.updateOutputEvents[event.event].name == "Hit") {
					++hits;
					EXPECT_EQ(event.count, 3u);
					EXPECT_NEAR(carried[contact + 1], 0.0f, 1e-5f);
					EXPECT_EQ(carried[normal + 1], 1.0f);
				} else {
					++deaths;
					EXPECT_EQ(frame, 5u);
					EXPECT_EQ(event.count, 1u);
					if (rising) {
						EXPECT_EQ(carried[position + 1], 3.5f);
					}
				}
			}
			if (frame == 5) {
				EXPECT_TRUE(cpu.GetAlive().empty());
			}
		}
		EXPECT_EQ(hits, rising ? 0u : 4u);
		EXPECT_EQ(deaths, 4u);
	}
}

TEST(GraphEffectCpuVMTest, CpuSystemCooksWhole) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(CPU, R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[1]}}})"),
						effect, diagnostics))
		<< Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	str error;
	{
		EffectDescription kernel = description;
		kernel.systems[0].initialize = 0;
		EXPECT_FALSE(EffectDescriptions::Validate(kernel, 2, error));
	}
	{
		EffectDescription shortRow = description;
		shortRow.systems[0].updateProgram.roots.pop_back();
		EXPECT_FALSE(EffectDescriptions::Validate(shortRow, 0, error));
	}
}

TEST(GraphEffectCpuVMTest, CpuSystemSamplesMeshPortHolds) {
	const str nodes = std::format(R"({{"id":60,"def":"Attribute/GetAttribute","settings":{{"Attribute":"spawnIndex"}}}},
{{"id":61,"def":"Sampling/SampleMesh","in":{{"Mesh":{{"value":"{0}"}},"Vertex":{{"from":[60,"spawnIndex"]}}}}}},
{{"id":62,"def":"Sampling/SampleMeshIndex","in":{{"Mesh":{{"value":"{0}"}},"Index":{{"from":[60,"spawnIndex"]}}}}}})",
								  PYRAMID_GUID);
	const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[61,"Position"]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[62,"Index"]}}})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(CPU, initialize, "", nodes), effect, diagnostics)) << Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	ASSERT_EQ(description.meshes.size(), 2u);
	u32 position = ~0u;
	u32 size = ~0u;
	for (const EffectStored& stored : description.systems[0].stored) {
		position = stored.attribute == "position" ? stored.slot : position;
		size = stored.attribute == "size" ? stored.slot : size;
	}
	ASSERT_TRUE(position != ~0u && size != ~0u);
	MeshAsset pyramid;
	FillPyramid(pyramid);
	const EffectKernelHarness::StagedMesh staged[] = {{Guid::FromString(PYRAMID_GUID), &pyramid}};
	constexpr u32 SPAWNS = 24;
	const u32 batch[] = {SPAWNS};
	for (const bool loaded : {true, false}) {
		SCOPED_TRACE(loaded ? "loaded" : "not loaded");
		const Vector<EffectMeshView> views = Views(description, loaded ? std::span(staged) : std::span<const EffectKernelHarness::StagedMesh>());
		ParticleCpuSystem cpu;
		ASSERT_TRUE(cpu.Build(description, 0));
		cpu.Advance(description, FrameFor(effect, 0.0f, 0.1f, batch, 5, 0, {}), Slots(description, 0.0f, 0.1f, views), SPAWNS, views);
		ASSERT_EQ(cpu.GetAlive().size(), SPAWNS);
		for (u32 k = 0; k < SPAWNS; ++k) {
			const u32 index = cpu.GetAlive()[k];
			const Vec3 want = loaded ? pyramid.vertices[std::min(k, 4u)].position : Vec3(0.0f, 0.0f, 0.0f);
			EXPECT_EQ(Vec3(cpu.Read(position, index), cpu.Read(position + 1, index), cpu.Read(position + 2, index)), want) << "spawn " << k;
			const f32 entry = loaded ? static_cast<f32>(pyramid.indices[k % pyramid.indices.size()]) : 0.0f;
			EXPECT_EQ(cpu.Read(size, index), entry);
		}
	}
}

TEST_F(GraphEffectCpuFixture, TargetsAgreeForOneSeed) {
	Parity parity;
	parity.localToWorld = TURNED;
	Agreement agreement;
	if (!BothTargets(PARITY_INITIALIZE, PARITY_UPDATE, PARITY_NODES, "Parity", parity, agreement)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EXPECT_EQ(agreement.disagreed, 0u) << "of " << agreement.compared << " floats:\n"
									   << agreement.first;
	EXPECT_GT(agreement.compared, 500u);
	EXPECT_GT(agreement.particles, 8u);
}

TEST_F(GraphEffectCpuFixture, TurbulenceAgreesOnBothTargets) {
	constexpr std::string_view TYPES[] = {"Value", "Perlin", "Cellular"};
	for (const std::string_view type : TYPES) {
		SCOPED_TRACE(type);
		const str update = std::format(
			R"({{"id":24,"def":"Block/Turbulence","settings":{{"Noise Type":"{}"}},"in":{{"Angles":{{"value":[20,35,-10]}},"Scale":{{"value":[1.5,0.75,2]}},"Intensity":{{"value":[6]}},"Octaves":{{"value":[3]}},"Frequency":{{"value":[1.7]}}}}}},{})",
			type, PARITY_UPDATE);
		Parity parity;
		parity.localToWorld = TURNED;
		parity.tolerance = 1e-3f;
		Agreement agreement;
		if (!BothTargets(PARITY_INITIALIZE, update, PARITY_NODES, std::format("Turbulence{}", type), parity, agreement)) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		EXPECT_EQ(agreement.disagreed, 0u) << "of " << agreement.compared << ' ' << agreement.worst << ":\n"
										   << agreement.first;
		EXPECT_GT(agreement.compared, 500u);
	}
}

TEST_F(GraphEffectCpuFixture, EveryNoiseAgreesOnBothTargets) {
	u32 inexact = 0;
	if (!ModuliDivideAsIEEE(inexact)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	constexpr std::string_view ID_UPDATE = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"pivot"},"in":{"Pivot":{"from":[39,"r"]}}})";
	constexpr std::string_view ID_NODE = R"({"id":39,"def":"Random/RandomNumber","settings":{"Constant":false}})";
	constexpr std::string_view TYPES[] = {"Value", "Perlin", "Cellular"};
	Parity parity;
	parity.frames = 1;
	parity.first[0] = 40;
	parity.first[1] = 24;
	const auto expect = [&](std::string_view initialize, std::string_view nodes, const str& name, u32 floats) {
		Agreement agreement;
		ASSERT_TRUE(BothTargets(initialize, ID_UPDATE, nodes, name, parity, agreement));
		EXPECT_EQ(agreement.disagreed, 0u) << "of " << agreement.compared << ' ' << agreement.worst << ":\n"
										   << agreement.first;
		EXPECT_EQ(agreement.particles, 64u);
		EXPECT_GT(agreement.compared, 64u * floats);
	};
	for (const std::string_view type : TYPES) {
		SCOPED_TRACE(type);
		const str planar = std::format(R"({0},
{{"id":30,"def":"Random/RandomNumber","settings":{{"Type":"Vector3"}},"in":{{"Min":{{"value":[-10,-10,-10]}},"Max":{{"value":[10,10,10]}}}}}},
{{"id":31,"def":"Random/RandomNumber","settings":{{"Type":"Vector2"}},"in":{{"Min":{{"value":[-10,-10]}},"Max":{{"value":[10,10]}}}}}},
{{"id":32,"def":"Random/RandomNumber","in":{{"Min":{{"value":[-10]}},"Max":{{"value":[10]}}}}}},
{{"id":41,"def":"Noise/Noise","settings":{{"Type":"{1}","Dimensions":"Two"}},"in":{{"Coordinate":{{"from":[31,"r"]}},"Octaves":{{"value":[2]}}}}}},
{{"id":42,"def":"Noise/Noise","settings":{{"Type":"{1}","Dimensions":"One"}},"in":{{"Coordinate":{{"from":[32,"r"]}},"Octaves":{{"value":[2]}}}}}},
{{"id":43,"def":"Noise/CurlNoise","settings":{{"Type":"{1}","Dimensions":"Three"}},"in":{{"Coordinate":{{"from":[30,"r"]}},"Octaves":{{"value":[2]}}}}}},
{{"id":44,"def":"Noise/CurlNoise","settings":{{"Type":"{1}","Dimensions":"Two"}},"in":{{"Coordinate":{{"from":[31,"r"]}},"Octaves":{{"value":[2]}}}}}},
{{"id":45,"def":"Input/Vector3","in":{{"X":{{"from":[41,"Noise"]}},"Y":{{"from":[42,"Noise"]}}}}}})",
									   ID_NODE, type);
		const str planarBlocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"from":[43,"Noise"]}}},
{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[41,"Derivatives"]}}},
{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[45,"Out"]}}},
{"id":15,"def":"Block/SetAttribute","settings":{"Attribute":"axisX"},"in":{"AxisX":{"from":[44,"Noise"]}}},
{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"axisY"},"in":{"AxisY":{"from":[42,"Derivatives"]}}})";
		expect(planarBlocks, planar, std::format("NoisesPlanar{}", type), 12);
		if (inexact > 0) {
			continue;
		}
		const str volume = std::format(R"({0},
{{"id":30,"def":"Random/RandomNumber","settings":{{"Type":"Vector3"}},"in":{{"Min":{{"value":[-10,-10,-10]}},"Max":{{"value":[10,10,10]}}}}}},
{{"id":40,"def":"Noise/Noise","settings":{{"Type":"{1}","Dimensions":"Three"}},"in":{{"Coordinate":{{"from":[30,"r"]}},"Octaves":{{"value":[2]}}}}}})",
									   ID_NODE, type);
		const str volumeBlocks = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[40,"Derivatives"]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[40,"Noise"]}}})";
		expect(volumeBlocks, volume, std::format("NoisesVolume{}", type), 4);
	}
	if (inexact > 0) {
		std::printf("[ NOISE ] this device divides %u of the 3D hash's 207 moduli otherwise than IEEE does. 3D noises weren't compared\n", inexact);
	}
	{
		SCOPED_TRACE("Node Shader");
		const str nodes = std::format(R"({},
{{"id":31,"def":"Random/RandomNumber","settings":{{"Type":"Vector2"}},"in":{{"Min":{{"value":[-20,-20]}},"Max":{{"value":[20,20]}}}}}},
{{"id":50,"def":"Procedural/SimpleNoise","in":{{"UV":{{"from":[31,"r"]}},"Scale":{{"value":[3]}}}}}},
{{"id":51,"def":"Procedural/GradientNoise","in":{{"UV":{{"from":[31,"r"]}},"Scale":{{"value":[1.3]}}}}}},
{{"id":52,"def":"Procedural/GradientNoise","settings":{{"Hash Type":"LegacyMod"}},"in":{{"UV":{{"from":[31,"r"]}},"Scale":{{"value":[1.3]}}}}}},
{{"id":53,"def":"Procedural/Voronoi","in":{{"UV":{{"from":[31,"r"]}},"AngleOffset":{{"value":[2]}},"CellDensity":{{"value":[0.7]}}}}}},
{{"id":54,"def":"Input/Vector3","in":{{"X":{{"from":[50,"Out"]}},"Y":{{"from":[51,"Out"]}},"Z":{{"from":[52,"Out"]}}}}}})",
									  ID_NODE);
		const str initialize = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[10]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"from":[54,"Out"]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[53,"Out"]}}},
{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[53,"Cells"]}}})";
		expect(initialize, nodes, "ShaderNoises", 5);
	}
}

TEST_F(GraphEffectCpuFixture, AMeshsSamplesAgreeOnBothTargets) {
	EffectKernelHarness::LoadableMesh pyramid;
	FillPyramid(pyramid);
	pyramid.OnLoad();
	const EffectKernelHarness::StagedMesh staged[] = {{Guid::FromString(PYRAMID_GUID), &pyramid}};
	const str nodes = std::format(R"({{"id":39,"def":"Random/RandomNumber","settings":{{"Constant":false}}}},
{{"id":30,"def":"Random/RandomNumber","in":{{"Min":{{"value":[0]}},"Max":{{"value":[12]}}}}}},
{{"id":61,"def":"Sampling/SampleMesh","settings":{{"Mode":"Wrap","Output Normal":true,"Output Tangent":true,"Output Bitangent":true}},"in":{{"Mesh":{{"value":"{0}"}},"Vertex":{{"from":[30,"r"]}}}}}})",
								  PYRAMID_GUID);
	const str initialize = std::format(R"({{"id":10,"def":"Block/SetAttribute","settings":{{"Attribute":"lifetime"}},"in":{{"Lifetime":{{"value":[10]}}}}}},
{{"id":11,"def":"Block/PositionMesh","settings":{{"Placement Mode":"Surface"}},"in":{{"Mesh":{{"value":"{0}"}}}}}},
{{"id":12,"def":"Block/SetAttribute","settings":{{"Attribute":"velocity"}},"in":{{"Velocity":{{"from":[61,"Normal"]}}}}}},
{{"id":13,"def":"Block/SetAttribute","settings":{{"Attribute":"color"}},"in":{{"Color":{{"from":[61,"Color"]}}}}}},
{{"id":14,"def":"Block/SetAttribute","settings":{{"Attribute":"scale"}},"in":{{"Scale":{{"from":[61,"Tangent"]}}}}}},
{{"id":15,"def":"Block/SetAttribute","settings":{{"Attribute":"axisX"}},"in":{{"AxisX":{{"from":[61,"Bitangent"]}}}}}},
{{"id":16,"def":"Block/SetAttribute","settings":{{"Attribute":"axisY"}},"in":{{"AxisY":{{"from":[61,"Tex Coord 0"]}}}}}},
{{"id":17,"def":"Block/SetAttribute","settings":{{"Attribute":"axisZ"}},"in":{{"AxisZ":{{"from":[61,"Position"]}}}}}})",
									   PYRAMID_GUID);
	const str update = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[39,"r"]}}})";
	Parity parity;
	parity.frames = 1;
	parity.first[0] = 40;
	parity.first[1] = 24;
	parity.meshes = staged;
	Agreement agreement;
	if (!BothTargets(initialize, update, nodes, "MeshSamples", parity, agreement)) {
		pyramid.OnUnload();
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	EXPECT_EQ(agreement.disagreed, 0u) << "of " << agreement.compared << " floats:\n"
									   << agreement.first;
	EXPECT_EQ(agreement.particles, 64u);
	EXPECT_GT(agreement.compared, 64u * 18u);
	RenderSystem::GetDevice()->WaitIdle();
	pyramid.OnUnload();
}

TEST_F(GraphEffectCpuFixture, CpuSystemFeedsBuffersOutputsDraw) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(CPU, PARITY_INITIALIZE, PARITY_UPDATE, PARITY_NODES), effect, diagnostics)) << Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	ParticleCpuSystem cpu;
	ASSERT_TRUE(cpu.Build(description, 0));
	ShaderAsset output;
	if (!Import(output, effect.systems[0].outputs[0].shader, "Fed.Output.hlsl")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	MaterialAsset material;
	material.shader = Handle<ShaderAsset>(&output);
	ParticleSystemDesc desc;
	desc.capacity = effect.systems[0].capacity;
	desc.attributeCount = effect.systems[0].attributeCount;
	desc.cpu = true;
	desc.outputs.push_back({});
	desc.outputs[0].material = Handle<MaterialAsset>(&material);
	ParticleSystem fed;
	ASSERT_TRUE(fed.Create(desc));

	u32 idBase = 0;
	const auto frame = [&](u32 spawn, bool feed) {
		const u32 batch[] = {spawn};
		const Vector<u8> block = FrameFor(effect, 0.1f * static_cast<f32>(cpu.GetFrame()), 0.1f, spawn > 0 ? std::span<const u32>(batch) : std::span<const u32>(),
										  7, idBase, {});
		idBase += spawn;
		fed.SetFrameData(block);
		fed.Advance(0.1f, spawn);
		if (feed) {
			cpu.Advance(description, block, Slots(description, 0.0f, 0.1f), spawn);
			fed.Feed(cpu.GetAlive(), cpu.GetAttributes(), cpu.GetDropped());
		}
		RenderSystem::RenderAll();
	};
	frame(24, true);
	frame(6, true);
	EXPECT_TRUE(fed.GetCounts().known);
	EXPECT_EQ(fed.GetCounts().alive, cpu.GetAlive().size());

	const auto matches = [&](const char* when) {
		SCOPED_TRACE(when);
		Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
		Alive(fed, indices);
		ASSERT_EQ(indices.size(), cpu.GetAlive().size());
		EXPECT_TRUE(std::equal(indices.begin(), indices.end(), cpu.GetAlive().begin()));
		const Vector<u32> words = Words(fed, desc.attributeCount);
		EXPECT_TRUE(std::equal(words.begin(), words.end(), cpu.GetAttributes().begin()));
		u32 draw[4] = {};
		RenderSystem::GetDevice()->ReadbackBuffer(fed.GetArgumentsBuffer(), {reinterpret_cast<u8*>(draw), sizeof(draw)}, fed.GetArgumentsOffset() + 12);
		EXPECT_EQ(draw[0], 6u * static_cast<u32>(cpu.GetAlive().size()));
	};
	matches("fed");
	frame(0, false);
	matches("a frame not fed");
}

namespace {

	AABB ParticleSphere(const Vec3& position, f32 size, const Vec3& scale, const Vec3& angle, const Vec3& pivot) {
		constexpr f32 DEGREES = 3.14159265358979f / 180.0f;
		const f32 s[3] = {std::sin(angle.x * DEGREES), std::sin(angle.y * DEGREES), std::sin(angle.z * DEGREES)};
		const f32 c[3] = {std::cos(angle.x * DEGREES), std::cos(angle.y * DEGREES), std::cos(angle.z * DEGREES)};
		const f32 euler[3][3] = {{c[1] * c[2] + s[0] * s[1] * s[2], c[2] * s[0] * s[1] - c[1] * s[2], c[0] * s[1]},
								 {c[0] * s[2], c[0] * c[2], -s[0]},
								 {-c[2] * s[1] + c[1] * s[0] * s[2], c[1] * c[2] * s[0] + s[1] * s[2], c[0] * c[1]}};
		const f32 size3[3] = {size * scale.x, size * scale.y, size * scale.z};
		const f32 laidPivot[3] = {pivot.x, pivot.y, pivot.z};
		f32 laid[3] = {};
		f32 lengths = 0.0f;
		for (u32 column = 0; column < 3; ++column) {
			for (u32 row = 0; row < 3; ++row) {
				const f32 m = euler[row][column] * size3[column];
				laid[row] += m * laidPivot[column];
				lengths += m * m;
			}
		}
		const f32 radius = 0.5f * std::sqrt(lengths);
		const Vec3 center(position.x - laid[0], position.y - laid[1], position.z - laid[2]);
		return AABB(center - Vec3(radius, radius, radius), center + Vec3(radius, radius, radius));
	}

	constexpr std::string_view CLOUD_NODES = R"({"id":30,"def":"Random/RandomNumber","settings":{"Type":"Vector3"},"in":{"Min":{"value":[-3,-1,-2]},"Max":{"value":[4,2,1]}}},
{"id":31,"def":"Random/RandomNumber","in":{"Min":{"value":[0.1]},"Max":{"value":[0.5]}}})";
	constexpr std::string_view CLOUD_INITIALIZE = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[1]}}},
{"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"from":[30,"r"]}}},
{"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[31,"r"]}}},
{"id":13,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[4,0,0]}}},
{"id":14,"def":"Block/SetAttribute","settings":{"Attribute":"pivot"},"in":{"Pivot":{"value":[0.5,0,0]}}},
{"id":15,"def":"Block/SetAttribute","settings":{"Attribute":"angle"},"in":{"Angle":{"value":[0,0,90]}}},
{"id":16,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"value":[2,1,1]}}})";
	constexpr std::string_view AUTOMATIC = R"(,"Bounds Setting Mode":"Automatic")";

	str Cloud(std::string_view settings) {
		str text = Effect(settings, CLOUD_INITIALIZE, "", CLOUD_NODES);
		text.replace(text.find(R"("Capacity":64)"), std::string_view(R"("Capacity":64)").size(), R"("Capacity":256)");
		return text;
	}

	template <typename Read>
	AABB Union(u32 particles, Read&& read) {
		AABB box = AABB::Empty();
		for (u32 p = 0; p < particles; ++p) {
			const auto v3 = [&](std::string_view name) { return Vec3(read(name, 0, p), read(name, 1, p), read(name, 2, p)); };
			box.Encapsulate(ParticleSphere(v3("position"), read("size", 0, p), v3("scale"), v3("angle"), v3("pivot")));
		}
		return box;
	}

	void ExpectNear(const AABB& measured, const AABB& expected, const char* what) {
		SCOPED_TRACE(what);
		for (u32 axis = 0; axis < 3; ++axis) {
			EXPECT_NEAR(measured.min[axis], expected.min[axis], 1e-4f) << axis;
			EXPECT_NEAR(measured.max[axis], expected.max[axis], 1e-4f) << axis;
		}
	}

} // anonymous namespace

TEST_F(GraphEffectCpuFixture, AutoBoxMatchesOnBothTargets) {
	EffectKernelHarness::BlockRun gpu;
	if (!Begin(gpu, Cloud(AUTOMATIC), "BoundsGpu")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ASSERT_TRUE(gpu.effect.systems[0].measuresBounds);
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Cloud(str(CPU) + str(AUTOMATIC)), effect, diagnostics)) << Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	ParticleCpuSystem cpu;
	ASSERT_TRUE(cpu.Build(description, 0));
	const EffectSystem& layout = description.systems[0];
	const auto cpuRead = [&](std::string_view name, u32 component, u32 particle) {
		for (const EffectStored& stored : layout.stored) {
			if (stored.attribute == name) {
				return cpu.Read(stored.slot + component, cpu.GetAlive()[particle]);
			}
		}
		ADD_FAILURE() << "CPU system stores no " << name;
		return 0.0f;
	};

	u32 idBase = 0;
	const auto frame = [&](u32 spawn, f32 delta) {
		const u32 batch[] = {spawn};
		const std::span<const u32> batches = spawn > 0 ? std::span<const u32>(batch) : std::span<const u32>();
		const Vector<u8> block = FrameFor(gpu.effect, 0.0f, delta, batches, 99, idBase, {});
		gpu.system.SetFrameData(block);
		gpu.system.Advance(delta, spawn);
		RenderSystem::RenderAll();
		const Vector<u8> cpuBlock = FrameFor(effect, 0.0f, delta, batches, 99, idBase, {});
		cpu.Advance(description, cpuBlock, Slots(description, 0.0f, delta), spawn);
		idBase += spawn;
		ASSERT_TRUE(cpu.GetMeasuredBounds().known);
		const u32 living = static_cast<u32>(cpu.GetAlive().size());
		ExpectNear(cpu.GetMeasuredBounds().box, Union(living, cpuRead), "the CPU's box");
	};
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
	const auto gpuRead = [&](std::string_view name, u32 component, u32 particle) {
		for (const GraphEffectStored& stored : gpu.effect.systems[0].stored) {
			if (stored.attribute == name) {
				return Attribute(gpu.system, stored.slot + component, indices[particle]);
			}
		}
		ADD_FAILURE() << "GPU system stores no " << name;
		return 0.0f;
	};
	const auto gpuBox = [&](u32 living, const char* what) {
		for (u32 i = 0; i <= RHI::FRAMES_IN_FLIGHT; ++i) {
			RenderSystem::RenderAll();
		}
		ASSERT_TRUE(gpu.system.GetMeasuredBounds().known);
		ASSERT_EQ(Alive(gpu.system, indices), living);
		ExpectNear(gpu.system.GetMeasuredBounds().box, Union(living, gpuRead), what);
	};

	frame(200, 0.05f);
	ASSERT_EQ(cpu.GetAlive().size(), 200u);
	gpuBox(200, "the GPU's box of a frame's spawns");
	frame(16, 0.05f);
	frame(16, 0.05f);
	ASSERT_EQ(cpu.GetAlive().size(), 232u);
	gpuBox(232, "the GPU's box of a frame's spawns and survivors");

	frame(0, 2.0f);
	EXPECT_TRUE(cpu.GetAlive().empty());
	EXPECT_TRUE(cpu.GetMeasuredBounds().known);
	EXPECT_FALSE(cpu.GetMeasuredBounds().box.IsValid());
	for (u32 i = 0; i <= RHI::FRAMES_IN_FLIGHT; ++i) {
		RenderSystem::RenderAll();
	}
	EXPECT_TRUE(gpu.system.GetMeasuredBounds().known);
	EXPECT_FALSE(gpu.system.GetMeasuredBounds().box.IsValid());
}

TEST_F(GraphEffectCpuFixture, ARecordedBoxHoldsEveryParticle) {
	GraphEffectCompileOptions recording;
	recording.recordBounds = true;
	const str text = Cloud("");
	EffectKernelHarness::BlockRun gpu;
	if (!Begin(gpu, text, "Recorded", {}, {}, recording)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ASSERT_TRUE(gpu.effect.systems[0].measuresBounds);
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(EffectKernelHarness::ParseEffect(Cloud(CPU)), "RecordedCpu", effect, diagnostics, nullptr, recording))
		<< Said(diagnostics);
	EffectDescription description;
	ASSERT_TRUE(Describe(effect, description));
	ParticleCpuSystem cpu;
	ASSERT_TRUE(cpu.Build(description, 0));
	const EffectSystem& layout = description.systems[0];
	const auto cpuRead = [&](std::string_view name, u32 component, u32 particle) {
		for (const EffectStored& stored : layout.stored) {
			if (stored.attribute == name) {
				return cpu.Read(stored.slot + component, cpu.GetAlive()[particle]);
			}
		}
		ADD_FAILURE() << "CPU system stores no " << name;
		return 0.0f;
	};
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
	const auto gpuRead = [&](std::string_view name, u32 component, u32 particle) {
		for (const GraphEffectStored& stored : gpu.effect.systems[0].stored) {
			if (stored.attribute == name) {
				return Attribute(gpu.system, stored.slot + component, indices[particle]);
			}
		}
		ADD_FAILURE() << "GPU system stores no " << name;
		return 0.0f;
	};
	AABB gpuRecorded = AABB::Empty();
	AABB gpuEvery = AABB::Empty();
	AABB cpuRecorded = AABB::Empty();
	AABB cpuEvery = AABB::Empty();
	const auto record = [](AABB& into, const ParticleBounds& measured) {
		if (measured.known && measured.box.IsValid()) {
			into.Encapsulate(measured.box);
		}
	};
	u32 idBase = 0;
	for (u32 frame = 0; frame < 20; ++frame) {
		const u32 batch[] = {12};
		const Vector<u8> block = FrameFor(gpu.effect, 0.05f * static_cast<f32>(frame), 0.05f, batch, 7, idBase, {});
		gpu.system.SetFrameData(block);
		gpu.system.Advance(0.05f, 12);
		RenderSystem::RenderAll();
		record(gpuRecorded, gpu.system.GetMeasuredBounds());
		gpuEvery.Encapsulate(Union(Alive(gpu.system, indices), gpuRead));
		const Vector<u8> cpuBlock = FrameFor(effect, 0.05f * static_cast<f32>(frame), 0.05f, batch, 7, idBase, {});
		cpu.Advance(description, cpuBlock, Slots(description, 0.0f, 0.05f), 12);
		record(cpuRecorded, cpu.GetMeasuredBounds());
		cpuEvery.Encapsulate(Union(static_cast<u32>(cpu.GetAlive().size()), cpuRead));
		idBase += 12;
	}
	for (u32 i = 0; i <= RHI::FRAMES_IN_FLIGHT; ++i) {
		RenderSystem::RenderAll();
		record(gpuRecorded, gpu.system.GetMeasuredBounds());
	}
	ASSERT_TRUE(gpuEvery.IsValid() && cpuEvery.IsValid());
	ExpectNear(gpuRecorded, gpuEvery, "the GPU's recorded box against every sphere it drew");
	ExpectNear(cpuRecorded, cpuEvery, "the CPU's recorded box against every sphere it drew");

	GraphDocument document = EffectKernelHarness::ParseEffect(text);
	ASSERT_TRUE(EffectGraphEditOps::ApplyRecordedBounds(document, 1, gpuRecorded, false, Mat4::Identity()));
	GraphEffect applied;
	diagnostics.clear();
	ASSERT_TRUE(GraphEffectTarget::Compile(document, "Applied", applied, diagnostics)) << Said(diagnostics);
	EffectProgram bounds;
	str error;
	ASSERT_TRUE(EffectPrograms::Assemble(applied.systems[0].boundsAssembly, bounds, error)) << error;
	EffectMachine machine;
	Vector<EffectValue> slots{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(machine.Run(bounds, slots, {}));
	const EffectValue& center = machine.Root(bounds, 0);
	const EffectValue& size = machine.Root(bounds, 1);
	ExpectNear(AABB::FromCenterExtents(Vec3(center.v[0], center.v[1], center.v[2]), Vec3(size.v[0], size.v[1], size.v[2]) * 0.5f), gpuRecorded,
			   "the box the applied graph compiles to");
}

#endif
