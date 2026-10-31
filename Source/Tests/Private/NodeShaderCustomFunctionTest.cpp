#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphDefinitions.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	using namespace ShadingTest;
	namespace Ops = NodeShaderEditOps;

	GraphCustomFunctionPort Port(u32 id, std::string_view name, std::string_view type) {
		GraphCustomFunctionPort port;
		port.id = id;
		port.name.assign(name);
		for (u32 i = 0; i < std::size(GRAPH_CUSTOM_FUNCTION_PORT_TYPES); ++i) {
			port.type = GRAPH_CUSTOM_FUNCTION_PORT_TYPES[i] == type ? i : port.type;
		}
		return port;
	}

	GraphNode& CustomFunction(DocumentBuilder& b, u32 id, std::string_view name, std::initializer_list<GraphCustomFunctionPort> inputs,
							  std::initializer_list<GraphCustomFunctionPort> outputs) {
		GraphNode& node = b.Node(id, GRAPH_CUSTOM_FUNCTION_NODE);
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_NAME, Ops::Quote(name));
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_INPUTS,
								 GraphCustomFunctions::WritePorts({inputs.begin(), inputs.size()}));
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_OUTPUTS,
								 GraphCustomFunctions::WritePorts({outputs.begin(), outputs.size()}));
		return node;
	}

	void StringBody(GraphNode& node, std::string_view body) {
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_TYPE, Ops::Quote("String"));
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_BODY, Ops::Quote(body));
	}

	void Replace(GraphNode& node, std::string_view key, std::string_view json) {
		for (GraphSetting& setting : node.settings) {
			if (setting.key == key) {
				setting.value.assign(json);
				return;
			}
		}
		DocumentBuilder::Setting(node, key, json);
	}

	void FileSource(GraphNode& node, const Guid& source) {
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_TYPE, Ops::Quote("File"));
		DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_SOURCE, Ops::Quote(source.ToString()));
	}

	constexpr std::string_view HALVE_BODY = "Out = In * 0.5;";
	constexpr std::string_view HALVE_FILE = "#ifndef HALVE_INCLUDED\n#define HALVE_INCLUDED\n"
											"void Halve_float(float4 In, out float4 Out)\n{\n\tOut = In * 0.5;\n}\n#endif\n";

	class NodeShaderCustomFunctionFixture : public MaterialReadbackFixture {
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

		Guid AddFunctionFile(const char* fileName, std::string_view text) {
			std::filesystem::create_directories((_root / fileName).parent_path());
			WriteProjectFile(fileName, str(text));
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path(str("project://") + fileName));
		}

		ShaderAsset* AddGraph(const char* fileName, const GraphDocument& document) {
			WriteProjectFile(fileName, GraphDocumentOps::Write(document));
			return static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
		}

		Compiled CompileNode(const GraphDocument& document, u32 node) {
			Vector<GraphCompileRoot> roots{GetMemoryResource(MemTag::Editor)};
			const GraphNode* found = nullptr;
			for (const GraphNode& candidate : document.nodes) {
				found = candidate.id == node ? &candidate : found;
			}
			const GraphNodeDefinition* definition = found ? _definitions.Find(document, *found) : nullptr;
			if (definition) {
				for (const GraphPortDefinition& port : definition->outputs) {
					roots.push_back({node, str(port.name), GraphStageFragment});
				}
			}
			GraphCompileRequest request{roots, TestBindings()};
			request.definitions = &_definitions;
			Compiled out;
			out.ok = GraphCompiler::Compile(document, request, out.program, out.diagnostics);
			if (out.ok) {
				GraphHLSL::Emit(out.program, GraphStageAll, out.text);
			}
			return out;
		}

		bool DrawGraph(const char* fileName, const GraphDocument& document, Vec3& texel) {
			ShaderAsset* graph = AddGraph(fileName, document);
			if (!graph) {
				return false;
			}
			MaterialAsset* material = LoadMaterial((str(fileName) + ".mat").c_str(), graph->assetId, {});
			ShadingStage stage;
			if (!BuildStage(material, stage)) {
				return false;
			}
			Vector<u8> pixels;
			Vec2I size{};
			const bool drawn = RenderStage(stage, pixels, size);
			texel = CenterTexel(pixels, size);
			TearDownStage(stage);
			return drawn;
		}

		void ExpectTexel(const Vec3& texel, const Vec3& linear) {
			const Vec3 expected = ExpectedTexel(linear);
			EXPECT_NEAR(texel.x, expected.x, 1.5f);
			EXPECT_NEAR(texel.y, expected.y, 1.5f);
			EXPECT_NEAR(texel.z, expected.z, 1.5f);
		}

		static size_t Count(const str& text, std::string_view needle) {
			size_t count = 0;
			for (size_t at = text.find(needle); at != str::npos; at = text.find(needle, at + needle.size())) {
				++count;
			}
			return count;
		}

		GraphDefinitions _definitions;
		str _why;
	};

} // anonymous namespace

