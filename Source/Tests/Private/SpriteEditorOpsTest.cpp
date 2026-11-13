#include "TestsPCH.h"

#include "TestImages.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/SpriteEditorOps.h"

#include <filesystem>
#include <vector>

using namespace Shalltear;
namespace Ops = SpriteEditorOps;

namespace {

	std::vector<u8> Blank(Vec2I size) { return std::vector<u8>(static_cast<size_t>(size.x) * size.y * 4, 0); }

	void Fill(std::vector<u8>& rgba, Vec2I size, const RectI& rect) {
		for (i32 y = rect.Top(); y < rect.Bottom(); ++y) {
			for (i32 x = rect.Left(); x < rect.Right(); ++x) {
				u8* texel = &rgba[(static_cast<size_t>(y) * size.x + x) * 4];
				texel[0] = texel[1] = texel[2] = texel[3] = 255;
			}
		}
	}

	TextureSprite Sprite(const char* id, const char* name, RectI rect) {
		TextureSprite sprite;
		sprite.id = id;
		sprite.name = name;
		sprite.rect = rect;
		return sprite;
	}

	Ops::CanvasView View() {
		Ops::CanvasView view;
		view.box = {10.0f, 20.0f, 200.0f, 100.0f};
		view.imageSize = {64, 32};
		return view;
	}

	class SpriteApplyFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_spriteops_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			std::vector<u8> pixels = Blank({16, 8});
			Fill(pixels, {16, 8}, RectI(0, 0, 8, 8));
			TestImages::WriteTGA(_root / "Atlas.tga", 16, 8, pixels.data());
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(Path("project://Atlas.tga")).IsValid());
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static str Meta() {
			return FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Atlas.tga"))), MemTag::Asset);
		}
	};

} // anonymous namespace

TEST(SpriteEditorOpsTest, CellSizeSkipsEmptyCells) {
	const Vec2I size{8, 4};
	std::vector<u8> rgba = Blank(size);
	Fill(rgba, size, RectI(1, 1, 2, 2));
	Ops::SliceDesc desc;
	desc.cell = {4, 4};

	Vector<RectI> rects{GetMemoryResource(MemTag::Debug)};
	Ops::Slice(desc, size, rgba.data(), rects);
	ASSERT_EQ(rects.size(), 1u);
	EXPECT_EQ(rects[0], RectI(0, 0, 4, 4));

	Ops::Slice(desc, size, nullptr, rects);
	ASSERT_EQ(rects.size(), 2u);
	EXPECT_EQ(rects[1], RectI(4, 0, 4, 4));
}

TEST(SpriteEditorOpsTest, CellCountSplitsAroundPadding) {
	Ops::SliceDesc desc;
	desc.mode = Ops::SliceMode::CellCount;
	desc.cell = {2, 1};
	desc.offset = {1, 0};
	desc.padding = {2, 0};

	Vector<RectI> rects{GetMemoryResource(MemTag::Debug)};
	Ops::Slice(desc, {11, 4}, nullptr, rects);
	ASSERT_EQ(rects.size(), 2u);
	EXPECT_EQ(rects[0], RectI(1, 0, 4, 4));
	EXPECT_EQ(rects[1], RectI(7, 0, 4, 4));
}

TEST(SpriteEditorOpsTest, AutomaticBoundsEachIsland) {
	const Vec2I size{16, 8};
	std::vector<u8> rgba = Blank(size);
	Fill(rgba, size, RectI(1, 1, 3, 2));
	Fill(rgba, size, RectI(8, 0, 6, 1));
	Fill(rgba, size, RectI(8, 5, 6, 1));
	Fill(rgba, size, RectI(8, 0, 1, 6));
	Fill(rgba, size, RectI(10, 2, 1, 1));
	Ops::SliceDesc desc;
	desc.mode = Ops::SliceMode::Automatic;

	Vector<RectI> rects{GetMemoryResource(MemTag::Debug)};
	Ops::Slice(desc, size, rgba.data(), rects);
	ASSERT_EQ(rects.size(), 2u);
	EXPECT_EQ(rects[0], RectI(8, 0, 6, 6));
	EXPECT_EQ(rects[1], RectI(1, 1, 3, 2));

	Ops::Slice(desc, size, nullptr, rects);
	EXPECT_TRUE(rects.empty());
}

