#include "TestsPCH.h"

#include "GraphDefinitions.h"
#include "GraphEffect.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#ifdef SHALLTEAR_SHARED
#include "EffectGraphEditOps.h"
#include "NodeShaderSwitch.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/CookOps.h"
#endif

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <span>

#ifndef SHALLTEAR_SHARED
#include "EffectDescription.h"
#include "EffectNoise.h"
#include "EffectProgram.h"
#include "ParticleSimulation.h"
#include "EffectAsset.h"

#include "Shalltear/Core/Graphics/MeshAsset.h"

#include <array>
#include <format>
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(VisualEffects)
#endif

namespace {

	std::filesystem::path AssetsRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			if (probe.parent_path() == probe) {
				break;
			}
			probe = probe.parent_path();
		}
		return {};
	}

	constexpr std::string_view RatedFountain = R"({
	"_type": "EffectGraph",
	"version": 2,
	"nextId": 20,
	"settings": {},
	"properties": [
		{"id":1,"name":"Rate","reference":"rate","type":"Float","default":[4]}
	],
	"nodes": [
		{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}},
		{"id":11,"def":"Math/Multiply","x":0,"y":0,"in":{"A":{"from":[10,"Out"]},"B":{"value":[2]}}},
		{"id":12,"def":"Input/Time","x":0,"y":0},
		{"id":13,"def":"Math/Add","x":0,"y":0,"in":{"A":{"from":[11,"Out"]},"B":{"from":[12,"Time"]}}}
	],
	"contexts": [
		{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[
			{"id":3,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[13,"Out"]}}}
		]},
		{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":64},"flow":[[2,"In"]],"blocks":[
			{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}},
			{"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,1,0]}}}
		]},
		{"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},
		{"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[8,"In"]],"blocks":[]}
	],
	"groups": [],
	"notes": []
}
)";

	GraphDocument ReadDocument(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

} // anonymous namespace

TEST(VisualEffectsAssemblyTest, UnrunnableSpawnRateRefused) {
	str graph(RatedFountain);
	const str from = R"("in":{"Rate":{"from":[13,"Out"]}})";
	graph.replace(graph.find(from), from.size(), R"("in":{"Rate":{"from":[14,"s"]}})");
	const str anchor = R"({"id":13,"def":"Math/Add")";
	graph.insert(graph.find(anchor), R"({"id":14,"def":"Sampling/SampleTexture2D","x":0,"y":0,"in":{"Texture":{"value":"00000000000000a1-00000000000000b1"},"UV":{"value":[0.5,0.5]}}},
		)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphEffectTarget::Compile(ReadDocument(graph), "noise.sleffect", effect, diagnostics));
	const bool named = std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& d) { return d.node == 14; });
	EXPECT_TRUE(named) << (diagnostics.empty() ? str("nothing said") : diagnostics[0].message);
}

TEST(VisualEffectsAssemblyTest, ImporterExistsWhereEditorHalfDoes) {
	TypeRegistry::InitializePrimitives();
	ModuleSystem::Initialize();
#ifdef SHALLTEAR_SHARED
	ModuleHandle module = ModuleSystem::Load(Path("VisualEffects.dll"));
	ASSERT_TRUE(ModuleSystem::IsLoaded(module));
	EXPECT_NE(AssetImporterRegistry::FindByExtension(".sleffect"), nullptr);
	ModuleSystem::Unload(module);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".sleffect"), nullptr);
#else
	char token = 0;
	SLT_CALL_LINKED_PACKAGE(VisualEffects, &token);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".sleffect"), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("VisualEffectAsset")), nullptr);
	VisualEffectsImpl::ShutdownSimulation();
	AssetImporterRegistry::UnregisterOwnedBy(&token);
	FramePasses::UnregisterOwnedBy(&token);
	FrameStages::UnregisterOwnedBy(&token);
#endif
	ModuleSystem::Terminate();
}

#ifndef SHALLTEAR_SHARED

namespace {

	using namespace VisualEffectsImpl;

	EffectProgram AssembleOrFail(std::string_view text) {
		EffectProgram program;
		str error;
		EXPECT_TRUE(EffectPrograms::Assemble(text, program, error)) << error;
		for (u32 i = 0; i < program.inputs.size(); ++i) {
			program.inputs[i].slot = i;
		}
		return program;
	}

	EffectValue Value(f32 x, f32 y = 0.0f, f32 z = 0.0f, f32 w = 0.0f) {
		EffectValue value;
		value.v[0] = x;
		value.v[1] = y;
		value.v[2] = z;
		value.v[3] = w;
		return value;
	}

	EffectValue RunText(std::string_view text, std::initializer_list<EffectValue> slots) {
		const EffectProgram program = AssembleOrFail(text);
		EffectMachine machine;
		const Vector<EffectValue> values{slots, GetMemoryResource(MemTag::Graphics)};
		EXPECT_TRUE(machine.Run(program, values));
		return program.roots.empty() ? EffectValue{} : machine.Root(program, 0);
	}

} // anonymous namespace

TEST(VisualEffectsProgramTest, EveryOpMeansItsHlslIntrinsic) {
	struct Case {
		const char* op;
		EffectValue a, b, c;
		f32 expected;
	};
	const Case cases[] = {
		{"step", Value(0.5f), Value(2.0f), Value(0.0f), 1.0f},
		{"step", Value(2.0f), Value(0.5f), Value(0.0f), 0.0f},
		{"atan2", Value(1.0f), Value(-1.0f), Value(0.0f), 0.75f * std::numbers::pi_v<f32>},
		{"fmod", Value(-1.25f), Value(0.75f), Value(0.0f), -0.5f},
		{"round", Value(2.5f), Value(0.0f), Value(0.0f), 2.0f},
		{"round", Value(3.5f), Value(0.0f), Value(0.0f), 4.0f},
		{"round", Value(-1.5f), Value(0.0f), Value(0.0f), -2.0f},
		{"sign", Value(-0.0f), Value(0.0f), Value(0.0f), 0.0f},
		{"frac", Value(-0.25f), Value(0.0f), Value(0.0f), 0.75f},
		{"lerp", Value(2.0f), Value(4.0f), Value(0.25f), 2.5f},
		{"clamp", Value(5.0f), Value(0.0f), Value(1.0f), 1.0f},
		{"smoothstep", Value(0.0f), Value(2.0f), Value(1.0f), 0.5f},
		{"pow", Value(2.0f), Value(10.0f), Value(0.0f), 1024.0f},
		{"sub", Value(1.0f), Value(3.0f), Value(0.0f), -2.0f},
		{"div", Value(3.0f), Value(4.0f), Value(0.0f), 0.75f},
		{"rcp", Value(4.0f), Value(0.0f), Value(0.0f), 0.25f},
		{"less", Value(1.0f), Value(2.0f), Value(0.0f), 1.0f},
		{"lessequal", Value(2.0f), Value(2.0f), Value(0.0f), 1.0f},
		{"equal", Value(2.0f), Value(3.0f), Value(0.0f), 0.0f},
		{"and", Value(1.0f), Value(0.0f), Value(0.0f), 0.0f},
		{"or", Value(1.0f), Value(0.0f), Value(0.0f), 1.0f},
		{"not", Value(0.0f), Value(0.0f), Value(0.0f), 1.0f},
		{"select", Value(0.0f), Value(7.0f), Value(9.0f), 9.0f},
	};
	for (const Case& entry : cases) {
		const u32 operands = std::string_view(entry.op) == "lerp" || std::string_view(entry.op) == "clamp" || std::string_view(entry.op) == "smoothstep" ||
									 std::string_view(entry.op) == "select"
								 ? 3
								 : (std::string_view(entry.op) == "round" || std::string_view(entry.op) == "sign" || std::string_view(entry.op) == "frac" ||
											std::string_view(entry.op) == "rcp" || std::string_view(entry.op) == "not"
										? 1
										: 2);
		str text = "input 1 a\ninput 1 b\ninput 1 c\n" + std::format("{} 1 0 0", entry.op);
		for (u32 i = 1; i < operands; ++i) {
			text += std::format(" {}", i);
		}
		text += "\nroot 3\n";
		EXPECT_FLOAT_EQ(RunText(text, {entry.a, entry.b, entry.c}).v[0], entry.expected) << entry.op;
	}
}