TEST_F(NodeShaderCustomFunctionFixture, StringFunctionDefinedOnce) {
	UnlitBuilder b;
	b.Node(10, "Input/Vector3");
	b.Node(11, "Input/Vector3");
	StringBody(CustomFunction(b, 20, "Twice", {Port(1, "In", "Vector3")}, {Port(2, "Out", "Vector3")}), "$precision3 twice = In * 2.0;\nOut = twice;");
	StringBody(CustomFunction(b, 21, "Twice", {Port(1, "In", "Vector3")}, {Port(2, "Out", "Vector3")}), "$precision3 twice = In * 2.0;\nOut = twice;");
	b.Connect(10, "Out", 20, "1");
	b.Connect(11, "Out", 21, "1");
	GraphNode& add = b.Node(30, "Math/Add");
	(void)add;
	b.Connect(20, "2", 30, "A");
	b.Connect(21, "2", 30, "B");
	Compiled c = CompileNode(b.document, 30);
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_EQ(Count(c.text.functions, "void Twice_float(float3 In, out float3 Out)"), 1u) << c.text.functions;
	EXPECT_NE(c.text.functions.find("float3 twice = In * 2.0;"), str::npos) << c.text.functions;
	EXPECT_EQ(Count(c.text.body, "Twice_float("), 2u) << c.text.body;
}

TEST_F(NodeShaderCustomFunctionFixture, ArgumentsAreInputsInOrder) {
	UnlitBuilder b;
	GraphNode& function = CustomFunction(b, 20, "Ordered", {Port(1, "A", "Float"), Port(2, "B", "Float"), Port(3, "C", "Float")},
										 {Port(4, "Out", "Float")});
	StringBody(function, "Out = A - B - C;");
	DocumentBuilder::Constant(function, "1", "[1]");
	DocumentBuilder::Constant(function, "2", "[2]");
	DocumentBuilder::Constant(function, "3", "[3]");
	Compiled c = CompileNode(b.document, 20);
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.body.find("Ordered_float(1.0, 2.0, 3.0, n20_Out);"), str::npos) << c.text.body;
}

TEST_F(NodeShaderCustomFunctionFixture, RenamingAPortKeepsItsWire) {
	UnlitBuilder b;
	DocumentBuilder::Constant(b.Node(10, "Input/Vector3"), "X", "[0.25]");
	GraphNode& function = CustomFunction(b, 20, "Pass", {Port(1, "In", "Vector3")}, {Port(2, "Out", "Vector3")});
	StringBody(function, "Out = In;");
	b.Connect(10, "Out", 20, "1");
	Replace(function, GRAPH_CUSTOM_FUNCTION_INPUTS, GraphCustomFunctions::WritePorts(std::array{Port(1, "Source", "Vector3")}));
	Replace(function, GRAPH_CUSTOM_FUNCTION_BODY, Ops::Quote("Out = Source;"));
	Compiled c = CompileNode(b.document, 20);
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.functions.find("void Pass_float(float3 Source, out float3 Out)"), str::npos) << c.text.functions;
	EXPECT_NE(c.text.body.find("Pass_float(float3(0.25, 0.0, 0.0)"), str::npos) << c.text.body;
}