TEST(SpriteEditorOpsTest, ResliceKeepsOverlappedIdentity) {
	Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Debug)};
	sprites.push_back(Sprite("a", "Hero", RectI(0, 0, 4, 4)));
	sprites.back().border = SpriteBorder{3, 3, 3, 3};
	sprites.push_back(Sprite("far", "Far", RectI(30, 30, 2, 2)));
	const RectI rects[] = {RectI(0, 0, 5, 5), RectI(8, 0, 4, 4), RectI(1, 1, 2, 2)};

	Ops::MergeSlices(sprites, rects, "Atlas");
	ASSERT_EQ(sprites.size(), 4u);
	EXPECT_EQ(sprites[0].id, "a");
	EXPECT_EQ(sprites[0].name, "Hero");
	EXPECT_EQ(sprites[0].rect, RectI(0, 0, 5, 5));
	EXPECT_EQ(sprites[0].border, (SpriteBorder{3, 3, 2, 2}));
	EXPECT_EQ(sprites[1].id, "far");
	EXPECT_EQ(sprites[2].rect, RectI(8, 0, 4, 4));
	EXPECT_EQ(sprites[2].name, "Atlas_0");
	EXPECT_EQ(sprites[2].id.size(), 16u);
	EXPECT_EQ(sprites[3].name, "Atlas_1");
	EXPECT_NE(sprites[2].id, sprites[3].id);
}

TEST(SpriteEditorOpsTest, UniqueNameSkipsTaken) {
	Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Debug)};
	sprites.push_back(Sprite("x", "Atlas_0", RectI(0, 0, 1, 1)));
	sprites.push_back(Sprite("y", "Atlas_2", RectI(0, 0, 1, 1)));
	EXPECT_EQ(Ops::UniqueName(sprites, "Atlas"), "Atlas_1");
}

TEST(SpriteEditorOpsTest, ViewMapsTexelsBothWays) {
	Ops::CanvasView view = View();
	EXPECT_FLOAT_EQ(view.Scale(), 3.125f);
	const Vec2 corner = view.ToPoint({0.0f, 0.0f});
	EXPECT_FLOAT_EQ(corner.x, 10.0f);
	EXPECT_FLOAT_EQ(corner.y, 20.0f);
	const Vec2 back = view.ToTexel(view.ToPoint({12.0f, 7.0f}));
	EXPECT_NEAR(back.x, 12.0f, 1e-4f);
	EXPECT_NEAR(back.y, 7.0f, 1e-4f);

	const Vec2 cursor{60.0f, 40.0f};
	const Vec2 under = view.ToTexel(cursor);
	Ops::ZoomAt(view, cursor, 3.0f);
	EXPECT_GT(view.zoom, 1.0f);
	const Vec2 still = view.ToTexel(cursor);
	EXPECT_NEAR(still.x, under.x, 1e-3f);
	EXPECT_NEAR(still.y, under.y, 1e-3f);
}

TEST(SpriteEditorOpsTest, PickPrefersHandlesThenTopmostSprite) {
	const Ops::CanvasView view = View();
	Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Debug)};
	sprites.push_back(Sprite("a", "A", RectI(0, 0, 16, 16)));
	sprites.push_back(Sprite("b", "B", RectI(8, 8, 16, 16)));

	const UI::Rect a = view.ToRect(sprites[0].rect);
	Ops::Grab grab = Ops::Pick(view, sprites, 0, {a.x + a.w, a.y + a.h}, 4.0f);
	EXPECT_EQ(grab.kind, Ops::GrabKind::Resize);
	EXPECT_EQ(grab.edges, static_cast<u8>(Ops::GrabEdge::Right) | static_cast<u8>(Ops::GrabEdge::Bottom));

	grab = Ops::Pick(view, sprites, 0, view.ToPoint({12.0f, 12.0f}), 4.0f);
	EXPECT_EQ(grab.kind, Ops::GrabKind::Move);
	EXPECT_EQ(grab.sprite, 1);

	grab = Ops::Pick(view, sprites, -1, view.ToPoint({50.0f, 4.0f}), 4.0f);
	EXPECT_EQ(grab.kind, Ops::GrabKind::Create);
	EXPECT_EQ(grab.sprite, -1);
}

TEST(SpriteEditorOpsTest, DragStaysInsideImage) {
	const Vec2I image{64, 32};
	const RectI start(10, 10, 8, 8);
	EXPECT_EQ(Ops::Drag(start, {Ops::GrabKind::Move, 0, 0}, {100, -100}, image), RectI(56, 0, 8, 8));

	const u8 left = static_cast<u8>(Ops::GrabEdge::Left);
	EXPECT_EQ(Ops::Drag(start, {Ops::GrabKind::Resize, 0, left}, {20, 0}, image), RectI(17, 10, 1, 8));
	const u8 corner = static_cast<u8>(Ops::GrabEdge::Right) | static_cast<u8>(Ops::GrabEdge::Bottom);
	EXPECT_EQ(Ops::Drag(start, {Ops::GrabKind::Resize, 0, corner}, {4, 40}, image), RectI(10, 10, 12, 22));
}

TEST(SpriteEditorOpsTest, SpanSnapsOutwardAndClamps) {
	EXPECT_EQ(Ops::SpanRect({7.8f, 2.2f}, {3.2f, 5.5f}, {64, 32}), RectI(3, 2, 5, 4));
	EXPECT_EQ(Ops::SpanRect({-4.0f, -4.0f}, {2.5f, 1.0f}, {64, 32}), RectI(0, 0, 3, 1));
	EXPECT_EQ(Ops::SpanRect({3.0f, 3.0f}, {3.0f, 9.0f}, {64, 32}).size.x, 0);
}

