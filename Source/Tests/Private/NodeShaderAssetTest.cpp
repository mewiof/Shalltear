#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Editor/CookOps.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Time.h"

#include "Asset/PakReader.h"

#include <cstring>
#include <format>
#include <fstream>
#include <functional>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	using namespace ShadingTest;

	using Pixel = std::array<u8, 3>;
	using PixelFn = std::function<Pixel(u32 x, u32 y)>;

	void WriteTGA(const std::filesystem::path& path, u16 width, u16 height, const PixelFn& fill) {
		u8 header[18] = {};
		header[2] = 2;
		header[12] = static_cast<u8>(width & 0xFF);
		header[13] = static_cast<u8>(width >> 8);
		header[14] = static_cast<u8>(height & 0xFF);
		header[15] = static_cast<u8>(height >> 8);
		header[16] = 24;
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(header), sizeof(header));
		for (u32 y = 0; y < height; ++y) {
			for (u32 x = 0; x < width; ++x) {
				Pixel rgb = fill(x, y);
				const u8 bgr[3] = {rgb[2], rgb[1], rgb[0]};
				file.write(reinterpret_cast<const char*>(bgr), 3);
			}
		}
	}

	struct StackBuilder : DocumentBuilder {
		GraphContext* fragment = nullptr;

		explicit StackBuilder(std::string_view target) {
			document.settings.push_back({str("target"), "\"" + str(target) + "\""});
			GraphContext context;
			context.node.id = 1;
			context.node.def = "Context/Fragment";
			document.contexts.push_back(std::move(context));
			fragment = &document.contexts.back();
		}

		u32 Block(u32 id, std::string_view def) {
			GraphNode block;
			block.id = id;
			block.def.assign(def);
			fragment->blocks.push_back(std::move(block));
			document.nextId = std::max(document.nextId, id + 1);
			return id;
		}

		u32 Read(u32 nodeId, std::string_view reference, std::string_view type, std::string_view mode = {}) {
			Property(nodeId + 200, nodeId, reference, type);
			if (!mode.empty()) {
				document.properties.back().settings.push_back({str("mode"), "\"" + str(mode) + "\""});
			}
			return nodeId;
		}

		u32 Op(u32 id, std::string_view def) {
			Node(id, def);
			return id;
		}
	};

	class NodeShaderAssetFixture : public MaterialReadbackFixture {
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

		Guid AddTexture(const char* fileName, u16 width, u16 height, const PixelFn& fill) {
			WriteTGA(_root / fileName, width, height, fill);
			Asset* texture = AssetDatabase::LoadAsset(Path(str("project://") + fileName));
			return texture ? texture->assetId : Guid{};
		}

		ShaderAsset* AddGraph(const char* fileName, const GraphDocument& document) {
			WriteProjectFile(fileName, GraphDocumentOps::Write(document));
			return static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
		}

		bool Draw(ShadingStage& stage, MaterialAsset* material, Vector<u8>& pixels, Vec2I& size) {
			stage.quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return RenderStage(stage, pixels, size);
		}

		str LitMaterialBody(f32 normalScale) {
			auto ramp = [](u32 x, u32 y) { return Pixel{static_cast<u8>(60 + x * 50), static_cast<u8>(200 - y * 40), static_cast<u8>(90 + (x ^ y) * 30)}; };
			Guid base = AddTexture("Base.tga", 4, 4, ramp);
			Guid mr = AddTexture("MetalRough.tga", 4, 4, [](u32 x, u32 y) { return Pixel{0, static_cast<u8>(100 + x * 40), static_cast<u8>(60 + y * 50)}; });
			Guid normal = AddTexture("Normal.tga", 4, 4, [](u32 x, u32) { return Pixel{static_cast<u8>(90 + x * 25), 150, 230}; });
			Guid occlusion = AddTexture("Occlusion.tga", 4, 4, [](u32 x, u32 y) { return Pixel{static_cast<u8>(120 + (x + y) * 15), 0, 0}; });
			Guid emissive = AddTexture("Emissive.tga", 4, 4, [](u32 x, u32) { return Pixel{static_cast<u8>(x * 60), 40, 10}; });
			if (!base.IsValid() || !mr.IsValid() || !normal.IsValid() || !occlusion.IsValid() || !emissive.IsValid()) {
				return {};
			}
			return std::format("\t\"colors\": {{ \"baseColor\": [0.9, 0.8, 0.7, 1] }},\n"
							   "\t\"floats\": {{ \"metallic\": 0.3, \"roughness\": 0.6, \"reflectance\": 0.5, \"normalScale\": {}, "
							   "\"occlusionStrength\": 0.7 }},\n"
							   "\t\"vec3s\": {{ \"emissive\": [0.05, 0.02, 0.0] }},\n"
							   "\t\"textures\": {{ \"baseColorTexture\": \"{}\", \"metallicRoughnessTexture\": \"{}\", \"normalTexture\": \"{}\", "
							   "\"occlusionTexture\": \"{}\", \"emissiveTexture\": \"{}\" }}",
							   normalScale, base.ToString(), mr.ToString(), normal.ToString(), occlusion.ToString(), emissive.ToString());
		}

		str _why;
	};

	GraphDocument LitAsAGraph(bool normalType = false) {
		StackBuilder b("lit");
		u32 baseColor = b.Read(101, "baseColor", "Color");
		u32 metallic = b.Read(102, "metallic", "Float");
		u32 roughness = b.Read(103, "roughness", "Float");
		u32 reflectance = b.Read(104, "reflectance", "Float");
		u32 normalScale = b.Read(105, "normalScale", "Float");
		u32 emissive = b.Read(106, "emissive", "Vector3");
		u32 occlusionStrength = b.Read(107, "occlusionStrength", "Float");
		u32 baseMap = b.Read(111, "baseColorTexture", "Texture2D");
		u32 mrMap = b.Read(112, "metallicRoughnessTexture", "Texture2D");
		u32 normalMap = b.Read(113, "normalTexture", "Texture2D", "normal");
		u32 occlusionMap = b.Read(114, "occlusionTexture", "Texture2D");
		u32 emissiveMap = b.Read(115, "emissiveTexture", "Texture2D");

		auto sample = [&b](u32 id, u32 map) {
			b.Op(id, "Input/SampleTexture2D");
			b.Connect(map, "Out", id, "Texture");
			return id;
		};
		u32 base = sample(21, baseMap);
		u32 mr = sample(22, mrMap);
		u32 normal = sample(23, normalMap);
		u32 occlusion = sample(24, occlusionMap);
		u32 emission = sample(25, emissiveMap);
		u32 color = b.Op(26, "Input/VertexColor");

		b.Op(31, "Math/Multiply");
		b.Connect(base, "RGBA", 31, "A");
		b.Connect(baseColor, "Out", 31, "B");
		b.Op(32, "Math/Multiply");
		b.Connect(31, "Out", 32, "A");
		b.Connect(color, "Out", 32, "B");
		b.Op(33, "Channel/Split");
		b.Connect(32, "Out", 33, "In");

		b.Op(34, "Math/Multiply");
		b.Connect(roughness, "Out", 34, "A");
		b.Connect(mr, "G", 34, "B");
		b.Op(35, "Math/Saturate");
		b.Connect(34, "Out", 35, "In");
		b.Op(36, "Math/Multiply");
		b.Connect(metallic, "Out", 36, "A");
		b.Connect(mr, "B", 36, "B");
		b.Op(37, "Math/Saturate");
		b.Connect(36, "Out", 37, "In");

		b.Op(40, "Channel/Swizzle");
		DocumentBuilder::Setting(b.document.nodes.back(), "Mask", "\"xy\"");
		b.Connect(normal, "RGBA", 40, "In");
		b.Op(41, "Math/Multiply");
		b.Connect(40, "Out", 41, "A");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[2]");
		b.Op(42, "Math/Subtract");
		b.Connect(41, "Out", 42, "A");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[1]");
		b.Op(43, "Math/Multiply");
		b.Connect(42, "Out", 43, "A");
		b.Connect(normalScale, "Out", 43, "B");
		b.Op(44, "Math/DotProduct");
		b.Connect(43, "Out", 44, "A");
		b.Connect(43, "Out", 44, "B");
		b.Op(45, "Math/OneMinus");
		b.Connect(44, "Out", 45, "In");
		b.Op(46, "Math/Saturate");
		b.Connect(45, "Out", 46, "In");
		b.Op(47, "Math/SquareRoot");
		b.Connect(46, "Out", 47, "In");
		b.Op(48, "Channel/Split");
		b.Connect(43, "Out", 48, "In");
		b.Op(49, "Channel/Combine");
		b.Connect(48, "R", 49, "R");
		b.Connect(48, "G", 49, "G");
		b.Connect(47, "Out", 49, "B");

		b.Op(50, "Math/Lerp");
		DocumentBuilder::Constant(b.document.nodes.back(), "A", "[1]");
		b.Connect(occlusion, "R", 50, "B");
		b.Connect(occlusionStrength, "Out", 50, "T");
		b.Op(51, "Math/Multiply");
		b.Connect(emission, "RGBA", 51, "A");
		b.Connect(emissive, "Out", 51, "B");

		b.Connect(32, "Out", b.Block(2, "Block/BaseColor"), "Value");
		b.Connect(33, "A", b.Block(3, "Block/Alpha"), "Value");
		b.Connect(37, "Out", b.Block(4, "Block/Metallic"), "Value");
		b.Connect(35, "Out", b.Block(5, "Block/Roughness"), "Value");
		b.Connect(reflectance, "Out", b.Block(6, "Block/Reflectance"), "Value");
		if (normalType) {
			DocumentBuilder::Setting(*GraphDocumentOps::FindNode(b.document, normal), "Type", "\"Normal\"");
			b.Connect(normal, "RGBA", b.Block(7, "Block/NormalTS"), "Value");
		} else {
			b.Connect(49, "RGB", b.Block(7, "Block/NormalTS"), "Value");
		}
		b.Connect(50, "Out", b.Block(8, "Block/Occlusion"), "Value");
		b.Connect(51, "Out", b.Block(9, "Block/Emission"), "Value");
		return b.document;
	}

	struct Difference {
		u32 worst = 0;
		u32 lit = 0;
	};

	Difference Compare(const Vector<u8>& a, const Vector<u8>& b) {
		Difference out;
		for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
			if (i % 4 == 3) {
				continue;
			}
			out.worst = std::max(out.worst, static_cast<u32>(std::abs(static_cast<i32>(a[i]) - static_cast<i32>(b[i]))));
			out.lit += a[i] > 8 ? 1u : 0u;
		}
		return out;
	}

} // anonymous namespace

