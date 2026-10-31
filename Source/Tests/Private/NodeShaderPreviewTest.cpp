#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphPreview.h"
#include "GraphPreviewAtlas.h"
#include "GraphSurface.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	GraphNode& ColorNode(DocumentBuilder& builder, u32 id, f32 r, f32 g, f32 b) {
		GraphNode& node = builder.Node(id, "Channel/Combine");
		DocumentBuilder::Constant(node, "A", "1");
		DocumentBuilder::Constant(node, "B", std::to_string(b));
		DocumentBuilder::Constant(node, "G", std::to_string(g));
		DocumentBuilder::Constant(node, "R", std::to_string(r));
		return node;
	}

	struct Built {
		bool ok = false;
		GraphPreview::Program program;
	};

	Built Generate(const GraphDocument& document) {
		Built built;
		built.ok = GraphPreview::Generate(document, "Test.slshader", built.program);
		return built;
	}

	bool NamesNode(const GraphPreview::Program& program, u32 node) {
		for (const GraphDiagnostic& diagnostic : program.diagnostics) {
			if (diagnostic.node == node) {
				return true;
			}
		}
		return false;
	}

	Vector<u32> TileNodes(const GraphPreview::Program& program) {
		Vector<u32> nodes{GetMemoryResource(MemTag::Editor)};
		for (const GraphPreview::Tile& tile : program.tiles) {
			nodes.push_back(tile.node);
		}
		return nodes;
	}

} // namespace

TEST(NodeShaderPreviewTest, PreviewSuppliesEveryNameSurfaceDoes) {
	std::vector<GraphBinding> surface(GraphSurface::Bindings().begin(), GraphSurface::Bindings().end());
	surface.insert(surface.end(), GraphSurface::LightingBindings().begin(), GraphSurface::LightingBindings().end());
	auto suppliedOnScreen = [](std::string_view name) {
		for (const std::span<const GraphBinding> table : {GraphSurface::FullscreenBindings(), GraphSurface::SkyBindings()}) {
			for (const GraphBinding& other : table) {
				if (other.name == name && other.unavailable.empty()) {
					return true;
				}
			}
		}
		return false;
	};
	for (const GraphBinding& binding : surface) {
		const GraphBinding* preview = nullptr;
		for (const GraphBinding& candidate : GraphPreview::Bindings()) {
			preview = candidate.name == binding.name ? &candidate : preview;
		}
		ASSERT_NE(preview, nullptr) << binding.name;
		EXPECT_EQ(preview->type, binding.type) << binding.name;
		const bool refused = !binding.unavailable.empty() && !suppliedOnScreen(binding.name);
		EXPECT_EQ(!preview->unavailable.empty(), refused) << binding.name;
	}
}

TEST(NodeShaderPreviewTest, OneDocumentGeneratesOneText) {
	DocumentBuilder builder;
	ColorNode(builder, 1, 0.25f, 0.5f, 0.75f);
	builder.Node(2, "Input/NormalVector");
	builder.Node(3, "Math/OneMinus");
	builder.Connect(1, "RGBA", 3, "In");

	const Built first = Generate(builder.document);
	const Built second = Generate(builder.document);
	ASSERT_TRUE(first.ok);
	EXPECT_FALSE(first.program.text.empty());
	EXPECT_EQ(first.program.text, second.program.text);
}

TEST(NodeShaderPreviewTest, PreviewsSkipOptimizer) {
	UnlitBuilder builder;
	builder.Node(30, "Math/OneMinus");
	builder.Connect(30, "Out", 2, "Value");
	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	EXPECT_NE(built.program.text.find("#pragma shalltear_skip_optimizations\n"), str::npos);
	str surface;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphSurface::Generate(builder.document, "Test", surface, diagnostics));
	EXPECT_EQ(surface.find("shalltear_skip_optimizations"), str::npos);
}

TEST(NodeShaderPreviewTest, NodeWithNoPreviewHasNoTile) {
	DocumentBuilder builder;
	GraphNode& vector = builder.Node(1, "Input/Vector4");
	DocumentBuilder::Constant(vector, "X", "1");
	builder.Node(2, "Input/Time");
	ColorNode(builder, 3, 1.0f, 0.0f, 0.0f);

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	const Vector<u32> nodes = TileNodes(built.program);
	ASSERT_EQ(nodes.size(), 1u);
	EXPECT_EQ(nodes[0], 3u);
}

