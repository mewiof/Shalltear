#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"
#include "TestImages.h"

#include "GraphSurface.h"

#include "Shalltear/Core/Serialization/JSON.h"

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;
using namespace ShadingTest;

namespace {

	class NodeShaderTextureKindsFixture : public MaterialReadbackFixture {
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

		Guid ImportAs(const char* file, const JSONValue& settings) {
			const Path path(str("project://") + file);
			Asset* minted = AssetDatabase::LoadAsset(path);
			if (!minted) {
				return {};
			}
			const Guid guid = minted->assetId;
			AssetDatabase::UnloadAsset(minted);
			return AssetDatabase::WriteImporterSettings(path, settings) && AssetDatabase::ReimportAsset(path) ? guid : Guid{};
		}

		Guid SkyCube() {
			u8 image[8 * 4 * 4];
			for (u32 i = 0; i < 8 * 4; ++i) {
				const bool upper = i / 8 < 2;
				const u8 texel[4] = {static_cast<u8>(upper ? 255 : 0), 0, static_cast<u8>(upper ? 0 : 255), 255};
				std::memcpy(&image[i * 4], texel, 4);
			}
			TestImages::WriteTga(_root / "Sky.tga", 8, 4, image);
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("shape", JSONValue::MakeString("cube"));
			settings.AddMember("convolveIBL", JSONValue::MakeBool(false));
			return ImportAs("Sky.tga", settings);
		}

		static GraphBuilder Unlit() {
			GraphBuilder b;
			b.Graph("target", "\"unlit\"");
			GraphContext& fragment = b.Context(10, "Context/Fragment");
			b.Block(fragment, 11, "Block/BaseColor");
			b.Block(fragment, 13, "Block/Alpha");
			return b;
		}

		Vec3 Render(MaterialAsset* material) {
			ShadingStage stage;
			EXPECT_TRUE(BuildStage(material, stage));
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			EXPECT_TRUE(RenderStage(stage, pixels, size));
			const Vec3 texel = CenterTexel(pixels, size);
			TearDownStage(stage);
			return texel;
		}
	};

} // anonymous namespace

TEST_F(NodeShaderTextureKindsFixture, LayerAndSliceReadBack) {
	TestImages::WriteFlipbook(_root / "Layers.tga");
	TestImages::WriteFlipbook(_root / "Volume.tga");
	auto flipbook = [](const char* shape) {
		JSONValue settings = JSONValue::MakeObject();
		settings.AddMember("shape", JSONValue::MakeString(shape));
		settings.AddMember("columns", JSONValue::MakeNumber(2.0));
		settings.AddMember("rows", JSONValue::MakeNumber(2.0));
		return settings;
	};
	const Guid layers = ImportAs("Layers.tga", flipbook("2darray"));
	const Guid volume = ImportAs("Volume.tga", flipbook("3d"));
	ASSERT_TRUE(layers.IsValid() && volume.IsValid());

	GraphBuilder b = Unlit();
	b.Property(20, 21, "layers", "Texture2DArray");
	b.Property(22, 23, "volume", "Texture3D");
	b.Node(30, "Input/SampleTexture2DArray");
	DocumentBuilder::Constant(b.document.nodes.back(), "Index", "2");
	b.Connect(21, "Out", 30, "Texture Array");
	b.Node(31, "Input/SampleTexture3D");
	DocumentBuilder::Constant(b.document.nodes.back(), "UV", "[0.5,0.5,0.625]");
	b.Connect(23, "Out", 31, "Texture");
	b.Node(32, "Channel/Combine");
	b.Connect(30, "R", 32, "R");
	b.Connect(30, "G", 32, "G");
	b.Connect(31, "B", 32, "B");
	b.Connect(32, "RGB", 11, "Value");
	ShaderAsset* shader = AddGraph("Cells.slshader", b.document);
	ASSERT_NE(shader, nullptr);
	EXPECT_EQ(shader->GetTextureSlotDimension("layers"), RHI::TextureDimension::Texture2DArray);
	EXPECT_EQ(shader->GetTextureSlotDimension("volume"), RHI::TextureDimension::Texture3D);

	const str textures = "\t\"textures\": { \"layers\": \"" + layers.ToString() + "\", \"volume\": \"" + volume.ToString() + "\" }";
	MaterialAsset* material = LoadMaterial("Cells.mat", shader->assetId, textures);
	ASSERT_NE(material, nullptr);
	const Vec3 measured = Render(material);
	EXPECT_NEAR(measured.x, TestImages::FLIPBOOK_CELLS[2][0], 2.0f);
	EXPECT_NEAR(measured.y, TestImages::FLIPBOOK_CELLS[2][1], 2.0f);
	EXPECT_NEAR(measured.z, TestImages::FLIPBOOK_CELLS[2][2], 2.0f);
}