TEST_F(NodeShaderCustomFunctionFixture, AnOutputNothingReadsIsStillPassed) {
	UnlitBuilder b;
	StringBody(CustomFunction(b, 20, "Split", {}, {Port(1, "First", "Float"), Port(2, "Second", "Float")}), "First = 1.0;\nSecond = 2.0;");
	b.Node(30, "Math/Negate");
	b.Connect(20, "2", 30, "In");
	Compiled c = CompileNode(b.document, 30);
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.body.find("float n20_0;"), str::npos) << c.text.body;
	EXPECT_NE(c.text.body.find("Split_float(n20_0, n20_Second);"), str::npos) << c.text.body;
}

TEST_F(NodeShaderCustomFunctionFixture, TwoTextsUnderOneNameAreRefused) {
	UnlitBuilder b;
	StringBody(CustomFunction(b, 20, "Shade", {}, {Port(1, "Out", "Float")}), "Out = 1.0;");
	StringBody(CustomFunction(b, 21, "Shade", {}, {Port(1, "Out", "Float")}), "Out = 2.0;");
	b.Node(30, "Math/Add");
	b.Connect(20, "1", 30, "A");
	b.Connect(21, "1", 30, "B");
	Compiled c = CompileNode(b.document, 30);
	ASSERT_FALSE(c.ok);
	EXPECT_NE(c.Report().find("defines `Shade_float` (another custom function in this graph defines it differently)"), str::npos) << c.Report();
}

TEST_F(NodeShaderCustomFunctionFixture, NonFunctionRefused) {
	struct Case {
		std::string_view name;
		std::string_view body;
		GraphCustomFunctionPort input;
		std::string_view refusal;
	};
	const Case cases[] = {
		{"", "Out = 1.0;", Port(1, "In", "Float"), "has no function name"},
		{"Lit", "", Port(1, "In", "Float"), "has no body"},
		{"two words", "Out = 1.0;", Port(1, "In", "Float"), "has function name 'two words' (not an HLSL name)"},
		{"Lit", "Out = 1.0;", Port(1, "float", "Float"), "has a port named 'float' (not an HLSL name)"},
		{"Lit", "Out = 1.0;", Port(1, "Out", "Float"), "has 2 ports named 'Out'"},
	};
	for (const Case& entry : cases) {
		UnlitBuilder b;
		StringBody(CustomFunction(b, 20, entry.name, {entry.input}, {Port(2, "Out", "Float")}), entry.body);
		b.Node(30, "Math/Negate");
		b.Connect(20, "2", 30, "In");
		_definitions.Clear();
		Compiled c = CompileNode(b.document, 30);
		EXPECT_FALSE(c.ok) << entry.refusal;
		EXPECT_NE(c.Report().find(entry.refusal), str::npos) << "wanted '" << entry.refusal << "', said:\n"
															 << c.Report();
	}
	DocumentBuilder b;
	GraphNode& node = b.Node(20, GRAPH_CUSTOM_FUNCTION_NODE);
	DocumentBuilder::Setting(node, GRAPH_CUSTOM_FUNCTION_INPUTS, R"([{"id":1,"name":"Map","type":"Texture2D"}])");
	str error;
	EXPECT_EQ(_definitions.Find(b.document, node, &error), nullptr);
	EXPECT_EQ(error, "has a port 'Map' of type 'Texture2D' (a custom function doesn't take it)");
}

