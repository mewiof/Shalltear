#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphPreview.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"
#include "NodeShaderStackOps.h"
#include "NodeShaderImporter.h"

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;
using namespace ShadingTest;

namespace Ops = NodeShaderEditOps;

namespace Stack = NodeShaderStackOps;

namespace {

	struct Generated {
		bool ok = false;
		str text;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};

		bool Has(std::string_view fragment) const { return text.find(fragment) != str::npos; }
		bool Names(u32 node, std::string_view fragment) const {
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				if (diagnostic.node == node && diagnostic.message.find(fragment) != str::npos) {
					return true;
				}
			}
			return false;
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
		out.ok = GraphSurface::Generate(document, "project://Assets/Test.slshader", out.text, out.diagnostics);
		return out;
	}

	GraphBuilder LightingStacks(std::string_view target = "customLighting") {
		GraphBuilder b;
		b.Graph("target", Ops::Quote(target));
		GraphContext& fragment = b.Context(10, "Context/Fragment");
		b.Block(fragment, 11, "Block/BaseColor");
		b.Block(fragment, 12, "Block/Reflectance");
		b.Block(fragment, 13, "Block/Alpha");
		GraphContext& lighting = b.Context(20, "Context/Lighting");
		b.Block(lighting, 21, "Block/Radiance");
		return b;
	}

	u32 Keyword(GraphBuilder& b, u32 propertyId, u32 node, std::string_view reference, bool defaultOn) {
		GraphProperty& property = b.Blackboard(propertyId, "Moss", reference, "BooleanKeyword", defaultOn ? "true" : "false");
		(void)property;
		GraphNode& keyword = b.Node(node, "Utility/Keyword");
		DocumentBuilder::Setting(keyword, "property", std::to_string(propertyId));
		return node;
	}

} // anonymous namespace

TEST(NodeShaderLightingTest, CustomLightingGraphWritesLightingFunction) {
	GraphBuilder b = LightingStacks();
	b.Node(30, "Input/Light");
	b.Node(31, "Math/Multiply");
	b.Connect(30, "Color", 31, "A");
	b.Connect(30, "Attenuation", 31, "B");
	b.Connect(31, "Out", 21, "Value");
	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_TRUE(generated.Has("#pragma shalltear_template lit\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#define SHALLTEAR_CUSTOM_LIGHTING\n")) << generated.text;
	EXPECT_TRUE(generated.Has("float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, "
							  "ShalltearLightSample light) {"))
		<< generated.text;
	EXPECT_TRUE(generated.Has("light.color")) << generated.text;
	EXPECT_TRUE(generated.Has("light.attenuation")) << generated.text;
}