TEST_F(NodeShaderAssetFixture, AGraphThatRebuildsLitMatchesLit) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const str body = LitMaterialBody(0.8f);
	ASSERT_FALSE(body.empty());

	ShaderAsset* graph = AddGraph("LitGraph.slshader", LitAsAGraph());
	ASSERT_NE(graph, nullptr);
	EXPECT_EQ(graph->integrator, ShaderTemplate::Lit);
	Asset* lit = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(lit, nullptr);

	MaterialAsset* asLit = LoadMaterial("AsLit.mat", lit->assetId, body);
	MaterialAsset* asGraph = LoadMaterial("AsGraph.mat", graph->assetId, body);
	ASSERT_NE(asLit, nullptr);
	ASSERT_NE(asGraph, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(asLit, stage));
	Vector<u8> litPixels;
	Vector<u8> graphPixels;
	Vec2I size{};
	ASSERT_TRUE(Draw(stage, asLit, litPixels, size));
	ASSERT_TRUE(Draw(stage, asGraph, graphPixels, size));

	Difference difference = Compare(litPixels, graphPixels);
	ASSERT_GT(difference.lit, 600u);
	EXPECT_LE(difference.worst, 1u);

	GraphDocument wrong = LitAsAGraph();
	for (GraphInput& input : GraphDocumentOps::FindNode(wrong, 36)->inputs) {
		if (input.port == "B") {
			input.fromPort = "R";
		}
	}
	ShaderAsset* broken = AddGraph("WrongGraph.slshader", wrong);
	ASSERT_NE(broken, nullptr);
	MaterialAsset* asWrong = LoadMaterial("AsWrong.mat", broken->assetId, body);
	Vector<u8> wrongPixels;
	ASSERT_TRUE(Draw(stage, asWrong, wrongPixels, size));
	const u32 wrongWorst = Compare(litPixels, wrongPixels).worst;
	EXPECT_GT(wrongWorst, 4u);
	std::printf("[graph vs Lit] worst channel difference %u over %u lit channels. Wrong-channel graph's is %u\n", difference.worst,
				difference.lit, wrongWorst);

	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, NormalTypeUnpacksAsContractDoes) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const str body = LitMaterialBody(1.0f);
	ASSERT_FALSE(body.empty());
	ShaderAsset* graph = AddGraph("NormalType.slshader", LitAsAGraph(true));
	ASSERT_NE(graph, nullptr);
	Asset* lit = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(lit, nullptr);
	MaterialAsset* asLit = LoadMaterial("AsLit.mat", lit->assetId, body);
	MaterialAsset* asGraph = LoadMaterial("AsGraph.mat", graph->assetId, body);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(asLit, stage));
	Vector<u8> litPixels;
	Vector<u8> graphPixels;
	Vec2I size{};
	ASSERT_TRUE(Draw(stage, asLit, litPixels, size));
	ASSERT_TRUE(Draw(stage, asGraph, graphPixels, size));
	Difference difference = Compare(litPixels, graphPixels);
	ASSERT_GT(difference.lit, 600u);
	EXPECT_LE(difference.worst, 1u);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, NonGraphDocumentIsNotImported) {
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));
	WriteProjectFile("Untyped.slshader", "{\n\"version\": 3\n}\n");
	Asset* imported = AssetDatabase::LoadAsset(Path("project://Untyped.slshader"));
	LogManager::Terminate();
	str captured = sink->Joined();
	sink.reset();
	JobSystem::Terminate();
	EXPECT_EQ(imported, nullptr);
	EXPECT_NE(captured.find("Untyped.slshader' isn't a graph document: the document has no _type"), str::npos) << captured;
}