TEST_F(NodeShaderTextureKindsFixture, SampleCubemapReadsFaceDirectionNames) {
	const Guid sky = SkyCube();
	ASSERT_TRUE(sky.IsValid());
	const str textures = "\t\"textures\": { \"sky\": \"" + sky.ToString() + "\" }";
	struct Row {
		const char* name;
		const char* y;
		Vec3 expected;
	};
	const Row rows[] = {{"Up", "1", Vec3(255.0f, 0.0f, 0.0f)}, {"Down", "-1", Vec3(0.0f, 0.0f, 255.0f)}};
	for (const Row& row : rows) {
		GraphBuilder b = Unlit();
		b.Property(20, 21, "sky", "Cubemap");
		b.Node(30, "Input/SampleCubemap");
		b.Node(31, "Input/Vector3");
		DocumentBuilder::Constant(b.document.nodes.back(), "Y", row.y);
		b.Connect(31, "Out", 30, "Dir");
		b.Connect(21, "Out", 30, "Cube");
		b.Connect(30, "Out", 11, "Value");
		ShaderAsset* shader = AddGraph((str(row.name) + ".slshader").c_str(), b.document);
		ASSERT_NE(shader, nullptr) << row.name;
		EXPECT_EQ(shader->GetTextureSlotDimension("sky"), RHI::TextureDimension::Cube);
		MaterialAsset* material = LoadMaterial((str(row.name) + ".mat").c_str(), shader->assetId, textures);
		ASSERT_NE(material, nullptr);
		const Vec3 measured = Render(material);
		EXPECT_NEAR(measured.x, row.expected.x, 3.0f) << row.name;
		EXPECT_NEAR(measured.z, row.expected.z, 3.0f) << row.name;
	}
}

TEST_F(NodeShaderTextureKindsFixture, SampleReflectedCubemapReadsAlongTheReflection) {
	const Guid sky = SkyCube();
	ASSERT_TRUE(sky.IsValid());
	const str textures = "\t\"textures\": { \"sky\": \"" + sky.ToString() + "\" }";
	struct Row {
		const char* name;
		const char* normalY;
		Vec3 expected;
	};
	const Row rows[] = {{"TiltedUp", "0.70710678", Vec3(255.0f, 0.0f, 0.0f)}, {"TiltedDown", "-0.70710678", Vec3(0.0f, 0.0f, 255.0f)}};
	for (const Row& row : rows) {
		GraphBuilder b = Unlit();
		b.Property(20, 21, "sky", "Cubemap");
		b.Node(30, "Input/SampleReflectedCubemap");
		b.Node(31, "Input/Vector3");
		DocumentBuilder::Constant(b.document.nodes.back(), "Z", "1");
		b.Node(32, "Input/Vector3");
		DocumentBuilder::Constant(b.document.nodes.back(), "Y", row.normalY);
		DocumentBuilder::Constant(b.document.nodes.back(), "Z", "0.70710678");
		b.Connect(31, "Out", 30, "ViewDir");
		b.Connect(32, "Out", 30, "Normal");
		b.Connect(21, "Out", 30, "Cube");
		b.Connect(30, "Out", 11, "Value");
		ShaderAsset* shader = AddGraph((str(row.name) + ".slshader").c_str(), b.document);
		ASSERT_NE(shader, nullptr) << row.name;
		MaterialAsset* material = LoadMaterial((str(row.name) + ".mat").c_str(), shader->assetId, textures);
		ASSERT_NE(material, nullptr);
		const Vec3 measured = Render(material);
		EXPECT_NEAR(measured.x, row.expected.x, 3.0f) << row.name;
		EXPECT_NEAR(measured.z, row.expected.z, 3.0f) << row.name;
	}
}

TEST(NodeShaderTextureKindsTest, UnwiredSampleIsRefusedNamingKind) {
	struct Row {
		const char* def;
		const char* output;
		const char* kind;
	};
	const Row rows[] = {{"Input/SampleTexture2DArray", "RGBA", "Texture 2D Array"},
						{"Input/SampleTexture3D", "RGBA", "Texture 3D"},
						{"Input/SampleCubemap", "Out", "Cubemap"},
						{"Input/SampleReflectedCubemap", "Out", "Cubemap"}};
	for (const Row& row : rows) {
		GraphBuilder b;
		b.Node(1, row.def);
		Compiled compiled = Compile(b.document, {{1, row.output}}, GraphStageFragment, TestBindings());
		EXPECT_FALSE(compiled.ok) << row.def;
		EXPECT_TRUE(compiled.Names(1, row.kind)) << row.def << ": " << compiled.Report();
	}
}

TEST(NodeShaderTextureKindsTest, KindsDeclaredOnly2DFallback) {
	GraphBuilder b;
	b.Graph("target", "\"unlit\"");
	GraphContext& fragment = b.Context(10, "Context/Fragment");
	b.Block(fragment, 11, "Block/BaseColor");
	b.Blackboard(20, "Layers", "layers", "Texture2DArray");
	b.Blackboard(21, "Volume", "volume", "Texture3D");
	b.Blackboard(22, "Sky", "sky", "Cubemap");
	b.Blackboard(23, "Ramp", "ramp", "Texture2D").settings.push_back({str("mode"), str("\"gray\"")});
	str text;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphSurface::Generate(b.document, "project://Assets/Kinds.slshader", text, diagnostics)) << text;
	EXPECT_NE(text.find("[[vk::binding(1, 1)]] Texture2DArray layers;\n[[vk::binding(2, 1)]] SamplerState sltSampler_layers;"), str::npos) << text;
	EXPECT_NE(text.find("[[vk::binding(3, 1)]] Texture3D volume;"), str::npos) << text;
	EXPECT_NE(text.find("[[vk::binding(5, 1)]] TextureCube sky;"), str::npos) << text;
	EXPECT_NE(text.find("#pragma shalltear_default ramp gray\n"), str::npos);
	for (const char* reference : {"layers", "volume", "sky"}) {
		EXPECT_EQ(text.find(str("#pragma shalltear_default ") + reference), str::npos) << reference;
	}
}