TEST_F(NodeShaderCustomFunctionFixture, PortEditsKeepOtherIds) {
	UnlitBuilder b;
	GraphDocument& document = b.document;
	const u32 node = Ops::AddCustomFunctionNode(document, {0.0f, 0.0f});
	const u32 first = Ops::AddCustomFunctionPort(document, node, false, "Float");
	const u32 second = Ops::AddCustomFunctionPort(document, node, false, "Vector3");
	const u32 output = Ops::AddCustomFunctionPort(document, node, true, "Vector4");
	ASSERT_NE(first, 0u);
	ASSERT_NE(second, 0u);
	ASSERT_NE(output, 0u);
	EXPECT_EQ(Ops::AddCustomFunctionPort(document, node, false, "Texture2D"), 0u);
	auto ports = [&](std::string_view key) {
		Vector<GraphCustomFunctionPort> out{GetMemoryResource(MemTag::Editor)};
		str error;
		EXPECT_TRUE(GraphCustomFunctions::ReadPorts(*GraphDocumentOps::FindNode(document, node), key, out, error)) << error;
		return out;
	};
	auto names = [&](std::string_view key) {
		str joined;
		for (const GraphCustomFunctionPort& port : ports(key)) {
			joined += (joined.empty() ? "" : ",") + port.name;
		}
		return joined;
	};
	EXPECT_EQ(names(GRAPH_CUSTOM_FUNCTION_INPUTS), "New,New_1");
	EXPECT_EQ(names(GRAPH_CUSTOM_FUNCTION_OUTPUTS), "New_2");

	EXPECT_TRUE(Ops::RenameCustomFunctionPort(document, node, first, "my input!"));
	EXPECT_TRUE(Ops::RenameCustomFunctionPort(document, node, second, "my_input"));
	EXPECT_EQ(names(GRAPH_CUSTOM_FUNCTION_INPUTS), "my_input,my_input_1");
	EXPECT_FALSE(Ops::RenameCustomFunctionPort(document, node, second, "my_input_1"));
	EXPECT_TRUE(Ops::SetCustomFunctionPortType(document, node, second, "Matrix3"));
	EXPECT_EQ(GRAPH_CUSTOM_FUNCTION_PORT_TYPES[ports(GRAPH_CUSTOM_FUNCTION_INPUTS)[1].type], "Matrix3");
	EXPECT_TRUE(Ops::MoveCustomFunctionPort(document, node, second, 0));
	EXPECT_EQ(names(GRAPH_CUSTOM_FUNCTION_INPUTS), "my_input_1,my_input");
	EXPECT_EQ(ports(GRAPH_CUSTOM_FUNCTION_INPUTS)[0].id, second);

	b.Node(30, "Input/Vector4");
	b.Connect(30, "Out", node, std::to_string(first));
	b.Connect(30, "Out", node, std::to_string(second));
	b.Node(31, "Math/Negate");
	b.Connect(node, std::to_string(output), 31, "In");
	const u32 removed[] = {first, output};
	EXPECT_EQ(Ops::RemoveCustomFunctionPorts(document, node, removed), 2u);
	EXPECT_EQ(names(GRAPH_CUSTOM_FUNCTION_INPUTS), "my_input_1");
	EXPECT_TRUE(ports(GRAPH_CUSTOM_FUNCTION_OUTPUTS).empty());
	const GraphNode* held = GraphDocumentOps::FindNode(document, node);
	EXPECT_TRUE(std::none_of(held->inputs.begin(), held->inputs.end(), [&](const GraphInput& input) { return input.port == std::to_string(first); }));
	EXPECT_TRUE(std::any_of(held->inputs.begin(), held->inputs.end(),
							[&](const GraphInput& input) { return input.port == std::to_string(second) && input.fromNode == 30; }));
	const GraphNode* reader = GraphDocumentOps::FindNode(document, 31);
	EXPECT_TRUE(std::none_of(reader->inputs.begin(), reader->inputs.end(), [&](const GraphInput& input) { return input.fromNode == node; }));
}