TEST(NodeShaderLightingTest, UnwiredRadianceIsStandardBrdf) {
	GraphBuilder fresh = LightingStacks();
	Generated standard = Generate(fresh.document);
	ASSERT_TRUE(standard.ok) << standard.Report();
	EXPECT_TRUE(standard.Has("\treturn (ShalltearStandardLighting(surface, normalWS, light, input.viewDirectionWS));\n")) << standard.text;

	GraphBuilder lit = LightingStacks("lit");
	lit.Node(30, "Input/Light");
	lit.Connect(30, "Color", 21, "Value");
	Generated ignored = Generate(lit.document);
	ASSERT_TRUE(ignored.ok) << ignored.Report();
	EXPECT_FALSE(ignored.Has("SHALLTEAR_CUSTOM_LIGHTING"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(lit.document, "Block/Radiance"));
	EXPECT_TRUE(GraphSurface::IsBlockActive(fresh.document, "Block/Radiance"));
}

TEST(NodeShaderLightingTest, LightingStageRefusedWhereInvalid) {
	{
		GraphBuilder b = LightingStacks();
		b.Node(30, "Input/Light");
		b.Connect(30, "Color", 11, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		EXPECT_TRUE(generated.Names(30, "only the lighting stage has, and feeds the fragment stage")) << generated.Report();
	}
	{
		GraphBuilder b = LightingStacks();
		GraphNode& position = b.Node(30, "Input/Position");
		DocumentBuilder::Setting(position, "Space", Ops::Quote("World"));
		b.Node(31, "Math/DDX");
		b.Connect(30, "Out", 31, "In");
		b.Connect(31, "Out", 21, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		EXPECT_TRUE(generated.Names(31, "only the fragment stage has, and feeds the lighting stage")) << generated.Report();
	}
}

TEST(NodeShaderLightingTest, KeywordDeclaresItselfToggleAndBranch) {
	GraphBuilder b = LightingStacks("unlit");
	Keyword(b, 40, 41, "_MOSS", true);
	b.Node(42, "Input/Vector3");
	DocumentBuilder::Constant(b.document.nodes.back(), "X", "1");
	b.Connect(42, "Out", 41, "On");
	b.Connect(41, "Out", 11, "Value");
	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_TRUE(generated.Has("#pragma shalltear_keyword _MOSS _MOSS_Toggle\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#pragma shalltear_default _MOSS_Toggle 1\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#pragma shalltear_hint _MOSS_Toggle hidden\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#pragma shalltear_label _MOSS_Toggle \"Moss\"\n")) << generated.text;
	EXPECT_TRUE(generated.Has("\tint _MOSS_Toggle;\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#if defined(_MOSS)\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#else\n")) << generated.text;
}

TEST(NodeShaderLightingTest, KeywordReadOnlyByNodes) {
	{
		GraphBuilder b = LightingStacks("unlit");
		b.Blackboard(40, "Moss", "_MOSS", "BooleanKeyword", "false");
		GraphNode& reader = b.Node(41, "Input/Property");
		DocumentBuilder::Setting(reader, "property", "40");
		b.Connect(41, "Out", 11, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		EXPECT_TRUE(generated.Names(41, "only a Keyword node can read")) << generated.Report();
	}
	{
		GraphBuilder b = LightingStacks("unlit");
		b.Property(40, 41, "tint", "Color");
		GraphNode& keyword = b.Node(42, "Utility/Keyword");
		DocumentBuilder::Setting(keyword, "property", "40");
		b.Connect(42, "Out", 11, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		EXPECT_TRUE(generated.Names(42, "(a property, not a keyword)")) << generated.Report();
	}
}

TEST(NodeShaderLightingTest, PreviewLightsLightingNodes) {
	GraphBuilder b = LightingStacks();
	Keyword(b, 40, 41, "_MOSS", true);
	b.Blackboard(43, "Dust", "_DUST", "BooleanKeyword", "false");
	b.Node(30, "Input/StandardLighting");
	b.Connect(30, "Out", 21, "Value");
	GraphPreview::Program program;
	ASSERT_TRUE(GraphPreview::Generate(b.document, "Test", program));
	EXPECT_NE(program.text.find("#define _MOSS\n"), str::npos);
	EXPECT_EQ(program.text.find("#define _DUST"), str::npos);
	bool tiled = false;
	for (const GraphPreview::Tile& tile : program.tiles) {
		tiled = tiled || tile.node == 30;
	}
	EXPECT_TRUE(tiled);
	for (const GraphDiagnostic& diagnostic : program.diagnostics) {
		ADD_FAILURE() << "node " << diagnostic.node << ": " << diagnostic.message;
	}
}

TEST(NodeShaderLightingTest, LightingNamesFollowRule) {
	GraphBuilder b = LightingStacks("lit");
	b.document.contexts.pop_back();
	const u32 keyword = Ops::AddProperty(b.document, "BooleanKeyword");
	ASSERT_NE(keyword, 0u);
	const GraphProperty& added = b.document.properties.back();
	EXPECT_EQ(added.name, "Boolean Keyword");
	EXPECT_EQ(added.reference, "_BOOLEAN_KEYWORD");
	EXPECT_EQ(added.defaultValue, "false");
	ASSERT_TRUE(Ops::RenameProperty(b.document, keyword, "Moss Patches"));
	EXPECT_EQ(b.document.properties.back().reference, "_MOSS_PATCHES");

	const u32 reader = Ops::AddPropertyNode(b.document, keyword, {0.0f, 0.0f});
	const GraphNode* node = GraphDocumentOps::FindNode(b.document, reader);
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->def, "Utility/Keyword");
	EXPECT_EQ(Ops::PropertyOf(b.document, reader), keyword);

	EXPECT_TRUE(Stack::EnsureLightingStack(b.document));
	EXPECT_FALSE(Stack::EnsureLightingStack(b.document));
	const u32 lighting = Stack::LightingContext(b.document);
	ASSERT_NE(lighting, 0u);
	Vector<std::string_view> missing{GetMemoryResource(MemTag::Editor)};
	Stack::MissingBlocks(b.document, lighting, missing);
	EXPECT_TRUE(missing.empty());

	GraphDocument created;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(NodeShaderImporter::TemplateDocument(2), created, error)) << error;
	EXPECT_EQ(Ops::GraphSettingText(created, "target"), "customLighting");
	EXPECT_NE(Stack::LightingContext(created), 0u);
	Generated generated = Generate(created);
	EXPECT_TRUE(generated.ok) << generated.Report();
}

namespace {

	class NodeShaderLightingFixture : public MaterialReadbackFixture {
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

		ShaderAsset* AddGraph(const char* fileName, const GraphDocument& document) {
			WriteProjectFile(fileName, GraphDocumentOps::Write(document));
			return static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
		}
	};

} // anonymous namespace

TEST_F(NodeShaderLightingFixture, ToonGraphsTexelEqualsCpuEvaluation) {
	GraphBuilder b = LightingStacks();
	b.Property(50, 51, "tint", "Color");
	b.document.properties.back().defaultValue = "[0.6,0.3,0.9,1]";
	b.Connect(51, "Out", 11, "Value");
	DocumentBuilder::Constant(b.document.contexts[0].blocks[1], "Value", "0");
	b.Node(30, "Input/Light");
	b.Node(31, "Input/Surface");
	b.Node(32, "Math/DotProduct");
	b.Connect(31, "Normal", 32, "A");
	b.Connect(30, "Direction", 32, "B");
	b.Node(33, "Math/Step");
	DocumentBuilder::Constant(b.document.nodes.back(), "Edge", "0.5");
	b.Connect(32, "Out", 33, "In");
	b.Node(34, "Math/Multiply");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "0.75");
	b.Connect(33, "Out", 34, "A");
	b.Node(35, "Math/Add");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "0.25");
	b.Connect(34, "Out", 35, "A");
	b.Node(36, "Math/Multiply");
	b.Connect(31, "Base Color", 36, "A");
	b.Connect(30, "Color", 36, "B");
	b.Node(37, "Math/Multiply");
	b.Connect(36, "Out", 37, "A");
	b.Connect(30, "Attenuation", 37, "B");
	b.Node(38, "Math/Multiply");
	b.Connect(37, "Out", 38, "A");
	b.Connect(35, "Out", 38, "B");
	b.Connect(38, "Out", 21, "Value");
	ShaderAsset* shader = AddGraph("Toon.slshader", b.document);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Toon.mat", shader->assetId, "\t\"colors\": { \"tint\": [0.6, 0.3, 0.9, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<SceneObject> sunObject = stage.sun->GetOwner();
	ASSERT_TRUE(sunObject.IsValid());
	const Vec3 baseColor(0.6f, 0.3f, 0.9f);
	u32 bands[2] = {0, 0};
	for (f32 yaw : {150.0f, 105.0f}) {
		sunObject->SetLocalRotation(Quat::AngleAxis(yaw, Vec3::Up()));
		SceneSystem::EndFrame();
		const Vec3 direction = -sunObject->TransformDirection(Vec3::Forward()).Normalized();
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(RenderStage(stage, pixels, size));
		const f32 noL = Vec3::Dot(Vec3(0.0f, 0.0f, 1.0f), direction);
		const f32 band = noL >= 0.5f ? 1.0f : 0.25f;
		++bands[band == 1.0f ? 0 : 1];
		const Vec3 expectedLinear = Vec3(VIEW_AMBIENT, VIEW_AMBIENT, VIEW_AMBIENT) * baseColor + baseColor * stage.lightColor * band;
		const Vec3 expected = ExpectedTexel(expectedLinear);
		const Vec3 measured = CenterTexel(pixels, size);
		EXPECT_NEAR(measured.x, expected.x, 1.5f) << yaw;
		EXPECT_NEAR(measured.y, expected.y, 1.5f) << yaw;
		EXPECT_NEAR(measured.z, expected.z, 1.5f) << yaw;
	}
	EXPECT_EQ(bands[0], 1u);
	EXPECT_EQ(bands[1], 1u);
	TearDownStage(stage);
}

TEST_F(NodeShaderLightingFixture, KeywordDrawsBranchMaterialEnables) {
	GraphBuilder b = LightingStacks("unlit");
	Keyword(b, 40, 41, "_MOSS", false);
	b.Node(42, "Input/Vector3");
	DocumentBuilder::Constant(b.document.nodes.back(), "X", "1");
	b.Node(43, "Input/Vector3");
	DocumentBuilder::Constant(b.document.nodes.back(), "Y", "1");
	b.Connect(42, "Out", 41, "On");
	b.Connect(43, "Out", 41, "Off");
	b.Connect(41, "Out", 11, "Value");
	ShaderAsset* shader = AddGraph("Moss.slshader", b.document);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* on = LoadMaterial("MossOn.mat", shader->assetId, "\t\"keywords\": [\"_MOSS\"]");
	MaterialAsset* off = LoadMaterial("MossOff.mat", shader->assetId, "");
	ASSERT_NE(on, nullptr);
	ASSERT_NE(off, nullptr);
	auto render = [&](MaterialAsset* material) {
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
	const Vec3 enabled = render(on);
	const Vec3 disabled = render(off);
	EXPECT_GT(enabled.x, 250.0f);
	EXPECT_LT(enabled.y, 5.0f);
	EXPECT_GT(disabled.y, 250.0f);
	EXPECT_LT(disabled.x, 5.0f);
}