TEST(SpriteEditorOpsTest, BorderFitsInsideRect) {
	EXPECT_EQ(Ops::ClampBorder({6, -1, 6, 2}, {8, 4}), (SpriteBorder{6, 0, 2, 2}));
	EXPECT_EQ(Ops::ClampBorder({1, 1, 1, 1}, {8, 4}), (SpriteBorder{1, 1, 1, 1}));
}

TEST_F(SpriteApplyFixture, ApplyWritesSpritesKeepingOtherSettings) {
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("compression", JSONValue::MakeString("none"));
	settings.AddMember("filter", JSONValue::MakeString("nearest"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), settings));

	Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Debug)};
	sprites.push_back(Sprite("a1", "Hero", RectI(0, 0, 8, 8)));
	sprites.back().border = SpriteBorder{2, 2, 2, 2};
	ASSERT_TRUE(Ops::ApplySprites(Path("project://Atlas.tga"), sprites));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Atlas.tga"));
	ASSERT_NE(loaded, nullptr);
	TextureAsset* texture = static_cast<TextureAsset*>(loaded);
	ASSERT_EQ(texture->sprites.size(), 1u);
	EXPECT_EQ(texture->sprites[0], sprites[0]);
	EXPECT_EQ(texture->settings.filter, TextureFilter::Point);
	EXPECT_EQ(texture->format, RHI::Format::RGBA8SRGB);

	Vector<u8> rgba{GetMemoryResource(MemTag::Debug)};
	ASSERT_TRUE(Ops::DecodeTexels(*texture, rgba));
	ASSERT_EQ(rgba.size(), 16u * 8u * 4u);
	EXPECT_EQ(rgba[3], 255);
	EXPECT_EQ(rgba[(8 * 4) + 3], 0);

	ASSERT_TRUE(Ops::ApplySprites(Path("project://Atlas.tga"), {}));
	EXPECT_TRUE(texture->sprites.empty());
	EXPECT_EQ(Meta().find("\"sprites\""), str::npos);
	EXPECT_NE(Meta().find("nearest"), str::npos);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(SpriteApplyFixture, CompressedTexelsDecode) {
	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Atlas.tga"));
	ASSERT_NE(loaded, nullptr);
	TextureAsset* texture = static_cast<TextureAsset*>(loaded);
	ASSERT_TRUE(RHI::IsBlockCompressed(texture->format));

	Vector<u8> rgba{GetMemoryResource(MemTag::Debug)};
	ASSERT_TRUE(Ops::DecodeTexels(*texture, rgba));
	ASSERT_EQ(rgba.size(), 16u * 8u * 4u);
	EXPECT_GT(rgba[3], 200);
	EXPECT_LT(rgba[(8 * 4 + 12) + 3], 50);

	Ops::SliceDesc desc;
	desc.mode = Ops::SliceMode::Automatic;
	Vector<RectI> rects{GetMemoryResource(MemTag::Debug)};
	Ops::Slice(desc, texture->size, rgba.data(), rects);
	ASSERT_EQ(rects.size(), 1u);
	EXPECT_EQ(rects[0], RectI(0, 0, 8, 8));
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(SpriteApplyFixture, PreviewCropsSpriteRect) {
	JSONValue settings = JSONValue::MakeObject();
	settings.AddMember("compression", JSONValue::MakeString("none"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), settings));
	Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Debug)};
	sprites.push_back(Sprite("edge", "Edge", RectI(6, 2, 4, 3)));
	ASSERT_TRUE(Ops::ApplySprites(Path("project://Atlas.tga"), sprites));

	Asset* texture = AssetDatabase::LoadAsset(Path("project://Atlas.tga"));
	ASSERT_NE(texture, nullptr);
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Debug)};
	AssetDatabase::GetProducts(texture->assetId, products);
	ASSERT_EQ(products.size(), 1u);
	Asset* sprite = AssetDatabase::LoadAssetByGuid(products[0].guid);
	ASSERT_NE(sprite, nullptr);

	EditorPreviews::Initialize();
	EditorPreviews::RegisterBuiltIns();
	AssetPreviewImage image;
	ASSERT_TRUE(EditorPreviews::Produce({sprite->assetId, sprite}, image));
	EXPECT_EQ(image.size, (Vec2I{4, 3}));
	ASSERT_EQ(image.pixels.size(), 4u * 3u * 4u);
	EXPECT_EQ(image.pixels[3], 255);
	EXPECT_EQ(image.pixels[2 * 4 + 3], 0);
	EXPECT_EQ(image.pixelsFormat, RHI::Format::RGBA8SRGB);
	ASSERT_EQ(image.dependencies.size(), 1u);
	EXPECT_EQ(image.dependencies[0], texture->assetId);
	EditorPreviews::Terminate();

	AssetDatabase::UnloadAsset(sprite);
	AssetDatabase::UnloadAsset(texture);
}
