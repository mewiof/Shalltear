#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "EffectSubgraphImporter.h"
#include "GraphCompiler.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>

#ifndef SHALLTEAR_SHARED
#include "EffectKernelHarness.h"
#endif

using namespace Shalltear;

namespace {

	GraphDocument Parse(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	constexpr std::string_view DOUBLER = R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,"settings":{"category":"Mine"},
"properties":[{"id":1,"name":"In","reference":"In","type":"Float","default":[0.25]},
{"id":2,"name":"Out","reference":"Out","type":"Float","default":[0],"settings":{"output":true}}],
"nodes":[{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}},
{"id":11,"def":"Math/Multiply","x":0,"y":0,"in":{"A":{"from":[10,"Out"]},"B":{"value":[2]}}},
{"id":12,"def":"Input/Property","x":0,"y":0,"settings":{"property":2},"in":{"Value":{"from":[11,"Out"]}}}],
"contexts":[],"groups":[],"notes":[]})";

	str BlockSubgraph(std::string_view blocks, std::string_view suitable = "InitAndUpdateAndOutput", std::string_view nodes = {}) {
		return std::format(R"({{"_type":"VisualEffectSubgraphBlock","version":1,"nextId":40,"settings":{{"category":"Blocks"}},
"properties":[{{"id":1,"name":"Amount","reference":"Amount","type":"Float","default":[0.5]}}],
"nodes":[{{"id":2,"def":"Input/Property","x":0,"y":0,"settings":{{"property":1}}}}{}{}],
"contexts":[{{"id":3,"def":"Context/BlockSubgraph","x":0,"y":0,"settings":{{"Suitable Contexts":"{}"}},"blocks":[{}]}}],"groups":[],"notes":[]}})",
						   nodes.empty() ? "" : ",", nodes, suitable, blocks);
	}

	str Effect(std::string_view initialize, std::string_view nodes = {}, std::string_view update = {}) {
		return std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[{}],"contexts":[
{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[{}]}},
{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]}}]}})",
						   nodes, initialize, update);
	}

	str Uses(std::string_view def, u32 id, const Guid& asset, std::string_view rest = {}) {
		return std::format(R"({{"id":{},"def":"{}","settings":{{"asset":"{}"}}{}}})", id, def, asset.ToString(), rest);
	}

	class GraphEffectSubgraphFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		char _owner = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() / ("shalltear_effect_subgraph_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			EffectSubgraphImporters::Register(&_owner);
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			AssetImporterRegistry::UnregisterOwnedBy(&_owner);
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid Add(const char* fileName, std::string_view text) {
			FileSystem::WriteTextFile(Path((_root / "Assets" / fileName).generic_string()), str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://Assets/") + fileName));
		}

		static bool Compile(const str& text, GraphEffect& out, Vector<GraphDiagnostic>& diagnostics) {
			GraphDefinitions definitions;
			return GraphEffectTarget::Compile(Parse(text), "Subgraph", out, diagnostics, &definitions);
		}

		static str Said(const Vector<GraphDiagnostic>& diagnostics) {
			str all;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				all += std::format("[node {}] {}\n", diagnostic.node, diagnostic.message);
			}
			return all;
		}

		static bool Program(const GraphDocument& document, std::span<const GraphCompileRoot> roots, GraphProgram& out, Vector<GraphDiagnostic>& diagnostics) {
			Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
			GraphEffectTarget::KernelBindings(GraphEffectKernel::Update, bindings);
			GraphDefinitions definitions;
			GraphCompileRequest request;
			request.roots = roots;
			request.bindings = bindings;
			request.definitions = &definitions;
			request.space = GraphSpace::Local;
			return GraphCompiler::Compile(document, request, out, diagnostics);
		}
	};

} // anonymous namespace