TEST(VisualEffectsProgramTest, VectorOpsMatchHlsl) {
	const EffectValue v = Value(1.0f, 2.0f, 3.0f);
	const EffectValue w = Value(0.0f, 1.0f, 0.0f);
	EffectValue sum = RunText("input 3 v\ninput 1 s\nadd 3 0 0 1\nroot 2\n", {v, Value(10.0f)});
	EXPECT_EQ(sum.v[0], 11.0f);
	EXPECT_EQ(sum.v[2], 13.0f);
	const u32 zyx = 3u | (2u << 4) | (1u << 6) | (0u << 8);
	EffectValue reversed = RunText(std::format("input 3 v\nswizzle 3 {} 0\nroot 1\n", zyx), {v});
	EXPECT_EQ(reversed.v[0], 3.0f);
	EXPECT_EQ(reversed.v[2], 1.0f);
	EffectValue splat = RunText("input 1 s\nswizzle 3 3 0\nroot 1\n", {Value(5.0f)});
	EXPECT_EQ(splat.v[2], 5.0f);
	EffectValue built = RunText("input 1 s\ninput 3 v\nconstruct 4 0 1 0\nroot 2\n", {Value(9.0f), v});
	EXPECT_EQ(built.v[0], 1.0f);
	EXPECT_EQ(built.v[3], 9.0f);
	EXPECT_EQ(RunText("input 3 v\ninput 3 w\ndot 1 0 0 1\nroot 2\n", {v, w}).v[0], 2.0f);
	EffectValue cross = RunText("input 3 v\ninput 3 w\ncross 3 0 0 1\nroot 2\n", {Value(1.0f, 0.0f, 0.0f), w});
	EXPECT_EQ(cross.v[2], 1.0f);
	EXPECT_FLOAT_EQ(RunText("input 3 v\nlength 1 0 0\nroot 1\n", {Value(3.0f, 4.0f, 0.0f)}).v[0], 5.0f);
	EXPECT_FLOAT_EQ(RunText("input 3 v\nnormalize 3 0 0\nroot 1\n", {Value(0.0f, 0.0f, 2.0f)}).v[2], 1.0f);
	EXPECT_EQ(RunText("input 3 v\nall 1 0 0\nroot 1\n", {w}).v[0], 0.0f);
	EXPECT_EQ(RunText("input 3 v\nany 1 0 0\nroot 1\n", {w}).v[0], 1.0f);
	EXPECT_EQ(RunText("const 1 0x40490FDB\nroot 0\n", {}).v[0], std::numbers::pi_v<f32>);
}

TEST(VisualEffectsProgramTest, NonProgramRefusedWithLine) {
	const std::string_view refused[] = {
		"frobnicate 1 0\n",
		"input 1 a\nadd 1 0 0 1\n",
		"input 3 a\ninput 2 b\nadd 3 0 0 1\n",
		"input 3 a\ndot 3 0 0 0\n",
		"const 1 1.5\n",
		"const 2 0x3F800000\n",
		"input 1 a\nroot 4\n",
		"input 1 a\nswizzle 1 0x11 0\n",
		"input 2 a\nvaluenoise 1 3 0\n",
		"input 2 a\ninput 1 b\nvoronoi 2 2 0 1 1\n",
		"input 3 c\ninput 3 p\ninput 1 o\nfractalnoise 3 0 0 1 2\n",
		"input 2 c\ninput 3 p\ninput 1 o\ncurlnoise 2 3 0 1 2\n",
		"input 1 c\ninput 3 p\ninput 1 o\ncurlnoise 1 0 0 1 2\n",
		"input 1 i\nmeshvertex 3 0 0\n",
		"mesh\n",
		"mesh m\ninput 1 i\nmeshvertex 3 5 0\n",
		"mesh m\ninput 1 i\nmeshvertex 4 0 0\n",
		"mesh m\ninput 1 i\nmeshindex 1 1 0\n",
	};
	for (std::string_view text : refused) {
		EffectProgram program;
		program.roots.push_back(0);
		str error;
		EXPECT_FALSE(EffectPrograms::Assemble(text, program, error)) << "assembled: " << text;
		EXPECT_NE(error.find("line"), str::npos) << text;
		EXPECT_TRUE(program.IsEmpty() && program.roots.empty()) << text;
	}
}

TEST(VisualEffectsProgramTest, CookedProgramRoundTrips) {
	const EffectProgram program = AssembleOrFail("input 4 t\nconst 1 0x40000000\nmul 4 0 0 1\nroot 2\n");
	PakStringPool pool;
	Vector<u8> bytes{GetMemoryResource(MemTag::Asset)};
	CookWriter writer(bytes, pool);
	EffectPrograms::Cook(program, writer);
	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};
	CookReader reader(bytes.data(), bytes.size(), strings);
	EffectProgram loaded;
	ASSERT_TRUE(EffectPrograms::LoadCooked(reader, loaded));
	ASSERT_EQ(loaded.code.size(), 3u);
	EXPECT_EQ(loaded.inputs[0].name, "t");
	EXPECT_EQ(loaded.constants[0], 2.0f);
	EXPECT_EQ(loaded.roots[0], 2u);

	Vector<u8> corrupt = bytes;
	const size_t operand = 4 + 2 * sizeof(EffectInstruction) + 8 + 4;
	const u32 forward = 7;
	std::memcpy(corrupt.data() + operand, &forward, sizeof(forward));
	CookReader corruptReader(corrupt.data(), corrupt.size(), strings);
	EXPECT_FALSE(EffectPrograms::LoadCooked(corruptReader, loaded));
}

TEST(VisualEffectsProgramTest, MeshReadIs0PastPayload) {
	MeshVertex vertices[2];
	vertices[1].position = Vec3(1.0f, 2.0f, 3.0f);
	vertices[1].normal = Vec3(0.0f, 1.0f, 0.0f);
	vertices[1].tangent = Vec3(1.0f, 0.0f, 0.0f);
	vertices[1].tangentSign = -1.0f;
	vertices[1].color = Color32{255, 51, 0, 102};
	vertices[1].u = 0.25f;
	vertices[1].v = 0.75f;
	vertices[0].position = Vec3(-4.0f, -5.0f, -6.0f);
	const u32 indices[] = {1, 0, 7};
	const EffectMeshView views[] = {{vertices, 2, indices, 3}, {}};
	const auto read = [&](std::string_view op, u32 width, u32 payload, f32 at, u32 mesh = 0) {
		EffectProgram program = AssembleOrFail(std::format("mesh a\nmesh b\ninput 1 at\n{} {} {} 0\nroot 1\n", op, width, payload));
		program.meshes[0].mesh = mesh;
		program.meshes[1].mesh = 1;
		EffectMachine machine;
		const EffectValue slot = Value(at);
		EXPECT_TRUE(machine.Run(program, {&slot, 1}, views));
		return machine.Root(program, 0);
	};
	const auto expect = [](const EffectValue& got, std::initializer_list<f32> want, const char* what) {
		u32 i = 0;
		for (const f32 component : want) {
			EXPECT_EQ(got.v[i], component) << what << ", component " << i;
			++i;
		}
	};
	expect(read("meshvertex", 3, 0, 1.75f), {1.0f, 2.0f, 3.0f}, "the position of the vertex a Float truncates to");
	expect(read("meshvertex", 3, 1, 1.0f), {0.0f, 1.0f, 0.0f}, "the normal");
	expect(read("meshvertex", 4, 2, 1.0f), {1.0f, 0.0f, 0.0f, -1.0f}, "the tangent and its sign");
	expect(read("meshvertex", 4, 3, 1.0f), {1.0f, 0.2f, 0.0f, 0.4f}, "the color as stored, over 255");
	expect(read("meshvertex", 4, 4, 1.0f), {0.25f, 0.75f, 0.0f, 0.0f}, "the UV");
	expect(read("meshvertex", 3, 0, -3.0f), {-4.0f, -5.0f, -6.0f}, "a negative position is not the first vertex");
	expect(read("meshvertex", 3, 0, NAN), {-4.0f, -5.0f, -6.0f}, "a NaN position is not the first vertex");
	expect(read("meshvertex", 3, 0, 2.0f), {0.0f, 0.0f, 0.0f}, "a vertex past the count doesn't read 0");
	expect(read("meshvertex", 3, 0, 1e20f), {0.0f, 0.0f, 0.0f}, "a vertex far past the count doesn't read 0");
	expect(read("meshindex", 1, 0, 0.0f), {1.0f}, "an index is not the entry at its position");
	expect(read("meshindex", 1, 0, 2.5f), {7.0f}, "an index's position is not truncated");
	expect(read("meshindex", 1, 0, 3.0f), {0.0f}, "an index past the count doesn't read 0");
	expect(read("meshvertex", 3, 1u << EFFECT_MESH_CHANNEL_BITS, 1.0f), {0.0f, 0.0f, 0.0f}, "the 2nd line's mesh (empty) didn't read 0");
	expect(read("meshindex", 1, 1, 0.0f), {0.0f}, "an empty mesh's index didn't read 0");

	EffectProgram program = AssembleOrFail("mesh a\ninput 1 at\nmeshindex 1 0 0\nroot 1\n");
	program.meshes[0].mesh = 2;
	EffectMachine machine;
	const EffectValue slot = Value(0.0f);
	EXPECT_FALSE(machine.Run(program, {&slot, 1}, views));
}

