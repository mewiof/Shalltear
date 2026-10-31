#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphDefinitions.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"
#include "ShaderSubgraphImporter.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	using namespace ShadingTest;

	struct SubgraphBuilder : GraphBuilder {
		GraphContext* output = nullptr;

		SubgraphBuilder() {
			document.type = str(GRAPH_SUBGRAPH_DOCUMENT_TYPE);
			output = &Context(1, GRAPH_SUBGRAPH_OUTPUT_CONTEXT);
		}

		GraphNode& Output(u32 id, std::string_view name, std::string_view type) {
			GraphNode& block = Block(*output, id, GRAPH_SUBGRAPH_OUTPUT_BLOCK);
			Setting(block, "Name", NodeShaderEditOps::Quote(name));
			Setting(block, "Type", NodeShaderEditOps::Quote(type));
			return block;
		}
	};

	GraphNode& UseSubgraph(DocumentBuilder& b, u32 id, const Guid& asset) {
		GraphNode& node = b.Node(id, GRAPH_SUBGRAPH_NODE);
		DocumentBuilder::Setting(node, GRAPH_SUBGRAPH_ASSET_SETTING, NodeShaderEditOps::Quote(asset.ToString()));
		return node;
	}

	GraphDocument Doubler() {
		SubgraphBuilder b;
		b.Blackboard(10, "In", "In", "Float", "0.25");
		b.Node(20, "Input/Property");
		DocumentBuilder::Setting(b.document.nodes.back(), "property", "10");
		b.Node(21, "Math/Multiply");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "2");
		b.Node(22, "Math/Add");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "1");
		b.Connect(20, "Out", 21, "A");
		b.Connect(21, "Out", 22, "A");
		b.Output(5, "Out", "Float");
		b.Connect(22, "Out", 5, "Value");
		return b.document;
	}

	class NodeShaderSubgraphFixture : public MaterialReadbackFixture {
	protected:
		bool Available() {
			if (!RenderSystem::IsRenderingAvailable()) {
				_why = "no Vulkan device available";
				return false;
			}
			if (!CompilerAvailable()) {
				_why = "dxcompiler.dll unavailable";
				return false;
			}
			return true;
		}

		Guid AddSubgraph(const char* fileName, const GraphDocument& document) {
			WriteProjectFile(fileName, GraphDocumentOps::Write(document));
			Asset* asset = AssetDatabase::LoadAsset(Path(str("project://") + fileName));
			return asset ? asset->assetId : Guid{};
		}

		ShaderAsset* AddGraph(const char* fileName, const GraphDocument& document) {
			WriteProjectFile(fileName, GraphDocumentOps::Write(document));
			return static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
		}

		Compiled CompileOutput(const GraphDocument& document, u32 node, std::string_view port, const Guid& asset = {}) {
			GraphCompileRoot root;
			root.node = node;
			root.port.assign(port);
			GraphCompileRequest request;
			request.roots = {&root, 1};
			request.bindings = TestBindings();
			request.asset = asset;
			Compiled out;
			out.ok = GraphCompiler::Compile(document, request, out.program, out.diagnostics);
			if (out.ok) {
				GraphHLSL::Emit(out.program, GraphStageFragment, out.text);
			}
			return out;
		}

		bool Draw(ShadingStage& stage, MaterialAsset* material, Vector<u8>& pixels, Vec2I& size) {
			stage.quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return RenderStage(stage, pixels, size);
		}

		str _why;
	};

} // anonymous namespace