TEST_F(NodeShaderAssetFixture, AnUnlitGraphDrawsItsBaseColor) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	StackBuilder b("unlit");
	u32 tint = b.Read(101, "tint", "Color");
	b.Connect(tint, "Out", b.Block(2, "Block/BaseColor"), "Value");
	b.Block(3, "Block/Alpha");
	ShaderAsset* graph = AddGraph("Unlit.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	EXPECT_EQ(graph->integrator, ShaderTemplate::Unlit);

	MaterialAsset* material = LoadMaterial("Unlit.mat", graph->assetId, "\t\"colors\": { \"tint\": [0.2, 0.6, 0.4, 1] }");
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	Vec3 texel = CenterTexel(pixels, size);
	Vec3 expected = ExpectedTexel(Vec3(0.2f, 0.6f, 0.4f));
	EXPECT_NEAR(texel.x, expected.x, 1.5f);
	EXPECT_NEAR(texel.y, expected.y, 1.5f);
	EXPECT_NEAR(texel.z, expected.z, 1.5f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, TextureNodesReadWhatMaterialBound) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	Guid wide = AddTexture("Wide.tga", 4, 2, [](u32, u32) { return Pixel{255, 255, 255}; });
	ASSERT_TRUE(wide.IsValid());

	StackBuilder sizes("unlit");
	u32 map = sizes.Read(101, "map", "Texture2D");
	sizes.Op(20, "Input/TextureSize");
	sizes.Connect(map, "Out", 20, "Texture");
	sizes.Op(21, "Math/Divide");
	sizes.Connect(20, "Width", 21, "A");
	DocumentBuilder::Constant(sizes.document.nodes.back(), "B", "[8]");
	sizes.Op(22, "Channel/Combine");
	sizes.Connect(20, "Texel Width", 22, "R");
	sizes.Connect(20, "Texel Height", 22, "G");
	sizes.Connect(21, "Out", 22, "B");
	sizes.Connect(22, "RGB", sizes.Block(2, "Block/BaseColor"), "Value");
	ShaderAsset* sizeGraph = AddGraph("Sizes.slshader", sizes.document);
	ASSERT_NE(sizeGraph, nullptr);
	MaterialAsset* sizeMaterial = LoadMaterial("Sizes.mat", sizeGraph->assetId, "\t\"textures\": { \"map\": \"" + wide.ToString() + "\" }");

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(sizeMaterial, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	Vec3 texel = CenterTexel(pixels, size);
	Vec3 expected = ExpectedTexel(Vec3(0.25f, 0.5f, 0.5f));
	EXPECT_NEAR(texel.x, expected.x, 1.5f);
	EXPECT_NEAR(texel.y, expected.y, 1.5f);
	EXPECT_NEAR(texel.z, expected.z, 1.5f);

	Guid solid = AddTexture("Solid.tga", 4, 4, [](u32, u32) { return Pixel{200, 100, 50}; });
	StackBuilder lod("unlit");
	u32 solidMap = lod.Read(101, "map", "Texture2D");
	lod.Op(20, "Input/SampleTexture2DLOD");
	lod.Connect(solidMap, "Out", 20, "Texture");
	lod.Connect(20, "RGBA", lod.Block(2, "Block/BaseColor"), "Value");
	ShaderAsset* lodGraph = AddGraph("Lod.slshader", lod.document);
	ASSERT_NE(lodGraph, nullptr);
	MaterialAsset* lodMaterial = LoadMaterial("Lod.mat", lodGraph->assetId, "\t\"textures\": { \"map\": \"" + solid.ToString() + "\" }");
	ASSERT_TRUE(Draw(stage, lodMaterial, pixels, size));
	texel = CenterTexel(pixels, size);
	EXPECT_NEAR(texel.x, 200.0f, 2.0f);
	EXPECT_NEAR(texel.y, 100.0f, 2.0f);
	EXPECT_NEAR(texel.z, 50.0f, 2.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, UVAndVertexColorReadTheMesh) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	StackBuilder b("unlit");
	b.Op(20, "Input/UV");
	b.Op(21, "Input/VertexColor");
	b.Op(22, "Channel/Split");
	b.Connect(20, "Out", 22, "In");
	b.Op(23, "Channel/Split");
	b.Connect(21, "Out", 23, "In");
	b.Op(24, "Math/Multiply");
	b.Connect(23, "B", 24, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5]");
	b.Op(25, "Channel/Combine");
	b.Connect(22, "R", 25, "R");
	b.Connect(22, "G", 25, "G");
	b.Connect(24, "Out", 25, "B");
	b.Connect(25, "RGB", b.Block(2, "Block/BaseColor"), "Value");
	ShaderAsset* graph = AddGraph("Geometry.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	MaterialAsset* material = LoadMaterial("Geometry.mat", graph->assetId, {});

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	Vec3 texel = CenterTexel(pixels, size);
	Vec3 expected = ExpectedTexel(Vec3(0.5f, 0.5f, 0.5f));
	EXPECT_NEAR(texel.x, expected.x, 8.0f);
	EXPECT_NEAR(texel.y, expected.y, 8.0f);
	EXPECT_NEAR(texel.z, expected.z, 1.5f);
	auto at = [&pixels, &size](i32 x, i32 y) {
		const u8* p = &pixels[(static_cast<size_t>(y) * size.x + x) * 4];
		return Vec3(p[0], p[1], p[2]);
	};
	const i32 cx = size.x / 2;
	const i32 cy = size.y / 2;
	EXPECT_GT(std::abs(at(cx + 6, cy).x - at(cx - 6, cy).x), 20.0f);
	EXPECT_LT(std::abs(at(cx + 6, cy).y - at(cx - 6, cy).y), 3.0f);
	EXPECT_GT(std::abs(at(cx, cy + 6).y - at(cx, cy - 6).y), 20.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, IsFrontFaceReadsFaceTargetDraws) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	StackBuilder b("unlit");
	b.document.settings.push_back({str("cull"), "\"off\""});
	u32 front = b.Read(101, "front", "Color");
	u32 back = b.Read(102, "back", "Color");
	b.Op(20, "Utility/IsFrontFace");
	b.Op(21, "Utility/Branch");
	b.Connect(20, "Out", 21, "Predicate");
	b.Connect(front, "Out", 21, "True");
	b.Connect(back, "Out", 21, "False");
	b.Connect(21, "Out", b.Block(2, "Block/BaseColor"), "Value");
	ShaderAsset* graph = AddGraph("Faces.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	MaterialAsset* material = LoadMaterial("Faces.mat", graph->assetId, "\t\"colors\": { \"front\": [1, 0, 0, 1], \"back\": [0, 1, 0, 1] }");

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 red = ExpectedTexel(Vec3(1.0f, 0.0f, 0.0f));
	Vec3 texel = CenterTexel(pixels, size);
	EXPECT_NEAR(texel.x, red.x, 1.5f);
	EXPECT_NEAR(texel.y, red.y, 1.5f);

	stage.quad->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3(0.0f, 1.0f, 0.0f)));
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 green = ExpectedTexel(Vec3(0.0f, 1.0f, 0.0f));
	texel = CenterTexel(pixels, size);
	EXPECT_NEAR(texel.x, green.x, 1.5f);
	EXPECT_NEAR(texel.y, green.y, 1.5f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, EditingTheGraphRedrawsItsMaterials) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	auto coloured = [](std::string_view rgba) {
		StackBuilder b("unlit");
		b.Op(20, "Input/Color");
		DocumentBuilder::Setting(b.document.nodes.back(), "Color", rgba);
		b.Connect(20, "Out", b.Block(2, "Block/BaseColor"), "Value");
		return b.document;
	};
	ShaderAsset* graph = AddGraph("Live.slshader", coloured("[1,0,0,1]"));
	ASSERT_NE(graph, nullptr);
	MaterialAsset* material = LoadMaterial("Live.mat", graph->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_GT(CenterTexel(pixels, size).x, 200.0f);

	WriteProjectFile("Live.slshader", GraphDocumentOps::Write(coloured("[0,1,0,1]")));
	RenderSystem::NotifyShaderFileChanged((_root / "Live.slshader").generic_string().c_str());
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	Vec3 after = CenterTexel(pixels, size);
	EXPECT_GT(after.y, 200.0f);
	EXPECT_LT(after.x, 20.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, FailingGraphIsNoShader) {
	StackBuilder b("lit");
	b.Op(20, "Input/SampleTexture2D");
	b.Connect(20, "RGBA", b.Block(2, "Block/BaseColor"), "Value");
	WriteProjectFile("Broken.slshader", GraphDocumentOps::Write(b.document));
	EXPECT_EQ(AssetDatabase::LoadAsset(Path("project://Broken.slshader")), nullptr);
	EXPECT_FALSE(std::filesystem::exists(_root / "Library" / "NodeShader"));
}

TEST_F(NodeShaderAssetFixture, GraphCooksAsOrdinaryShaderRecord) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	StackBuilder b("lit");
	u32 tint = b.Read(101, "tint", "Color");
	b.Connect(tint, "Out", b.Block(2, "Block/BaseColor"), "Value");
	ShaderAsset* graph = AddGraph("Cooked.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	const Guid shaderGuid = graph->assetId;
	MaterialAsset* material = LoadMaterial("Cooked.mat", shaderGuid, "\t\"colors\": { \"tint\": [0.5, 0.25, 1, 1] }");
	ASSERT_NE(material, nullptr);

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(material->assetId);
	CookOps::CookClosure closure;
	str error;
	ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;
	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	CookOps::PakWriteDesc desc;
	ASSERT_TRUE(CookOps::CookPackage(closure, desc, package, error)) << error;

	std::filesystem::path file = _root / "Graph.slpak";
	Buffer bytes(package.size(), MemTag::Editor);
	std::memcpy(bytes.data, package.data(), package.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(file.generic_string()), bytes));
	PakReader reader;
	ASSERT_TRUE(reader.Mount(Path(file.generic_string()), error)) << error;
	u32 index = reader.FindAsset(shaderGuid);
	ASSERT_NE(PAK_INVALID_INDEX, index);
	const PakAssetEntry* entry = reader.GetAsset(index);
	ASSERT_NE(nullptr, entry);
	Buffer payloadBytes;
	ASSERT_TRUE(reader.ReadRegion(entry->regionIndex, payloadBytes, error)) << error;
	CookReader descriptor = reader.GetDescriptor(index);
	CookReader payload(payloadBytes.data, payloadBytes.size);
	ShaderAsset shipped;
	ASSERT_TRUE(shipped.LoadCooked(descriptor, payload));
	EXPECT_EQ(shipped.integrator, ShaderTemplate::Lit);
	ASSERT_FALSE(shipped.variants.empty());
	EXPECT_FALSE(shipped.variants[SHADER_BASE_VARIANT].pixelSpirv.empty());
	ASSERT_NE(shipped.GetMaterialParamLayout(), nullptr);
	EXPECT_EQ(shipped.GetMaterialParamLayout()->members[0].name, "tint");
}

namespace {

	GraphDocument SpaceGraph(std::string_view node, std::string_view space, bool viaTransform, std::string_view type = "Position",
							 f32 scale = 0.5f, std::array<f32, 3> offset = {0.5f, 0.5f, 0.5f}) {
		StackBuilder b("unlit");
		b.document.settings.push_back({str("cull"), "\"off\""});
		GraphNode& read = b.Node(10, node);
		DocumentBuilder::Setting(read, "Space", viaTransform ? "\"World\"" : "\"" + str(space) + "\"");
		u32 source = 10;
		if (viaTransform) {
			GraphNode& transform = b.Node(11, "Math/Transform");
			DocumentBuilder::Setting(transform, "From", "\"World\"");
			DocumentBuilder::Setting(transform, "To", "\"" + str(space) + "\"");
			DocumentBuilder::Setting(transform, "Type", "\"" + str(type) + "\"");
			b.Connect(10, "Out", 11, "In");
			source = 11;
		}
		GraphNode& multiply = b.Node(12, "Math/Multiply");
		DocumentBuilder::Constant(multiply, "B", std::format("[{},{},{}]", scale, scale, scale));
		b.Connect(source, "Out", 12, "A");
		GraphNode& add = b.Node(13, "Math/Add");
		DocumentBuilder::Constant(add, "B", std::format("[{},{},{}]", offset[0], offset[1], offset[2]));
		b.Connect(12, "Out", 13, "A");
		b.Connect(13, "Out", b.Block(2, "Block/BaseColor"), "Value");
		b.Block(3, "Block/Alpha");
		return b.document;
	}

	struct Agreement {
		u32 covered = 0;
		f32 worst = 0.0f;
		f32 spread = 0.0f;
	};

	Agreement Agree(const Vector<u8>& a, const Vector<u8>& b) {
		Agreement out;
		f32 low = 255.0f;
		f32 high = 0.0f;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			if (a[i] + a[i + 1] + a[i + 2] == 0 && b[i] + b[i + 1] + b[i + 2] == 0) {
				continue;
			}
			++out.covered;
			for (u32 c = 0; c < 3; ++c) {
				out.worst = std::max(out.worst, std::fabs(static_cast<f32>(a[i + c]) - static_cast<f32>(b[i + c])));
			}
			low = std::min(low, static_cast<f32>(a[i]));
			high = std::max(high, static_cast<f32>(a[i]));
		}
		out.spread = high - low;
		return out;
	}

	void PlaceAwkwardly(const ShadingStage& stage) {
		stage.quad->SetLocalPosition(Vec3(0.1f, -0.05f, 0.0f));
		stage.quad->SetLocalRotation(Quat::AngleAxis(25.0f, Vec3(0.3f, 1.0f, 0.2f).Normalized()));
		stage.quad->SetLocalScale(Vec3(-1.3f, 0.9f, 1.1f));
		SceneSystem::EndFrame();
	}

} // anonymous namespace

