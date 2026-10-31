#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"

#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphSurface.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	DocumentBuilder TwoPropertiesInto(std::string_view def, std::string_view typeA = "Vector4", std::string_view typeB = "Vector4") {
		DocumentBuilder b;
		b.Property(1, 1, "pA", typeA);
		b.Property(2, 2, "pB", typeB);
		b.Node(3, def);
		b.Connect(1, "Out", 3, "A");
		b.Connect(2, "Out", 3, "B");
		return b;
	}

	u32 Lines(const str& body) { return static_cast<u32>(std::count(body.begin(), body.end(), '\n')); }

	bool IsEffectDefinition(const GraphNodeDefinition& definition) { return GraphEffectRules::IsEffectOnly(definition.id); }

} // anonymous namespace

TEST(NodeShaderCompilerTest, TheTableIsWellFormed) {
	std::span<const GraphNodeDefinition* const> all = GraphNodeLibrary::All();
	EXPECT_GE(all.size(), 80u);
	for (size_t i = 0; i < all.size(); ++i) {
		const GraphNodeDefinition& definition = *all[i];
		EXPECT_NE(definition.id.find('/'), std::string_view::npos) << definition.id;
		for (size_t j = i + 1; j < all.size(); ++j) {
			EXPECT_NE(definition.id, all[j]->id);
		}
		if (IsEffectDefinition(definition)) {
			continue;
		}
		if (definition.category == "Context" || definition.category == "Block") {
			EXPECT_EQ(definition.body, nullptr) << definition.id;
			EXPECT_TRUE(definition.outputs.empty()) << definition.id;
			if (definition.category == "Block") {
				ASSERT_EQ(definition.inputs.size(), 1u) << definition.id;
				EXPECT_EQ(definition.inputs[0].name, "Value") << definition.id;
			} else {
				EXPECT_TRUE(definition.inputs.empty()) << definition.id;
			}
			continue;
		}
		EXPECT_NE(definition.body, nullptr) << definition.id;
		EXPECT_FALSE(definition.outputs.empty()) << definition.id;
		for (std::span<const GraphPortDefinition> ports : {definition.inputs, definition.outputs}) {
			for (size_t a = 0; a < ports.size(); ++a) {
				for (size_t b = a + 1; b < ports.size(); ++b) {
					EXPECT_NE(ports[a].name, ports[b].name) << definition.id;
				}
			}
		}
		for (const GraphSettingDefinition& setting : definition.settings) {
			if (setting.kind == GraphSettingKind::Choice) {
				EXPECT_LT(setting.defaultIndex, setting.options.size()) << definition.id << " " << setting.key;
			}
		}
	}
}

namespace {

	bool ReadsWhatTheTargetCannotSupply(std::string_view definition, std::string_view output) {
		if (definition == "Input/Time") {
			return output == "Smooth Delta";
		}
		if (definition == "Input/Object") {
			return output == "World Bounds Min" || output == "World Bounds Max" || output == "Bounds Size";
		}
		return definition == "Input/VertexID";
	}

	bool ReadsTheScene(std::string_view definition) {
		return definition == "Input/SceneColor" || definition == "Input/SceneDepth" || definition == "Input/SampleBuffer";
	}

	bool ReadsTheSky(std::string_view definition) { return definition == "Input/Sky" || definition == "Input/SunDisc"; }

} // anonymous namespace