TEST_F(GraphEffectSubgraphFixture, OperatorSubgraphPortsFromProperties) {
	const Guid doubler = Add("Doubler.sleffectop", DOUBLER);
	ASSERT_TRUE(doubler.IsValid());
	const GraphDocument graph = Parse(Effect({}, Uses(GRAPH_SUBGRAPH_NODE, 50, doubler)));
	GraphDefinitions definitions;
	str error;
	const GraphNodeDefinition* definition = definitions.Find(graph, graph.nodes[0], &error);
	ASSERT_NE(definition, nullptr) << error;
	EXPECT_EQ(definition->name, "Doubler");
	EXPECT_EQ(definition->category, "Mine");
	ASSERT_EQ(definition->inputs.size(), 1u);
	EXPECT_EQ(definition->inputs[0].name, "1");
	EXPECT_EQ(definition->inputs[0].label, "In");
	EXPECT_FLOAT_EQ(definition->inputs[0].defaultValue[0], 0.25f);
	ASSERT_EQ(definition->outputs.size(), 1u);
	EXPECT_EQ(definition->outputs[0].name, "2");
	EXPECT_EQ(definition->outputs[0].label, "Out");
	EXPECT_EQ(definition->outputs[0].type, GraphPortType::Float);
	ASSERT_NE(definition->subgraph, nullptr);
	EXPECT_EQ(definition->subgraph->kind, GraphSubgraphKind::EffectOperator);
}

TEST_F(GraphEffectSubgraphFixture, OperatorSubgraphInlinesAndNests) {
	const Guid doubler = Add("Doubler.sleffectop", DOUBLER);
	ASSERT_TRUE(doubler.IsValid());
	const str twice = std::format(R"({{"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,
"properties":[{{"id":1,"name":"X","reference":"X","type":"Float","default":[0]}},{{"id":2,"name":"Y","reference":"Y","type":"Float","default":[0],"settings":{{"output":true}}}}],
"nodes":[{{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{{"property":1}}}},{},{},
{{"id":13,"def":"Input/Property","x":0,"y":0,"settings":{{"property":2}},"in":{{"Value":{{"from":[12,"2"]}}}}}}],
"contexts":[],"groups":[],"notes":[]}})",
								  Uses(GRAPH_SUBGRAPH_NODE, 11, doubler, R"(,"in":{"1":{"from":[10,"Out"]}})"),
								  Uses(GRAPH_SUBGRAPH_NODE, 12, doubler, R"(,"in":{"1":{"from":[11,"2"]}})"));
	const Guid nested = Add("Twice.sleffectop", twice);
	ASSERT_TRUE(nested.IsValid());

	const GraphDocument graph = Parse(Effect({}, R"({"id":40,"def":"Input/Float","x":0,"y":0,"in":{"X":{"value":[3]}}},)" +
													 Uses(GRAPH_SUBGRAPH_NODE, 41, doubler, R"(,"in":{"1":{"from":[40,"Out"]}})") + "," +
													 Uses(GRAPH_SUBGRAPH_NODE, 42, nested, R"(,"in":{"1":{"from":[40,"Out"]}})") + "," +
													 Uses(GRAPH_SUBGRAPH_NODE, 43, doubler)));
	const GraphCompileRoot roots[] = {{41, "2", GraphStageVertex}, {42, "2", GraphStageVertex}, {43, "2", GraphStageVertex}};
	GraphProgram program;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Program(graph, roots, program, diagnostics)) << Said(diagnostics);
	const f32 expected[] = {6.0f, 12.0f, 0.5f};
	for (u32 i = 0; i < 3; ++i) {
		const std::span<const f32> value = program.ConstantOf(program.roots[i].value);
		ASSERT_EQ(value.size(), 1u) << "root " << i;
		EXPECT_FLOAT_EQ(value[0], expected[i]) << "root " << i;
	}
	for (const GraphValue& value : program.values) {
		EXPECT_TRUE(value.node == 0 || (value.node >= 40 && value.node <= 43));
	}
}

TEST_F(GraphEffectSubgraphFixture, OutputFeedRules) {
	const Guid bare = Add("Bare.sleffectop", R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":5,
"properties":[{"id":1,"name":"Seven","reference":"Seven","type":"Float","default":[7],"settings":{"output":true}}],
"nodes":[],"contexts":[],"groups":[],"notes":[]})");
	ASSERT_TRUE(bare.IsValid());
	const GraphDocument graph = Parse(Effect({}, Uses(GRAPH_SUBGRAPH_NODE, 50, bare)));
	const GraphCompileRoot root{50, "1", GraphStageVertex};
	GraphProgram program;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Program(graph, {&root, 1}, program, diagnostics)) << Said(diagnostics);
	const std::span<const f32> seven = program.ConstantOf(program.roots[0].value);
	ASSERT_EQ(seven.size(), 1u);
	EXPECT_FLOAT_EQ(seven[0], 7.0f);

	const Guid twice = Add("TwoFeeds.sleffectop", R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,
"properties":[{"id":1,"name":"Out","reference":"Out","type":"Float","default":[0],"settings":{"output":true}}],
"nodes":[{"id":10,"def":"Input/Float","x":0,"y":0},
{"id":11,"def":"Input/Property","x":0,"y":0,"settings":{"property":1},"in":{"Value":{"from":[10,"Out"]}}},
{"id":12,"def":"Input/Property","x":0,"y":0,"settings":{"property":1},"in":{"Value":{"from":[10,"Out"]}}}],
"contexts":[],"groups":[],"notes":[]})");
	ASSERT_TRUE(twice.IsValid());
	const GraphDocument fed = Parse(Effect({}, Uses(GRAPH_SUBGRAPH_NODE, 50, twice)));
	GraphDefinitions definitions;
	str error;
	EXPECT_EQ(definitions.Find(fed, fed.nodes[0], &error), nullptr);
	EXPECT_NE(error.find("2 nodes feeding its Output 'Out'"), str::npos) << error;
}

