#include "TestsPCH.h"

#include "TestImages.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/Context.h"

#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <vector>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr RHI::TextureHandle FakeGPUTexture{5u, 1u};

	UI::ContextDesc Desc(f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = scale;
		return desc;
	}

	void MakeTexture(TextureAsset& out, Vec2I size) {
		out.size = size;
		out.texture = FakeGPUTexture;
	}

	UI::ElementHandle AddImage(UI::Context& ctx, const UI::ImageDesc& image, f32 width = -1.0f, f32 height = -1.0f) {
		UI::ElementHandle element = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), element);
		UI::Style style;
		style.alignSelf = UI::Align::Start;
		if (width >= 0.0f) {
			style.width = UI::Length::Pixel(width);
		}
		if (height >= 0.0f) {
			style.height = UI::Length::Pixel(height);
		}
		ctx.SetStyle(element, style);
		ctx.SetImage(element, image);
		ctx.Update();
		return element;
	}

	std::vector<UI::UIQuad> ImageQuads(UI::Context& ctx) {
		UI::RenderList list;
		ctx.BuildRenderList(list);
		std::vector<UI::UIQuad> quads;
		for (const UI::UICommand& command : list.commands) {
			if (command.kind != UI::UICommandKind::Quads || command.texture != FakeGPUTexture) {
				continue;
			}
			for (u32 i = 0; i < command.quadCount; ++i) {
				quads.push_back(list.quads[command.firstQuad + i]);
			}
		}
		return quads;
	}

	::testing::AssertionResult RectNear(const UI::Rect& actual, const UI::Rect& expected, f32 tolerance = 0.01f) {
		if (std::abs(actual.x - expected.x) <= tolerance && std::abs(actual.y - expected.y) <= tolerance &&
			std::abs(actual.w - expected.w) <= tolerance && std::abs(actual.h - expected.h) <= tolerance) {
			return ::testing::AssertionSuccess();
		}
		return ::testing::AssertionFailure() << "{" << actual.x << ", " << actual.y << ", " << actual.w << ", " << actual.h << "} vs {"
											 << expected.x << ", " << expected.y << ", " << expected.w << ", " << expected.h << "}";
	}

	class UIImageSpriteFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		TextureAsset* _atlas = nullptr;
		std::vector<SpriteAsset*> _sprites;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_uiimage_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			std::vector<u8> pixels(32 * 16 * 4, 255);
			TestImages::WriteTGA(_root / "Atlas.tga", 32, 16, pixels.data());
			ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(Path("project://Atlas.tga")).IsValid());
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), Settings(0.0)));

			Asset* loaded = AssetDatabase::LoadAsset(Path("project://Atlas.tga"));
			ASSERT_NE(loaded, nullptr);
			_atlas = static_cast<TextureAsset*>(loaded);
			_atlas->texture = FakeGPUTexture;
		}

		static JSONValue Settings(f64 pixelsPerUnit) {
			JSONValue frame = JSONValue::MakeObject();
			frame.AddMember("id", JSONValue::MakeString("frame"));
			frame.AddMember("name", JSONValue::MakeString("Frame"));
			JSONValue rect = JSONValue::MakeArray();
			for (f64 value : {0.0, 0.0, 16.0, 16.0}) {
				rect.Push(JSONValue::MakeNumber(value));
			}
			frame.AddMember("rect", rect);
			JSONValue border = JSONValue::MakeArray();
			for (f64 value : {4.0, 4.0, 4.0, 4.0}) {
				border.Push(JSONValue::MakeNumber(value));
			}
			frame.AddMember("border", border);
			JSONValue list = JSONValue::MakeArray();
			list.Push(frame);
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("sprites", list);
			if (pixelsPerUnit > 0.0) {
				settings.AddMember("pixelsPerUnit", JSONValue::MakeNumber(pixelsPerUnit));
			}
			return settings;
		}

		void Reimport(f64 pixelsPerUnit) {
			ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://Atlas.tga"), Settings(pixelsPerUnit)));
			ASSERT_TRUE(AssetDatabase::ReimportAsset(Path("project://Atlas.tga")));
			ASSERT_EQ(_atlas->settings.pixelsPerUnit, static_cast<f32>(pixelsPerUnit));
			_atlas->texture = FakeGPUTexture;
		}

		void TearDown() override {
			for (SpriteAsset* sprite : _sprites) {
				AssetDatabase::UnloadAsset(sprite);
			}
			if (_atlas) {
				_atlas->texture = {};
				AssetDatabase::UnloadAsset(_atlas);
			}
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		SpriteAsset* Frame() {
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(_atlas->assetId, products);
			for (const AssetProduct& product : products) {
				if (product.key == "Sprites/frame") {
					Asset* loaded = AssetDatabase::LoadAssetByGuid(product.guid);
					if (loaded) {
						_sprites.push_back(static_cast<SpriteAsset*>(loaded));
						return _sprites.back();
					}
				}
			}
			return nullptr;
		}
	};

} // anonymous namespace