TEST_F(NodeShaderSubgraphFixture, SubgraphPortsById) {
	SubgraphBuilder s;
	s.Blackboard(2, "Amount", "_Amount", "Float", "0.5");
	s.Blackboard(3, "Detail", "_DETAIL", "BooleanKeyword", "false");
	s.Blackboard(4, "Tint", "_Tint", "Color", "[1,0.5,0.25,0.75]");
	s.Output(5, "Sum", "Float");
	s.Output(6, "Tinted", "Vector4");
	const Guid asset = AddSubgraph("Ports.slsubgraph", s.document);
	ASSERT_TRUE(asset.IsValid());

	UnlitBuilder b;
	const GraphNode& node = UseSubgraph(b, 30, asset);
	GraphDefinitions definitions;
	str error;
	const GraphNodeDefinition* definition = definitions.Find(b.document, node, &error);
	ASSERT_NE(definition, nullptr) << error;
	EXPECT_EQ(definition->name, "Ports");
	EXPECT_EQ(definition->category, GRAPH_SUBGRAPH_DEFAULT_CATEGORY);
	ASSERT_EQ(definition->inputs.size(), 2u);
	EXPECT_EQ(definition->inputs[0].name, "2");
	EXPECT_EQ(definition->inputs[0].label, "Amount");
	EXPECT_EQ(definition->inputs[0].type, GraphPortType::Float);
	EXPECT_FLOAT_EQ(definition->inputs[0].defaultValue[0], 0.5f);
	EXPECT_EQ(definition->inputs[1].name, "4");
	EXPECT_EQ(definition->inputs[1].type, GraphPortType::Float4);
	EXPECT_FLOAT_EQ(definition->inputs[1].defaultValue[3], 0.75f);
	ASSERT_EQ(definition->outputs.size(), 2u);
	EXPECT_EQ(definition->outputs[0].name, "5");
	EXPECT_EQ(definition->outputs[0].label, "Sum");
	EXPECT_EQ(definition->outputs[1].name, "6");
	EXPECT_EQ(definition->outputs[1].type, GraphPortType::Float4);
}

TEST_F(NodeShaderSubgraphFixture, SubgraphInlinesAndFoldsThroughBoundary) {
	const Guid doubler = AddSubgraph("Doubler.slsubgraph", Doubler());
	ASSERT_TRUE(doubler.IsValid());

	DocumentBuilder b;
	b.Node(40, "Input/Float");
	DocumentBuilder::Constant(b.document.nodes.back(), "X", "3");
	UseSubgraph(b, 41, doubler);
	b.Connect(40, "Out", 41, "10");
	Compiled folded = CompileOutput(b.document, 41, "5");
	ASSERT_TRUE(folded.ok) << folded.Report();
	const std::span<const f32> seven = folded.program.ConstantOf(folded.program.roots[0].value);
	ASSERT_EQ(seven.size(), 1u);
	EXPECT_FLOAT_EQ(seven[0], 7.0f);

	DocumentBuilder live;
	live.Node(50, "Input/UV");
	live.Node(51, "Channel/Split");
	live.Connect(50, "Out", 51, "In");
	UseSubgraph(live, 52, doubler);
	live.Connect(51, "R", 52, "10");
	Compiled inlined = CompileOutput(live.document, 52, "5");
	ASSERT_TRUE(inlined.ok) << inlined.Report();
	for (const GraphValue& value : inlined.program.values) {
		EXPECT_TRUE(value.node == 50 || value.node == 51 || value.node == 52);
	}
	EXPECT_NE(inlined.text.body.find("n52_Out"), str::npos) << inlined.text.body;
}

TEST_F(NodeShaderSubgraphFixture, ANestedSubgraphInlinesThroughBothLevels) {
	const Guid doubler = AddSubgraph("Doubler.slsubgraph", Doubler());
	ASSERT_TRUE(doubler.IsValid());
	SubgraphBuilder outer;
	outer.Blackboard(10, "X", "X", "Float");
	outer.Node(20, "Input/Property");
	DocumentBuilder::Setting(outer.document.nodes.back(), "property", "10");
	UseSubgraph(outer, 21, doubler);
	outer.Connect(20, "Out", 21, "10");
	UseSubgraph(outer, 22, doubler);
	outer.Connect(21, "5", 22, "10");
	outer.Output(5, "Out", "Float");
	outer.Connect(22, "5", 5, "Value");
	const Guid twice = AddSubgraph("Twice.slsubgraph", outer.document);
	ASSERT_TRUE(twice.IsValid());

	DocumentBuilder b;
	b.Node(40, "Input/Float");
	DocumentBuilder::Constant(b.document.nodes.back(), "X", "3");
	UseSubgraph(b, 41, twice);
	b.Connect(40, "Out", 41, "10");
	Compiled c = CompileOutput(b.document, 41, "5");
	ASSERT_TRUE(c.ok) << c.Report();
	const std::span<const f32> value = c.program.ConstantOf(c.program.roots[0].value);
	ASSERT_EQ(value.size(), 1u);
	EXPECT_FLOAT_EQ(value[0], 15.0f);
}