TEST(NodeShaderCompilerTest, EveryDefinitionCompilesAtItsDefaults) {
	for (const GraphNodeDefinition* definition : GraphNodeLibrary::All()) {
		if (definition->id == "Input/Property" || definition->id == "CustomInterpolators/Instance" || definition->id == "Utility/Keyword") {
			continue;
		}
		if (IsEffectDefinition(*definition)) {
			continue;
		}
		bool texture = false;
		for (const GraphPortDefinition& input : definition->inputs) {
			texture = texture || input.type == GraphPortType::Texture2D || input.type == GraphPortType::Texture2DArray ||
					  input.type == GraphPortType::Texture3D || input.type == GraphPortType::Cubemap;
		}
		if (texture) {
			continue;
		}
		DocumentBuilder b;
		b.Node(1, definition->id);
		if (definition->subgraphOnly) {
			b.document.type = str(GRAPH_SUBGRAPH_DOCUMENT_TYPE);
		}
		Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
		for (const GraphPortDefinition& output : definition->outputs) {
			if (!ReadsWhatTheTargetCannotSupply(definition->id, output.name)) {
				roots.push_back({1, str(output.name), GraphStageFragment});
			}
		}
		if (definition->category == "Block") {
			roots.push_back({1, str("Value"), GraphStageFragment, true});
		}
		GraphCompileRequest request{roots, ReadsTheScene(definition->id) ? GraphSurface::FullscreenBindings()
										   : ReadsTheSky(definition->id) ? GraphSurface::SkyBindings()
																		 : TestBindings()};
		GraphProgram program;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		bool ok = GraphCompiler::Compile(b.document, request, program, diagnostics);
		EXPECT_TRUE(ok) << definition->id << ": " << (diagnostics.empty() ? str() : diagnostics[0].message);
	}
}

TEST(NodeShaderCompilerTest, UnsuppliedValueRefusedWhenRead) {
	DocumentBuilder b;
	b.Node(1, "Input/Time");
	b.Node(2, "Input/Object");
	b.Node(3, "Input/VertexID");
	b.Node(4, "Utility/IsFrontFace");

	Compiled offered = Compile(b.document, {{1, "Time"}, {1, "Delta Time"}, {2, "Position"}, {2, "Scale"}}, GraphStageFragment, TestBindings());
	EXPECT_TRUE(offered.ok) << offered.Report();

	Compiled smooth = Compile(b.document, {{1, "Smooth Delta"}}, GraphStageFragment, TestBindings());
	EXPECT_FALSE(smooth.ok);
	EXPECT_TRUE(smooth.Names(1, "the frame clock has no smoothed delta")) << smooth.Report();

	Compiled bounds = Compile(b.document, {{2, "Bounds Size"}}, GraphStageFragment, TestBindings());
	EXPECT_FALSE(bounds.ok);
	EXPECT_TRUE(bounds.Names(2, "a draw does not provide its bounds")) << bounds.Report();

	Compiled vertex = Compile(b.document, {{3, "Out"}}, GraphStageFragment, TestBindings());
	EXPECT_FALSE(vertex.ok);
	EXPECT_TRUE(vertex.Names(3, "only the vertex stage has")) << vertex.Report();

	for (GraphStageMask stage : {GraphStageFragment, GraphStageLighting}) {
		Compiled face = Compile(b.document, {{4, "Out"}}, stage, GraphSurface::Bindings());
		EXPECT_TRUE(face.ok) << face.Report();
	}
	Compiled face = Compile(b.document, {{4, "Out"}}, GraphStageVertex, GraphSurface::Bindings());
	EXPECT_FALSE(face.ok);
	EXPECT_TRUE(face.Names(4, "only the fragment and lighting stages have, and feeds the vertex stage")) << face.Report();
}

TEST(NodeShaderCompilerTest, OutputIgnoresNodeOrder) {
	DocumentBuilder b;
	b.Property(1, 1, "pA", "Vector3");
	b.Property(2, 2, "pB", "Float");
	b.Node(20, "Math/Sine");
	b.Node(10, "Math/Cosine");
	b.Node(30, "Math/Add");
	b.Node(40, "Procedural/SimpleNoise");
	b.Node(50, "Math/Multiply");
	b.Connect(1, "Out", 20, "In");
	b.Connect(2, "Out", 10, "In");
	b.Connect(10, "Out", 30, "A");
	b.Connect(20, "Out", 30, "B");
	b.Connect(30, "Out", 40, "Scale");
	b.Connect(30, "Out", 50, "A");
	b.Connect(2, "Out", 50, "B");

	Compiled first = Compile(b.document, {{50, "Out"}, {40, "Out"}}, GraphStageFragment, TestBindings());
	ASSERT_TRUE(first.ok) << first.Report();
	Compiled again = Compile(b.document, {{50, "Out"}, {40, "Out"}}, GraphStageFragment, TestBindings());
	EXPECT_EQ(first.text.body, again.text.body);
	EXPECT_EQ(first.text.functions, again.text.functions);
	EXPECT_LT(first.text.body.find("n10_Out"), first.text.body.find("n20_Out")) << first.text.body;

	std::reverse(b.document.nodes.begin(), b.document.nodes.end());
	Compiled reversed = Compile(b.document, {{50, "Out"}, {40, "Out"}}, GraphStageFragment, TestBindings());
	ASSERT_TRUE(reversed.ok) << reversed.Report();
	EXPECT_EQ(first.text.body, reversed.text.body);
	EXPECT_EQ(first.text.functions, reversed.text.functions);
	EXPECT_EQ(first.text.roots, reversed.text.roots);
}