TEST_F(GraphEffectSubgraphFixture, NewSubgraphsAreItsTemplates) {
	const Guid range = Add("Range.sleffectop", EffectGraphEditOps::OperatorSubgraphTemplate());
	const Guid empty = Add("Empty.sleffectblock", EffectGraphEditOps::BlockSubgraphTemplate());
	ASSERT_TRUE(range.IsValid() && empty.IsValid());
	GraphDefinitions definitions;
	str error;
	const GraphSubgraph* made = definitions.Subgraph(range, &error);
	ASSERT_NE(made, nullptr) << error;
	EXPECT_EQ(made->category, GRAPH_EFFECT_OPERATOR_SUBGRAPH_CATEGORY);
	ASSERT_EQ(made->inputs.size(), 1u);
	EXPECT_EQ(made->inputs[0].label, "range");
	ASSERT_EQ(made->outputs.size(), 2u);
	EXPECT_EQ(made->outputs[0].label, "min");
	EXPECT_EQ(made->outputs[1].label, "max");

	const str min(made->outputs[0].name);
	const str max(made->outputs[1].name);
	const GraphDocument graph = Parse(Effect({}, R"({"id":40,"def":"Input/Float","x":0,"y":0,"in":{"X":{"value":[3]}}},)" +
													 Uses(GRAPH_SUBGRAPH_NODE, 41, range, std::format(R"(,"in":{{"{}":{{"from":[40,"Out"]}}}})", made->inputs[0].name))));
	const GraphCompileRoot roots[] = {{41, min, GraphStageVertex}, {41, max, GraphStageVertex}};
	GraphProgram program;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Program(graph, roots, program, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(program.ConstantOf(program.roots[0].value).size(), 1u);
	ASSERT_EQ(program.ConstantOf(program.roots[1].value).size(), 1u);
	EXPECT_FLOAT_EQ(program.ConstantOf(program.roots[0].value)[0], -3.0f);
	EXPECT_FLOAT_EQ(program.ConstantOf(program.roots[1].value)[0], 3.0f);

	const GraphSubgraph* block = definitions.Subgraph(empty, &error);
	ASSERT_NE(block, nullptr) << error;
	EXPECT_EQ(block->kind, GraphSubgraphKind::EffectBlock);
	EXPECT_EQ(block->category, GRAPH_EFFECT_BLOCK_SUBGRAPH_CATEGORY);
	EXPECT_NE(block->blockContext, 0u);
	EXPECT_EQ(block->suitable, GRAPH_EFFECT_DEFAULT_SUITABLE_CONTEXTS);
	EXPECT_TRUE(GraphEffectRules::Suits(block->suitable, GRAPH_EFFECT_INITIALIZE));
	EXPECT_TRUE(GraphEffectRules::Suits(block->suitable, GRAPH_EFFECT_OUTPUT));
	EXPECT_FALSE(GraphEffectRules::Suits(block->suitable, GRAPH_EFFECT_SPAWN));
}

TEST_F(GraphEffectSubgraphFixture, ForeignSubgraphRefused) {
	const Guid doubler = Add("Doubler.sleffectop", DOUBLER);
	const Guid block = Add("Sized.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"size"}})"));
	ASSERT_TRUE(doubler.IsValid() && block.IsValid());
	GraphDefinitions definitions;
	str error;

	GraphDocument shader;
	shader.type = "NodeShader";
	GraphNode node;
	node.id = 5;
	node.def = str(GRAPH_SUBGRAPH_NODE);
	node.settings.push_back({str(GRAPH_SUBGRAPH_ASSET_SETTING), NodeShaderEditOps::Quote(doubler.ToString())});
	EXPECT_EQ(definitions.Find(shader, node, &error), nullptr);
	EXPECT_NE(error.find("an effect operator"), str::npos) << error;

	const GraphDocument effect = Parse(Effect(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, doubler), Uses(GRAPH_SUBGRAPH_NODE, 50, block)));
	EXPECT_EQ(definitions.Find(effect, effect.nodes[0], &error), nullptr);
	EXPECT_NE(error.find("is an effect block, where it takes an effect operator"), str::npos) << error;
	EXPECT_EQ(definitions.Find(effect, effect.contexts[0].blocks[0], &error), nullptr);
}

