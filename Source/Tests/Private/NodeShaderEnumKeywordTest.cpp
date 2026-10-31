#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphDefinitions.h"
#include "GraphPreview.h"
#include "GraphProperties.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace Ops = NodeShaderEditOps;

namespace {

	using namespace ShadingTest;

	constexpr std::string_view MODE_ENTRIES = R"([{"id":1,"name":"Alpha","reference":"ALPHA"},{"id":2,"name":"Beta","reference":"BETA"},)"
											  R"({"id":3,"name":"Gamma","reference":"GAMMA"}])";

	UnlitBuilder ModeGraph(std::string_view entries = MODE_ENTRIES) {
		UnlitBuilder b;
		GraphProperty& mode = b.Blackboard(40, "Mode", "_MODE", "EnumKeyword", "1");
		mode.settings.push_back({"entries", str(entries)});
		DocumentBuilder::Setting(b.Node(41, "Utility/Keyword"), "property", "40");
		const char* channels[] = {"X", "Y", "Z"};
		for (u32 i = 0; i < 3; ++i) {
			DocumentBuilder::Constant(b.Node(45 + i, "Input/Vector3"), channels[i], "1");
			b.Connect(45 + i, "Out", 41, std::to_string(i + 1));
		}
		b.Connect(41, "Out", 2, "Value");
		return b;
	}

	struct Generated {
		bool ok = false;
		str text;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};

		bool Has(std::string_view fragment) const { return text.find(fragment) != str::npos; }
		bool Names(u32 node, std::string_view fragment) const {
			return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const GraphDiagnostic& diagnostic) {
				return diagnostic.node == node && diagnostic.message.find(fragment) != str::npos;
			});
		}
		str Report() const {
			str out;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				out += "node " + std::to_string(diagnostic.node) + ": " + diagnostic.message + "\n";
			}
			return out + text;
		}
	};

	Generated Generate(const GraphDocument& document) {
		Generated out;
		out.ok = GraphSurface::Generate(document, "project://Assets/Mode.slshader", out.text, out.diagnostics);
		return out;
	}

	str Refusal(std::string_view entries, std::string_view defaultJson = "0") {
		GraphProperty property;
		property.id = 40;
		property.name = "Mode";
		property.reference = "_MODE";
		property.type = "EnumKeyword";
		property.defaultValue = str(defaultJson);
		if (!entries.empty()) {
			property.settings.push_back({"entries", str(entries)});
		}
		GraphPropertyInfo info;
		str error;
		return GraphProperties::Parse(property, info, error) ? str("(accepted)") : error;
	}

	const GraphProperty& PropertyIn(const GraphDocument& document, u32 id) {
		return *std::find_if(document.properties.begin(), document.properties.end(), [id](const GraphProperty& held) { return held.id == id; });
	}

	str Entries(const GraphDocument& document, u32 property) {
		Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
		str error;
		if (!GraphProperties::ReadEntries(PropertyIn(document, property), entries, error)) {
			return error;
		}
		str out;
		for (const GraphKeywordEntry& entry : entries) {
			out += entry.name + "/" + entry.reference + " ";
		}
		return out + "default " + PropertyIn(document, property).defaultValue;
	}

} // anonymous namespace

TEST(NodeShaderEnumKeywordTest, EnumKeywordDeclaresGroupAndNodeChain) {
	Generated g = Generate(ModeGraph().document);
	ASSERT_TRUE(g.ok) << g.Report();
	EXPECT_TRUE(g.Has("#pragma shalltear_keyword _MODE_ALPHA _MODE 0 \"Alpha\"\n")) << g.text;
	EXPECT_TRUE(g.Has("#pragma shalltear_keyword _MODE_BETA _MODE 1 \"Beta\"\n")) << g.text;
	EXPECT_TRUE(g.Has("#pragma shalltear_keyword _MODE_GAMMA _MODE 2 \"Gamma\"\n")) << g.text;
	EXPECT_TRUE(g.Has("#pragma shalltear_default _MODE 1\n"));
	EXPECT_TRUE(g.Has("#pragma shalltear_hint _MODE hidden\n")) << g.text;
	EXPECT_TRUE(g.Has("#pragma shalltear_label _MODE \"Mode\"\n")) << g.text;
	EXPECT_TRUE(g.Has("\tint _MODE;\n")) << g.text;
	EXPECT_TRUE(g.Has("#if defined(_MODE_ALPHA)\n")) << g.text;
	EXPECT_TRUE(g.Has("#if defined(_MODE_BETA)\n")) << g.text;
	EXPECT_FALSE(g.Has("defined(_MODE_GAMMA)"));
}