TEST_F(NodeShaderAssetFixture, ObjectSpaceAndWorldSpaceGraphAgree) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	ShaderAsset* direct = AddGraph("ObjectDirect.slshader", SpaceGraph("Input/Position", "Object", false));
	ShaderAsset* converted = AddGraph("ObjectConverted.slshader", SpaceGraph("Input/Position", "Object", true));
	ASSERT_NE(direct, nullptr);
	ASSERT_NE(converted, nullptr);
	MaterialAsset* directMaterial = LoadMaterial("ObjectDirect.mat", direct->assetId, {});
	MaterialAsset* convertedMaterial = LoadMaterial("ObjectConverted.mat", converted->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(directMaterial, stage));
	PlaceAwkwardly(stage);

	Vector<u8> a;
	Vector<u8> b;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, a, size));
	ASSERT_TRUE(Draw(stage, convertedMaterial, b, size));
	Agreement agreement = Agree(a, b);
	EXPECT_GT(agreement.covered, 150u);
	EXPECT_GT(agreement.spread, 20.0f);
	EXPECT_LE(agreement.worst, 1.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, ViewSpaceMatchesCamera) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	const std::array<f32, 3> offset = {0.5f, 0.5f, 0.0f};
	ShaderAsset* direct = AddGraph("ViewDirect.slshader", SpaceGraph("Input/Position", "View", false, "Position", 1.0f / 6.0f, offset));
	ShaderAsset* converted = AddGraph("ViewConverted.slshader", SpaceGraph("Input/Position", "View", true, "Position", 1.0f / 6.0f, offset));
	ASSERT_NE(direct, nullptr);
	ASSERT_NE(converted, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(LoadMaterial("ViewDirect.mat", direct->assetId, {}), stage));
	Vector<u8> a;
	Vector<u8> b;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, a, size));
	const Vec3 center = CenterTexel(a, size);
	EXPECT_NEAR(center.z, ExpectedTexel(Vec3(0.5f, 0.5f, 0.5f)).z, 2.0f);
	ASSERT_TRUE(Draw(stage, LoadMaterial("ViewConverted.mat", converted->assetId, {}), b, size));
	Agreement agreement = Agree(a, b);
	EXPECT_GT(agreement.covered, 150u);
	EXPECT_LE(agreement.worst, 1.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, TangentSpaceViewDirectionAgreesWithTransform) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	ShaderAsset* direct = AddGraph("TangentDirect.slshader", SpaceGraph("Input/ViewDirection", "Tangent", false));
	ShaderAsset* converted = AddGraph("TangentConverted.slshader", SpaceGraph("Input/ViewDirection", "Tangent", true, "Normal"));
	ASSERT_NE(direct, nullptr);
	ASSERT_NE(converted, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(LoadMaterial("TangentDirect.mat", direct->assetId, {}), stage));
	PlaceAwkwardly(stage);
	Vector<u8> a;
	Vector<u8> b;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, a, size));
	ASSERT_TRUE(Draw(stage, LoadMaterial("TangentConverted.mat", converted->assetId, {}), b, size));
	Agreement agreement = Agree(a, b);
	EXPECT_GT(agreement.covered, 150u);
	EXPECT_LE(agreement.worst, 1.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, ScreenPositionRunsDown) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	auto graph = [](bool viaTransform) {
		StackBuilder b("unlit");
		b.document.settings.push_back({str("cull"), "\"off\""});
		u32 source = 10;
		if (viaTransform) {
			GraphNode& position = b.Node(10, "Input/Position");
			DocumentBuilder::Setting(position, "Space", "\"World\"");
			GraphNode& transform = b.Node(11, "Math/Transform");
			DocumentBuilder::Setting(transform, "From", "\"World\"");
			DocumentBuilder::Setting(transform, "To", "\"Screen\"");
			b.Connect(10, "Out", 11, "In");
			GraphNode& swizzle = b.Node(12, "Channel/Swizzle");
			DocumentBuilder::Setting(swizzle, "Mask", "\"xy\"");
			b.Connect(11, "Out", 12, "In");
			source = 12;
		} else {
			GraphNode& screen = b.Node(10, "Input/ScreenPosition");
			DocumentBuilder::Setting(screen, "Mode", "\"Default\"");
		}
		b.Connect(source, "Out", b.Block(2, "Block/BaseColor"), "Value");
		b.Block(3, "Block/Alpha");
		return b.document;
	};
	ShaderAsset* direct = AddGraph("ScreenDirect.slshader", graph(false));
	ShaderAsset* converted = AddGraph("ScreenConverted.slshader", graph(true));
	ASSERT_NE(direct, nullptr);
	ASSERT_NE(converted, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(LoadMaterial("ScreenDirect.mat", direct->assetId, {}), stage));
	PlaceAwkwardly(stage);
	Vector<u8> a;
	Vector<u8> b;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, a, size));
	ASSERT_TRUE(Draw(stage, LoadMaterial("ScreenConverted.mat", converted->assetId, {}), b, size));
	Agreement agreement = Agree(a, b);
	EXPECT_GT(agreement.covered, 150u);
	EXPECT_LE(agreement.worst, 1.0f);

	const size_t above = (static_cast<size_t>(size.y / 2 - 6) * size.x + size.x / 2) * 4;
	const size_t below = (static_cast<size_t>(size.y / 2 + 6) * size.x + size.x / 2) * 4;
	EXPECT_LT(a[above + 1], a[below + 1]);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, TimeCameraObjectReadFrame) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	FrameTime frame;
	frame.time = 0.75;
	Time::FrameScope clock(frame);

	StackBuilder b("unlit");
	b.Node(10, "Input/Time");
	b.Node(11, "Input/Camera");
	b.Node(12, "Input/Object");
	GraphNode& half = b.Node(13, "Math/Multiply");
	DocumentBuilder::Constant(half, "B", "[0.5]");
	b.Connect(10, "Time", 13, "A");
	GraphNode& far = b.Node(14, "Math/Divide");
	DocumentBuilder::Constant(far, "B", "[200]");
	b.Connect(11, "Far Plane", 14, "A");
	b.Node(15, "Channel/Split");
	b.Connect(12, "Position", 15, "In");
	GraphNode& lift = b.Node(16, "Math/Add");
	DocumentBuilder::Constant(lift, "B", "[0.5]");
	b.Connect(15, "R", 16, "A");
	b.Node(17, "Channel/Combine");
	b.Connect(13, "Out", 17, "R");
	b.Connect(14, "Out", 17, "G");
	b.Connect(16, "Out", 17, "B");
	b.Connect(17, "RGB", b.Block(2, "Block/BaseColor"), "Value");
	b.Block(3, "Block/Alpha");
	ShaderAsset* graph = AddGraph("Facts.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(LoadMaterial("Facts.mat", graph->assetId, {}), stage));
	stage.quad->SetLocalPosition(Vec3(0.3f, 0.0f, 0.0f));
	SceneSystem::EndFrame();
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 texel = CenterTexel(pixels, size);
	const Vec3 expected = ExpectedTexel(Vec3(0.375f, 0.5f, 0.8f));
	EXPECT_NEAR(texel.x, expected.x, 1.5f);
	EXPECT_NEAR(texel.y, expected.y, 1.5f);
	EXPECT_NEAR(texel.z, expected.z, 1.5f);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, TriplanarStandsAnImageUpright) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	Guid image = AddTexture("Rows.tga", 2, 2, [](u32, u32 y) { return y == 1 ? Pixel{255, 0, 0} : Pixel{0, 0, 255}; });
	ASSERT_TRUE(image.IsValid());
	StackBuilder b("unlit");
	u32 map = b.Read(101, "map", "Texture2D");
	b.Op(20, "UV/Triplanar");
	b.Connect(map, "Out", 20, "Texture");
	b.Connect(20, "Out", b.Block(2, "Block/BaseColor"), "Value");
	b.Block(3, "Block/Alpha");
	ShaderAsset* graph = AddGraph("Triplanar.slshader", b.document);
	ASSERT_NE(graph, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(LoadMaterial("Triplanar.mat", graph->assetId, "\t\"textures\": { \"map\": \"" + image.ToString() + "\" }"), stage));
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const size_t upper = (static_cast<size_t>(size.y / 2 - 5) * size.x + size.x / 2) * 4;
	const size_t lower = (static_cast<size_t>(size.y / 2 + 5) * size.x + size.x / 2) * 4;
	EXPECT_GT(pixels[upper + 2], pixels[upper]);
	EXPECT_GT(pixels[lower], pixels[lower + 2]);
	TearDownStage(stage);
}