TEST(NodeShaderPreviewTest, Inheriting3DFromUpstream) {
	DocumentBuilder builder;
	builder.Node(1, "Input/NormalVector");
	builder.Node(2, "Math/OneMinus");
	builder.Connect(1, "Out", 2, "In");
	ColorNode(builder, 3, 0.5f, 0.5f, 0.5f);
	builder.Node(4, "Math/OneMinus");
	builder.Connect(3, "RGBA", 4, "In");

	Vector<GraphPreview::Tile> tiles{GetMemoryResource(MemTag::Editor)};
	GraphPreview::Previewable(builder.document, tiles);
	ASSERT_EQ(tiles.size(), 4u);
	EXPECT_TRUE(tiles[0].threeD);
	EXPECT_TRUE(tiles[1].threeD);
	EXPECT_FALSE(tiles[2].threeD);
	EXPECT_FALSE(tiles[3].threeD);
}

TEST(NodeShaderPreviewTest, ATileReadingTheClockIsTimed) {
	DocumentBuilder builder;
	builder.Node(1, "Input/Time");
	ColorNode(builder, 2, 0.0f, 0.0f, 0.0f);
	builder.Connect(1, "Time", 2, "R");
	builder.Node(3, "Math/OneMinus");
	builder.Connect(2, "RGBA", 3, "In");
	ColorNode(builder, 4, 1.0f, 1.0f, 1.0f);

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	ASSERT_EQ(built.program.tiles.size(), 3u);
	EXPECT_TRUE(built.program.tiles[0].timed);
	EXPECT_TRUE(built.program.tiles[1].timed);
	EXPECT_FALSE(built.program.tiles[2].timed);
}

TEST(NodeShaderPreviewTest, FailingNodeCostsTilesAndNoOthers) {
	DocumentBuilder builder;
	ColorNode(builder, 1, 0.2f, 0.4f, 0.6f);
	builder.Node(2, "Input/SampleTexture2D");
	builder.Node(3, "Math/OneMinus");
	builder.Connect(2, "RGBA", 3, "In");
	ColorNode(builder, 4, 0.6f, 0.4f, 0.2f);

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	const Vector<u32> nodes = TileNodes(built.program);
	ASSERT_EQ(nodes.size(), 2u);
	EXPECT_EQ(nodes[0], 1u);
	EXPECT_EQ(nodes[1], 4u);
	EXPECT_TRUE(NamesNode(built.program, 2));
	EXPECT_FALSE(built.program.text.empty());
}

TEST(NodeShaderPreviewTest, ADocumentOfAnotherKindIsRefused) {
	DocumentBuilder builder;
	builder.document.type = "EffectGraph";
	GraphPreview::Program program;
	EXPECT_FALSE(GraphPreview::Generate(builder.document, "Test.sleffect", program));
	EXPECT_TRUE(program.text.empty());
	EXPECT_FALSE(program.diagnostics.empty());
}

namespace {

	using namespace ShadingTest;

	class NodeShaderPreviewFixture : public MaterialReadbackFixture {
	public:
		bool Ready() { return RenderSystem::IsRenderingAvailable() && CompilerAvailable(); }

		ShaderAsset* Compile(const GraphPreview::Program& program) {
			WriteProjectFile("Test.preview.hlsl", program.text);
			const ShaderCompileTicket ticket = ShaderCompileQueue::Submit(FileSystem::Resolve(Path("project://Test.preview.hlsl")));
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
			while (std::chrono::steady_clock::now() < deadline) {
				ShaderAsset* shader = nullptr;
				const ShaderCompileStatus status = ShaderCompileQueue::Collect(ticket, shader);
				if (status == ShaderCompileStatus::Succeeded) {
					return shader;
				}
				if (status != ShaderCompileStatus::Pending) {
					return nullptr;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			ShaderCompileQueue::Cancel(ticket);
			return nullptr;
		}

		static f32 Decode(u8 encoded) {
			const f32 value = static_cast<f32>(encoded) / 255.0f;
			return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
		}

		static Color32 TileTexel(const Vector<u8>& pixels, Vec2I size, const GraphPreviewAtlas& atlas, u32 tile, Vec2 uv) {
			const UI::Rect rect = atlas.TileUV(tile);
			const i32 x = static_cast<i32>((rect.x + rect.w * uv.x) * static_cast<f32>(size.x));
			const i32 y = static_cast<i32>((rect.y + rect.h * uv.y) * static_cast<f32>(size.y));
			const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return Color32(pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]);
		}
	};