TEST(NodeShaderEnumKeywordTest, ThePreviewDefinesTheDefaultEntryAlone) {
	GraphPreview::Program program;
	ASSERT_TRUE(GraphPreview::Generate(ModeGraph().document, "Mode", program));
	EXPECT_NE(program.text.find("#define _MODE_BETA\n"), str::npos);
	EXPECT_EQ(program.text.find("#define _MODE_ALPHA"), str::npos);
	EXPECT_EQ(program.text.find("#define _MODE_GAMMA"), str::npos);
	for (const GraphDiagnostic& diagnostic : program.diagnostics) {
		ADD_FAILURE() << "node " << diagnostic.node << ": " << diagnostic.message;
	}
}

TEST(NodeShaderEnumKeywordTest, InputPerEntryById) {
	UnlitBuilder b = ModeGraph();
	b.Blackboard(50, "Moss", "_MOSS", "BooleanKeyword", "false");
	GraphNode& moss = b.Node(51, "Utility/Keyword");
	DocumentBuilder::Setting(moss, "property", "50");
	GraphDefinitions definitions;
	str error;
	const GraphNodeDefinition* definition = definitions.Find(b.document, b.document.nodes[0], &error);
	ASSERT_NE(definition, nullptr) << error;
	ASSERT_EQ(definition->inputs.size(), 3u);
	const char* names[] = {"1", "2", "3"};
	const char* labels[] = {"Alpha", "Beta", "Gamma"};
	const char* keywords[] = {"_MODE_ALPHA", "_MODE_BETA", "_MODE_GAMMA"};
	for (size_t i = 0; i < 3; ++i) {
		EXPECT_EQ(definition->inputs[i].name, names[i]);
		EXPECT_EQ(GraphPortLabel(definition->inputs[i]), labels[i]);
		EXPECT_EQ(definition->inputs[i].keyword, keywords[i]);
	}
	ASSERT_EQ(definition->outputs.size(), 1u);
	EXPECT_EQ(definition->outputs[0].name, "Out");

	const GraphNodeDefinition* boolean = definitions.Find(b.document, b.document.nodes.back(), &error);
	ASSERT_NE(boolean, nullptr) << error;
	ASSERT_EQ(boolean->inputs.size(), 2u);
	EXPECT_EQ(boolean->inputs[0].name, "On");
	EXPECT_TRUE(boolean->inputs[0].keyword.empty());
}

TEST(NodeShaderEnumKeywordTest, RenameAndReorderKeepEveryWire) {
	UnlitBuilder b = ModeGraph();
	ASSERT_TRUE(Ops::RenameKeywordEntry(b.document, 40, 3, "Deep Blue"));
	ASSERT_TRUE(Ops::MoveKeywordEntry(b.document, 40, 3, 0));
	EXPECT_EQ(Entries(b.document, 40), "Deep Blue/DEEP_BLUE Alpha/ALPHA Beta/BETA default 2");

	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = definitions.Find(b.document, b.document.nodes[0]);
	ASSERT_NE(definition, nullptr);
	ASSERT_EQ(definition->inputs.size(), 3u);
	EXPECT_EQ(definition->inputs[0].name, "3");
	EXPECT_EQ(GraphPortLabel(definition->inputs[0]), "Deep Blue");
	const GraphNode* keyword = GraphDocumentOps::FindNode(b.document, 41);
	EXPECT_TRUE(std::any_of(keyword->inputs.begin(), keyword->inputs.end(), [](const GraphInput& input) { return input.port == "3" && input.fromNode == 47; }));

	Generated g = Generate(b.document);
	ASSERT_TRUE(g.ok) << g.Report();
	EXPECT_TRUE(g.Has("#pragma shalltear_keyword _MODE_DEEP_BLUE _MODE 0 \"Deep Blue\"\n")) << g.text;
	EXPECT_TRUE(g.Has("#if defined(_MODE_DEEP_BLUE)\n")) << g.text;
	EXPECT_FALSE(g.Has("defined(_MODE_BETA)"));
}