TEST(VisualEffectsProgramTest, ProgramMeshesCookWhole) {
	const EffectProgram program = AssembleOrFail("mesh PortMesh/9/Mesh\nmesh PortMesh/4/Mesh\ninput 1 PortMesh/4/Mesh/VertexCount\nmeshindex 1 1 0\nroot 1\n");
	PakStringPool pool;
	Vector<u8> bytes{GetMemoryResource(MemTag::Asset)};
	CookWriter writer(bytes, pool);
	EffectPrograms::Cook(program, writer);
	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};
	CookReader reader(bytes.data(), bytes.size(), strings);
	EffectProgram loaded;
	ASSERT_TRUE(EffectPrograms::LoadCooked(reader, loaded));
	ASSERT_EQ(loaded.meshes.size(), 2u);
	EXPECT_EQ(loaded.meshes[0].name, "PortMesh/9/Mesh");
	EXPECT_EQ(loaded.meshes[1].name, "PortMesh/4/Mesh");

	EffectDescription description;
	description.meshes.push_back({"PortMesh/4/Mesh", 96, Guid()});
	description.meshes.push_back({"PortMesh/9/Mesh", 128, Guid()});
	EffectSpawner spawner;
	EffectSpawnBlock block;
	block.program = std::move(loaded);
	spawner.blocks.push_back(std::move(block));
	description.spawners.push_back(std::move(spawner));
	str error;
	ASSERT_TRUE(EffectDescriptions::Bind(description, error)) << error;
	const EffectProgram& bound = description.spawners[0].blocks[0].program;
	EXPECT_EQ(bound.meshes[0].mesh, 1u);
	EXPECT_EQ(bound.meshes[1].mesh, 0u);
	description.meshes.pop_back();
	EXPECT_FALSE(EffectDescriptions::Bind(description, error));
	EXPECT_NE(error.find("PortMesh/9/Mesh"), str::npos) << error;
}

TEST(VisualEffectsProgramTest, VMsNoisesHoldLaws) {
	constexpr std::string_view TYPES[] = {"Value", "Perlin", "Cellular"};
	const auto fractal = [](u32 type, u32 dimension, const f32* at, f32 frequency, f32 octaves) {
		const EffectProgram program = AssembleOrFail(std::format("input {0} c\ninput 3 p\ninput 1 o\nfractalnoise {1} {2} 0 1 2\nroot 3\n", dimension, dimension + 1, type));
		const EffectValue slots[] = {Value(at[0], at[1], at[2]), Value(frequency, 0.4f, 2.1f), Value(octaves)};
		EffectMachine machine;
		EXPECT_TRUE(machine.Run(program, slots));
		return machine.Root(program, 0);
	};
	const auto curl = [](u32 type, u32 dimension, const f32* at) {
		const EffectProgram program = AssembleOrFail(std::format("input {0} c\ninput 3 p\ninput 1 o\ncurlnoise {0} {1} 0 1 2\nroot 3\n", dimension, type));
		const EffectValue slots[] = {Value(at[0], at[1], at[2]), Value(1.0f, 0.5f, 2.0f), Value(1.0f)};
		EffectMachine machine;
		EXPECT_TRUE(machine.Run(program, slots));
		return machine.Root(program, 0);
	};
	constexpr f32 STEP = 1.0e-3f;
	constexpr f32 BASE[3] = {0.37f, 1.61f, 2.29f};
	for (u32 type = 0; type < 3; ++type) {
		for (u32 dimension = 1; dimension <= 3; ++dimension) {
			SCOPED_TRACE(std::format("{} Noise {}D", TYPES[type], dimension));
			const EffectValue at = fractal(type, dimension, BASE, 1.0f, 1.0f);
			EXPECT_TRUE(std::isfinite(at.v[0]));
			for (u32 axis = 0; axis < dimension; ++axis) {
				f32 ahead[3] = {BASE[0], BASE[1], BASE[2]};
				f32 behind[3] = {BASE[0], BASE[1], BASE[2]};
				ahead[axis] += STEP;
				behind[axis] -= STEP;
				const f32 slope = (fractal(type, dimension, ahead, 1.0f, 1.0f).v[0] - fractal(type, dimension, behind, 1.0f, 1.0f).v[0]) / (2.0f * STEP);
				EXPECT_NEAR(at.v[1 + axis], slope, 5.0e-3f) << "axis " << axis;
			}
		}
		const f32 point[3] = {0.41f, 1.73f, 2.9f};
		const f32 first = fractal(type, 3, point, 0.7f, 1.0f).v[0];
		const f32 second = fractal(type, 3, point, 0.7f * 2.1f, 1.0f).v[0];
		EXPECT_NEAR(fractal(type, 3, point, 0.7f, 2.0f).v[0], (first + 0.4f * second) / 1.4f, 1.0e-5f) << TYPES[type];
		EXPECT_NE(first, second) << TYPES[type];
		const auto plane = [&](f32 x, f32 y) {
			const f32 xy[3] = {x, y, 0.0f};
			const EffectValue noise = fractal(type, 2, xy, 1.0f, 1.0f);
			return std::array<f32, 2>{noise.v[1], noise.v[2]};
		};
		const f32 at2[3] = {0.53f, 1.27f, 0.0f};
		const EffectValue curl2 = curl(type, 2, at2);
		const auto d = plane(at2[0], at2[1]);
		EXPECT_NEAR(curl2.v[0], d[1], 1.0e-5f) << TYPES[type];
		EXPECT_NEAR(curl2.v[1], -d[0], 1.0e-5f) << TYPES[type];
		const f32 at3[3] = {0.53f, 1.27f, 2.11f};
		const EffectValue curl3 = curl(type, 3, at3);
		const auto a = plane(at3[2], at3[1]);
		const auto b = plane(at3[0] + 100.0f, at3[2] + 100.0f);
		const auto c = plane(at3[1] + 200.0f, at3[0] + 200.0f);
		EXPECT_NEAR(curl3.v[0], c[0] - b[1], 1.0e-5f) << TYPES[type];
		EXPECT_NEAR(curl3.v[1], a[0] - c[1], 1.0e-5f) << TYPES[type];
		EXPECT_NEAR(curl3.v[2], b[0] - a[1], 1.0e-5f) << TYPES[type];
		EXPECT_GT(std::fabs(curl3.v[0]) + std::fabs(curl3.v[1]) + std::fabs(curl3.v[2]), 0.0f);
	}
	const f32 point[3] = {0.41f, 1.73f, 2.9f};
	EXPECT_EQ(fractal(0, 3, point, 1.0f, 2.9f).v[0], fractal(0, 3, point, 1.0f, 2.0f).v[0]);
	EXPECT_TRUE(std::isnan(fractal(0, 3, point, 1.0f, 0.5f).v[0]));
	EXPECT_TRUE(std::isnan(fractal(0, 3, point, 1.0f, NAN).v[0]));
	EXPECT_EQ(fractal(0, 3, point, 1.0f, 1.0e9f).v[0], fractal(0, 3, point, 1.0f, EffectNoises::MAX_OCTAVES).v[0]);
}

TEST(VisualEffectsProgramTest, CompilersSpawnRateRunsOnMachine) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadDocument(RatedFountain), "rated.sleffect", effect, diagnostics))
		<< (diagnostics.empty() ? str() : diagnostics[0].message);
	EffectDescription description;
	EffectProperty rate;
	rate.reference = "rate";
	rate.components = 1;
	description.properties.push_back(rate);
	EffectSpawner spawner;
	EffectSpawnBlock block;
	str error;
	ASSERT_TRUE(EffectPrograms::Assemble(effect.spawners[0].blocks[0].assembly, block.program, error)) << error << "\n"
																									   << effect.spawners[0].blocks[0].assembly;
	spawner.blocks.push_back(std::move(block));
	description.spawners.push_back(std::move(spawner));
	ASSERT_TRUE(EffectDescriptions::Bind(description, error)) << error;

	EffectValue slots[EFFECT_SLOT_PROPERTIES + 1] = {};
	slots[EFFECT_SLOT_TIME] = Value(1.5f, std::sin(1.5f), std::cos(1.5f), 0.25f);
	slots[EFFECT_SLOT_SMOOTH_DELTA] = Value(0.25f);
	slots[EFFECT_SLOT_PROPERTIES] = Value(4.0f);
	EffectMachine machine;
	const EffectProgram& program = description.spawners[0].blocks[0].program;
	ASSERT_TRUE(machine.Run(program, slots));
	EXPECT_EQ(machine.Root(program, 0).v[0], 9.5f);
}