	constexpr f32 TOLERANCE = 0.012f;

	::testing::AssertionResult Near(Color32 got, f32 r, f32 g, f32 b) {
		const f32 read[3] = {NodeShaderPreviewFixture::Decode(got.r), NodeShaderPreviewFixture::Decode(got.g), NodeShaderPreviewFixture::Decode(got.b)};
		const f32 wanted[3] = {r, g, b};
		for (u32 i = 0; i < 3; ++i) {
			if (std::fabs(read[i] - wanted[i]) > TOLERANCE) {
				return ::testing::AssertionFailure() << "read linear (" << read[0] << ", " << read[1] << ", " << read[2] << "), wanted (" << r << ", " << g
													 << ", " << b << ")";
			}
		}
		return ::testing::AssertionSuccess();
	}

} // namespace

TEST_F(NodeShaderPreviewFixture, TileIShowsNodeIsOutput) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no compiler";
	}
	DocumentBuilder builder;
	constexpr u32 COLORS = 11;
	for (u32 i = 0; i < COLORS; ++i) {
		const f32 step = static_cast<f32>(i) / static_cast<f32>(COLORS);
		ColorNode(builder, i + 1, 0.05f + 0.9f * step, 0.95f - 0.9f * step, 0.25f + 0.5f * step);
	}
	builder.Node(20, "Input/NormalVector");
	builder.Node(21, "Input/Time");
	ColorNode(builder, 22, 0.0f, 0.0f, 0.0f);
	builder.Connect(21, "Time", 22, "R");

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	ASSERT_EQ(built.program.tiles.size(), COLORS + 2);
	ASSERT_EQ(built.program.rows, 2u);
	ShaderAsset* shader = Compile(built.program);
	ASSERT_NE(shader, nullptr);

	GraphPreviewAtlas atlas;
	ASSERT_TRUE(atlas.Adopt(shader, built.program, {}));
	const Color clear(0.3f, 0.1f, 0.6f, 1.0f);
	constexpr f64 TIME = 0.5;
	atlas.Submit(TIME, clear);
	RenderSystem::RenderAll();
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(atlas.GetTarget(), pixels, size));
	ASSERT_EQ(size.x, static_cast<i32>(GraphPreview::TILE_SIZE * GraphPreview::ATLAS_COLUMNS));
	ASSERT_EQ(size.y, static_cast<i32>(GraphPreview::TILE_SIZE * 2));

	for (u32 i = 0; i < COLORS; ++i) {
		ASSERT_EQ(built.program.tiles[i].node, i + 1);
		const f32 step = static_cast<f32>(i) / static_cast<f32>(COLORS);
		const f32 r = 0.05f + 0.9f * step;
		const f32 g = 0.95f - 0.9f * step;
		const f32 b = 0.25f + 0.5f * step;
		EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, i, {0.5f, 0.5f}), r, g, b)) << "tile " << i << " center";
		EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, i, {0.1f, 0.9f}), r, g, b)) << "tile " << i;
	}

	const u32 sphere = COLORS;
	ASSERT_EQ(built.program.tiles[sphere].node, 20u);
	ASSERT_TRUE(built.program.tiles[sphere].threeD);
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, sphere, {0.5f, 0.3f}), 0.0f, 0.4804f, 0.0f));
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, sphere, {0.02f, 0.02f}), 0.3f, 0.1f, 0.6f));

	const u32 timed = COLORS + 1;
	ASSERT_EQ(built.program.tiles[timed].node, 22u);
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, timed, {0.5f, 0.5f}), static_cast<f32>(TIME), 0.0f, 0.0f));
}