TEST(NodeShaderEnumKeywordTest, EntriesAShaderCannotHoldAreRefused) {
	EXPECT_EQ(Refusal(MODE_ENTRIES), "(accepted)");
	EXPECT_EQ(Refusal({}), "has no entries");
	EXPECT_EQ(Refusal(R"([{"id":1,"name":"A","reference":"A"}])"), "has 1 entry (an enum keyword has 2-8)");
	str nine = "[";
	for (u32 i = 1; i <= 9; ++i) {
		const str n = std::to_string(i);
		nine += str(i > 1 ? "," : "") + R"({"id":)" + n + R"(,"name":"E)" + n + R"(","reference":"E)" + n + R"("})";
	}
	EXPECT_EQ(Refusal(nine + "]"), "has 9 entries (an enum keyword has 2-8)");
	EXPECT_EQ(Refusal(R"([{"id":1,"name":"A","reference":"A"},{"id":1,"name":"B","reference":"B"}])"), "has 2 entries with the id 1");
	EXPECT_EQ(Refusal(R"([{"id":1,"name":"A","reference":"A"},{"id":2,"name":"Also A","reference":"A"}])"),
			  "has entries 'A' and 'Also A', which both enable '_MODE_A'");
	EXPECT_EQ(Refusal(R"([{"id":1,"name":"A","reference":"A-1"},{"id":2,"name":"B","reference":"B"}])"),
			  "has entry 'A' (its keyword '_MODE_A-1' isn't a name a shader can define)");
	EXPECT_EQ(Refusal(R"([{"id":1,"name":"Say \"A\"","reference":"A"},{"id":2,"name":"B","reference":"B"}])"),
			  "has an entry with an empty name, or a quote/line break in it (an Inspector label can't show it)");
	EXPECT_EQ(Refusal(MODE_ENTRIES, "3"), "has a default that is not one of its entries");
	EXPECT_EQ(Refusal(MODE_ENTRIES, "0.5"), "has a default that is not one of its entries");
}

TEST(NodeShaderEnumKeywordTest, CollidingKeywordRefused) {
	{
		UnlitBuilder b = ModeGraph();
		b.Blackboard(50, "Alpha Strength", "_MODE_ALPHA", "Float");
		Generated g = Generate(b.document);
		EXPECT_FALSE(g.ok);
		EXPECT_TRUE(g.Names(0, "meet at '_MODE_ALPHA'")) << g.Report();
	}
	{
		UnlitBuilder b = ModeGraph();
		b.Blackboard(50, "Beta Too", "_MODE_BETA", "BooleanKeyword", "false");
		Generated g = Generate(b.document);
		EXPECT_FALSE(g.ok);
		EXPECT_TRUE(g.Names(0, "both enable a keyword '_MODE_BETA'")) << g.Report();
	}
	{
		UnlitBuilder b;
		b.Blackboard(50, "Moss", "_MOSS", "BooleanKeyword", "false");
		b.Blackboard(51, "Toggle", "_MOSS_Toggle", "BooleanKeyword", "false");
		Generated g = Generate(b.document);
		EXPECT_FALSE(g.ok);
		EXPECT_TRUE(g.Names(0, "meet at '_MOSS_Toggle'")) << g.Report();
	}
}

TEST(NodeShaderEnumKeywordTest, EnumKeywordNamesFollowRule) {
	UnlitBuilder b;
	const u32 mode = Ops::AddProperty(b.document, "EnumKeyword");
	ASSERT_NE(mode, 0u);
	EXPECT_EQ(PropertyIn(b.document, mode).name, "Enum Keyword");
	EXPECT_EQ(PropertyIn(b.document, mode).reference, "_ENUM_KEYWORD");
	EXPECT_EQ(Entries(b.document, mode), "A/A B/B C/C default 0");
	EXPECT_EQ(Ops::PropertyTypeLabel("EnumKeyword"), "Enum Keyword");
	const u32 reader = Ops::AddPropertyNode(b.document, mode, {0.0f, 0.0f});
	EXPECT_EQ(GraphDocumentOps::FindNode(b.document, reader)->def, "Utility/Keyword");

	EXPECT_EQ(Ops::AddKeywordEntry(b.document, mode), 4u);
	EXPECT_EQ(Ops::AddKeywordEntry(b.document, mode), 5u);
	EXPECT_EQ(Entries(b.document, mode), "A/A B/B C/C New/NEW New 1/NEW_1 default 0");
	for (u32 i = 0; i < 3; ++i) {
		EXPECT_NE(Ops::AddKeywordEntry(b.document, mode), 0u);
	}
	EXPECT_EQ(Ops::AddKeywordEntry(b.document, mode), 0u);

	EXPECT_TRUE(Ops::RenameKeywordEntry(b.document, mode, 4, "  very low "));
	EXPECT_FALSE(Ops::RenameKeywordEntry(b.document, mode, 5, "a"));
	EXPECT_FALSE(Ops::RenameKeywordEntry(b.document, mode, 5, "   "));
	EXPECT_FALSE(Ops::RenameKeywordEntry(b.document, mode, 5, "Say \"hi\""));
	Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(GraphProperties::ReadEntries(PropertyIn(b.document, mode), entries, error)) << error;
	EXPECT_EQ(entries[3].name, "very low");
	EXPECT_EQ(entries[3].reference, "VERY_LOW");

	Generated g = Generate(b.document);
	EXPECT_TRUE(g.ok) << g.Report();
}