TEST(VisualEffectsProgramTest, MeshsCountRunsOnMachineFromSlot) {
	const str counted = R"({"_type":"EffectGraph","version":4,"nextId":100,"nodes":[
		{"id":20,"def":"Sampling/MeshVertexCount","in":{"Mesh":{"value":"00000000000000a5-00000000000000b5"}}}],"contexts":[
		{"id":5,"def":"Context/Spawn","blocks":[{"id":6,"def":"Block/Burst","in":{"Count":{"from":[20,"Count"]}}}]},
		{"id":1,"def":"Context/Initialize","flow":[[5,"In"]],"blocks":[]}]})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(ReadDocument(counted), "counted.sleffect", effect, diagnostics))
		<< (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.meshes.size(), 1u);
	EffectDescription description;
	EffectProperty rate;
	rate.reference = "rate";
	rate.components = 1;
	description.properties.push_back(rate);
	description.meshes.push_back({effect.meshes[0].binding, effect.meshes[0].offset, effect.meshes[0].mesh});
	EffectSpawner spawner;
	EffectSpawnBlock block;
	block.kind = EffectSpawnKind::SingleBurst;
	str error;
	ASSERT_TRUE(EffectPrograms::Assemble(effect.spawners[0].blocks[0].assembly, block.program, error)) << error << "\n"
																									   << effect.spawners[0].blocks[0].assembly;
	spawner.blocks.push_back(std::move(block));
	description.spawners.push_back(std::move(spawner));
	ASSERT_TRUE(EffectDescriptions::Bind(description, error)) << error;

	Vector<EffectValue> slots{GetMemoryResource(MemTag::Graphics)};
	slots.assign(EffectSlotCount(description), EffectValue{});
	ASSERT_EQ(slots.size(), EFFECT_SLOT_PROPERTIES + 3u);
	slots[EFFECT_SLOT_PROPERTIES] = Value(9.0f);
	slots[EffectMeshSlot(description, 0, false)] = Value(4.0f);
	slots[EffectMeshSlot(description, 0, true)] = Value(6.0f);
	EffectMachine machine;
	const EffectProgram& program = description.spawners[0].blocks[0].program;
	ASSERT_TRUE(machine.Run(program, slots));
	EXPECT_EQ(machine.Root(program, 0).v[0], 4.0f);
}

namespace {

	EffectSpawnBlock Block(EffectSpawnKind kind, std::string_view text) {
		EffectSpawnBlock block;
		block.kind = kind;
		block.program = AssembleOrFail(text);
		return block;
	}

	Vector<u32> Frames(const EffectSpawner& spawner, u32 frames, f32 delta) {
		EffectSpawnerState state;
		EffectMachine machine;
		EffectSpawners::Start(spawner, state, {}, machine);
		Vector<u32> counts{GetMemoryResource(MemTag::Graphics)};
		for (u32 i = 0; i < frames; ++i) {
			counts.push_back(EffectSpawners::Advance(spawner, state, delta, {}, machine));
		}
		return counts;
	}

} // anonymous namespace

TEST(VisualEffectsSpawnerTest, RateCarriesFraction) {
	EffectSpawner rate;
	rate.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "const 1 0x41200000\nroot 0\n"));
	EXPECT_EQ(Frames(rate, 4, 0.25f), (Vector<u32>{{2, 3, 2, 3}, GetMemoryResource(MemTag::Graphics)}));

	EffectSpawner single;
	single.blocks.push_back(Block(EffectSpawnKind::SingleBurst, "const 1 0x40A00000\nconst 1 0x3F000000\nroot 0\nroot 1\n"));
	EXPECT_EQ(Frames(single, 4, 0.25f), (Vector<u32>{{0, 5, 0, 0}, GetMemoryResource(MemTag::Graphics)}));

	EffectSpawner periodic;
	periodic.blocks.push_back(Block(EffectSpawnKind::PeriodicBurst, "const 1 0x40400000\nconst 1 0x3F000000\nroot 0\nroot 1\n"));
	EXPECT_EQ(Frames(periodic, 4, 0.25f), (Vector<u32>{{0, 3, 0, 3}, GetMemoryResource(MemTag::Graphics)}));

	EffectSpawner everyFrame;
	everyFrame.blocks.push_back(Block(EffectSpawnKind::PeriodicBurst, "const 1 0x40000000\nconst 1 0x00000000\nroot 0\nroot 1\n"));
	EXPECT_EQ(Frames(everyFrame, 3, 0.25f), (Vector<u32>{{2, 2, 2}, GetMemoryResource(MemTag::Graphics)}));

	EffectSpawnerState state;
	EffectMachine machine;
	EffectSpawners::Start(single, state, {}, machine);
	EffectSpawners::Advance(single, state, 1.0f, {}, machine);
	EffectSpawners::Stop(state);
	EXPECT_EQ(EffectSpawners::Advance(single, state, 1.0f, {}, machine), 0u);
	EffectSpawners::Start(single, state, {}, machine);
	EXPECT_EQ(EffectSpawners::Advance(single, state, 1.0f, {}, machine), 5u);
}

namespace {

	EffectProgram Loop(f32 duration, f32 count, f32 before, f32 after) {
		auto bits = [](f32 value) { return std::format("0x{:08X}", std::bit_cast<u32>(value)); };
		return AssembleOrFail(std::format("const 1 {}\nconst 1 {}\nconst 1 {}\nconst 1 {}\nroot 0\nroot 1\nroot 2\nroot 3\n", bits(duration), bits(count),
										  bits(before), bits(after)));
	}

} // anonymous namespace

TEST(VisualEffectsSpawnerTest, LoopRepeatsBursts) {
	auto counts = [](std::initializer_list<u32> values) { return Vector<u32>{values, GetMemoryResource(MemTag::Graphics)}; };

	EffectSpawner looped;
	looped.blocks.push_back(Block(EffectSpawnKind::SingleBurst, "const 1 0x40A00000\nconst 1 0x00000000\nroot 0\nroot 1\n"));
	looped.loop = Loop(1.0f, -1.0f, 0.0f, 0.0f);
	EXPECT_EQ(Frames(looped, 9, 0.25f), counts({5, 0, 0, 0, 5, 0, 0, 0, 5}));

	EffectSpawner delayed;
	delayed.blocks.push_back(Block(EffectSpawnKind::SingleBurst, "const 1 0x40A00000\nconst 1 0x3E800000\nroot 0\nroot 1\n"));
	delayed.loop = Loop(1.0f, -1.0f, 0.0f, 0.0f);
	EXPECT_EQ(Frames(delayed, 6, 0.25f), counts({5, 0, 0, 0, 5, 0}));

	EffectSpawner counted;
	counted.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "const 1 0x41200000\nroot 0\n"));
	counted.loop = Loop(0.5f, 2.0f, 0.0f, 0.0f);
	EXPECT_EQ(Frames(counted, 6, 0.25f), counts({2, 3, 2, 3, 0, 0}));

	EffectSpawner waiting;
	waiting.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "const 1 0x40800000\nroot 0\n"));
	waiting.loop = Loop(0.5f, -1.0f, 0.5f, 0.5f);
	EXPECT_EQ(Frames(waiting, 12, 0.25f), counts({0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0}));

	EffectSpawner empty;
	empty.blocks.push_back(Block(EffectSpawnKind::SingleBurst, "const 1 0x40A00000\nconst 1 0x00000000\nroot 0\nroot 1\n"));
	empty.loop = Loop(0.0f, -1.0f, 0.0f, 0.0f);
	EXPECT_EQ(Frames(empty, 3, 0.25f), counts({5, 5, 5}));

	EffectSpawner none;
	none.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "const 1 0x41200000\nroot 0\n"));
	none.loop = Loop(-1.0f, 0.0f, 0.0f, 0.0f);
	EXPECT_EQ(Frames(none, 2, 0.25f), counts({0, 0}));
}

TEST(VisualEffectsSpawnerTest, LastSpawnEventValueWins) {
	EffectSpawner spawner;
	spawner.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "const 1 0x41200000\nroot 0\n"));
	EffectSpawnBlock first = Block(EffectSpawnKind::SetAttribute, "const 3 0x3F800000 0x40000000 0x40400000\nroot 0\n");
	first.attribute = "position";
	first.components = 3;
	EffectSpawnBlock second = Block(EffectSpawnKind::SetAttribute, "const 3 0x40800000 0x40A00000 0x40C00000\nroot 0\n");
	second.attribute = "position";
	second.components = 3;
	spawner.blocks.push_back(std::move(first));
	spawner.blocks.push_back(std::move(second));

	EffectSpawnerState state;
	EffectMachine machine;
	EffectSpawners::Start(spawner, state, {}, machine);
	EffectSpawners::Advance(spawner, state, 0.25f, {}, machine);
	EffectValue value;
	ASSERT_TRUE(EffectSpawners::FindAttribute(spawner, state, "position", value));
	EXPECT_EQ(value.v[0], 4.0f);
	EXPECT_EQ(value.v[2], 6.0f);
	EXPECT_FALSE(EffectSpawners::FindAttribute(spawner, state, "color", value));
}

TEST(VisualEffectsSpawnerTest, StripIndexFollowsStartStop) {
	EffectSpawner spawner;
	spawner.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "const 1 0x41200000\nroot 0\n"));
	EffectSpawnBlock increment = Block(EffectSpawnKind::IncrementStripIndex, "const 1 0x40400000\nroot 0\n");
	increment.attribute = "stripIndex";
	increment.components = 1;
	spawner.blocks.push_back(std::move(increment));

	EffectSpawnerState state;
	EffectMachine machine;
	const auto strip = [&]() {
		EffectValue value;
		EXPECT_TRUE(EffectSpawners::FindAttribute(spawner, state, "stripIndex", value));
		return value.v[0];
	};
	Vector<f32> started{GetMemoryResource(MemTag::Graphics)};
	for (u32 i = 0; i < 4; ++i) {
		EffectSpawners::Start(spawner, state, {}, machine);
		EffectSpawners::Advance(spawner, state, 0.25f, {}, machine);
		started.push_back(strip());
	}
	EXPECT_EQ(started, (Vector<f32>{{1.0f, 2.0f, 0.0f, 1.0f}, GetMemoryResource(MemTag::Graphics)}));
	EffectSpawners::Advance(spawner, state, 0.25f, {}, machine);
	EXPECT_EQ(strip(), 1.0f);
	EffectSpawners::Stop(state);
	EffectSpawners::Start(spawner, state, {}, machine);
	EXPECT_EQ(strip(), 1.0f);
}