TEST_F(GraphEffectSubgraphFixture, CycleThroughSubgraphsIsRefusedNamingChain) {
	const Guid a = Add("A.sleffectop", DOUBLER);
	const Guid b = Add("B.sleffectop", DOUBLER);
	ASSERT_TRUE(a.IsValid() && b.IsValid());
	const auto using_ = [](const Guid& other) {
		return std::format(R"({{"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,
"properties":[{{"id":2,"name":"Out","reference":"Out","type":"Float","default":[0],"settings":{{"output":true}}}}],
"nodes":[{},{{"id":12,"def":"Input/Property","x":0,"y":0,"settings":{{"property":2}},"in":{{"Value":{{"from":[11,"2"]}}}}}}],
"contexts":[],"groups":[],"notes":[]}})",
						   std::format(R"({{"id":11,"def":"Utility/Subgraph","settings":{{"asset":"{}"}}}})", other.ToString()));
	};
	Add("A.sleffectop", using_(b));
	Add("B.sleffectop", using_(a));
	const GraphDocument graph = Parse(Effect(R"({"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[50,"2"]}}})",
											 Uses(GRAPH_SUBGRAPH_NODE, 50, a)));
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	GraphDefinitions definitions;
	EXPECT_FALSE(GraphEffectTarget::Compile(graph, "Cycle", effect, diagnostics, &definitions));
	EXPECT_NE(Said(diagnostics).find("which uses itself: 'A' -> 'B' -> 'A'"), str::npos) << Said(diagnostics);

	const Guid x = Add("X.sleffectblock", BlockSubgraph({}));
	const Guid y = Add("Y.sleffectblock", BlockSubgraph({}));
	Add("X.sleffectblock", BlockSubgraph(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 4, y)));
	Add("Y.sleffectblock", BlockSubgraph(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 4, x)));
	diagnostics.clear();
	GraphDefinitions fresh;
	EXPECT_FALSE(GraphEffectTarget::Compile(Parse(Effect(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, x))), "Cycle", effect, diagnostics, &fresh));
	ASSERT_FALSE(diagnostics.empty());
	EXPECT_EQ(diagnostics[0].node, 60u);
	EXPECT_NE(Said(diagnostics).find("which uses itself: 'X' -> 'Y' -> 'X'"), str::npos) << Said(diagnostics);
}

TEST_F(GraphEffectSubgraphFixture, BlockSubgraphRespectsContexts) {
	const Guid drag = Add("Drag.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/LinearDrag"})", "Update"));
	ASSERT_TRUE(drag.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(Effect(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, drag)), effect, diagnostics));
	EXPECT_NE(Said(diagnostics).find("uses 'Drag' (its Suitable Contexts are Update, Initialize Particle context 1 isn't one)"), str::npos)
		<< Said(diagnostics);
	diagnostics.clear();
	EXPECT_TRUE(Compile(Effect({}, {}, Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, drag)), effect, diagnostics)) << Said(diagnostics);

	GraphDocument document = Parse(Effect({}));
	GraphDefinitions definitions;
	EffectGraphEditOps::BlockChoice choice;
	choice.def = str(GRAPH_EFFECT_SUBGRAPH_BLOCK);
	choice.settings.push_back({str(GRAPH_SUBGRAPH_ASSET_SETTING), NodeShaderEditOps::Quote(drag.ToString())});
	EXPECT_EQ(EffectGraphEditOps::AddBlock(document, 1, 0, choice, &definitions), 0u);
	EXPECT_NE(EffectGraphEditOps::AddBlock(document, 2, 0, choice, &definitions), 0u);
	const u32 moved = document.contexts[1].blocks[0].id;
	EXPECT_FALSE(EffectGraphEditOps::CanMoveBlocks(document, {&moved, 1}, 1, &definitions));
}