TEST_F(NodeShaderSubgraphFixture, UnwiredSubgraphInputReadsPropertysDefault) {
	const Guid doubler = AddSubgraph("Doubler.slsubgraph", Doubler());
	ASSERT_TRUE(doubler.IsValid());
	DocumentBuilder b;
	UseSubgraph(b, 41, doubler);
	Compiled c = CompileOutput(b.document, 41, "5");
	ASSERT_TRUE(c.ok) << c.Report();
	const std::span<const f32> value = c.program.ConstantOf(c.program.roots[0].value);
	ASSERT_EQ(value.size(), 1u);
	EXPECT_FLOAT_EQ(value[0], 1.5f);

	DocumentBuilder::Constant(b.document.nodes.back(), "10", "4");
	Compiled typed = CompileOutput(b.document, 41, "5");
	ASSERT_TRUE(typed.ok) << typed.Report();
	ASSERT_EQ(typed.program.ConstantOf(typed.program.roots[0].value).size(), 1u);
	EXPECT_FLOAT_EQ(typed.program.ConstantOf(typed.program.roots[0].value)[0], 9.0f);
}

TEST_F(NodeShaderSubgraphFixture, RenamingASubgraphsPortsKeepsEveryWire) {
	const Guid doubler = AddSubgraph("Doubler.slsubgraph", Doubler());
	ASSERT_TRUE(doubler.IsValid());
	DocumentBuilder b;
	b.Node(40, "Input/Float");
	DocumentBuilder::Constant(b.document.nodes.back(), "X", "3");
	UseSubgraph(b, 41, doubler);
	b.Connect(40, "Out", 41, "10");

	GraphDocument renamed = Doubler();
	ASSERT_TRUE(NodeShaderEditOps::RenameProperty(renamed, 10, "Amount"));
	ASSERT_TRUE(NodeShaderEditOps::SetPropertyReference(renamed, 10, "_Amount"));
	ASSERT_TRUE(NodeShaderEditOps::RenameSubgraphOutput(renamed, 5, "Result"));
	WriteProjectFile("Doubler.slsubgraph", GraphDocumentOps::Write(renamed));

	Compiled c = CompileOutput(b.document, 41, "5");
	ASSERT_TRUE(c.ok) << c.Report();
	ASSERT_EQ(c.program.ConstantOf(c.program.roots[0].value).size(), 1u);
	EXPECT_FLOAT_EQ(c.program.ConstantOf(c.program.roots[0].value)[0], 7.0f);
	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = definitions.Find(b.document, b.document.nodes.back());
	ASSERT_NE(definition, nullptr);
	EXPECT_EQ(definition->inputs[0].label, "Amount");
	EXPECT_EQ(definition->outputs[0].label, "Result");
}

TEST_F(NodeShaderSubgraphFixture, ACycleIsRefusedNamingTheChain) {
	SubgraphBuilder placeholder;
	placeholder.Output(5, "Out", "Float");
	const Guid a = AddSubgraph("Alpha.slsubgraph", placeholder.document);
	ASSERT_TRUE(a.IsValid());
	SubgraphBuilder beta;
	UseSubgraph(beta, 20, a);
	beta.Output(5, "Out", "Float");
	beta.Connect(20, "5", 5, "Value");
	const Guid b = AddSubgraph("Beta.slsubgraph", beta.document);
	ASSERT_TRUE(b.IsValid());
	SubgraphBuilder alpha;
	UseSubgraph(alpha, 20, b);
	alpha.Output(5, "Out", "Float");
	alpha.Connect(20, "5", 5, "Value");
	WriteProjectFile("Alpha.slsubgraph", GraphDocumentOps::Write(alpha.document));

	DocumentBuilder graph;
	UseSubgraph(graph, 41, a);
	Compiled c = CompileOutput(graph.document, 41, "5");
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(41, "'Alpha' -> 'Beta' -> 'Alpha'")) << c.Report();

	SubgraphBuilder self;
	UseSubgraph(self, 20, a);
	self.Output(5, "Out", "Float");
	self.Connect(20, "5", 5, "Value");
	WriteProjectFile("Alpha.slsubgraph", GraphDocumentOps::Write(self.document));
	Compiled selfUse = CompileOutput(self.document, 20, "5", a);
	EXPECT_FALSE(selfUse.ok);
	EXPECT_TRUE(selfUse.Names(20, "'Alpha' -> 'Alpha'")) << selfUse.Report();
}