TEST(VisualEffectsProgramTest, RandomInUnitRange) {
	auto draw = [](u32 key, f32 seed, u32 salt) {
		return RunText(std::format("input 1 key\ninput 1 seed\nrandom 1 {} 0 1\nroot 2\n", salt), {Value(std::bit_cast<f32>(key)), Value(seed)}).v[0];
	};
	const f32 base = draw(1234u, 0.0f, 0u);
	EXPECT_GE(base, 0.0f);
	EXPECT_LT(base, 1.0f);
	EXPECT_EQ(draw(1234u, 0.0f, 0u), base);
	EXPECT_NE(draw(1235u, 0.0f, 0u), base);
	EXPECT_NE(draw(1234u, 1.0f, 0u), base);
	EXPECT_NE(draw(1234u, 0.0f, 1u), base);
	f32 lowest = 1.0f;
	f32 highest = 0.0f;
	for (u32 key = 0; key < 4096; ++key) {
		const f32 value = draw(key, 0.0f, 0u);
		lowest = std::min(lowest, value);
		highest = std::max(highest, value);
		EXPECT_TRUE(value >= 0.0f && value < 1.0f) << value;
	}
	EXPECT_LT(lowest, 0.01f);
	EXPECT_GT(highest, 0.99f);
}

namespace {

	class VisualEffectAssetFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		ShaderAsset* _initialize = nullptr;
		ShaderAsset* _update = nullptr;
		ShaderAsset* _output = nullptr;

		void SetUp() override {
			const std::filesystem::path engineAssets = AssetsRepoDir("EngineAssets");
			const std::filesystem::path fixtures = AssetsRepoDir("Source/Tests/Fixtures/VisualEffects");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			_initialize = New<ShaderAsset>(MemTag::Asset);
			_update = New<ShaderAsset>(MemTag::Asset);
			_output = New<ShaderAsset>(MemTag::Asset);
			if (fixtures.empty() || !_initialize->Import(Path((fixtures / "AgeInitialize.hlsl").generic_string())) ||
				!_update->Import(Path((fixtures / "AgeUpdate.hlsl").generic_string())) ||
				!_output->Import(Path((fixtures / "QuadOutput.hlsl").generic_string()))) {
				GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
			}
		}

		void TearDown() override {
			for (ShaderAsset* shader : {_initialize, _update, _output}) {
				if (shader) {
					Delete(shader);
				}
			}
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
		}

		EffectDescription Description() {
			EffectDescription description;
			EffectProperty rate;
			rate.name = "Rate";
			rate.reference = "rate";
			rate.offset = 96;
			rate.components = 1;
			rate.defaultValue[0] = 4.0f;
			description.properties.push_back(rate);
			description.batchesOffset = 112;
			EffectSpawner spawner;
			spawner.start.push_back("OnPlay");
			spawner.stop.push_back("OnStop");
			spawner.blocks.push_back(Block(EffectSpawnKind::ConstantRate, "input 1 rate\nconst 1 0x40000000\nmul 1 0 0 1\nroot 2\n"));
			description.spawners.push_back(std::move(spawner));
			EffectSystem system;
			system.capacity = 32;
			system.attributeCount = 2;
			system.stored.push_back({"age", 0, 1});
			system.stored.push_back({"lifetime", 1, 1});
			system.initialize = 0;
			system.update = 1;
			system.outputs.push_back({2, 6});
			system.spawners.push_back(0);
			system.bounds = AssembleOrFail("const 3 0x00000000 0x3F800000 0x00000000\nconst 3 0x40000000 0x40000000 0x40000000\nconst 3 0x00000000 0x00000000 0x00000000\nroot 0\nroot 1\nroot 2\n");
			description.systems.push_back(std::move(system));
			description.events = {{"OnPlay", "OnStop"}, GetMemoryResource(MemTag::Asset)};
			return description;
		}

		Vector<ShaderAsset*> TakeShaders() {
			Vector<ShaderAsset*> shaders{{_initialize, _update, _output}, GetMemoryResource(MemTag::Asset)};
			_initialize = _update = _output = nullptr;
			return shaders;
		}

		static void CookOf(const Asset& asset, PakStringPool& pool, Vector<u8>& descriptor, Vector<u8>& payload) {
			CookWriter descriptorWriter(descriptor, pool);
			CookWriter payloadWriter(payload, pool);
			ASSERT_TRUE(asset.Cook(descriptorWriter, payloadWriter));
		}
	};

} // anonymous namespace

TEST_F(VisualEffectAssetFixture, CookedEffectRoundTrips) {
	VisualEffectAsset asset;
	Vector<ShaderAsset*> shaders = TakeShaders();
	EffectDescription cooked = Description();
	cooked.systems[0].boundsWorld = true;
	cooked.systems[0].events.push_back("Hit");
	cooked.systems[0].boundsMode = EffectBoundsMode::Automatic;
	cooked.systems[0].measuresBounds = true;
	cooked.timing = {false, true, true, 12, 0.125f, false, true};
	cooked.instancing = {EFFECT_INSTANCING_CUSTOM, 5};
	cooked.spawners[0].outputEvents.push_back("Burst");
	cooked.relays.push_back({"Hit", "Echo"});
	ASSERT_TRUE(EffectAssetAccess::Adopt(asset, std::move(cooked), shaders));
	EXPECT_TRUE(shaders.empty());
	ASSERT_TRUE(EffectAssetAccess::OutputMaterial(asset, 0, 0).IsValid());

	PakStringPool pool;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	CookOf(asset, pool, descriptor, payload);

	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};

	const TypeInfo* type = TypeRegistry::FindType(StringHash("VisualEffectAsset"));
	ASSERT_NE(type, nullptr);
	Asset* loaded = static_cast<Asset*>(TypeRegistry::CreateObjectInstance(type, MemTag::Asset));
	ASSERT_NE(loaded, nullptr);
	CookReader descriptorReader(descriptor.data(), descriptor.size(), strings);
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));

	const VisualEffectAsset& effect = static_cast<const VisualEffectAsset&>(*loaded);
	ASSERT_EQ(EffectAssetAccess::Description(effect).systems.size(), 1u);
	EXPECT_EQ(EffectAssetAccess::Description(effect).systems[0].capacity, 32u);
	EXPECT_TRUE(EffectAssetAccess::Description(effect).systems[0].boundsWorld);
	EXPECT_EQ(EffectAssetAccess::Description(effect).systems[0].events, (Vector<str>{{"Hit"}, GetMemoryResource(MemTag::Asset)}));
	EXPECT_TRUE(EffectAssetAccess::Shader(effect, 0).Get() && EffectAssetAccess::Shader(effect, 0).Get()->IsKernel());
	EXPECT_TRUE(EffectAssetAccess::OutputMaterial(effect, 0, 0).IsValid());
	EXPECT_EQ(EffectAssetAccess::Description(effect).spawners[0].blocks[0].program.inputs[0].slot, EFFECT_SLOT_PROPERTIES);
	const EffectTiming& timing = EffectAssetAccess::Description(effect).timing;
	EXPECT_FALSE(timing.fixedDeltaTime);
	EXPECT_TRUE(timing.exactFixedTime && timing.ignoreTimeScale);
	EXPECT_EQ(timing.prewarmStepCount, 12u);
	EXPECT_EQ(timing.prewarmDeltaTime, 0.125f);
	EXPECT_FALSE(timing.cullSimulation);
	EXPECT_TRUE(timing.cullBoundsUpdate);
	EXPECT_EQ(EffectAssetAccess::Description(effect).systems[0].boundsMode, EffectBoundsMode::Automatic);
	EXPECT_TRUE(EffectAssetAccess::Description(effect).systems[0].measuresBounds);
	EXPECT_EQ(EffectAssetAccess::Description(effect).instancing.mode, EFFECT_INSTANCING_CUSTOM);
	EXPECT_EQ(EffectAssetAccess::Description(effect).instancing.capacity, 5u);
	EXPECT_EQ(EffectAssetAccess::Description(effect).spawners[0].outputEvents, (Vector<str>{{"Burst"}, GetMemoryResource(MemTag::Asset)}));
	ASSERT_EQ(EffectAssetAccess::Description(effect).relays.size(), 1u);
	EXPECT_EQ(EffectAssetAccess::Description(effect).relays[0].event, "Hit");
	EXPECT_EQ(EffectAssetAccess::Description(effect).relays[0].output, "Echo");

	PakStringPool againPool;
	Vector<u8> againDescriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> againPayload{GetMemoryResource(MemTag::Asset)};
	CookOf(*loaded, againPool, againDescriptor, againPayload);
	EXPECT_EQ(againDescriptor, descriptor);
	EXPECT_EQ(againPayload, payload);
	EXPECT_EQ(againPool.Texts(), pool.Texts());
	TypeRegistry::DestroyObjectInstance(loaded);
}