TEST(UIImageTest, NativeSizeSizesAutoBox) {
	TextureAsset texture;
	MakeTexture(texture, {32, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image);
	EXPECT_TRUE(RectNear(ctx.GetLayout(element).borderBox, {0.0f, 0.0f, 32.0f, 16.0f}));

	UI::Context scaled(Desc(1.25f), nullptr, nullptr);
	UI::ElementHandle big = AddImage(scaled, image);
	EXPECT_TRUE(RectNear(scaled.GetLayout(big).borderBox, {0.0f, 0.0f, 40.0f, 20.0f}));
}

TEST(UIImageTest, StretchFillsContentBox) {
	TextureAsset texture;
	MakeTexture(texture, {32, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::StretchToFill;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 100.0f, 50.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 100.0f, 50.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.0f, 1.0f, 1.0f}));
}

TEST(UIImageTest, ScaleToFitKeepsAspectCentered) {
	TextureAsset texture;
	MakeTexture(texture, {32, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 100.0f, 100.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 25.0f, 100.0f, 50.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.0f, 1.0f, 1.0f}));
}

TEST(UIImageTest, ScaleAndCropCropsUv) {
	TextureAsset texture;
	MakeTexture(texture, {32, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::ScaleAndCrop;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 100.0f, 100.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 100.0f, 100.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.25f, 0.0f, 0.5f, 1.0f}, 0.001f));
}

TEST(UIImageTest, TiledRepeatsAtNativeSize) {
	TextureAsset texture;
	MakeTexture(texture, {16, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::Tiled;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 40.0f, 16.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 3u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 16.0f, 16.0f}));
	EXPECT_TRUE(RectNear(quads[1].rect, {16.0f, 0.0f, 16.0f, 16.0f}));
	EXPECT_TRUE(RectNear(quads[2].rect, {32.0f, 0.0f, 8.0f, 16.0f}));
	EXPECT_TRUE(RectNear(quads[2].uv, {0.0f, 0.0f, 0.5f, 1.0f}, 0.001f));
}

TEST(UIImageTest, TiledCapsTileCount) {
	TextureAsset texture;
	MakeTexture(texture, {1, 1});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::Tiled;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 300.0f, 280.0f);
	const size_t count = ImageQuads(ctx).size();
	EXPECT_LE(count, UI::MAX_IMAGE_TILES);
	EXPECT_GT(count, UI::MAX_IMAGE_TILES / 4);
}

TEST(UIImageTest, HorizontalFillClipsFromOrigin) {
	TextureAsset texture;
	MakeTexture(texture, {32, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::StretchToFill;
	image.fill.method = UI::ImageFillMethod::Horizontal;
	image.fill.origin = UI::ImageFillOrigin::Left;
	image.fill.amount = 0.25f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image, 100.0f, 20.0f);
	std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 25.0f, 20.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.0f, 0.25f, 1.0f}, 0.001f));

	image.fill.origin = UI::ImageFillOrigin::Right;
	ctx.SetImage(element, image);
	ctx.Update();
	quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {75.0f, 0.0f, 25.0f, 20.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.75f, 0.0f, 0.25f, 1.0f}, 0.001f));
}

TEST(UIImageTest, VerticalFillRisesFromBottom) {
	TextureAsset texture;
	MakeTexture(texture, {16, 32});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::StretchToFill;
	image.fill.method = UI::ImageFillMethod::Vertical;
	image.fill.origin = UI::ImageFillOrigin::Bottom;
	image.fill.amount = 0.5f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 20.0f, 100.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 50.0f, 20.0f, 50.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.5f, 1.0f, 0.5f}, 0.001f));
}