TEST_F(NodeShaderSubgraphFixture, MissingSubgraphKeepsNodeAndSaysSo) {
	DocumentBuilder b;
	b.Node(40, "Input/Float");
	UseSubgraph(b, 41, AssetDatabase::GenerateGuid());
	b.Connect(40, "Out", 41, "10");
	DocumentBuilder::Constant(b.document.nodes.back(), "11", "0.5");
	Compiled c = CompileOutput(b.document, 41, "5");
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(41, "the project doesn't have")) << c.Report();

	const str text = GraphDocumentOps::Write(b.document);
	GraphDocument read;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(text, read, error)) << error;
	EXPECT_EQ(GraphDocumentOps::Write(read), text);
}

TEST_F(NodeShaderSubgraphFixture, InnerStageRefusalNamesUser) {
	SubgraphBuilder s;
	s.Blackboard(10, "Map", "_Map", "Texture2D");
	s.Node(20, "Input/Property");
	DocumentBuilder::Setting(s.document.nodes.back(), "property", "10");
	s.Node(21, "Input/SampleTexture2D");
	s.Connect(20, "Out", 21, "Texture");
	s.Output(5, "Out", "Vector4");
	s.Connect(21, "RGBA", 5, "Value");
	const Guid sampler = AddSubgraph("Sampler.slsubgraph", s.document);
	ASSERT_TRUE(sampler.IsValid());

	GraphBuilder b;
	b.Graph("target", "\"unlit\"");
	GraphContext& fragment = b.Context(1, "Context/Fragment");
	b.Block(fragment, 2, "Block/BaseColor");
	GraphContext& vertex = b.Context(3, "Context/Vertex");
	b.Block(vertex, 4, "Block/Position");
	b.Blackboard(60, "Map", "_Map", "Texture2D");
	b.Node(61, "Input/Property");
	DocumentBuilder::Setting(b.document.nodes.back(), "property", "60");
	UseSubgraph(b, 62, sampler);
	b.Connect(61, "Out", 62, "10");
	b.Connect(62, "5", 4, "Value");
	str text;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphSurface::Generate(b.document, "Stage", text, diagnostics));
	bool named = false;
	for (const GraphDiagnostic& diagnostic : diagnostics) {
		named = named || (diagnostic.node == 62 && diagnostic.message.find("vertex") != str::npos);
	}
	EXPECT_TRUE(named);
}

TEST_F(NodeShaderSubgraphFixture, InterpolatorReaderNeverTakesSubgraphOutput) {
	SubgraphBuilder s;
	s.Node(20, "CustomInterpolators/Instance");
	DocumentBuilder::Setting(s.document.nodes.back(), "Interpolator", "\"Out\"");
	s.Output(5, "Out", "Vector4");
	Compiled c = CompileOutput(s.document, 20, "Out");
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(20, "no block on the stack has that name")) << c.Report();
}

namespace {

	GraphDocument Fallback(bool binding = true, bool redirect = false) {
		SubgraphBuilder b;
		GraphProperty& in = b.Blackboard(10, "In", "In", "Float", "0.25");
		if (binding) {
			in.settings.push_back({str("customBinding"), str("true")});
			in.settings.push_back({str("bindingLabel"), str("\"UV\"")});
		}
		b.Node(20, "Input/Property");
		DocumentBuilder::Setting(b.document.nodes.back(), "property", "10");
		b.Node(21, "Utility/BranchOnInputConnection");
		DocumentBuilder::Constant(b.document.nodes.back(), "NotConnected", "9");
		if (redirect) {
			b.Node(22, GraphDocumentOps::REDIRECT_DEF);
			b.Connect(20, "Out", 22, GraphDocumentOps::REDIRECT_IN);
			b.Connect(22, GraphDocumentOps::REDIRECT_OUT, 21, "Input");
		} else {
			b.Connect(20, "Out", 21, "Input");
		}
		b.Connect(20, "Out", 21, "Connected");
		b.Output(5, "Out", "Float");
		b.Connect(21, "Out", 5, "Value");
		return b.document;
	}

	f32 FoldedTo(const Compiled& compiled) {
		const std::span<const f32> value = compiled.program.ConstantOf(compiled.program.roots[0].value);
		return value.size() == 1 ? value[0] : -1.0f;
	}

	f32 DecidedState(const Compiled& compiled) {
		const GraphValue& select = compiled.program.At(compiled.program.roots[0].value);
		if (select.op != GraphOp::Select) {
			return -1.0f;
		}
		const std::span<const f32> state = compiled.program.ConstantOf(select.operands[0]);
		return state.size() == 1 ? state[0] : -1.0f;
	}

} // anonymous namespace