TEST_F(VisualEffectAssetFixture, DescriptionThatDisagreesWithShadersIsRefused) {
	EffectDescription pastTheShaders = Description();
	pastTheShaders.systems[0].update = 7;
	VisualEffectAsset asset;
	Vector<ShaderAsset*> shaders = TakeShaders();
	EXPECT_FALSE(EffectAssetAccess::Adopt(asset, std::move(pastTheShaders), shaders));
	EXPECT_TRUE(shaders.empty());
	EXPECT_TRUE(EffectAssetAccess::Description(asset).systems.empty());
	EXPECT_FALSE(EffectAssetAccess::Shader(asset, 0).IsValid());

	EffectDescription unknownInput = Description();
	unknownInput.spawners[0].blocks[0] = Block(EffectSpawnKind::ConstantRate, "input 1 speed\nroot 0\n");
	str error;
	EXPECT_FALSE(EffectDescriptions::Bind(unknownInput, error));
	EXPECT_NE(error.find("speed"), str::npos) << error;

	EffectDescription backwards = Description();
	backwards.timing.prewarmStepCount = 4;
	backwards.timing.prewarmDeltaTime = std::nanf("");
	EXPECT_FALSE(EffectDescriptions::Validate(backwards, 3, error));
	EXPECT_NE(error.find("prewarm"), str::npos) << error;
	backwards.timing.prewarmDeltaTime = -0.5f;
	error.clear();
	EXPECT_FALSE(EffectDescriptions::Validate(backwards, 3, error));
	EXPECT_NE(error.find("prewarm"), str::npos) << error;

	EffectDescription unpadded = Description();
	unpadded.systems[0].bounds = AssembleOrFail("const 3 0x00000000 0x3F800000 0x00000000\nconst 3 0x40000000 0x40000000 0x40000000\nroot 0\nroot 1\n");
	error.clear();
	EXPECT_FALSE(EffectDescriptions::Validate(unpadded, 3, error));
	EXPECT_NE(error.find("padding"), str::npos) << error;
	EffectDescription automatic = Description();
	automatic.systems[0].boundsMode = EffectBoundsMode::Automatic;
	automatic.timing.cullSimulation = false;
	error.clear();
	EXPECT_FALSE(EffectDescriptions::Validate(automatic, 3, error));
	automatic.systems[0].measuresBounds = true;
	automatic.timing.cullSimulation = true;
	error.clear();
	EXPECT_FALSE(EffectDescriptions::Validate(automatic, 3, error));
	EXPECT_NE(error.find("Automatic"), str::npos) << error;
	automatic.timing.cullSimulation = false;
	error.clear();
	EXPECT_TRUE(EffectDescriptions::Validate(automatic, 3, error)) << error;
}

TEST_F(VisualEffectAssetFixture, StripSystemSurvivesCook) {
	EffectDescription stripped = Description();
	stripped.systems[0].strips = 4;
	stripped.systems[0].outputs[0].strip = true;
	EffectSpawnBlock increment = Block(EffectSpawnKind::IncrementStripIndex, "const 1 0x41000000\nroot 0\n");
	increment.attribute = "stripIndex";
	increment.components = 1;
	stripped.spawners[0].blocks.push_back(std::move(increment));
	VisualEffectAsset asset;
	Vector<ShaderAsset*> shaders = TakeShaders();
	ASSERT_TRUE(EffectAssetAccess::Adopt(asset, std::move(stripped), shaders));

	PakStringPool pool;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	CookOf(asset, pool, descriptor, payload);
	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};
	VisualEffectAsset loaded;
	CookReader descriptorReader(descriptor.data(), descriptor.size(), strings);
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(loaded.LoadCooked(descriptorReader, payloadReader));
	const EffectDescription& description = EffectAssetAccess::Description(loaded);
	ASSERT_EQ(description.systems.size(), 1u);
	EXPECT_EQ(description.systems[0].strips, 4u);
	ASSERT_EQ(description.systems[0].outputs.size(), 1u);
	EXPECT_TRUE(description.systems[0].outputs[0].strip);
	ASSERT_EQ(description.spawners[0].blocks.size(), 2u);
	EXPECT_EQ(description.spawners[0].blocks[1].kind, EffectSpawnKind::IncrementStripIndex);
	EXPECT_EQ(description.spawners[0].blocks[1].attribute, "stripIndex");

	str error;
	EffectDescription uneven = Description();
	uneven.systems[0].strips = 5;
	EXPECT_FALSE(EffectDescriptions::Validate(uneven, 3, error));
	EXPECT_NE(error.find("strip"), str::npos) << error;
	EffectDescription particles = Description();
	particles.systems[0].outputs[0].strip = true;
	EXPECT_FALSE(EffectDescriptions::Validate(particles, 3, error));
	EXPECT_NE(error.find("strip"), str::npos) << error;
}

#else

#include "Shalltear/Core/Settings.h"

#include "ScopedImportCostThreshold.h"

namespace {

	class VisualEffectImportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;
		bool _mounted = false;
		ScopedImportCostThreshold _cacheAll{ScopedImportCostThreshold::CacheEverything};

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() / ("shalltear_effect_import_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			const std::filesystem::path engineAssets = AssetsRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path("VisualEffects.dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Write(const char* name, std::string_view text) { std::ofstream(_root / "Assets" / name, std::ios::binary) << text; }

		static Path AssetPath(const char* name) { return Path(str("project://Assets/") + name); }

		static void CookOf(const Asset& asset, PakStringPool& pool, Vector<u8>& descriptor, Vector<u8>& payload) {
			CookWriter descriptorWriter(descriptor, pool);
			CookWriter payloadWriter(payload, pool);
			ASSERT_TRUE(asset.Cook(descriptorWriter, payloadWriter));
		}
	};

} // anonymous namespace

TEST_F(VisualEffectImportFixture, EffectGraphImportsCooksAndLoadsBack) {
	Write("Rated.sleffect", RatedFountain);
	Asset* asset = AssetDatabase::LoadAsset(AssetPath("Rated.sleffect"));
	if (!asset) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(asset->GetAssetTypeId(), StringHash("VisualEffectAsset"));
	const Guid guid = AssetDatabase::LookupGuid(AssetPath("Rated.sleffect"));
	const std::filesystem::path generated = _root / "Library" / "VisualEffects" / guid.ToString();
	EXPECT_TRUE(std::filesystem::exists(generated / "System0.Initialize.hlsl"));
	EXPECT_TRUE(std::filesystem::exists(generated / "System0.Output0.hlsl"));

	PakStringPool pool;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	CookOf(*asset, pool, descriptor, payload);
	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};

	Asset* loaded = static_cast<Asset*>(TypeRegistry::CreateObjectInstance(TypeRegistry::FindType(StringHash("VisualEffectAsset")), MemTag::Asset));
	ASSERT_NE(loaded, nullptr);
	CookReader descriptorReader(descriptor.data(), descriptor.size(), strings);
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));
	PakStringPool againPool;
	Vector<u8> againDescriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> againPayload{GetMemoryResource(MemTag::Asset)};
	CookOf(*loaded, againPool, againDescriptor, againPayload);
	EXPECT_EQ(againPayload, payload);
	TypeRegistry::DestroyObjectInstance(loaded);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(VisualEffectImportFixture, TemplatesLoadFromPackage) {
	PakStringPool strings;
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	for (u32 index = 0; index < EffectGraphEditOps::TemplateCount(); ++index) {
		SCOPED_TRACE(str(EffectGraphEditOps::TemplateLabel(index)));
		const str name = std::format("Template{}.sleffect", index);
		Write(name.c_str(), EffectGraphEditOps::TemplateDocument(index));
		Asset* imported = AssetDatabase::LoadAsset(AssetPath(name.c_str()));
		if (!imported && index > 0) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		ASSERT_NE(imported, nullptr);
		CookOps::PakAssetSource source;
		str error;
		ASSERT_TRUE(CookOps::CookAsset(*imported, strings, source, error)) << error;
		AssetDatabase::UnloadAsset(imported);
		source.guid = Guid(0x5EED0000u + index, 0xC00Cu);
		source.dependencies.clear();
		sources.push_back(std::move(source));
	}
	CookOps::PakWriteDesc desc;
	desc.strings.assign(strings.Texts().begin(), strings.Texts().end());
	const Path package((_root / "Templates.slpak").generic_string());
	str error;
	ASSERT_TRUE(CookOps::WritePackage(package, sources, desc, error)) << error;
	ASSERT_TRUE(AssetDatabase::MountPackage(package, error)) << error;

	for (u32 index = 0; index < EffectGraphEditOps::TemplateCount(); ++index) {
		SCOPED_TRACE(str(EffectGraphEditOps::TemplateLabel(index)));
		Asset* shipped = AssetDatabase::LoadAssetByGuid(Guid(0x5EED0000u + index, 0xC00Cu));
		ASSERT_NE(shipped, nullptr);
		EXPECT_EQ(shipped->GetAssetTypeId(), StringHash("VisualEffectAsset"));
		AssetDatabase::UnloadAsset(shipped);
	}
}