TEST_F(NodeShaderAssetFixture, ParallaxOffsetsAlongView) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	Guid white = AddTexture("White.tga", 2, 2, [](u32, u32) { return Pixel{255, 255, 255}; });
	Guid black = AddTexture("Black.tga", 2, 2, [](u32, u32) { return Pixel{0, 0, 0}; });
	ASSERT_TRUE(white.IsValid());
	ASSERT_TRUE(black.IsValid());

	const Vec3 eye(1.0f, 0.6f, 3.0f);
	const Vec3 view = eye.Normalized();
	const Vec3 tangent(-1.0f, 0.0f, 0.0f);
	const Vec3 normal(0.0f, 0.0f, 1.0f);
	const Vec3 bitangent = Vec3::Cross(normal, tangent);
	Vec3 inTangent(Vec3::Dot(view, tangent), Vec3::Dot(view, bitangent), Vec3::Dot(view, normal));
	inTangent = inTangent.Normalized();
	const Vec3 inTexture(inTangent.x, -inTangent.y, inTangent.z);

	auto render = [&](const char* name, Guid map, const std::function<void(StackBuilder&)>& build, Vector<u8>& pixels, Vec2I& size) {
		StackBuilder b("unlit");
		build(b);
		ShaderAsset* graph = AddGraph((str(name) + ".slshader").c_str(), b.document);
		if (graph == nullptr) {
			return false;
		}
		ShadingStage stage;
		if (!BuildStage(LoadMaterial((str(name) + ".mat").c_str(), graph->assetId, "\t\"textures\": { \"map\": \"" + map.ToString() + "\" }"),
						stage)) {
			return false;
		}
		ViewDesc desc = ShadingTest::QuadView();
		desc.view = Mat4::LookAt(eye, Vec3(0.0f, 0.0f, 0.0f));
		desc.cameraPosition = eye;
		RenderSystem::SubmitView(stage.target, desc);
		RenderSystem::RenderAll();
		const bool read = RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		TearDownStage(stage);
		return read;
	};

	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(render(
		"Parallax", white,
		[](StackBuilder& b) {
			u32 map = b.Read(101, "map", "Texture2D");
			GraphNode& parallax = b.Node(20, "UV/ParallaxMapping");
			DocumentBuilder::Constant(parallax, "Amplitude", "[10]");
			b.Connect(map, "Out", 20, "Heightmap");
			b.Node(21, "Input/UV");
			b.Node(22, "Channel/Swizzle");
			DocumentBuilder::Setting(*GraphDocumentOps::FindNode(b.document, 22), "Mask", "\"xy\"");
			b.Connect(21, "Out", 22, "In");
			b.Node(23, "Math/Subtract");
			b.Connect(20, "ParallaxUVs", 23, "A");
			b.Connect(22, "Out", 23, "B");
			GraphNode& gain = b.Node(24, "Math/Multiply");
			DocumentBuilder::Constant(gain, "B", "[4,4]");
			b.Connect(23, "Out", 24, "A");
			GraphNode& lift = b.Node(25, "Math/Add");
			DocumentBuilder::Constant(lift, "B", "[0.5,0.5]");
			b.Connect(24, "Out", 25, "A");
			b.Connect(25, "Out", b.Block(2, "Block/BaseColor"), "Value");
			b.Block(3, "Block/Alpha");
		},
		pixels, size));
	const f32 height = 1.0f * 0.1f - 0.1f / 2.0f;
	const f32 lifted = inTexture.z + 0.42f;
	const Vec3 parallax = ExpectedTexel(Vec3(height * inTexture.x / lifted * 4.0f + 0.5f, height * inTexture.y / lifted * 4.0f + 0.5f, 0.0f));
	Vec3 texel = CenterTexel(pixels, size);
	EXPECT_NEAR(texel.x, parallax.x, 2.0f);
	EXPECT_NEAR(texel.y, parallax.y, 2.0f);

	ASSERT_TRUE(render(
		"Occlusion", black,
		[](StackBuilder& b) {
			u32 map = b.Read(101, "map", "Texture2D");
			GraphNode& search = b.Node(20, "UV/ParallaxOcclusionMapping");
			DocumentBuilder::Constant(search, "Amplitude", "[100]");
			b.Connect(map, "Out", 20, "Heightmap");
			b.Node(21, "Input/UV");
			b.Node(22, "Channel/Swizzle");
			DocumentBuilder::Setting(*GraphDocumentOps::FindNode(b.document, 22), "Mask", "\"xy\"");
			b.Connect(21, "Out", 22, "In");
			b.Node(23, "Math/Subtract");
			b.Connect(20, "ParallaxUVs", 23, "A");
			b.Connect(22, "Out", 23, "B");
			GraphNode& gain = b.Node(24, "Math/Multiply");
			DocumentBuilder::Constant(gain, "B", "[0.5,0.5]");
			b.Connect(23, "Out", 24, "A");
			GraphNode& lift = b.Node(25, "Math/Add");
			DocumentBuilder::Constant(lift, "B", "[0.5,0.5]");
			b.Connect(24, "Out", 25, "A");
			GraphNode& depth = b.Node(26, "Math/Multiply");
			DocumentBuilder::Constant(depth, "B", "[0.25]");
			b.Connect(20, "PixelDepthOffset", 26, "A");
			b.Node(27, "Channel/Combine");
			b.Node(28, "Channel/Split");
			b.Connect(25, "Out", 28, "In");
			b.Connect(28, "R", 27, "R");
			b.Connect(28, "G", 27, "G");
			b.Connect(26, "Out", 27, "B");
			b.Connect(27, "RGB", b.Block(2, "Block/BaseColor"), "Value");
			b.Block(3, "Block/Alpha");
		},
		pixels, size));
	const f32 maxHeight = 100.0f * 0.01f * 2.0f / 2.0f;
	Vec3 viewUV(inTexture.x * maxHeight, inTexture.y * maxHeight, inTexture.z);
	viewUV = viewUV.Normalized();
	const f32 offsetU = viewUV.x / -viewUV.z;
	const f32 offsetV = viewUV.y / -viewUV.z;
	const Vec3 occlusion = ExpectedTexel(Vec3(offsetU * 0.5f + 0.5f, offsetV * 0.5f + 0.5f, maxHeight / inTexture.z * 0.25f));
	texel = CenterTexel(pixels, size);
	EXPECT_NEAR(texel.x, occlusion.x, 2.0f);
	EXPECT_NEAR(texel.y, occlusion.y, 2.0f);
	EXPECT_NEAR(texel.z, occlusion.z, 2.0f);
}