TEST_F(GraphEffectSubgraphFixture, InnerBlockRefusalNamesSubgraph) {
	const Guid orient = Add("Turn.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/Orient"})"));
	const Guid trigger = Add("Fire.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/TriggerEvent"})", "InitAndUpdate"));
	ASSERT_TRUE(orient.IsValid() && trigger.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(Effect(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, orient)), effect, diagnostics));
	ASSERT_FALSE(diagnostics.empty());
	EXPECT_EQ(diagnostics[0].node, 60u);
	EXPECT_NE(diagnostics[0].message.find("in subgraph 'Turn', block 4 is a"), str::npos) << Said(diagnostics);
	EXPECT_NE(diagnostics[0].message.find("which Initialize Particle context 1 cannot hold"), str::npos) << Said(diagnostics);

	diagnostics.clear();
	EXPECT_FALSE(Compile(Effect(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, trigger)), effect, diagnostics));
	EXPECT_NE(Said(diagnostics).find("is a Trigger Event. No GPU Event"), str::npos) << Said(diagnostics);

	diagnostics.clear();
	EXPECT_TRUE(Compile(Effect(Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, orient, R"(,"enabled":false)")), effect, diagnostics)) << Said(diagnostics);
}

TEST_F(GraphEffectSubgraphFixture, SpawnerSubgraphRunsSpawnBlocks) {
	const Guid rate = Add("Rate.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[2,"Out"]}}})", "Spawner"));
	ASSERT_TRUE(rate.IsValid());
	const str graph = std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[],"contexts":[
{{"id":5,"def":"Context/Spawn","blocks":[{}]}},
{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"flow":[[5,"In"]],"blocks":[]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[1,"In"]],"blocks":[]}}]}})",
								  Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, rate, R"(,"in":{"1":{"value":[7]}})"));
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(graph, effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.spawners.size(), 1u);
	ASSERT_EQ(effect.spawners[0].blocks.size(), 1u);
	const GraphEffectSpawnBlock& block = effect.spawners[0].blocks[0];
	EXPECT_EQ(block.kind, GraphEffectSpawnBlock::Kind::ConstantRate);
	EXPECT_EQ(block.node, 60u);
	const std::span<const f32> seven = block.program.ConstantOf(block.program.roots[0].value);
	ASSERT_EQ(seven.size(), 1u);
	EXPECT_FLOAT_EQ(seven[0], 7.0f);
}

TEST_F(GraphEffectSubgraphFixture, SubgraphRandomPerUse) {
	const Guid dice = Add("Dice.sleffectop", R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,
"properties":[{"id":1,"name":"A","reference":"A","type":"Float","default":[0],"settings":{"output":true}},
{"id":2,"name":"B","reference":"B","type":"Float","default":[0],"settings":{"output":true}}],
"nodes":[{"id":10,"def":"Random/RandomNumber","x":0,"y":0,"settings":{"Constant":false}},
{"id":11,"def":"Random/RandomNumber","x":0,"y":0,"settings":{"Constant":false}},
{"id":12,"def":"Input/Property","x":0,"y":0,"settings":{"property":1},"in":{"Value":{"from":[10,"r"]}}},
{"id":13,"def":"Input/Property","x":0,"y":0,"settings":{"property":2},"in":{"Value":{"from":[11,"r"]}}}],
"contexts":[],"groups":[],"notes":[]})");
	ASSERT_TRUE(dice.IsValid());
	const GraphDocument graph = Parse(Effect({}, Uses(GRAPH_SUBGRAPH_NODE, 50, dice) + "," + Uses(GRAPH_SUBGRAPH_NODE, 51, dice) +
													 R"(,{"id":52,"def":"Random/RandomNumber","x":0,"y":0,"settings":{"Constant":false}})"));
	const GraphCompileRoot roots[] = {{50, "1", GraphStageVertex}, {50, "2", GraphStageVertex}, {51, "1", GraphStageVertex}, {52, "r", GraphStageVertex}};
	GraphProgram program;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Program(graph, roots, program, diagnostics)) << Said(diagnostics);
	Vector<u32> salts{GetMemoryResource(MemTag::Editor)};
	for (const GraphValue& value : program.values) {
		if (value.op == GraphOp::Random) {
			salts.push_back(value.payload);
		}
	}
	ASSERT_EQ(salts.size(), 4u);
	std::sort(salts.begin(), salts.end());
	EXPECT_EQ(std::unique(salts.begin(), salts.end()), salts.end());
	EXPECT_NE(std::find(salts.begin(), salts.end(), 52u * 8u), salts.end());
	EXPECT_EQ(GraphCompiler::Identity(52, {}), 52u);
}