TEST(NodeShaderCompilerTest, EveryLineNamesNodeItCameFrom) {
	DocumentBuilder b = TwoPropertiesInto("Math/Add");
	b.Node(4, "Math/Sine");
	b.Connect(3, "Out", 4, "In");
	Compiled c = Compile(b.document, {{4, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	ASSERT_EQ(c.text.lineNodes.size(), Lines(c.text.body));
	EXPECT_NE(c.text.body.find("float4 n3_Out = pA + pB;"), str::npos) << c.text.body;
	EXPECT_NE(c.text.body.find("float4 n4_Out = sin(n3_Out);"), str::npos) << c.text.body;
	size_t line = 0;
	for (size_t at = 0; at < c.text.body.size(); ++line) {
		size_t end = c.text.body.find('\n', at);
		str statement = c.text.body.substr(at, end - at);
		EXPECT_NE(statement.find(" n" + std::to_string(c.text.lineNodes[line]) + "_"), str::npos)
			<< "line " << line << " says node " << c.text.lineNodes[line] << ": " << statement;
		at = end + 1;
	}
	EXPECT_EQ(c.text.roots[0], "n4_Out");
}

TEST(NodeShaderCompilerTest, ConstantsFoldAndRootIsLiteral) {
	DocumentBuilder b;
	GraphNode& add = b.Node(1, "Math/Add");
	DocumentBuilder::Constant(add, "A", "[2]");
	DocumentBuilder::Constant(add, "B", "[3]");
	GraphNode& multiply = b.Node(2, "Math/Multiply");
	DocumentBuilder::Constant(multiply, "B", "[0.5]");
	b.Connect(1, "Out", 2, "A");
	Compiled c = Compile(b.document, {{2, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_TRUE(c.text.body.empty()) << c.text.body;
	EXPECT_EQ(c.text.roots[0], "2.5");
}

TEST(NodeShaderCompilerTest, NaNAndInfinityFoldThroughClassifications) {
	DocumentBuilder b;
	GraphNode& nan = b.Node(1, "Math/Divide");
	DocumentBuilder::Constant(nan, "A", "[0]");
	DocumentBuilder::Constant(nan, "B", "[0]");
	GraphNode& infinity = b.Node(2, "Math/Divide");
	DocumentBuilder::Constant(infinity, "A", "[1]");
	DocumentBuilder::Constant(infinity, "B", "[0]");
	b.Node(3, "Utility/IsNaN");
	b.Node(4, "Utility/IsInfinite");
	b.Node(5, "Utility/IsNaN");
	b.Node(6, "Utility/IsInfinite");
	b.Connect(1, "Out", 3, "In");
	b.Connect(1, "Out", 4, "In");
	b.Connect(2, "Out", 5, "In");
	b.Connect(2, "Out", 6, "In");
	Compiled c = Compile(b.document, {{3, "Out"}, {4, "Out"}, {5, "Out"}, {6, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_TRUE(c.text.body.empty()) << c.text.body;
	ASSERT_EQ(c.text.roots.size(), 4u);
	EXPECT_EQ(c.text.roots[0], "true");
	EXPECT_EQ(c.text.roots[1], "false");
	EXPECT_EQ(c.text.roots[2], "false");
	EXPECT_EQ(c.text.roots[3], "true");
}

TEST(NodeShaderCompilerTest, TranscendentalOfConstantIsLeftToGPU) {
	DocumentBuilder b;
	GraphNode& sine = b.Node(1, "Math/Sine");
	DocumentBuilder::Constant(sine, "In", "[1]");
	Compiled c = Compile(b.document, {{1, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.body.find("sin(1.0)"), str::npos) << c.text.body;
}

TEST(NodeShaderCompilerTest, EachPassWorksAlone) {
	GraphProgram program;
	GraphProgramBuilder b(program);
	b.SetNode(1);
	GraphValueId p = b.External(GraphValueType::Float2, "p", "p");
	GraphValueId twin1 = b.Add(p, b.Scalar(1.0f));
	GraphValueId twin2 = b.Add(p, b.Scalar(1.0f));
	GraphValueId folded = b.Multiply(b.Scalar(2.0f), b.Scalar(4.0f));
	GraphValueId dead = b.Unary(GraphOp::Sine, p);
	GraphValueId root = b.Multiply(b.Add(twin1, twin2), folded);
	program.roots.push_back({root, GraphStageFragment});
	(void)dead;

	EXPECT_EQ(GraphPasses::Fold(program), 1u);
	ASSERT_EQ(program.ConstantOf(folded).size(), 1u);
	EXPECT_EQ(program.ConstantOf(folded)[0], 8.0f);

	u32 before = static_cast<u32>(program.values.size());
	EXPECT_EQ(GraphPasses::Deduplicate(program), 2u);
	EXPECT_EQ(program.values.size(), before);

	u32 removed = GraphPasses::Eliminate(program);
	EXPECT_EQ(removed, 5u);
	for (const GraphValue& value : program.values) {
		EXPECT_NE(value.op, GraphOp::Sine);
	}
	GraphHLSLText text;
	GraphHLSL::Emit(program, GraphStageFragment, text);
	EXPECT_EQ(Lines(text.body), 3u) << text.body;
}

TEST(NodeShaderCompilerTest, TwoIdenticalNodesEmitOneStatement) {
	DocumentBuilder b = TwoPropertiesInto("Math/Add");
	b.Node(4, "Math/Add");
	b.Connect(1, "Out", 4, "A");
	b.Connect(2, "Out", 4, "B");
	b.Node(5, "Math/Multiply");
	b.Connect(3, "Out", 5, "A");
	b.Connect(4, "Out", 5, "B");
	Compiled c = Compile(b.document, {{5, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_EQ(Lines(c.text.body), 2u) << c.text.body;
	EXPECT_NE(c.text.body.find("n3_Out * n3_Out"), str::npos) << c.text.body;
}

TEST(NodeShaderCompilerTest, NodeNoRootReadsIsNotCompiled) {
	DocumentBuilder b = TwoPropertiesInto("Math/Add");
	b.Node(4, "Math/Cosine");
	b.Connect(3, "Out", 4, "In");
	b.Node(9, "Math/NoSuchNode");
	Compiled c = Compile(b.document, {{3, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_EQ(c.text.body.find("cos"), str::npos) << c.text.body;
	EXPECT_EQ(Lines(c.text.body), 1u);
}

TEST(NodeShaderCompilerTest, VertexDerivativeRefused) {
	DocumentBuilder b;
	b.Property(1, 1, "pA", "Vector2");
	b.Node(2, "Math/DDX");
	b.Connect(1, "Out", 2, "In");
	b.Node(3, "Math/Add");
	b.Connect(2, "Out", 3, "A");

	Compiled vertex = Compile(b.document, {{3, "Out"}}, GraphStageVertex);
	EXPECT_FALSE(vertex.ok);
	EXPECT_TRUE(vertex.Names(2, "only the fragment stage has")) << vertex.Report();
	EXPECT_FALSE(vertex.Names(3, "fragment"));
	EXPECT_TRUE(vertex.program.values.empty());

	Compiled fragment = Compile(b.document, {{3, "Out"}}, GraphStageFragment);
	EXPECT_TRUE(fragment.ok) << fragment.Report();
}

TEST(NodeShaderCompilerTest, DifferentiatingShapeIsFragmentOnly) {
	for (std::string_view def : {"Procedural/RoundedPolygon", "Procedural/Checkerboard", "Procedural/Ellipse"}) {
		DocumentBuilder b;
		b.Node(7, def);
		Compiled vertex = Compile(b.document, {{7, "Out"}}, GraphStageVertex, TestBindings());
		EXPECT_FALSE(vertex.ok) << def;
		EXPECT_TRUE(vertex.Names(7, "fwidth") || vertex.Names(7, "ddx")) << def << ": " << vertex.Report();
	}
}

TEST(NodeShaderCompilerTest, StageBindingRefusedElsewhere) {
	const GraphBinding fragmentUV[] = {{"UV0", "screenUV", GraphValueType::Float2, GraphStageFragment}};
	DocumentBuilder b;
	b.Node(4, "Procedural/GradientNoise");
	Compiled vertex = Compile(b.document, {{4, "Out"}}, GraphStageVertex, fragmentUV);
	EXPECT_FALSE(vertex.ok);
	EXPECT_TRUE(vertex.Names(4, "reads UV0")) << vertex.Report();
	EXPECT_TRUE(Compile(b.document, {{4, "Out"}}, GraphStageFragment, fragmentUV).ok);
}

TEST(NodeShaderCompilerTest, CycleRefusedNamingNodes) {
	DocumentBuilder b;
	b.Node(1, "Math/Add");
	b.Node(2, "Math/Sine");
	b.Node(3, "Math/Cosine");
	b.Connect(1, "Out", 2, "In");
	b.Connect(2, "Out", 3, "In");
	b.Connect(3, "Out", 1, "A");
	b.Node(4, "Math/Negate");
	b.Connect(1, "Out", 4, "In");
	Compiled c = Compile(b.document, {{4, "Out"}});
	EXPECT_FALSE(c.ok);
	for (u32 node : {1u, 2u, 3u}) {
		EXPECT_TRUE(c.Names(node, "is part of a cycle")) << "node " << node << "\n"
														 << c.Report();
	}
	EXPECT_FALSE(c.Names(4, "cycle"));
}

TEST(NodeShaderCompilerTest, MissingDefinitionRefusedKept) {
	DocumentBuilder b;
	b.Node(5, "Math/FromAPackageThatIsGone");
	b.Node(6, "Math/Negate");
	b.Connect(5, "Out", 6, "In");
	Compiled c = Compile(b.document, {{6, "Out"}});
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(5, "Math/FromAPackageThatIsGone")) << c.Report();
	ASSERT_EQ(b.document.nodes.size(), 2u);
	EXPECT_EQ(b.document.nodes[0].def, "Math/FromAPackageThatIsGone");
}

TEST(NodeShaderCompilerTest, MatrixIntoVectorRefused) {
	DocumentBuilder b;
	b.Node(1, "Math/MatrixConstruction");
	b.Node(2, "Math/Sine");
	b.Connect(1, "4x4", 2, "In");
	Compiled c = Compile(b.document, {{2, "Out"}});
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(2, "input 'In'")) << c.Report();
	EXPECT_TRUE(c.Names(2, "Matrix 4")) << c.Report();
}

TEST(NodeShaderCompilerTest, MissingBindingOrPropertyRefused) {
	DocumentBuilder b;
	b.Node(1, "Procedural/Voronoi");
	Compiled noUV = Compile(b.document, {{1, "Out"}});
	EXPECT_FALSE(noUV.ok);
	EXPECT_TRUE(noUV.Names(1, "UV0")) << noUV.Report();

	DocumentBuilder p;
	GraphNode& read = p.Node(2, "Input/Property");
	DocumentBuilder::Setting(read, "property", "41");
	Compiled noProperty = Compile(p.document, {{2, "Out"}});
	EXPECT_FALSE(noProperty.ok);
	EXPECT_TRUE(noProperty.Names(2, "property the blackboard doesn't have")) << noProperty.Report();
}

TEST(NodeShaderCompilerTest, OverlongSwizzleRefused) {
	DocumentBuilder b;
	b.Property(1, 1, "pA", "Vector2");
	GraphNode& swizzle = b.Node(2, "Channel/Swizzle");
	DocumentBuilder::Setting(swizzle, "Mask", "\"xyz\"");
	b.Connect(1, "Out", 2, "In");
	Compiled c = Compile(b.document, {{2, "Out"}});
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(2, "Invalid mask for a 'Vector2' input.")) << c.Report();

	DocumentBuilder free;
	GraphNode& unconnected = free.Node(2, "Channel/Swizzle");
	DocumentBuilder::Setting(unconnected, "Mask", "\"wz\"");
	EXPECT_TRUE(Compile(free.document, {{2, "Out"}}).ok);
}

TEST(NodeShaderCompilerTest, DynamicVectorsResolveNarrowest) {
	DocumentBuilder narrow = TwoPropertiesInto("Math/Add", "Vector3", "Vector2");
	Compiled c = Compile(narrow.document, {{3, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_EQ(c.program.At(c.program.roots[0].value).type, GraphValueType::Float2);
	EXPECT_NE(c.text.body.find("pA.xy"), str::npos) << c.text.body;

	DocumentBuilder promote = TwoPropertiesInto("Math/Add", "Vector3", "Float");
	Compiled p = Compile(promote.document, {{3, "Out"}});
	ASSERT_TRUE(p.ok) << p.Report();
	EXPECT_EQ(p.program.At(p.program.roots[0].value).type, GraphValueType::Float3);
	EXPECT_NE(p.text.body.find("pB.xxx"), str::npos) << p.text.body;

	DocumentBuilder none;
	none.Node(1, "Math/Add");
	Compiled n = Compile(none.document, {{1, "Out"}});
	ASSERT_TRUE(n.ok);
	EXPECT_EQ(n.program.At(n.program.roots[0].value).type, GraphValueType::Float);
}

TEST(NodeShaderCompilerTest, FixedPortWidensWith0Then1) {
	DocumentBuilder b;
	b.Property(1, 1, "pA", "Vector2");
	b.Node(2, "Math/MatrixConstruction");
	b.Connect(1, "Out", 2, "M0");
	Compiled c = Compile(b.document, {{2, "4x4"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.body.find("float4(pA, 0.0, 1.0)"), str::npos) << c.text.body;
}

TEST(NodeShaderCompilerTest, ShortConstantTakesRestFromDefault) {
	DocumentBuilder b;
	b.Property(1, 1, "pA", "Vector3");
	GraphNode& multiply = b.Node(2, "Math/Multiply");
	DocumentBuilder::Constant(multiply, "B", "[0.5]");
	b.Connect(1, "Out", 2, "A");
	Compiled c = Compile(b.document, {{2, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.body.find("pA * float3(0.5, 2.0, 2.0)"), str::npos) << c.text.body;
}

TEST(NodeShaderCompilerTest, MultiplyTakesMixedOperands) {
	DocumentBuilder vectors = TwoPropertiesInto("Math/Multiply");
	Compiled v = Compile(vectors.document, {{3, "Out"}});
	ASSERT_TRUE(v.ok);
	EXPECT_NE(v.text.body.find("pA * pB"), str::npos) << v.text.body;

	DocumentBuilder mixed;
	mixed.Property(1, 1, "pA", "Vector4");
	mixed.Node(2, "Math/MatrixConstruction");
	mixed.Connect(1, "Out", 2, "M0");
	mixed.Node(3, "Math/Multiply");
	mixed.Connect(2, "3x3", 3, "A");
	mixed.Connect(1, "Out", 3, "B");
	Compiled m = Compile(mixed.document, {{3, "Out"}});
	ASSERT_TRUE(m.ok) << m.Report();
	EXPECT_EQ(m.program.At(m.program.roots[0].value).type, GraphValueType::Float3);
	EXPECT_NE(m.text.body.find("mul("), str::npos) << m.text.body;

	DocumentBuilder matrices;
	matrices.Node(1, "Math/MatrixConstruction");
	matrices.Node(2, "Math/MatrixConstruction");
	matrices.Node(3, "Math/Multiply");
	matrices.Connect(1, "4x4", 3, "A");
	matrices.Connect(2, "2x2", 3, "B");
	Compiled mm = Compile(matrices.document, {{3, "Out"}});
	ASSERT_TRUE(mm.ok) << mm.Report();
	EXPECT_EQ(mm.program.At(mm.program.roots[0].value).type, GraphValueType::Float2x2);
}

TEST(NodeShaderCompilerTest, ARedirectCompilesToNothing) {
	DocumentBuilder b = TwoPropertiesInto("Math/Add", "Vector3", "Vector3");
	b.Node(4, GraphDocumentOps::REDIRECT_DEF);
	b.Connect(3, "Out", 4, GraphDocumentOps::REDIRECT_IN);
	b.Node(5, "Math/Negate");
	b.Connect(4, GraphDocumentOps::REDIRECT_OUT, 5, "In");
	Compiled c = Compile(b.document, {{5, "Out"}});
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_EQ(Lines(c.text.body), 2u) << c.text.body;
	EXPECT_NE(c.text.body.find("float3 n5_Out = -n3_Out;"), str::npos) << c.text.body;
}

TEST(NodeShaderCompilerTest, LiteralReadsBackToSameFloat) {
	EXPECT_EQ(GraphHLSL::FloatLiteral(1.0f), "1.0");
	EXPECT_EQ(GraphHLSL::FloatLiteral(0.1f), "0.1");
	EXPECT_EQ(GraphHLSL::FloatLiteral(-2.0f), "(-2.0)");
	EXPECT_EQ(GraphHLSL::FloatLiteral(-0.0f), "(-0.0)");
	EXPECT_EQ(GraphHLSL::FloatLiteral(1e20f), "1e+20");
	EXPECT_EQ(GraphHLSL::FloatLiteral(std::numeric_limits<f32>::infinity()), "asfloat(0x7F800000u)");
	for (f32 value : {3.14159265f, 1.0f / 3.0f, 12.9898f, 43758.5453f, 1e-7f}) {
		str literal = GraphHLSL::FloatLiteral(value);
		EXPECT_EQ(std::strtof(literal.c_str(), nullptr), value) << literal;
	}
}

TEST(NodeShaderCompilerTest, NoiseWrittenOnceWhenUsed) {
	DocumentBuilder b;
	b.Node(1, "Procedural/SimpleNoise");
	b.Node(2, "Procedural/SimpleNoise");
	GraphNode& scale = b.document.nodes.back();
	DocumentBuilder::Constant(scale, "Scale", "[20]");
	Compiled c = Compile(b.document, {{1, "Out"}, {2, "Out"}}, GraphStageFragment, TestBindings());
	ASSERT_TRUE(c.ok) << c.Report();
	size_t first = c.text.functions.find("float ShalltearGraphValueNoiseDeterministic(");
	ASSERT_NE(first, str::npos) << c.text.functions;
	EXPECT_EQ(c.text.functions.find("float ShalltearGraphValueNoiseDeterministic(", first + 1), str::npos);
	EXPECT_EQ(c.text.functions.find("LegacySine"), str::npos);
	EXPECT_EQ(c.text.functions.find("Voronoi"), str::npos);
}

namespace {

	Compiled CompileInput(const GraphDocument& document, u32 node, std::string_view port) {
		Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
		roots.push_back({node, str(port), GraphStageFragment, true});
		GraphCompileRequest request{roots, TestBindings()};
		Compiled out;
		out.ok = GraphCompiler::Compile(document, request, out.program, out.diagnostics);
		if (out.ok) {
			GraphHLSL::Emit(out.program, GraphStageFragment, out.text);
		}
		return out;
	}

	GraphProperty& AddProperty(DocumentBuilder& b, u32 id, std::string_view reference, std::string_view type) {
		b.Property(id, 100 + id, reference, type);
		return b.document.properties.back();
	}

} // anonymous namespace

TEST(NodeShaderCompilerTest, BlockReadThroughAdaptedInput) {
	DocumentBuilder b;
	b.Node(1, "Input/Float");
	DocumentBuilder::Constant(b.document.nodes.back(), "X", "[0.25]");
	b.Node(2, "Block/BaseColor");
	b.Connect(1, "Out", 2, "Value");
	b.Node(3, "Block/Roughness");
	b.Node(4, "Block/NormalTS");

	Compiled wired = CompileInput(b.document, 2, "Value");
	ASSERT_TRUE(wired.ok) << wired.Report();
	ASSERT_EQ(wired.program.roots.size(), 1u);
	EXPECT_EQ(wired.program.At(wired.program.roots[0].value).type, GraphValueType::Float3);
	EXPECT_EQ(wired.text.roots[0], "float3(0.25, 0.25, 0.25)");

	Compiled unwired = CompileInput(b.document, 3, "Value");
	ASSERT_TRUE(unwired.ok) << unwired.Report();
	EXPECT_EQ(unwired.text.roots[0], "0.5");

	Compiled normal = CompileInput(b.document, 4, "Value");
	ASSERT_TRUE(normal.ok) << normal.Report();
	EXPECT_EQ(normal.text.roots[0], "tTangentSpaceNormal");

	Compiled asked = Compile(b.document, {{3, "Value"}});
	EXPECT_FALSE(asked.ok);
	EXPECT_TRUE(asked.Names(3, "no output named 'Value'")) << asked.Report();
}

TEST(NodeShaderCompilerTest, UnwiredTextureIsRefusedNamingPort) {
	DocumentBuilder b;
	b.Node(1, "Input/SampleTexture2D");
	Compiled c = Compile(b.document, {{1, "RGBA"}}, GraphStageFragment, TestBindings());
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(1, "input 'Texture' needs a texture")) << c.Report();
}

TEST(NodeShaderCompilerTest, PropertyIsReadAsDeclarationReadsIt) {
	DocumentBuilder b;
	AddProperty(b, 1, "useRim", "Boolean");
	GraphProperty& steps = AddProperty(b, 2, "steps", "Float");
	steps.settings.push_back({str("mode"), str("\"integer\"")});
	AddProperty(b, 3, "albedo", "Texture2D");
	b.Node(4, "Input/SampleTexture2D");
	b.Connect(103, "Out", 4, "Texture");

	Compiled boolean = Compile(b.document, {{101, "Out"}});
	ASSERT_TRUE(boolean.ok) << boolean.Report();
	EXPECT_EQ(boolean.program.At(boolean.program.roots[0].value).type, GraphValueType::Bool);
	EXPECT_EQ(boolean.text.roots[0], "(useRim != 0)");

	Compiled integer = Compile(b.document, {{102, "Out"}});
	ASSERT_TRUE(integer.ok) << integer.Report();
	EXPECT_EQ(integer.text.roots[0], "((float)steps)");

	Compiled sample = Compile(b.document, {{4, "RGBA"}}, GraphStageFragment, TestBindings());
	ASSERT_TRUE(sample.ok) << sample.Report();
	EXPECT_NE(sample.text.body.find("albedo.Sample(sltSampler_albedo, tUV0)"), str::npos) << sample.text.body;
}

TEST(NodeShaderCompilerTest, UndeclarablePropertyRefused) {
	DocumentBuilder b;
	AddProperty(b, 1, "warp", "Matrix4");
	AddProperty(b, 2, "float", "Float");
	AddProperty(b, 3, "n5_Out", "Float");
	Compiled matrix = Compile(b.document, {{101, "Out"}});
	EXPECT_TRUE(matrix.Names(101, "graph-local constant")) << matrix.Report();
	Compiled keyword = Compile(b.document, {{102, "Out"}});
	EXPECT_TRUE(keyword.Names(102, "the shading language already uses")) << keyword.Report();
	Compiled emitter = Compile(b.document, {{103, "Out"}});
	EXPECT_TRUE(emitter.Names(103, "a prefix the engine reserves")) << emitter.Report();
}

TEST(NodeShaderCompilerTest, ATextureCannotBeChosenBetween) {
	GraphProgram program;
	GraphProgramBuilder builder(program);
	const f32 one = 1.0f;
	GraphValueId condition = builder.Binary(GraphOp::Less, builder.Scalar(0.0f), builder.Constant(GraphValueType::Float, {&one, 1}));
	GraphValueId a = builder.Texture(GraphValueType::Texture2D, "a", "a", "sa");
	GraphValueId b = builder.Texture(GraphValueType::Texture2D, "b", "b", "sb");
	EXPECT_EQ(builder.Select(condition, a, b), GRAPH_NO_VALUE);
	EXPECT_TRUE(builder.Failed());
}

TEST(NodeShaderCompilerTest, MissingUVChannelRefused) {
	DocumentBuilder b;
	b.Node(1, "Input/UV");
	DocumentBuilder::Setting(b.document.nodes.back(), "Channel", "\"UV2\"");
	Compiled c = Compile(b.document, {{1, "Out"}}, GraphStageFragment, TestBindings());
	EXPECT_FALSE(c.ok);
	EXPECT_TRUE(c.Names(1, "reads channel UV2 (a mesh here has 1 UV set")) << c.Report();
}