TEST_F(VisualEffectImportFixture, DepthCollisionsRecordAndBindingsSurviveCook) {
	str graph(RatedFountain);
	const std::string_view update = R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]})";
	graph.replace(graph.find(update), update.size(),
				  R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[{"id":32,"def":"Block/VectorFieldForce"},{"id":30,"def":"Block/CollisionDepth"}]})");
	const std::string_view initialize = R"("settings":{"Capacity":64},"flow":[[2,"In"]],"blocks":[)";
	graph.replace(graph.find(initialize), initialize.size(),
				  R"("settings":{"Capacity":64},"flow":[[2,"In"]],"blocks":[{"id":31,"def":"Block/CollisionDepth","settings":{"Behavior":"Kill"}},)");
	Write("Depth.sleffect", graph);
	Asset* asset = AssetDatabase::LoadAsset(AssetPath("Depth.sleffect"));
	if (!asset) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	PakStringPool pool;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	CookOf(*asset, pool, descriptor, payload);
	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};
	Asset* loaded = static_cast<Asset*>(TypeRegistry::CreateObjectInstance(TypeRegistry::FindType(StringHash("VisualEffectAsset")), MemTag::Asset));
	ASSERT_NE(loaded, nullptr);
	CookReader descriptorReader(descriptor.data(), descriptor.size(), strings);
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));
	PakStringPool againPool;
	Vector<u8> againDescriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> againPayload{GetMemoryResource(MemTag::Asset)};
	CookOf(*loaded, againPool, againDescriptor, againPayload);
	EXPECT_EQ(againPayload, payload);
	TypeRegistry::DestroyObjectInstance(loaded);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(VisualEffectImportFixture, OutputTextureHeldByEffect) {
	const std::filesystem::path fixtures = AssetsRepoDir("Source/Tests/Fixtures/Graphics");
	ASSERT_FALSE(fixtures.empty());
	std::filesystem::copy_file(fixtures / "checker.png", _root / "Assets" / "checker.png");
	Asset* texture = AssetDatabase::LoadAsset(AssetPath("checker.png"));
	ASSERT_NE(texture, nullptr);
	const Guid guid = texture->assetId;
	const int before = texture->GetRefCount();

	str graph(RatedFountain);
	const str output = R"({"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[8,"In"]],"blocks":[]})";
	graph.replace(graph.find(output), output.size(),
				  std::format(R"({{"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[8,"In"]],"in":{{"Main Texture":{{"value":"{}"}}}},"blocks":[]}})",
							  guid.ToString()));
	Write("Textured.sleffect", graph);
	Asset* asset = AssetDatabase::LoadAsset(AssetPath("Textured.sleffect"));
	if (!asset) {
		AssetDatabase::UnloadAsset(texture);
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(texture->GetRefCount(), before + 1);

	PakStringPool pool;
	CookOps::PakAssetSource source;
	str error;
	ASSERT_TRUE(CookOps::CookAsset(*asset, pool, source, error)) << error;
	EXPECT_NE(std::find(source.dependencies.begin(), source.dependencies.end(), guid), source.dependencies.end());

	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(texture->GetRefCount(), before);
	AssetDatabase::UnloadAsset(texture);
}

TEST_F(VisualEffectImportFixture, BuildCarriesUsedKernels) {
	if (!_mounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	static constexpr const char* KERNELS[] = {"ParticleReset", "ParticleArguments", "ParticleSortLocal", "ParticleSortMerge", "ParticleStrips"};
	Guid kernels[std::size(KERNELS)];
	for (size_t k = 0; k < std::size(KERNELS); ++k) {
		kernels[k] = AssetDatabase::LookupGuid(Path(std::format("engine://Shaders/VisualEffects/{}.hlsl", KERNELS[k])));
		ASSERT_TRUE(kernels[k].IsValid()) << KERNELS[k];
	}

	str trail;
	for (u32 index = 0; index < EffectGraphEditOps::TemplateCount(); ++index) {
		if (EffectGraphEditOps::TemplateLabel(index).ends_with("/Head & Trail")) {
			trail = EffectGraphEditOps::TemplateDocument(index);
		}
	}
	ASSERT_FALSE(trail.empty());
	str unsorted(RatedFountain);
	const std::string_view output = R"({"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"flow":[[8,"In"]],"blocks":[]})";
	unsorted.replace(unsorted.find(output), output.size(),
					 R"({"id":9,"def":"Context/OutputParticlePrimitive","x":0,"y":600,"settings":{"Sort":"Off"},"flow":[[8,"In"]],"blocks":[]})");

	struct Case {
		const char* name;
		str document;
		bool carries[std::size(KERNELS)];
	};
	const Case cases[] = {
		{"Trail.sleffect", trail, {true, true, true, true, true}},
		{"Sorted.sleffect", str(RatedFountain), {true, true, true, true, false}},
		{"Unsorted.sleffect", unsorted, {true, true, false, false, false}},
	};
	for (const Case& effect : cases) {
		SCOPED_TRACE(effect.name);
		Write(effect.name, effect.document);
		Asset* asset = AssetDatabase::LoadAsset(AssetPath(effect.name));
		if (!asset) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
		roots.push_back(asset->assetId);
		AssetDatabase::UnloadAsset(asset);

		CookOps::CookClosure closure;
		str error;
		ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;
		for (size_t k = 0; k < std::size(KERNELS); ++k) {
			const bool carried = std::find(closure.assets.begin(), closure.assets.end(), kernels[k]) != closure.assets.end();
			EXPECT_EQ(carried, effect.carries[k])
				<< KERNELS[k];
		}
	}
}

namespace {

	void WriteVectorField(const std::filesystem::path& file, std::string_view fourcc, u16 width, u16 height, u16 depth, std::span<const f32> values) {
		std::ofstream out(file, std::ios::binary);
		out.write(fourcc.data(), 4);
		for (u16 size : {width, height, depth}) {
			const u8 bytes[2] = {static_cast<u8>(size & 0xFF), static_cast<u8>(size >> 8)};
			out.write(reinterpret_cast<const char*>(bytes), 2);
		}
		out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size_bytes()));
	}

	f32 Half(const TextureAsset& texture, size_t texel, u32 channel) {
		u16 half = 0;
		std::memcpy(&half, texture.pixels.data() + (texel * 4 + channel) * 2, sizeof(half));
		return Math::HalfToFloat(half);
	}

} // anonymous namespace

TEST_F(VisualEffectImportFixture, VectorFieldIsFloatVolume) {
	const f32 vectors[] = {0.5f, -1.5f, 2.25f, 0.125f, 0.0f, -0.25f, 1.0f, 2.0f, 3.0f, -4.0f, 0.75f, 0.5f, 8.0f, -8.0f, 0.0625f, 1.5f, 1.25f, -0.5f};
	WriteVectorField(_root / "Assets" / "Wind.vf", "VF_V", 2, 1, 3, vectors);
	Asset* asset = AssetDatabase::LoadAsset(AssetPath("Wind.vf"));
	ASSERT_NE(asset, nullptr);
	ASSERT_EQ(asset->GetAssetTypeId(), StringHash("TextureAsset"));
	const TextureAsset& wind = *static_cast<TextureAsset*>(asset);
	EXPECT_EQ(wind.GetDimension(), RHI::TextureDimension::Texture3D);
	EXPECT_EQ(wind.size.x, 2);
	EXPECT_EQ(wind.size.y, 1);
	EXPECT_EQ(wind.slices, 3u);
	EXPECT_EQ(wind.format, RHI::Format::RGBA16Float);
	ASSERT_EQ(wind.pixels.size(), 6u * 8u);
	for (size_t texel = 0; texel < 6; ++texel) {
		for (u32 c = 0; c < 3; ++c) {
			EXPECT_EQ(Half(wind, texel, c), vectors[texel * 3 + c]) << "texel " << texel << " channel " << c;
		}
		EXPECT_EQ(Half(wind, texel, 3), 1.0f);
	}
	AssetDatabase::UnloadAsset(asset);

	const f32 distances[] = {-0.5f, 0.25f, 1.75f, -2.0f};
	WriteVectorField(_root / "Assets" / "Field.vf", "VF_F", 1, 2, 2, distances);
	asset = AssetDatabase::LoadAsset(AssetPath("Field.vf"));
	ASSERT_NE(asset, nullptr);
	const TextureAsset& field = *static_cast<TextureAsset*>(asset);
	for (size_t texel = 0; texel < 4; ++texel) {
		EXPECT_EQ(Half(field, texel, 0), distances[texel]);
		EXPECT_EQ(Half(field, texel, 1), 0.0f);
	}
	const Guid guid = asset->assetId;
	AssetDatabase::UnloadAsset(asset);

	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("outputFormat", JSONValue::MakeString("byte"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(AssetPath("Field.vf"), settings));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(AssetPath("Field.vf")));
	asset = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(asset, nullptr);
	const TextureAsset& bytes = *static_cast<TextureAsset*>(asset);
	EXPECT_EQ(bytes.format, RHI::Format::RGBA8Unorm);
	ASSERT_EQ(bytes.pixels.size(), 4u * 4u);
	EXPECT_EQ(bytes.pixels[0], 0u);
	EXPECT_EQ(bytes.pixels[4], 64u);
	EXPECT_EQ(bytes.pixels[8], 255u);
	AssetDatabase::UnloadAsset(asset);

	std::ofstream(_root / "Assets" / "Short.vf", std::ios::binary) << "VF_F\x02";
	WriteVectorField(_root / "Assets" / "Long.vf", "VF_F", 1, 1, 1, distances);
	EXPECT_EQ(AssetDatabase::LoadAsset(AssetPath("Short.vf")), nullptr);
	EXPECT_EQ(AssetDatabase::LoadAsset(AssetPath("Long.vf")), nullptr);
}