TEST_F(GraphEffectSubgraphFixture, SubgraphTexturePerUse) {
	const Guid sampler = Add("Sampler.sleffectop", R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,
"properties":[{"id":1,"name":"Texel","reference":"Texel","type":"Vector4","default":[0,0,0,0],"settings":{"output":true}}],
"nodes":[{"id":10,"def":"Sampling/SampleTexture2D","x":0,"y":0},
{"id":12,"def":"Input/Property","x":0,"y":0,"settings":{"property":1},"in":{"Value":{"from":[10,"s"]}}}],
"contexts":[],"groups":[],"notes":[]})");
	ASSERT_TRUE(sampler.IsValid());
	const GraphDocument graph = Parse(Effect({}, Uses(GRAPH_SUBGRAPH_NODE, 50, sampler) + "," + Uses(GRAPH_SUBGRAPH_NODE, 51, sampler)));
	GraphDefinitions definitions;
	Vector<GraphBinding> bindings{GetMemoryResource(MemTag::Editor)};
	GraphEffectTarget::PortTextureBindings(graph, definitions, bindings);
	ASSERT_EQ(bindings.size(), 2u);
	EXPECT_EQ(bindings[0].name, "PortTexture/50/10/Texture");
	EXPECT_EQ(bindings[1].name, "PortTexture/51/10/Texture");
	EXPECT_NE(bindings[0].expression, bindings[1].expression);
	EXPECT_EQ(GraphCompiler::PortTextureBinding(10, "Texture"), "PortTexture/10/Texture");

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(R"({"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[50,"1"]}}},
{"id":5,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[51,"1"]}}})",
							   Uses(GRAPH_SUBGRAPH_NODE, 50, sampler) + "," + Uses(GRAPH_SUBGRAPH_NODE, 51, sampler)),
						effect, diagnostics))
		<< Said(diagnostics);
	EXPECT_EQ(effect.systems[0].initializeTextures.size(), 2u);
}

TEST_F(GraphEffectSubgraphFixture, OutputPropertyIsOperatorSubgraphsAlone) {
	str graph = Effect({});
	graph.insert(graph.find("\"nodes\""), R"("properties":[{"id":7,"name":"Leak","reference":"Leak","type":"Float","default":[0],"settings":{"output":true}}],)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(graph, effect, diagnostics));
	EXPECT_NE(Said(diagnostics).find("property 'Leak' is an Output (only a subgraph operator has one)"), str::npos) << Said(diagnostics);
	const Guid block = Add("Leaks.sleffectblock", R"({"_type":"VisualEffectSubgraphBlock","version":1,"nextId":9,
"properties":[{"id":7,"name":"Leak","reference":"Leak","type":"Float","default":[0],"settings":{"output":true}}],
"nodes":[],"contexts":[{"id":3,"def":"Context/BlockSubgraph","x":0,"y":0,"blocks":[]}],"groups":[],"notes":[]})");
	GraphDefinitions definitions;
	str error;
	EXPECT_EQ(definitions.Subgraph(block, &error), nullptr);
	EXPECT_NE(error.find("only an operator subgraph has one"), str::npos) << error;
}

TEST_F(GraphEffectSubgraphFixture, SubgraphCheckBadgesRefusals) {
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_TRUE(GraphEffectTarget::CheckSubgraph(Parse(EffectGraphEditOps::OperatorSubgraphTemplate()), {}, diagnostics)) << Said(diagnostics);
	EXPECT_TRUE(GraphEffectTarget::CheckSubgraph(Parse(EffectGraphEditOps::BlockSubgraphTemplate()), {}, diagnostics)) << Said(diagnostics);

	EXPECT_FALSE(GraphEffectTarget::CheckSubgraph(Parse(BlockSubgraph(R"({"id":4,"def":"Block/Gravity"})", "InitAndUpdate")), {}, diagnostics));
	ASSERT_EQ(diagnostics.size(), 1u) << Said(diagnostics);
	EXPECT_EQ(diagnostics[0].node, 4u);
	EXPECT_NE(diagnostics[0].message.find("its Suitable Contexts list Initialize Particle (can't hold one)"), str::npos) << Said(diagnostics);

	diagnostics.clear();
	str broken(DOUBLER);
	broken.replace(broken.find(R"("B":{"value":[2]})"), 17, R"("B":{"value":"two"})");
	EXPECT_FALSE(GraphEffectTarget::CheckSubgraph(Parse(broken), {}, diagnostics));
	ASSERT_FALSE(diagnostics.empty());
	EXPECT_EQ(diagnostics[0].node, 11u) << Said(diagnostics);
}