TEST(UIImageTest, RadialFillCarriesWedge) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.fill.method = UI::ImageFillMethod::Radial360;
	image.fill.origin = UI::ImageFillOrigin::Bottom;
	image.fill.amount = 0.5f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image, 64.0f, 64.0f);
	std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NE(quads[0].flags & UI::QUAD_FLAG_RADIAL_FILL, 0u);
	EXPECT_NEAR(quads[0].color1.r, 32.0f, 0.01f);
	EXPECT_NEAR(quads[0].color1.g, 32.0f, 0.01f);
	EXPECT_NEAR(quads[0].color1.b, std::numbers::pi_v<f32> * 0.5f, 1e-4f);
	EXPECT_NEAR(quads[0].color1.a, std::numbers::pi_v<f32>, 1e-4f);

	image.fill.method = UI::ImageFillMethod::Radial90;
	image.fill.origin = UI::ImageFillOrigin::BottomLeft;
	image.fill.clockwise = false;
	image.fill.amount = 1.0f;
	ctx.SetImage(element, image);
	ctx.Update();
	quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_EQ(quads[0].flags & UI::QUAD_FLAG_RADIAL_FILL, 0u);

	image.fill.amount = 0.5f;
	ctx.SetImage(element, image);
	ctx.Update();
	quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_NEAR(quads[0].color1.r, 0.0f, 0.01f);
	EXPECT_NEAR(quads[0].color1.g, 64.0f, 0.01f);
	EXPECT_NEAR(quads[0].color1.b, std::numbers::pi_v<f32> * 2.0f, 1e-4f);
	EXPECT_NEAR(quads[0].color1.a, -std::numbers::pi_v<f32> * 0.25f, 1e-4f);
}

TEST(UIImageTest, EmptyFillDrawsNothing) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.fill.method = UI::ImageFillMethod::Radial180;
	image.fill.amount = 0.0f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image, 64.0f, 64.0f);
	EXPECT_TRUE(ImageQuads(ctx).empty());
	EXPECT_TRUE(RectNear(ctx.GetLayout(element).borderBox, {0.0f, 0.0f, 64.0f, 64.0f}));
}

TEST(UIImageTest, GetImageAnswersBoxFillingImage) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.fill.method = UI::ImageFillMethod::Horizontal;
	image.fill.origin = UI::ImageFillOrigin::Left;
	image.fill.amount = 1.5f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image);
	const UI::ImageDesc* stored = ctx.GetImage(element);
	ASSERT_NE(stored, nullptr);
	EXPECT_EQ(stored->fill.amount, 1.0f);
	EXPECT_EQ(ctx.GetImageAsset(element), Handle<TextureAsset>(&texture));

	ctx.SetImage(element, Handle<TextureAsset>(&texture), 16.0f);
	EXPECT_EQ(ctx.GetImage(element), nullptr);
	EXPECT_TRUE(ctx.HasImage(element));

	ctx.SetImage(element, image);
	ctx.ClearImage(element);
	EXPECT_EQ(ctx.GetImage(element), nullptr);
	EXPECT_FALSE(ctx.HasImage(element));
}

TEST(UIImageTest, PixelsPerUnitSizesAutoBox) {
	TextureAsset texture;
	MakeTexture(texture, {32, 16});
	texture.settings.pixelsPerUnit = 200.0f;
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image);
	EXPECT_TRUE(RectNear(ctx.GetLayout(element).borderBox, {0.0f, 0.0f, 16.0f, 8.0f}));

	UI::Context scaled(Desc(1.25f), nullptr, nullptr);
	UI::ElementHandle big = AddImage(scaled, image);
	EXPECT_TRUE(RectNear(scaled.GetLayout(big).borderBox, {0.0f, 0.0f, 20.0f, 10.0f}));

	texture.settings.pixelsPerUnit = 50.0f;
	UI::Context coarse(Desc(), nullptr, nullptr);
	UI::ElementHandle doubled = AddImage(coarse, image);
	EXPECT_TRUE(RectNear(coarse.GetLayout(doubled).borderBox, {0.0f, 0.0f, 64.0f, 32.0f}));
}