TEST_F(NodeShaderSubgraphFixture, BranchReadsInputWiring) {
	const Guid fallback = AddSubgraph("Fallback.slsubgraph", Fallback());
	ASSERT_TRUE(fallback.IsValid());

	DocumentBuilder wired;
	wired.Node(40, "Input/Float");
	DocumentBuilder::Constant(wired.document.nodes.back(), "X", "3");
	UseSubgraph(wired, 41, fallback);
	wired.Connect(40, "Out", 41, "10");
	Compiled connected = CompileOutput(wired.document, 41, "5");
	ASSERT_TRUE(connected.ok) << connected.Report();
	EXPECT_FLOAT_EQ(FoldedTo(connected), 3.0f);

	DocumentBuilder open;
	UseSubgraph(open, 41, fallback);
	Compiled unwired = CompileOutput(open.document, 41, "5");
	ASSERT_TRUE(unwired.ok) << unwired.Report();
	EXPECT_FLOAT_EQ(FoldedTo(unwired), 9.0f);
	DocumentBuilder::Constant(open.document.nodes.back(), "10", "4");
	Compiled typed = CompileOutput(open.document, 41, "5");
	ASSERT_TRUE(typed.ok) << typed.Report();
	EXPECT_FLOAT_EQ(FoldedTo(typed), 9.0f);

	DocumentBuilder live;
	live.Node(50, "Input/UV");
	live.Node(51, "Channel/Split");
	live.Connect(50, "Out", 51, "In");
	UseSubgraph(live, 52, fallback);
	live.Connect(51, "R", 52, "10");
	Compiled runtime = CompileOutput(live.document, 52, "5");
	ASSERT_TRUE(runtime.ok) << runtime.Report();
	EXPECT_EQ(DecidedState(runtime), 1.0f);

	Compiled alone = CompileOutput(Fallback(), 21, "Out", fallback);
	ASSERT_TRUE(alone.ok) << alone.Report();
	EXPECT_EQ(DecidedState(alone), 0.0f);

	const Guid redirected = AddSubgraph("Redirected.slsubgraph", Fallback(true, true));
	ASSERT_TRUE(redirected.IsValid());
	UseSubgraph(wired, 42, redirected);
	wired.Connect(40, "Out", 42, "10");
	Compiled through = CompileOutput(wired.document, 42, "5");
	ASSERT_TRUE(through.ok) << through.Report();
	EXPECT_FLOAT_EQ(FoldedTo(through), 3.0f);

	SubgraphBuilder relay;
	relay.Blackboard(10, "X", "X", "Float", "0.5");
	relay.Node(20, "Input/Property");
	DocumentBuilder::Setting(relay.document.nodes.back(), "property", "10");
	UseSubgraph(relay, 21, fallback);
	relay.Connect(20, "Out", 21, "10");
	relay.Output(5, "Out", "Float");
	relay.Connect(21, "5", 5, "Value");
	const Guid relayed = AddSubgraph("Relay.slsubgraph", relay.document);
	ASSERT_TRUE(relayed.IsValid());
	DocumentBuilder outer;
	UseSubgraph(outer, 41, relayed);
	Compiled nested = CompileOutput(outer.document, 41, "5");
	ASSERT_TRUE(nested.ok) << nested.Report();
	EXPECT_FLOAT_EQ(FoldedTo(nested), 0.5f);
}

TEST_F(NodeShaderSubgraphFixture, BranchRefusesUntestable) {
	const Guid unbound = AddSubgraph("Unbound.slsubgraph", Fallback(false));
	ASSERT_TRUE(unbound.IsValid());
	DocumentBuilder b;
	UseSubgraph(b, 41, unbound);
	Compiled off = CompileOutput(b.document, 41, "5");
	EXPECT_FALSE(off.ok);
	EXPECT_TRUE(off.Names(41, "its Use Custom Binding is off")) << off.Report();

	SubgraphBuilder constant;
	constant.Node(20, "Input/Float");
	constant.Node(21, "Utility/BranchOnInputConnection");
	constant.Connect(20, "Out", 21, "Input");
	Compiled notProperty = CompileOutput(constant.document, 21, "Out");
	EXPECT_FALSE(notProperty.ok);
	EXPECT_TRUE(notProperty.Names(21, "(not a property)")) << notProperty.Report();

	DocumentBuilder main;
	main.Node(21, "Utility/BranchOnInputConnection");
	Compiled inMain = CompileOutput(main.document, 21, "Out");
	EXPECT_FALSE(inMain.ok);
	EXPECT_TRUE(inMain.Names(21, "belongs in a subgraph")) << inMain.Report();
}