TEST_F(GraphEffectSubgraphFixture, SubgraphWarningShownToUser) {
	const Guid idle = Add("Idle.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/CollisionShape","settings":{"Behavior":"None"}})", "Update"));
	ASSERT_TRUE(idle.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect({}, {}, Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, idle)), effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(diagnostics.size(), 1u) << Said(diagnostics);
	EXPECT_EQ(diagnostics[0].severity, GraphDiagnosticSeverity::Warning);
	EXPECT_EQ(diagnostics[0].node, 60u);
	EXPECT_EQ(diagnostics[0].message,
			  "in subgraph 'Idle', block 4: This block does nothing (its behavior is None, it writes no collision attributes).");

	diagnostics.clear();
	const GraphDocument subgraph = Parse(BlockSubgraph(R"({"id":4,"def":"Block/CollisionShape","settings":{"Behavior":"None"}})", "Update"));
	EXPECT_TRUE(GraphEffectTarget::CheckSubgraph(subgraph, idle, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(diagnostics.size(), 1u) << Said(diagnostics);
	EXPECT_EQ(diagnostics[0].node, 4u);
	EXPECT_EQ(diagnostics[0].key, "collision-does-nothing");
}

TEST_F(GraphEffectSubgraphFixture, SubgraphContextOffersCommonBlocks) {
	GraphDocument document = Parse(EffectGraphEditOps::BlockSubgraphTemplate());
	const u32 context = GraphSubgraphs::BlockContext(document);
	ASSERT_NE(context, 0u);
	Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	EffectGraphEditOps::BlockChoices(document, context, choices);
	const auto offers = [&choices](std::string_view def) {
		return std::any_of(choices.begin(), choices.end(), [def](const EffectGraphEditOps::BlockChoice& choice) { return choice.def == def; });
	};
	EXPECT_TRUE(offers(GRAPH_EFFECT_SET_ATTRIBUTE));
	EXPECT_FALSE(offers(GRAPH_EFFECT_GRAVITY));
	EXPECT_FALSE(offers(GRAPH_EFFECT_ORIENT));
	EXPECT_FALSE(offers(GRAPH_EFFECT_CONSTANT_RATE));

	NodeShaderEditOps::SetNodeSetting(document, context, GRAPH_EFFECT_SUITABLE_CONTEXTS_SETTING, NodeShaderEditOps::Quote("Update"));
	EffectGraphEditOps::BlockChoices(document, context, choices);
	EXPECT_TRUE(offers(GRAPH_EFFECT_GRAVITY));
	EXPECT_FALSE(offers(GRAPH_EFFECT_ORIENT));
	ASSERT_FALSE(choices.empty());
	EXPECT_NE(EffectGraphEditOps::AddBlock(document, context, 0, choices[0]), 0u);
}

TEST_F(GraphEffectSubgraphFixture, SearchersOfferProjectsSubgraphsButNeverCycle) {
	const Guid doubler = Add("Doubler.sleffectop", DOUBLER);
	const Guid user = Add("User.sleffectop", std::format(R"({{"_type":"VisualEffectSubgraphOperator","version":1,"nextId":20,
"properties":[],"nodes":[{}],"contexts":[],"groups":[],"notes":[]}})",
														 Uses(GRAPH_SUBGRAPH_NODE, 11, doubler)));
	const Guid sized = Add("Sized.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"size"}})"));
	ASSERT_TRUE(doubler.IsValid() && user.IsValid() && sized.IsValid());
	GraphDefinitions definitions;
	Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	const GraphDocument graph = Parse(Effect({}));
	EffectGraphEditOps::SubgraphChoices(graph, {}, 0, definitions, choices);
	ASSERT_EQ(choices.size(), 2u);
	EXPECT_EQ(choices[0].name, "Doubler");
	EXPECT_EQ(choices[0].category, "Mine");
	EXPECT_EQ(choices[0].def, GRAPH_SUBGRAPH_NODE);

	EffectGraphEditOps::SubgraphChoices(Parse(DOUBLER), doubler, 0, definitions, choices);
	EXPECT_TRUE(choices.empty());

	EffectGraphEditOps::SubgraphChoices(graph, {}, 1, definitions, choices);
	ASSERT_EQ(choices.size(), 1u);
	EXPECT_EQ(choices[0].def, GRAPH_EFFECT_SUBGRAPH_BLOCK);
	EffectGraphEditOps::SubgraphChoices(graph, {}, 3, definitions, choices);
	EXPECT_EQ(choices.size(), 1u);
	GraphDocument edited = graph;
	EffectGraphEditOps::SubgraphChoices(graph, {}, 0, definitions, choices);
	ASSERT_FALSE(choices.empty());
	EXPECT_NE(EffectGraphEditOps::AddOperator(edited, choices[0], {}), 0u);
	EXPECT_EQ(GraphSubgraphs::AssetOf(edited.nodes.back()), doubler);
}