TEST(UIImageTest, PixelsPerUnitSizesTiles) {
	TextureAsset texture;
	MakeTexture(texture, {16, 16});
	texture.settings.pixelsPerUnit = 200.0f;
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.scaleMode = UI::ImageScaleMode::Tiled;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 20.0f, 8.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 3u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 8.0f, 8.0f}));
	EXPECT_TRUE(RectNear(quads[1].rect, {8.0f, 0.0f, 8.0f, 8.0f}));
	EXPECT_TRUE(RectNear(quads[2].rect, {16.0f, 0.0f, 4.0f, 8.0f}));
	EXPECT_TRUE(RectNear(quads[2].uv, {0.0f, 0.0f, 0.5f, 1.0f}, 0.001f));
}

TEST(UIImageTest, SetImageSanitizesSliceScale) {
	TextureAsset texture;
	MakeTexture(texture, {16, 16});
	UI::ImageDesc image;
	image.texture = Handle<TextureAsset>(&texture);
	image.sliceScale = -2.0f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image);
	ASSERT_NE(ctx.GetImage(element), nullptr);
	EXPECT_EQ(ctx.GetImage(element)->sliceScale, 0.0f);

	image.sliceScale = std::numeric_limits<f32>::quiet_NaN();
	ctx.SetImage(element, image);
	EXPECT_EQ(ctx.GetImage(element)->sliceScale, 1.0f);
	image.sliceScale = 2.5f;
	ctx.SetImage(element, image);
	EXPECT_EQ(ctx.GetImage(element)->sliceScale, 2.5f);
}

TEST_F(UIImageSpriteFixture, SpriteSizesBoxByRect) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image);
	EXPECT_TRUE(RectNear(ctx.GetLayout(element).borderBox, {0.0f, 0.0f, 16.0f, 16.0f}));
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.0f, 0.5f, 1.0f}, 0.001f));
}

TEST_F(UIImageSpriteFixture, SlicedKeepsBorderUnscaled) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::Sliced;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 64.0f, 48.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 9u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 4.0f, 4.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.0f, 4.0f / 32.0f, 4.0f / 16.0f}, 0.001f));
	EXPECT_TRUE(RectNear(quads[4].rect, {4.0f, 4.0f, 56.0f, 40.0f}));
	EXPECT_TRUE(RectNear(quads[4].uv, {4.0f / 32.0f, 4.0f / 16.0f, 8.0f / 32.0f, 8.0f / 16.0f}, 0.001f));
	EXPECT_TRUE(RectNear(quads[8].rect, {60.0f, 44.0f, 4.0f, 4.0f}));
}

TEST_F(UIImageSpriteFixture, SlicedBorderShrinksInSmallBox) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::Sliced;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 4.0f, 4.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 4u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 2.0f, 2.0f}));
	EXPECT_TRUE(RectNear(quads[3].rect, {2.0f, 2.0f, 2.0f, 2.0f}));
}

TEST_F(UIImageSpriteFixture, TiledBorderTilesCenterOnly) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::Tiled;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 28.0f, 16.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 5u * 3u);
	EXPECT_TRUE(RectNear(quads[1].rect, {4.0f, 0.0f, 8.0f, 4.0f}));
	EXPECT_TRUE(RectNear(quads[2].rect, {12.0f, 0.0f, 8.0f, 4.0f}));
	EXPECT_TRUE(RectNear(quads[3].rect, {20.0f, 0.0f, 4.0f, 4.0f}));
	EXPECT_TRUE(RectNear(quads[3].uv, {4.0f / 32.0f, 0.0f, 4.0f / 32.0f, 4.0f / 16.0f}, 0.001f));
}

TEST_F(UIImageSpriteFixture, EditedSpriteRedrawsNewRect) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::StretchToFill;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 20.0f, 20.0f);
	ASSERT_EQ(ImageQuads(ctx).size(), 1u);

	_atlas->sprites[0].rect = RectI(16, 0, 16, 16);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].uv, {0.5f, 0.0f, 0.5f, 1.0f}, 0.001f));

	_atlas->sprites.clear();
	EXPECT_TRUE(ImageQuads(ctx).empty());
}