TEST_F(NodeShaderCustomFunctionFixture, DxcErrorsLandOnNodes) {
	str text;
	for (u32 line = 1; line <= 20; ++line) {
		text += line == 7 ? "#include \"../../Assets/f.hlsl\"\n" : (line == 9 ? "#include \"../../Assets/g.hlsl\"\n" : "\n");
	}
	const str messages = "C:/project/Library/NodeShader/graph.hlsl:12:5: error: use of undeclared identifier 'Ou'\n"
						 "C:/project/Library/NodeShader/../../Assets/f.hlsl:3:5: error: unknown type name 'flot'\n"
						 "In file included from C:/engine/Templates/ForwardUnlit.hlsli:23:\n"
						 "In file included from C:\\Project\\Library\\NodeShader\\Graph.hlsl:9:\n"
						 "C:/elsewhere/g.hlsl:1:1: error: reached through a chain\n"
						 "C:/engine/Templates/ForwardUnlit.hlsli:40:1: error: the template's own\n"
						 "C:/project/Library/NodeShader/graph.hlsl:12:9: warning: implicit truncation\n"
						 "C:/project/Library/NodeShader/graph.hlsl:2:1: error: a line no node wrote\n"
						 "C:/project/Library/NodeShader/graph.hlsl:12:5: error: use of undeclared identifier 'Ou'\n";
	Vector<u32> lines{GetMemoryResource(MemTag::Editor)};
	lines.assign(20, 0u);
	lines[11] = 20;
	lines[6] = 21;
	lines[8] = 22;
	Vector<GraphDiagnostic> out{GetMemoryResource(MemTag::Editor)};
	EXPECT_EQ(GraphHLSL::MapCompilerMessages(messages, "C:/project/Library/NodeShader/graph.hlsl", text, lines, out), 3u);
	ASSERT_EQ(out.size(), 3u);
	EXPECT_EQ(out[0].node, 20u);
	EXPECT_EQ(out[0].message, "does not compile: use of undeclared identifier 'Ou'");
	EXPECT_EQ(out[1].node, 21u);
	EXPECT_EQ(out[1].message, "does not compile: unknown type name 'flot'");
	EXPECT_EQ(out[2].node, 22u);
}

TEST_F(NodeShaderCustomFunctionFixture, ATypedNameBecomesAnIdentifier) {
	EXPECT_EQ(GraphCustomFunctions::Identifier("  1   var  * q-30 ( 0 ) (1)   "), "_1_var_q_30_0_1");
	EXPECT_EQ(GraphCustomFunctions::Identifier("float3"), "_float3");
	EXPECT_EQ(GraphCustomFunctions::Identifier("_float3"), "_float3");
	EXPECT_EQ(GraphCustomFunctions::Identifier(""), "_");
	EXPECT_EQ(GraphCustomFunctions::Identifier("Out"), "Out");
	EXPECT_EQ(GraphCustomFunctions::Identifier("New (1)"), "New_1");
}

TEST_F(NodeShaderCustomFunctionFixture, FileFunctionIncludedRelative) {
	const Guid source = AddFunctionFile("Assets/Functions/Halve.hlsl", HALVE_FILE);
	ASSERT_TRUE(source.IsValid());
	UnlitBuilder b;
	FileSource(CustomFunction(b, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), source);
	Compiled c = CompileNode(b.document, 20);
	ASSERT_TRUE(c.ok) << c.Report();
	EXPECT_NE(c.text.functions.find("#include \"../../Assets/Functions/Halve.hlsl\""), str::npos) << c.text.functions;
	EXPECT_EQ(c.text.functions.find("void Halve_float"), str::npos);

	const Guid text = AddFunctionFile("Assets/Functions/Notes.txt", "Halve");
	UnlitBuilder wrong;
	FileSource(CustomFunction(wrong, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), text.IsValid() ? text : AssetDatabase::GenerateGuid());
	Compiled refused = CompileNode(wrong.document, 20);
	EXPECT_FALSE(refused.ok);
	EXPECT_NE(refused.Report().find(text.IsValid() ? "(not an .hlsl file)" : "uses a source file the project doesn't have"),
			  str::npos)
		<< refused.Report();

	UnlitBuilder missing;
	FileSource(CustomFunction(missing, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), AssetDatabase::GenerateGuid());
	Compiled gone = CompileNode(missing.document, 20);
	EXPECT_FALSE(gone.ok);
	EXPECT_NE(gone.Report().find("uses a source file the project doesn't have"), str::npos) << gone.Report();
}