TEST_F(NodeShaderAssetFixture, MipModesReadNamedLevel) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	Guid quadrants = AddTexture("Quadrants.tga", 4, 4, [](u32 x, u32 y) {
		if (x < 2) {
			return y < 2 ? Pixel{255, 0, 0} : Pixel{0, 0, 255};
		}
		return y < 2 ? Pixel{0, 255, 0} : Pixel{255, 255, 255};
	});
	ASSERT_TRUE(quadrants.IsValid());

	struct Mode {
		const char* name;
		const char* file;
		std::string_view port;
		const char* constant;
	};
	auto render = [&](const Mode& mode, Vec3& out) {
		StackBuilder b("unlit");
		u32 map = b.Read(101, "map", "Texture2D");
		b.Op(10, "Input/UV");
		b.Op(11, "Math/Multiply");
		b.Connect(10, "Out", 11, "A");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.001]");
		b.Op(12, "Math/Add");
		b.Connect(11, "Out", 12, "A");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.25,0.25,0,0]");
		GraphNode& sample = b.Node(20, "Input/SampleTexture2D");
		DocumentBuilder::Setting(sample, "Mip Sampling Mode", str("\"") + mode.name + "\"");
		if (mode.port == "DDX") {
			const bool steep = std::string_view(mode.constant) != "0";
			DocumentBuilder::Constant(sample, "DDX", steep ? "[1,0]" : "[0,0]");
			DocumentBuilder::Constant(sample, "DDY", steep ? "[0,1]" : "[0,0]");
		} else if (!mode.port.empty()) {
			DocumentBuilder::Constant(sample, mode.port, mode.constant);
		}
		b.Connect(map, "Out", 20, "Texture");
		b.Connect(12, "Out", 20, "UV");
		b.Connect(20, "RGBA", b.Block(2, "Block/BaseColor"), "Value");
		ShaderAsset* graph = AddGraph(mode.file, b.document);
		if (!graph) {
			return false;
		}
		MaterialAsset* material = LoadMaterial((str(mode.file) + ".mat").c_str(), graph->assetId,
											   "\t\"textures\": { \"map\": \"" + quadrants.ToString() + "\" }");
		ShadingStage stage;
		if (!BuildStage(material, stage)) {
			return false;
		}
		Vector<u8> pixels;
		Vec2I size{};
		const bool drawn = RenderStage(stage, pixels, size);
		out = CenterTexel(pixels, size);
		TearDownStage(stage);
		return drawn;
	};
	auto pure = [](Vec3 c) {
		auto end = [](f32 v) { return v >= 250.0f || v <= 5.0f; };
		return end(c.x) && end(c.y) && end(c.z) && (c.x + c.y + c.z) >= 250.0f;
	};
	auto averaged = [](Vec3 c) {
		return std::abs(c.x - c.y) <= 6.0f && std::abs(c.y - c.z) <= 6.0f && c.x > 60.0f && c.x < 220.0f;
	};

	Vec3 standard;
	ASSERT_TRUE(render({"Standard", "MipStandard.slshader", {}, nullptr}, standard));
	EXPECT_TRUE(pure(standard)) << "Standard read (" << standard.x << ", " << standard.y << ", " << standard.z;
	Vec3 lod;
	ASSERT_TRUE(render({"LOD", "MipLod.slshader", "LOD", "2"}, lod));
	EXPECT_TRUE(averaged(lod)) << "LOD 2 read (" << lod.x << ", " << lod.y << ", " << lod.z;
	Vec3 bias;
	ASSERT_TRUE(render({"Bias", "MipBias.slshader", "Bias", "20"}, bias));
	EXPECT_TRUE(averaged(bias)) << "Bias 20 read (" << bias.x << ", " << bias.y << ", " << bias.z;
	Vec3 steep;
	ASSERT_TRUE(render({"Gradient", "MipSteep.slshader", "DDX", "1"}, steep));
	EXPECT_TRUE(averaged(steep)) << steep.x << ", " << steep.y << ", " << steep.z;
	Vec3 flat;
	ASSERT_TRUE(render({"Gradient", "MipFlat.slshader", "DDX", "0"}, flat));
	EXPECT_NEAR(flat.x, standard.x, 2.0f);
	EXPECT_NEAR(flat.y, standard.y, 2.0f);
	EXPECT_NEAR(flat.z, standard.z, 2.0f);
}