TEST_F(VisualEffectImportFixture, FieldVolumeHeldByEffect) {
	const f32 wind[] = {1.0f, 0.0f, 0.0f};
	WriteVectorField(_root / "Assets" / "Wind.vf", "VF_V", 1, 1, 1, wind);
	Asset* texture = AssetDatabase::LoadAsset(AssetPath("Wind.vf"));
	ASSERT_NE(texture, nullptr);
	const Guid guid = texture->assetId;
	const int before = texture->GetRefCount();

	str graph(RatedFountain);
	const str update = R"({"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]})";
	graph.replace(graph.find(update), update.size(),
				  std::format(R"({{"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[
					{{"id":7,"def":"Block/VectorFieldForce","in":{{"Vector Field":{{"value":"{}"}}}}}}]}})",
							  guid.ToString()));
	Write("Windy.sleffect", graph);
	Asset* asset = AssetDatabase::LoadAsset(AssetPath("Windy.sleffect"));
	if (!asset) {
		AssetDatabase::UnloadAsset(texture);
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(texture->GetRefCount(), before + 1);

	PakStringPool pool;
	CookOps::PakAssetSource source;
	str error;
	ASSERT_TRUE(CookOps::CookAsset(*asset, pool, source, error)) << error;
	EXPECT_NE(std::find(source.dependencies.begin(), source.dependencies.end(), guid), source.dependencies.end());

	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(texture->GetRefCount(), before);
	AssetDatabase::UnloadAsset(texture);
}

TEST_F(VisualEffectImportFixture, MeshHeldByEffect) {
	Asset* cube = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(cube, nullptr);
	const Guid guid = cube->assetId;
	const int before = cube->GetRefCount();

	str graph(RatedFountain);
	const std::string_view block = R"({"id":6,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,1,0]}}})";
	graph.replace(graph.find(block), block.size(), str(block) + R"(,{"id":7,"def":"Block/PositionMesh"})");
	Write("Meshed.sleffect", graph);
	Asset* asset = AssetDatabase::LoadAsset(AssetPath("Meshed.sleffect"));
	if (!asset) {
		AssetDatabase::UnloadAsset(cube);
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	EXPECT_EQ(cube->GetRefCount(), before + 1);

	PakStringPool pool;
	Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	CookOf(*asset, pool, descriptor, payload);
	{
		PakStringPool scratchPool;
		CookOps::PakAssetSource source;
		str error;
		ASSERT_TRUE(CookOps::CookAsset(*asset, scratchPool, source, error)) << error;
		EXPECT_NE(std::find(source.dependencies.begin(), source.dependencies.end(), guid), source.dependencies.end());
	}
	Vector<u32> offsets{GetMemoryResource(MemTag::Asset)};
	Vector<u8> blob{GetMemoryResource(MemTag::Asset)};
	for (const str& text : pool.Texts()) {
		offsets.push_back(static_cast<u32>(blob.size()));
		blob.insert(blob.end(), text.begin(), text.end());
	}
	offsets.push_back(static_cast<u32>(blob.size()));
	const PakStringTable strings{offsets.data(), blob.data(), static_cast<u32>(pool.Count()), static_cast<u32>(blob.size())};
	Asset* loaded = static_cast<Asset*>(TypeRegistry::CreateObjectInstance(TypeRegistry::FindType(StringHash("VisualEffectAsset")), MemTag::Asset));
	ASSERT_NE(loaded, nullptr);
	CookReader descriptorReader(descriptor.data(), descriptor.size(), strings);
	CookReader payloadReader(payload.data(), payload.size());
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));
	PakStringPool againPool;
	Vector<u8> againDescriptor{GetMemoryResource(MemTag::Asset)};
	Vector<u8> againPayload{GetMemoryResource(MemTag::Asset)};
	CookOf(*loaded, againPool, againDescriptor, againPayload);
	EXPECT_EQ(againPayload, payload);
	TypeRegistry::DestroyObjectInstance(loaded);

	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(cube->GetRefCount(), before);

	str cleared(graph);
	const std::string_view meshed = R"({"id":7,"def":"Block/PositionMesh"})";
	cleared.replace(cleared.find(meshed), meshed.size(), R"({"id":7,"def":"Block/PositionMesh","in":{"Mesh":{"value":null}}})");
	Write("Cleared.sleffect", cleared);
	Asset* none = AssetDatabase::LoadAsset(AssetPath("Cleared.sleffect"));
	ASSERT_NE(none, nullptr);
	EXPECT_EQ(cube->GetRefCount(), before);
	AssetDatabase::UnloadAsset(none);
	AssetDatabase::UnloadAsset(cube);
}

TEST_F(VisualEffectImportFixture, FailingGraphImportsNothing) {
	str graph(RatedFountain);
	const str block = R"({"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}})";
	graph.replace(graph.find(block), block.size(), R"({"id":5,"def":"Block/ConstantSpawnRate"})");
	Write("Broken.sleffect", graph);
	EXPECT_EQ(AssetDatabase::LoadAsset(AssetPath("Broken.sleffect")), nullptr);
}

TEST_F(VisualEffectImportFixture, CreateMenuTemplatesImport) {
	Vector<AssetBrowserOps::CreateMenuEntry> entries{GetMemoryResource(MemTag::Editor)};
	AssetBrowserOps::GetCreateMenuEntries(entries);
	for (u32 index = 0; index < EffectGraphEditOps::TemplateCount(); ++index) {
		const std::string_view label = EffectGraphEditOps::TemplateLabel(index);
		auto entry = std::find_if(entries.begin(), entries.end(), [&](const AssetBrowserOps::CreateMenuEntry& held) { return held.label == label; });
		ASSERT_NE(entry, entries.end()) << label << "'";
		EXPECT_EQ(entry->typeId, StringHash("VisualEffectAsset"));
		EXPECT_EQ(entry->fileName, "New Effect.sleffect");
		EXPECT_EQ(entry->templateIndex, index);

		str path;
		const Guid created = AssetBrowserOps::CreateAsset(Path("project://Assets"), entry->typeId, entry->fileName, &path, entry->templateIndex);
		ASSERT_TRUE(created.IsValid()) << "'" << label;
		EXPECT_EQ(FileSystem::ReadTextFile(Path("project://" + path), MemTag::Editor), EffectGraphEditOps::TemplateDocument(index));
		Asset* asset = AssetDatabase::LoadAssetByGuid(created);
		if (!asset && index > 0) {
			GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
		}
		ASSERT_NE(asset, nullptr) << "'" << label;
		EXPECT_EQ(asset->GetAssetTypeId(), StringHash("VisualEffectAsset"));
	}
}

TEST_F(VisualEffectImportFixture, NodeShaderToggleReimports) {
	constexpr std::string_view Shaded = R"({"_type":"EffectGraph","version":4,"nextId":20,"settings":{},"properties":[],"nodes":[],
"contexts":[
	{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{"id":3,"def":"Block/ConstantSpawnRate","in":{"Rate":{"value":[8]}}}]},
	{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{"Capacity":16},"flow":[[2,"In"]],"blocks":[
		{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[1]}}}]},
	{"id":7,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[]},
	{"id":8,"def":"Context/OutputParticleNodeShaderPrimitive","x":0,"y":600,"flow":[[7,"In"]],"blocks":[]}],
"groups":[],"notes":[]})";
	Write("Shaded.sleffect", Shaded);
	Asset* on = AssetDatabase::LoadAsset(AssetPath("Shaded.sleffect"));
	if (!on) {
		GTEST_SKIP() << "import failed (dxcompiler.dll unavailable?)";
	}
	AssetDatabase::UnloadAsset(on);
	AssetDatabase::CollectGarbage();

	{
		const NodeShaderTesting::NodeShaderSwitchedOff off;
		ASSERT_TRUE(off.HadTarget());
		Asset* served = AssetDatabase::LoadAsset(AssetPath("Shaded.sleffect"));
		EXPECT_EQ(served, nullptr);
		if (served) {
			AssetDatabase::UnloadAsset(served);
		}
		AssetDatabase::CollectGarbage();
	}

	Asset* again = AssetDatabase::LoadAsset(AssetPath("Shaded.sleffect"));
	EXPECT_NE(again, nullptr);
	if (again) {
		AssetDatabase::UnloadAsset(again);
	}
}

#endif