TEST_F(NodeShaderSubgraphFixture, ASubgraphsKeywordsAreTheGraphsOwn) {
	SubgraphBuilder s;
	s.Blackboard(10, "Detail", "_DETAIL", "BooleanKeyword", "true");
	s.Node(20, "Utility/Keyword");
	DocumentBuilder::Setting(s.document.nodes.back(), "property", "10");
	DocumentBuilder::Constant(s.document.nodes.back(), "On", "[0,1,0,1]");
	DocumentBuilder::Constant(s.document.nodes.back(), "Off", "[1,0,0,1]");
	s.Output(5, "Out", "Vector4");
	s.Connect(20, "Out", 5, "Value");
	const Guid detail = AddSubgraph("Detail.slsubgraph", s.document);
	ASSERT_TRUE(detail.IsValid());

	UnlitBuilder b;
	UseSubgraph(b, 30, detail);
	b.Connect(30, "5", 2, "Value");
	str text;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphSurface::Generate(b.document, "Detailed", text, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	EXPECT_NE(text.find("#pragma shalltear_keyword _DETAIL _DETAIL_Toggle"), str::npos) << text;
	EXPECT_NE(text.find("#pragma shalltear_default _DETAIL_Toggle 1"), str::npos);
	EXPECT_NE(text.find("#if defined(_DETAIL)"), str::npos);

	b.Blackboard(70, "Detail", "_DETAIL", "Float");
	diagnostics.clear();
	EXPECT_FALSE(GraphSurface::Generate(b.document, "Clash", text, diagnostics));
	bool clash = false;
	for (const GraphDiagnostic& diagnostic : diagnostics) {
		clash = clash || (diagnostic.node == 30 && diagnostic.message.find("_DETAIL") != str::npos);
	}
	EXPECT_TRUE(clash);
}

TEST_F(NodeShaderSubgraphFixture, SubgraphsEnumKeywordIsGraphsGroup) {
	constexpr std::string_view LOD_ENTRIES = R"([{"id":1,"name":"High","reference":"HIGH"},{"id":2,"name":"Low","reference":"LOW"}])";
	SubgraphBuilder s;
	s.Blackboard(10, "Detail", "_LOD", "EnumKeyword", "1").settings.push_back({"entries", str(LOD_ENTRIES)});
	s.Node(20, "Utility/Keyword");
	DocumentBuilder::Setting(s.document.nodes.back(), "property", "10");
	DocumentBuilder::Constant(s.document.nodes.back(), "1", "[0,1,0,1]");
	DocumentBuilder::Constant(s.document.nodes.back(), "2", "[1,0,0,1]");
	s.Output(5, "Out", "Vector4");
	s.Connect(20, "Out", 5, "Value");
	const Guid detail = AddSubgraph("Lod.slsubgraph", s.document);
	ASSERT_TRUE(detail.IsValid());

	UnlitBuilder b;
	UseSubgraph(b, 30, detail);
	b.Connect(30, "5", 2, "Value");
	str text;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphSurface::Generate(b.document, "Lod", text, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	EXPECT_NE(text.find("#pragma shalltear_keyword _LOD_HIGH _LOD 0 \"High\""), str::npos) << text;
	EXPECT_NE(text.find("#pragma shalltear_keyword _LOD_LOW _LOD 1 \"Low\""), str::npos) << text;
	EXPECT_NE(text.find("#pragma shalltear_default _LOD 1"), str::npos);
	EXPECT_NE(text.find("#if defined(_LOD_HIGH)"), str::npos);

	auto refused = [&](std::string_view fragment) {
		diagnostics.clear();
		EXPECT_FALSE(GraphSurface::Generate(b.document, "Clash", text, diagnostics));
		return std::any_of(diagnostics.begin(), diagnostics.end(),
						   [&](const GraphDiagnostic& diagnostic) { return diagnostic.node == 30 && diagnostic.message.find(fragment) != str::npos; });
	};
	b.Blackboard(70, "Detail", "_LOD", "EnumKeyword", "0").settings.push_back({"entries", R"([{"id":1,"name":"Low","reference":"LOW"},{"id":2,"name":"High","reference":"HIGH"}])"});
	EXPECT_TRUE(refused("1 reference is 1 keyword"));
	b.document.properties.pop_back();
	b.Blackboard(71, "High Bias", "_LOD_HIGH", "Float");
	EXPECT_TRUE(refused("meet at '_LOD_HIGH'"));
}

TEST_F(NodeShaderSubgraphFixture, NewSubgraphFollowsTemplate) {
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(ShaderSubgraphImporter::TemplateDocument(), document, error)) << error;
	EXPECT_EQ(document.type, GRAPH_SUBGRAPH_DOCUMENT_TYPE);
	EXPECT_EQ(NodeShaderEditOps::GraphSettingText(document, GRAPH_SUBGRAPH_CATEGORY_SETTING), GRAPH_SUBGRAPH_DEFAULT_CATEGORY);
	const u32 output = GraphSubgraphs::OutputContext(document);
	ASSERT_NE(output, 0u);
	ASSERT_EQ(document.contexts[0].blocks.size(), 1u);
	EXPECT_EQ(GraphSubgraphs::OutputNameOf(document.contexts[0].blocks[0]), "Out_Vector4");
	EXPECT_EQ(GRAPH_SUBGRAPH_OUTPUT_TYPES[GraphSubgraphs::OutputTypeOf(document.contexts[0].blocks[0])], "Vector4");

	const u32 first = NodeShaderEditOps::AddSubgraphOutput(document, output, "Vector3");
	const u32 second = NodeShaderEditOps::AddSubgraphOutput(document, output, "Vector3");
	ASSERT_NE(first, 0u);
	ASSERT_NE(second, 0u);
	EXPECT_EQ(GraphSubgraphs::OutputNameOf(*GraphDocumentOps::FindNode(document, first)), "Out_Vector3");
	EXPECT_EQ(GraphSubgraphs::OutputNameOf(*GraphDocumentOps::FindNode(document, second)), "Out_Vector3 (1)");
	EXPECT_EQ(NodeShaderEditOps::AddSubgraphOutput(document, output, "SamplerState"), 0u);
	EXPECT_FALSE(NodeShaderEditOps::RenameSubgraphOutput(document, second, "Out_Vector3"));
	EXPECT_FALSE(NodeShaderEditOps::RenameSubgraphOutput(document, second, ""));
	EXPECT_TRUE(NodeShaderEditOps::RenameSubgraphOutput(document, second, "Normal"));
	EXPECT_TRUE(NodeShaderEditOps::SetSubgraphOutputType(document, second, "Matrix3"));
	EXPECT_FALSE(NodeShaderEditOps::SetSubgraphOutputType(document, second, "Matrix3"));
	EXPECT_EQ(NodeShaderEditOps::Delete(document, std::array<u32, 1>{output}), 0u);
}

TEST_F(NodeShaderSubgraphFixture, GraphUsingSubgraphDrawsWhatNodesCompute) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	SubgraphBuilder s;
	s.Blackboard(10, "Color", "_Color", "Color", "[0,0,0,1]");
	s.Node(20, "Input/Property");
	DocumentBuilder::Setting(s.document.nodes.back(), "property", "10");
	s.Node(21, "Math/Multiply");
	DocumentBuilder::Constant(s.document.nodes.back(), "B", "[0.5,0.5,0.5,0.5]");
	s.Connect(20, "Out", 21, "A");
	s.Output(5, "Half", "Vector4");
	s.Connect(21, "Out", 5, "Value");
	const Guid halve = AddSubgraph("Halve.slsubgraph", s.document);
	ASSERT_TRUE(halve.IsValid());

	UnlitBuilder b;
	b.Node(30, "Input/Color");
	DocumentBuilder::Setting(b.document.nodes.back(), "Color", "[0.8,0.4,0.2,1]");
	UseSubgraph(b, 31, halve);
	b.Connect(30, "Out", 31, "10");
	b.Connect(31, "5", 2, "Value");
	ShaderAsset* graph = AddGraph("Halved.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	MaterialAsset* material = LoadMaterial("Halved.mat", graph->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 texel = CenterTexel(pixels, size);
	const Vec3 expected = ExpectedTexel(Vec3(0.4f, 0.2f, 0.1f));
	EXPECT_NEAR(texel.x, expected.x, 1.5f);
	EXPECT_NEAR(texel.y, expected.y, 1.5f);
	EXPECT_NEAR(texel.z, expected.z, 1.5f);
	TearDownStage(stage);
}

TEST_F(NodeShaderSubgraphFixture, EditingSubgraphReimportsDependentsAndNothingElse) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	auto colored = [](std::string_view rgba) {
		SubgraphBuilder s;
		s.Node(20, "Input/Color");
		DocumentBuilder::Setting(s.document.nodes.back(), "Color", rgba);
		s.Output(5, "Out", "Vector4");
		s.Connect(20, "Out", 5, "Value");
		return s.document;
	};
	const Guid paint = AddSubgraph("Paint.slsubgraph", colored("[1,0,0,1]"));
	ASSERT_TRUE(paint.IsValid());
	UnlitBuilder user;
	UseSubgraph(user, 30, paint);
	user.Connect(30, "5", 2, "Value");
	ShaderAsset* dependent = AddGraph("Dependent.slshader", user.document);
	ASSERT_NE(dependent, nullptr);
	UnlitBuilder other;
	other.Node(30, "Input/Color");
	DocumentBuilder::Setting(other.document.nodes.back(), "Color", "[0,0,1,1]");
	other.Connect(30, "Out", 2, "Value");
	ShaderAsset* bystander = AddGraph("Bystander.slshader", other.document);
	ASSERT_NE(bystander, nullptr);

	MaterialAsset* dependentMaterial = LoadMaterial("Dependent.mat", dependent->assetId, {});
	MaterialAsset* bystanderMaterial = LoadMaterial("Bystander.mat", bystander->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(dependentMaterial, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(Draw(stage, bystanderMaterial, pixels, size));
	ASSERT_TRUE(Draw(stage, dependentMaterial, pixels, size));
	ASSERT_GT(CenterTexel(pixels, size).x, 200.0f);
	const u32 dependentVersion = dependent->version;
	const u32 bystanderVersion = bystander->version;

	WriteProjectFile("Paint.slsubgraph", GraphDocumentOps::Write(colored("[0,1,0,1]")));
	RenderSystem::NotifyShaderFileChanged((_root / "Paint.slsubgraph").generic_string().c_str());
	ASSERT_TRUE(Draw(stage, dependentMaterial, pixels, size));
	const Vec3 after = CenterTexel(pixels, size);
	EXPECT_GT(after.y, 200.0f);
	EXPECT_LT(after.x, 20.0f);
	EXPECT_EQ(dependent->version, dependentVersion + 1);
	EXPECT_EQ(bystander->version, bystanderVersion);
	TearDownStage(stage);
}

TEST_F(NodeShaderSubgraphFixture, WarmArtifactMissesAfterSubgraphChanges) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const Guid doubler = AddSubgraph("Doubler.slsubgraph", Doubler());
	ASSERT_TRUE(doubler.IsValid());
	UnlitBuilder b;
	UseSubgraph(b, 30, doubler);
	b.Connect(30, "5", 2, "Value");
	ShaderAsset* graph = AddGraph("Cached.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	const str subgraphPath = FileSystem::Resolve(Path("project://Doubler.slsubgraph")).ToString();
	auto listedHash = [&subgraphPath](const ShaderAsset& shader) {
		for (const ShaderAsset::Dependency& dependency : shader.dependencies) {
			if (dependency.physicalPath == subgraphPath) {
				return dependency.contentHash;
			}
		}
		return u64{0};
	};
	EXPECT_NE(listedHash(*graph), 0u);
	bool artifact = false;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(_root / "Library" / "Artifacts", ec)) {
		artifact = artifact || entry.path().extension() == ".slart";
	}
	ASSERT_TRUE(artifact);

	AssetDatabase::UnloadAsset(graph);
	AssetDatabase::CollectGarbage();
	GraphDocument edited = Doubler();
	ASSERT_TRUE(NodeShaderEditOps::SetPropertyDefault(edited, 10, "0.75"));
	const str editedText = GraphDocumentOps::Write(edited);
	WriteProjectFile("Doubler.slsubgraph", editedText);
	ShaderAsset* reloaded = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("project://Cached.slshader")));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(listedHash(*reloaded), StringHash(std::string_view(editedText)).GetValue());
}