TEST_F(NodeShaderCustomFunctionFixture, AStringFunctionReadsBack) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	UnlitBuilder b;
	b.Node(10, "Input/Color");
	DocumentBuilder::Setting(b.document.nodes.back(), "Color", "[0.8,0.4,0.2,1]");
	StringBody(CustomFunction(b, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), HALVE_BODY);
	b.Connect(10, "Out", 20, "1");
	b.Connect(20, "2", 2, "Value");
	Vec3 texel;
	ASSERT_TRUE(DrawGraph("StringHalve.slshader", b.document, texel));
	ExpectTexel(texel, Vec3(0.4f, 0.2f, 0.1f));
}

TEST_F(NodeShaderCustomFunctionFixture, AFileFunctionReadsBack) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const Guid source = AddFunctionFile("Assets/Functions/Halve.hlsl", HALVE_FILE);
	ASSERT_TRUE(source.IsValid());
	UnlitBuilder b;
	b.Node(10, "Input/Color");
	DocumentBuilder::Setting(b.document.nodes.back(), "Color", "[0.8,0.4,0.2,1]");
	FileSource(CustomFunction(b, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), source);
	b.Connect(10, "Out", 20, "1");
	b.Connect(20, "2", 2, "Value");
	Vec3 texel;
	ASSERT_TRUE(DrawGraph("FileHalve.slshader", b.document, texel));
	ExpectTexel(texel, Vec3(0.4f, 0.2f, 0.1f));
}

TEST_F(NodeShaderCustomFunctionFixture, EditingAFileFunctionRecompilesItsGraph) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const Guid source = AddFunctionFile("Assets/Functions/Halve.hlsl", HALVE_FILE);
	ASSERT_TRUE(source.IsValid());
	UnlitBuilder b;
	b.Node(10, "Input/Color");
	DocumentBuilder::Setting(b.document.nodes.back(), "Color", "[0.8,0.4,0.2,1]");
	FileSource(CustomFunction(b, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), source);
	b.Connect(10, "Out", 20, "1");
	b.Connect(20, "2", 2, "Value");
	ShaderAsset* graph = AddGraph("Watched.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	MaterialAsset* material = LoadMaterial("Watched.mat", graph->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectTexel(CenterTexel(pixels, size), Vec3(0.4f, 0.2f, 0.1f));
	const u32 version = graph->version;

	str quartered(HALVE_FILE);
	quartered.replace(quartered.find("0.5"), 3, "0.25");
	WriteProjectFile("Assets/Functions/Halve.hlsl", quartered);
	RenderSystem::NotifyShaderFileChanged((_root / "Assets" / "Functions" / "Halve.hlsl").generic_string().c_str());
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ExpectTexel(CenterTexel(pixels, size), Vec3(0.2f, 0.1f, 0.05f));
	EXPECT_EQ(graph->version, version + 1);
	TearDownStage(stage);
}

TEST_F(NodeShaderCustomFunctionFixture, WarmArtifactMissesAfterFunctionFileChanges) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const Guid source = AddFunctionFile("Assets/Functions/Halve.hlsl", HALVE_FILE);
	ASSERT_TRUE(source.IsValid());
	UnlitBuilder b;
	b.Node(10, "Input/Color");
	FileSource(CustomFunction(b, 20, "Halve", {Port(1, "In", "Vector4")}, {Port(2, "Out", "Vector4")}), source);
	b.Connect(10, "Out", 20, "1");
	b.Connect(20, "2", 2, "Value");
	ShaderAsset* graph = AddGraph("Cached.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	const str filePath = FileSystem::Resolve(Path("project://Assets/Functions/Halve.hlsl")).ToString();
	auto listedHash = [&filePath](const ShaderAsset& shader) {
		for (const ShaderAsset::Dependency& dependency : shader.dependencies) {
			if (std::filesystem::path(dependency.physicalPath).lexically_normal() == std::filesystem::path(filePath).lexically_normal()) {
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
	str quartered(HALVE_FILE);
	quartered.replace(quartered.find("0.5"), 3, "0.25");
	WriteProjectFile("Assets/Functions/Halve.hlsl", quartered);
	ShaderAsset* reloaded = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("project://Cached.slshader")));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(listedHash(*reloaded), StringHash(std::string_view(quartered)).GetValue());
}