TEST_F(NodeShaderPreviewFixture, PropertyEditReachesTilesWithNoCompile) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no compiler";
	}
	DocumentBuilder builder;
	builder.Property(1, 1, "tint", "Color");
	builder.document.properties.back().defaultValue = "[0.2,0.4,0.6,1]";
	builder.Node(2, "Math/OneMinus");
	builder.Connect(1, "Out", 2, "In");

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	ASSERT_EQ(built.program.tiles.size(), 1u);
	Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphProperties::ParseBlackboard(builder.document, properties, diagnostics));
	ShaderAsset* shader = Compile(built.program);
	ASSERT_NE(shader, nullptr);

	GraphPreviewAtlas atlas;
	ASSERT_TRUE(atlas.Adopt(shader, built.program, properties));
	auto read = [&]() {
		atlas.Submit(0.0, Color::Black());
		RenderSystem::RenderAll();
		Vector<u8> pixels;
		Vec2I size{};
		EXPECT_TRUE(RenderSystem::ReadbackViewTarget(atlas.GetTarget(), pixels, size));
		return TileTexel(pixels, size, atlas, 0, {0.5f, 0.5f});
	};
	EXPECT_TRUE(Near(read(), 0.8f, 0.6f, 0.4f));

	properties[0].defaultValue[0] = 0.9f;
	properties[0].defaultValue[1] = 0.1f;
	properties[0].defaultValue[2] = 0.5f;
	atlas.SetProperties(properties);
	EXPECT_TRUE(Near(read(), 0.1f, 0.9f, 0.5f));
}

TEST_F(NodeShaderPreviewFixture, ASceneReadPreviewsAnEmptyScene) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no compiler";
	}
	DocumentBuilder builder;
	builder.Node(1, "Input/SceneColor");
	builder.Node(2, "Math/Add");
	builder.Connect(1, "Out", 2, "A");
	DocumentBuilder::Constant(builder.document.nodes.back(), "B", "[0.25, 0.5, 0.75]");
	DocumentBuilder::Setting(builder.Node(3, "Input/SceneDepth"), "Sampling Mode", "\"Eye\"");
	builder.Node(4, "Math/Multiply");
	builder.Connect(3, "Out", 4, "A");
	DocumentBuilder::Constant(builder.document.nodes.back(), "B", "[0.05]");

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	const Vector<u32> nodes = TileNodes(built.program);
	ASSERT_EQ(nodes.size(), 2u);
	EXPECT_EQ(nodes[0], 2u);
	EXPECT_EQ(nodes[1], 4u);
	ShaderAsset* shader = Compile(built.program);
	ASSERT_NE(shader, nullptr);

	GraphPreviewAtlas atlas;
	ASSERT_TRUE(atlas.Adopt(shader, built.program, {}));
	atlas.Submit(0.0, Color(0.3f, 0.1f, 0.6f, 1.0f));
	RenderSystem::RenderAll();
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(atlas.GetTarget(), pixels, size));
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, 0, {0.5f, 0.5f}), 0.25f, 0.5f, 0.75f));
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, 1, {0.5f, 0.5f}), 0.5f, 0.5f, 0.5f));
}

TEST_F(NodeShaderPreviewFixture, SkyTileShowsSkyOnBall) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no compiler";
	}
	DocumentBuilder builder;
	builder.Node(1, "Input/Sky");
	builder.Node(2, "Input/SunDisc");

	const Built built = Generate(builder.document);
	ASSERT_TRUE(built.ok);
	const Vector<u32> nodes = TileNodes(built.program);
	ASSERT_EQ(nodes.size(), 2u);
	ASSERT_TRUE(built.program.tiles[0].threeD);
	ASSERT_TRUE(built.program.tiles[1].threeD);
	EXPECT_NE(built.program.text.find("#include \"SunDisc.hlsli\""), str::npos);
	ShaderAsset* shader = Compile(built.program);
	ASSERT_NE(shader, nullptr);

	GraphPreviewAtlas atlas;
	ASSERT_TRUE(atlas.Adopt(shader, built.program, {}));
	atlas.Submit(0.0, Color(0.3f, 0.1f, 0.6f, 1.0f));
	RenderSystem::RenderAll();
	Vector<u8> pixels;
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(atlas.GetTarget(), pixels, size));

	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, 0, {0.5f, 0.3f}), 0.0f, 0.4804f, 0.0f));

	const Vec3 sun = Vec3(-0.45f, 0.8f, -0.4f).Normalized();
	const Vec3 seen = sun * 0.5f - Vec3(0.0f, 0.0f, -5.0f);
	const f32 tanHalfFov = std::tan(Math::DegToRad(7.5f));
	const Vec2 sunUV((seen.x / seen.z / tanHalfFov + 1.0f) * 0.5f, (1.0f - seen.y / seen.z / tanHalfFov) * 0.5f);
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, 1, sunUV), 1.0f, 1.0f, 1.0f));
	EXPECT_TRUE(Near(TileTexel(pixels, size, atlas, 1, {0.5f, 0.5f}), 0.0f, 0.0f, 0.0f));
}