TEST_F(NodeShaderAssetFixture, NormalTiltsAwayFromHeight) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	Guid rightward = AddTexture("RampRight.tga", 4, 4, [](u32 x, u32) { return Pixel{static_cast<u8>(40 + x * 60), 0, 0}; });
	Guid upward = AddTexture("RampUp.tga", 4, 4, [](u32, u32 y) { return Pixel{static_cast<u8>(40 + y * 60), 0, 0}; });
	ASSERT_TRUE(rightward.IsValid());
	ASSERT_TRUE(upward.IsValid());

	StackBuilder b("unlit");
	u32 map = b.Read(101, "map", "Texture2D");
	b.Op(20, "Artistic/NormalFromTexture");
	b.Connect(map, "Out", 20, "Texture");
	b.Op(21, "Math/Multiply");
	b.Connect(20, "Out", 21, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5,0.5,0.5]");
	b.Op(22, "Math/Add");
	b.Connect(21, "Out", 22, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5,0.5,0.5]");
	b.Connect(22, "Out", b.Block(2, "Block/BaseColor"), "Value");
	ShaderAsset* graph = AddGraph("FromTexture.slshader", b.document);
	ASSERT_NE(graph, nullptr);

	const Vec3 flat = ExpectedTexel(Vec3(0.5f, 0.5f, 0.5f));
	auto read = [&](const Guid& texture, const char* file) {
		MaterialAsset* material = LoadMaterial(file, graph->assetId, "\t\"textures\": { \"map\": \"" + texture.ToString() + "\" }");
		ShadingStage stage;
		Vec3 texel{};
		Vector<u8> pixels;
		Vec2I size{};
		if (BuildStage(material, stage) && RenderStage(stage, pixels, size)) {
			texel = CenterTexel(pixels, size);
		}
		TearDownStage(stage);
		return texel;
	};
	const Vec3 right = read(rightward, "Right.mat");
	EXPECT_LT(right.x, flat.x - 4.0f);
	EXPECT_NEAR(right.y, flat.y, 3.0f);
	const Vec3 up = read(upward, "Up.mat");
	EXPECT_LT(up.y, flat.y - 4.0f);
	EXPECT_NEAR(up.x, flat.x, 3.0f);
}

TEST_F(NodeShaderAssetFixture, GraphsAlphaIsClippedByMaterialsThreshold) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	StackBuilder b("unlit");
	u32 cut = b.Read(101, "cut", "Float");
	b.Connect(cut, "Out", b.Block(2, "Block/Alpha"), "Value");
	ShaderAsset* graph = AddGraph("Clipped.slshader", b.document);
	ASSERT_NE(graph, nullptr);

	auto read = [&](const char* file, const char* value) {
		MaterialAsset* material = LoadMaterial(file, graph->assetId, str("\t\"alphaClip\": 0.5,\n\t\"floats\": { \"cut\": ") + value + " }");
		ShadingStage stage;
		Vec3 texel{-1.0f, -1.0f, -1.0f};
		Vector<u8> pixels;
		Vec2I size{};
		if (BuildStage(material, stage) && RenderStage(stage, pixels, size)) {
			texel = CenterTexel(pixels, size);
		}
		TearDownStage(stage);
		return texel;
	};
	const Vec3 kept = read("Kept.mat", "1");
	const Vec3 clipped = read("Clipped.mat", "0");
	EXPECT_GT(kept.x, 200.0f);
	EXPECT_LT(clipped.x, 2.0f);
}