TEST_F(UIImageSpriteFixture, PixelsPerUnitScalesSprite) {
	ASSERT_NO_FATAL_FAILURE(Reimport(200.0));
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::Sliced;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle native = AddImage(ctx, image);
	EXPECT_TRUE(RectNear(ctx.GetLayout(native).borderBox, {0.0f, 0.0f, 8.0f, 8.0f}));

	UI::Context sized(Desc(1.5f), nullptr, nullptr);
	AddImage(sized, image, 64.0f, 48.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(sized);
	ASSERT_EQ(quads.size(), 9u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 3.0f, 3.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {0.0f, 0.0f, 4.0f / 32.0f, 4.0f / 16.0f}, 0.001f));
	EXPECT_TRUE(RectNear(quads[4].rect, {3.0f, 3.0f, 90.0f, 66.0f}));
	EXPECT_TRUE(RectNear(quads[8].rect, {93.0f, 69.0f, 3.0f, 3.0f}));
}

TEST_F(UIImageSpriteFixture, SliceScaleMultipliesBorder) {
	ASSERT_NO_FATAL_FAILURE(Reimport(200.0));
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::Sliced;
	image.sliceScale = 3.0f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle native = AddImage(ctx, image);
	EXPECT_TRUE(RectNear(ctx.GetLayout(native).borderBox, {0.0f, 0.0f, 8.0f, 8.0f}));

	UI::Context sized(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(sized, image, 64.0f, 48.0f);
	std::vector<UI::UIQuad> quads = ImageQuads(sized);
	ASSERT_EQ(quads.size(), 9u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 6.0f, 6.0f}));
	EXPECT_TRUE(RectNear(quads[4].rect, {6.0f, 6.0f, 52.0f, 36.0f}));

	image.sliceScale = 0.0f;
	sized.SetImage(element, image);
	sized.Update();
	quads = ImageQuads(sized);
	ASSERT_EQ(quads.size(), 1u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 64.0f, 48.0f}));
	EXPECT_TRUE(RectNear(quads[0].uv, {4.0f / 32.0f, 4.0f / 16.0f, 8.0f / 32.0f, 8.0f / 16.0f}, 0.001f));
}

TEST_F(UIImageSpriteFixture, SliceScaleSizesTiles) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);
	image.scaleMode = UI::ImageScaleMode::Tiled;
	image.sliceScale = 0.5f;

	UI::Context ctx(Desc(), nullptr, nullptr);
	AddImage(ctx, image, 28.0f, 16.0f);
	const std::vector<UI::UIQuad> quads = ImageQuads(ctx);
	ASSERT_EQ(quads.size(), 8u * 5u);
	EXPECT_TRUE(RectNear(quads[0].rect, {0.0f, 0.0f, 2.0f, 2.0f}));
	EXPECT_TRUE(RectNear(quads[1].rect, {2.0f, 0.0f, 4.0f, 2.0f}));
	EXPECT_TRUE(RectNear(quads[1].uv, {4.0f / 32.0f, 0.0f, 8.0f / 32.0f, 4.0f / 16.0f}, 0.001f));
	EXPECT_TRUE(RectNear(quads[7].rect, {26.0f, 0.0f, 2.0f, 2.0f}));
}

TEST_F(UIImageSpriteFixture, HeldSpriteReadsNewPixelsPerUnit) {
	SpriteAsset* frame = Frame();
	ASSERT_NE(frame, nullptr);
	UI::ImageDesc image;
	image.sprite = Handle<SpriteAsset>(frame);

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = AddImage(ctx, image);
	ASSERT_TRUE(RectNear(ctx.GetLayout(element).borderBox, {0.0f, 0.0f, 16.0f, 16.0f}));

	const u32 version = _atlas->version;
	ASSERT_NO_FATAL_FAILURE(Reimport(400.0));
	EXPECT_NE(_atlas->version, version);
	EXPECT_EQ(frame->GetTexture(), _atlas);
	ctx.ClearImage(element);
	ctx.SetImage(element, image);
	ctx.Update();
	EXPECT_TRUE(RectNear(ctx.GetLayout(element).borderBox, {0.0f, 0.0f, 4.0f, 4.0f}));
}