TEST_F(GraphEffectSubgraphFixture, SubgraphContextStays) {
	GraphDocument block = Parse(EffectGraphEditOps::BlockSubgraphTemplate());
	const u32 context = GraphSubgraphs::BlockContext(block);
	EXPECT_EQ(EffectGraphEditOps::Delete(block, {&context, 1}), 0u);
	EXPECT_EQ(GraphSubgraphs::BlockContext(block), context);
	EXPECT_EQ(EffectGraphEditOps::Copy(block, {&context, 1}).find("BlockSubgraph"), str::npos);
	EXPECT_EQ(EffectGraphEditOps::AddOutputProperty(block, "Float"), 0u);

	GraphDocument operators = Parse(DOUBLER);
	const u32 output = EffectGraphEditOps::AddOutputProperty(operators, "Vector3");
	ASSERT_NE(output, 0u);
	EXPECT_TRUE(GraphSubgraphs::IsOutput(operators.properties.back()));
}

#ifndef SHALLTEAR_SHARED

namespace {

	class GraphEffectSubgraphReadbackFixture : public EffectKernelHarness::GraphEffectBlockFixture {
	protected:
		void SetUp() override {
			EffectKernelHarness::GraphEffectBlockFixture::SetUp();
			EffectSubgraphImporters::Register(&_token);
		}
		void TearDown() override {
			AssetImporterRegistry::UnregisterOwnedBy(&_token);
			EffectKernelHarness::GraphEffectBlockFixture::TearDown();
		}
		Guid Add(const char* fileName, std::string_view text) {
			FileSystem::WriteTextFile(Path((_root / fileName).generic_string()), str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + fileName));
		}
	};

} // anonymous namespace

TEST_F(GraphEffectSubgraphReadbackFixture, SubgraphWriteReadsBack) {
	const Guid doubler = Add("Doubler.sleffectop", DOUBLER);
	ASSERT_TRUE(doubler.IsValid());
	const Guid sized = Add("Sized.sleffectblock", BlockSubgraph(R"({"id":4,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[5,"2"]}}})",
																"InitAndUpdateAndOutput",
																std::format(R"({{"id":5,"def":"Utility/Subgraph","settings":{{"asset":"{}"}},"in":{{"1":{{"from":[2,"Out"]}}}}}})",
																			doubler.ToString())));
	ASSERT_TRUE(sized.IsValid());
	const str text = EffectKernelHarness::EffectWith(
		{}, Uses(GRAPH_EFFECT_SUBGRAPH_BLOCK, 60, sized, R"(,"in":{"1":{"value":[0.25]}})") + R"(,{"id":61,"def":"Block/SetAttribute","settings":{"Attribute":"size","Composition":"Multiply"},"in":{"Size":{"value":[3]}}})",
		{});
	EffectKernelHarness::BlockRun run;
	if (!Begin(run, text, "SubgraphSize")) {
		GTEST_SKIP();
	}
	Advance(run, 0.0f, 1.0f / 60.0f, {4});
	const Vector<f32> sizes = Values(run, "size");
	ASSERT_EQ(sizes.size(), 4u);
	for (const f32 size : sizes) {
		EXPECT_FLOAT_EQ(size, 1.5f);
	}
}

#endif