TEST(NodeShaderEnumKeywordTest, RemovedEntryTakesWire) {
	UnlitBuilder b = ModeGraph();
	ASSERT_TRUE(Ops::SetInputConstant(b.document, 41, "1", std::array<f32, 3>{0.5f, 0.5f, 0.5f}));
	ASSERT_EQ(Ops::RemoveKeywordEntries(b.document, 40, std::array<u32, 1>{1}), 1u);
	EXPECT_EQ(Entries(b.document, 40), "Beta/BETA Gamma/GAMMA default 0");
	const GraphNode* keyword = GraphDocumentOps::FindNode(b.document, 41);
	EXPECT_FALSE(std::any_of(keyword->inputs.begin(), keyword->inputs.end(), [](const GraphInput& input) { return input.port == "1"; }));
	EXPECT_TRUE(std::any_of(keyword->inputs.begin(), keyword->inputs.end(), [](const GraphInput& input) { return input.port == "2" && input.fromNode == 46; }));

	EXPECT_EQ(Ops::RemoveKeywordEntries(b.document, 40, std::array<u32, 1>{2}), 0u);
	ASSERT_EQ(Ops::AddKeywordEntry(b.document, 40), 1u);
	ASSERT_TRUE(Ops::MoveKeywordEntry(b.document, 40, 3, 0));
	EXPECT_EQ(Entries(b.document, 40), "Gamma/GAMMA Beta/BETA New/NEW default 1");
	ASSERT_EQ(Ops::RemoveKeywordEntries(b.document, 40, std::array<u32, 1>{2}), 1u);
	EXPECT_EQ(Entries(b.document, 40), "Gamma/GAMMA New/NEW default 1");
	Generated g = Generate(b.document);
	EXPECT_TRUE(g.ok) << g.Report();
}

namespace {

	class NodeShaderEnumKeywordFixture : public MaterialReadbackFixture {
	protected:
		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			if (!CompilerAvailable()) {
				GTEST_SKIP() << "dxcompiler.dll unavailable";
			}
		}
	};

} // anonymous namespace

TEST_F(NodeShaderEnumKeywordFixture, EachMaterialDrawsTheEntryItEnables) {
	WriteProjectFile("Mode.slshader", GraphDocumentOps::Write(ModeGraph().document));
	ShaderAsset* shader = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://Mode.slshader"))));
	ASSERT_NE(shader, nullptr);
	const char* names[] = {"Alpha", "Beta", "Gamma"};
	u32 entries = 0;
	for (const ShaderKeywordBinding& binding : shader->keywordBindings) {
		if (binding.member == "_MODE" && binding.value >= 0 && binding.value < 3) {
			++entries;
			EXPECT_EQ(binding.entry, names[binding.value]) << binding.keyword;
		}
	}
	EXPECT_EQ(entries, 3u);

	auto render = [&](const char* file, const char* keywords) {
		MaterialAsset* material = LoadMaterial(file, shader->assetId, keywords);
		ShadingStage stage;
		EXPECT_TRUE(BuildStage(material, stage));
		stage.sun->SetEnabled(false);
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		EXPECT_TRUE(RenderStage(stage, pixels, size));
		const Vec3 texel = CenterTexel(pixels, size);
		TearDownStage(stage);
		return texel;
	};
	const Vec3 alpha = render("Alpha.mat", "\t\"keywords\": [\"_MODE_ALPHA\"],\n\t\"ints\": { \"_MODE\": 0 }");
	const Vec3 beta = render("Beta.mat", "\t\"keywords\": [\"_MODE_BETA\"],\n\t\"ints\": { \"_MODE\": 1 }");
	const Vec3 gamma = render("Gamma.mat", "\t\"keywords\": [\"_MODE_GAMMA\"],\n\t\"ints\": { \"_MODE\": 2 }");
	const Vec3 none = render("None.mat", "");
	EXPECT_GT(alpha.x, 250.0f);
	EXPECT_LT(alpha.y + alpha.z, 5.0f);
	EXPECT_GT(beta.y, 250.0f);
	EXPECT_LT(beta.x + beta.z, 5.0f);
	EXPECT_GT(gamma.z, 250.0f);
	EXPECT_LT(gamma.x + gamma.y, 5.0f);
	EXPECT_GT(none.z, 250.0f);
}
