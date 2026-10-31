#include "TestsPCH.h"

#include "UI/VectorIconSystem.h"
#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class MemoryAtlas final : public UI::IAtlasBackend {
	public:
		struct Upload {
			RectI region;
			u8 firstAlpha = 0;
		};

		RHI::TextureHandle CreateAtlas(Vec2I size, UI::AtlasFormat format) override {
			created = size;
			createdFormat = format;
			++createdCount;
			return RHI::TextureHandle{1u, 1u};
		}

		void UpdateAtlas(RHI::TextureHandle, RectI region, std::span<const u8> pixels) override {
			uploads.push_back({region, pixels.size() >= 4 ? pixels[3] : u8{0}});
		}

		Vec2I created{};
		UI::AtlasFormat createdFormat = UI::AtlasFormat::R8;
		u32 createdCount = 0;
		std::vector<Upload> uploads;
	};

	void MakeSquare(VectorAsset& asset, f32 viewSize = 16.0f, Color color = Color::White()) {
		asset.viewSize = {viewSize, viewSize};
		asset.verbs.push_back(VectorVerb::Move);
		asset.points.push_back({0.0f, 0.0f});
		for (Vec2 point : {Vec2{viewSize, 0.0f}, Vec2{viewSize, viewSize}, Vec2{0.0f, viewSize}}) {
			asset.verbs.push_back(VectorVerb::Line);
			asset.points.push_back(point);
		}
		asset.verbs.push_back(VectorVerb::Close);

		VectorShape shape;
		shape.verbCount = static_cast<u32>(asset.verbs.size());
		shape.pointCount = static_cast<u32>(asset.points.size());
		asset.shapes.push_back(shape);
		asset.slotDefaults.push_back(color);
	}

	void MakeWideBar(VectorAsset& asset) {
		asset.viewSize = {32.0f, 8.0f};
		asset.verbs.push_back(VectorVerb::Move);
		asset.points.push_back({0.0f, 0.0f});
		for (Vec2 point : {Vec2{32.0f, 0.0f}, Vec2{32.0f, 8.0f}, Vec2{0.0f, 8.0f}}) {
			asset.verbs.push_back(VectorVerb::Line);
			asset.points.push_back(point);
		}
		asset.verbs.push_back(VectorVerb::Close);

		VectorShape shape;
		shape.verbCount = static_cast<u32>(asset.verbs.size());
		shape.pointCount = static_cast<u32>(asset.points.size());
		asset.shapes.push_back(shape);
		asset.slotDefaults.push_back(Color::White());
	}

	UI::ContextDesc Desc(f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = scale;
		return desc;
	}

} // anonymous namespace

TEST(UIIconTest, AtlasPageOnFirstIcon) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	info.atlasSize = {256, 256};
	UI::VectorIconSystem icons(info);

	EXPECT_EQ(atlas.createdCount, 0u);

	VectorAsset asset;
	MakeSquare(asset);
	UI::IconQuery query;
	query.asset = Handle<VectorAsset>(&asset);
	query.sizePx = 24.0f;

	UI::RasterizedIcon first;
	ASSERT_TRUE(icons.GetIcon(query, first));
	EXPECT_EQ(atlas.createdCount, 1u);
	EXPECT_EQ(atlas.created, (Vec2I{256, 256}));
	EXPECT_EQ(atlas.createdFormat, UI::AtlasFormat::RGBA8);
	EXPECT_EQ(first.sizePx, (Vec2{24.0f, 24.0f}));
	EXPECT_TRUE(first.atlas.IsValid());
	EXPECT_EQ(atlas.uploads.size(), 1u);

	UI::RasterizedIcon second;
	ASSERT_TRUE(icons.GetIcon(query, second));
	EXPECT_EQ(second.uv, first.uv);
	EXPECT_EQ(atlas.uploads.size(), 1u);
	EXPECT_EQ(icons.GetStats().rasterizations, 1u);
	EXPECT_EQ(icons.GetStats().entries, 1u);
}

TEST(UIIconTest, AtlasPageReadableWhenPresent) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	info.atlasSize = {256, 256};
	UI::VectorIconSystem icons(info);

	UI::AtlasPage page;
	EXPECT_FALSE(icons.GetAtlasPage(page));

	VectorAsset asset;
	MakeSquare(asset);
	UI::IconQuery query;
	query.asset = Handle<VectorAsset>(&asset);
	query.sizePx = 24.0f;
	UI::RasterizedIcon icon;
	ASSERT_TRUE(icons.GetIcon(query, icon));

	ASSERT_TRUE(icons.GetAtlasPage(page));
	EXPECT_EQ(page.texture, icon.atlas);
	EXPECT_EQ(page.size, (Vec2I{256, 256}));
	EXPECT_EQ(page.format, UI::AtlasFormat::RGBA8);
	EXPECT_EQ(page.entries, 1u);
	EXPECT_FALSE(page.full);

	query.sizePx = 32.0f;
	ASSERT_TRUE(icons.GetIcon(query, icon));
	ASSERT_TRUE(icons.GetAtlasPage(page));
	EXPECT_EQ(page.entries, 2u);
}

TEST(UIIconTest, FullPageSaysSoNotReportingRoom) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	info.atlasSize = {64, 64};
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);
	UI::IconQuery query;
	query.asset = Handle<VectorAsset>(&asset);
	UI::RasterizedIcon icon;

	bool refused = false;
	for (u32 i = 0; i < 64 && !refused; ++i) {
		query.sizePx = 8.0f + static_cast<f32>(i);
		refused = !icons.GetIcon(query, icon);
	}
	ASSERT_TRUE(refused);

	UI::AtlasPage page;
	ASSERT_TRUE(icons.GetAtlasPage(page));
	EXPECT_TRUE(page.full);
	EXPECT_EQ(page.size, (Vec2I{64, 64}));
	EXPECT_GT(page.entries, 0u);
}

TEST(UIIconTest, KeyCoversSizePaletteAsset) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset first;
	MakeSquare(first);
	VectorAsset second;
	MakeSquare(second);

	UI::IconQuery query;
	query.asset = Handle<VectorAsset>(&first);
	query.sizePx = 16.0f;
	UI::RasterizedIcon out;

	ASSERT_TRUE(icons.GetIcon(query, out));
	EXPECT_EQ(icons.GetStats().entries, 1u);

	query.sizePx = 32.0f;
	ASSERT_TRUE(icons.GetIcon(query, out));
	EXPECT_EQ(icons.GetStats().entries, 2u);

	query.palette.Set(0, Color(1.0f, 0.0f, 0.0f, 1.0f));
	ASSERT_TRUE(icons.GetIcon(query, out));
	EXPECT_EQ(icons.GetStats().entries, 3u);

	query.asset = Handle<VectorAsset>(&second);
	ASSERT_TRUE(icons.GetIcon(query, out));
	EXPECT_EQ(icons.GetStats().entries, 4u);

	query.asset = Handle<VectorAsset>(&first);
	query.palette = {};
	query.sizePx = 31.6f;
	ASSERT_TRUE(icons.GetIcon(query, out));
	EXPECT_EQ(icons.GetStats().entries, 4u);
}

TEST(UIIconTest, UnusedPaletteSlotsDoNotEnterKey) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::IconQuery a;
	a.asset = Handle<VectorAsset>(&asset);
	a.sizePx = 16.0f;
	a.palette.Set(0, Color::White());
	a.palette.slots[3] = Color(0.3f, 0.7f, 0.1f, 1.0f);

	UI::IconQuery b = a;
	b.palette.slots[3] = Color(0.9f, 0.2f, 0.4f, 1.0f);

	UI::RasterizedIcon out;
	ASSERT_TRUE(icons.GetIcon(a, out));
	ASSERT_TRUE(icons.GetIcon(b, out));
	EXPECT_EQ(icons.GetStats().entries, 1u);

	EXPECT_EQ(a.palette, b.palette);
	UI::IconPalette different = a.palette;
	different.Set(1, Color::Black());
	EXPECT_NE(a.palette, different);
}

TEST(UIIconTest, FullPageCompacts) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	info.atlasSize = {64, 64};
	info.retentionFrames = 1;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);
	auto ask = [&](f32 sizePx, UI::RasterizedIcon& out) {
		UI::IconQuery query;
		query.asset = Handle<VectorAsset>(&asset);
		query.sizePx = sizePx;
		return icons.GetIcon(query, out);
	};

	UI::RasterizedIcon out;
	ASSERT_TRUE(ask(40.0f, out));
	EXPECT_EQ(icons.GetStats().entries, 1u);

	for (int frame = 0; frame < 4; ++frame) {
		icons.EndFrame();
	}

	ASSERT_TRUE(ask(44.0f, out));
	EXPECT_EQ(icons.GetStats().compactions, 1u);
	EXPECT_EQ(icons.GetStats().entries, 1u);

	const u32 before = icons.GetStats().rasterizations;
	ASSERT_TRUE(ask(44.0f, out));
	EXPECT_EQ(icons.GetStats().rasterizations, before);
}

TEST(UIIconTest, SmallPageStopsThrashing) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	info.atlasSize = {64, 64};
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);
	UI::RasterizedIcon out;
	auto ask = [&](f32 sizePx) {
		UI::IconQuery query;
		query.asset = Handle<VectorAsset>(&asset);
		query.sizePx = sizePx;
		return icons.GetIcon(query, out);
	};

	EXPECT_TRUE(ask(40.0f));
	EXPECT_FALSE(ask(44.0f));
	EXPECT_FALSE(ask(48.0f));
	EXPECT_EQ(icons.GetStats().compactions, 1u);
}

TEST(UIIconTest, DeadAssetDrawsNothing) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	UI::IconQuery query;
	query.sizePx = 16.0f;
	UI::RasterizedIcon out;
	EXPECT_FALSE(icons.GetIcon(query, out));
	EXPECT_EQ(icons.MeasureIcon({}, 16.0f), Vec2{});

	VectorAsset empty;
	query.asset = Handle<VectorAsset>(&empty);
	EXPECT_FALSE(icons.GetIcon(query, out));
	EXPECT_EQ(atlas.uploads.size(), 0u);
}

TEST(UIIconTest, IconElementIsMeasuredAsFittedBox) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset bar;
	MakeWideBar(bar);

	UI::Context ctx(Desc(), nullptr, &icons);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(element, style);
	ctx.SetIcon(element, Handle<VectorAsset>(&bar), 24.0f);
	ctx.Update();

	EXPECT_TRUE(ctx.HasIcon(element));
	const UI::Rect box = ctx.GetLayout(element).borderBox;
	EXPECT_NEAR(box.w, 24.0f, 0.01f);
	EXPECT_NEAR(box.h, 6.0f, 0.01f);
}

TEST(UIIconTest, IconSizeScalesWithContext) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(1.25f), nullptr, &icons);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(element, style);
	ctx.SetIcon(element, Handle<VectorAsset>(&asset), 16.0f);
	ctx.Update();

	const UI::Rect box = ctx.GetLayout(element).borderBox;
	EXPECT_NEAR(box.w, 20.0f, 0.01f);
	EXPECT_NEAR(box.h, 20.0f, 0.01f);

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(atlas.uploads.size(), 1u);
	EXPECT_EQ(atlas.uploads[0].region.size, (Vec2I{20, 20}));
}

TEST(UIIconTest, PaintEmitsOneQuad) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(), nullptr, &icons);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetIcon(element, Handle<VectorAsset>(&asset), 16.0f);
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	ASSERT_EQ(list.commands.size(), 1u);
	EXPECT_TRUE(list.commands[0].texture.IsValid());
	EXPECT_EQ(list.commands[0].sampler, UI::SamplerKind::NearestClamp);

	const UI::UIQuad& quad = list.quads[0];
	EXPECT_NE(quad.flags & UI::QUAD_FLAG_PREMULTIPLIED, 0u);
	EXPECT_EQ(quad.flags & UI::QUAD_FLAG_GLYPH, 0u);
	EXPECT_EQ(quad.rect, (UI::Rect{0.0f, 0.0f, 16.0f, 16.0f}));
	EXPECT_GT(quad.uv.w, 0.0f);
}

TEST(UIIconTest, SameIconSharesEntry) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(), nullptr, &icons);
	for (int i = 0; i < 2; ++i) {
		UI::ElementHandle element = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), element);
		ctx.SetIcon(element, Handle<VectorAsset>(&asset), 16.0f);
	}
	ctx.Update();

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 2u);
	EXPECT_EQ(icons.GetStats().entries, 1u);
	EXPECT_EQ(atlas.uploads.size(), 1u);
	EXPECT_EQ(list.commands.size(), 1u);
}

TEST(UIIconTest, RetintReachesPixelsWithoutRelayingOut) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(), nullptr, &icons);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetIcon(element, Handle<VectorAsset>(&asset), 16.0f);
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	const UI::Rect before = ctx.GetLayout(element).borderBox;
	ASSERT_EQ(atlas.uploads.size(), 1u);

	ctx.SetIconPalette(element, UI::IconPalette::Tint(Color(1.0f, 0.0f, 0.0f, 1.0f)));
	ctx.Update();
	list.Clear();
	ctx.BuildRenderList(list);

	EXPECT_EQ(ctx.GetLayout(element).borderBox, before);
	EXPECT_EQ(atlas.uploads.size(), 2u);
	EXPECT_EQ(icons.GetStats().entries, 2u);
	EXPECT_EQ(list.quads.size(), 1u);
}

TEST(UIIconTest, ClearIconRemovesContentAndQuad) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(), nullptr, &icons);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetIcon(element, Handle<VectorAsset>(&asset), 16.0f);
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);

	ctx.ClearIcon(element);
	ctx.Update();
	list.Clear();
	ctx.BuildRenderList(list);
	EXPECT_FALSE(ctx.HasIcon(element));
	EXPECT_EQ(list.quads.size(), 0u);
	EXPECT_NEAR(ctx.GetLayout(element).borderBox.w, 0.0f, 0.01f);
}

TEST(UIIconTest, IconlessContextMeasures) {
	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetIcon(element, Handle<VectorAsset>(&asset), 16.0f);
	ctx.Update();

	EXPECT_TRUE(ctx.HasIcon(element));
	EXPECT_NEAR(ctx.GetLayout(element).borderBox.w, asset.FitInto(16.0f).x, 0.01f);

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 0u);
}

TEST(UIIconTest, RecycledSlotDropsIcon) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset asset;
	MakeSquare(asset);

	UI::Context ctx(Desc(), nullptr, &icons);
	UI::ElementHandle first = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), first);
	ctx.SetIcon(first, Handle<VectorAsset>(&asset), 16.0f);
	ctx.Update();
	ctx.DestroyElement(first);

	UI::ElementHandle second = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), second);
	ctx.Update();
	EXPECT_FALSE(ctx.HasIcon(second));

	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 0u);
}

namespace {

	void MakeTexture(TextureAsset& out, Vec2I size, RHI::FilterMode filter = RHI::FilterMode::Linear) {
		out.size = size;
		out.settings.filter = filter;
		out.texture = RHI::TextureHandle{5u, 1u};
	}

} // namespace

TEST(UIIconTest, PictureSurvivesSheetRule) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle slot = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), slot);
	ctx.AddClass(slot, StringHash("slot"));
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(slot, style);
	ctx.SetImage(slot, Handle<TextureAsset>(&texture), 24.0f);
	ctx.Update();
	ASSERT_TRUE(ctx.HasImage(slot));

	UI::RenderList before;
	ctx.BuildRenderList(before);
	ASSERT_EQ(before.quads.size(), 1u);

	ctx.LoadStyleSheet(".slot { background-color: #402010; }");
	ctx.Update();

	EXPECT_TRUE(ctx.HasImage(slot));
	EXPECT_EQ(ctx.GetImageAsset(slot), Handle<TextureAsset>(&texture));
	UI::RenderList after;
	ctx.BuildRenderList(after);
	ASSERT_EQ(after.quads.size(), 2u);
	bool textured = false;
	for (const UI::UICommand& command : after.commands) {
		if (command.kind == UI::UICommandKind::Quads && command.texture == texture.texture) {
			textured = true;
		}
	}
	EXPECT_TRUE(textured);
}

TEST(UIIconTest, RasterPictureFitsAspectIntoNamedSquare) {
	TextureAsset texture;
	MakeTexture(texture, {64, 16});

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(element, style);
	ctx.SetImage(element, Handle<TextureAsset>(&texture), 24.0f);
	ctx.Update();

	const UI::Rect box = ctx.GetLayout(element).borderBox;
	EXPECT_NEAR(box.w, 24.0f, 0.01f);
	EXPECT_NEAR(box.h, 6.0f, 0.01f);
}

TEST(UIIconTest, PictureSizeScalesWithContext) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});

	UI::Context ctx(Desc(1.25f), nullptr, nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(element, style);
	ctx.SetImage(element, Handle<TextureAsset>(&texture), 16.0f);
	ctx.Update();

	const UI::Rect box = ctx.GetLayout(element).borderBox;
	EXPECT_NEAR(box.w, 20.0f, 0.01f);
	EXPECT_NEAR(box.h, 20.0f, 0.01f);
}

TEST(UIIconTest, PictureOfEitherKindReplacesOther) {
	MemoryAtlas atlas;
	UI::VectorIconSystem::CreateInfo info;
	info.backend = &atlas;
	UI::VectorIconSystem icons(info);

	VectorAsset mark;
	MakeSquare(mark);
	TextureAsset texture;
	MakeTexture(texture, {32, 32});

	UI::Context ctx(Desc(), nullptr, &icons);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);

	ctx.SetIcon(element, Handle<VectorAsset>(&mark), 16.0f);
	ctx.Update();
	ASSERT_TRUE(ctx.HasIcon(element));
	ASSERT_FALSE(ctx.HasImage(element));

	ctx.SetImage(element, Handle<TextureAsset>(&texture), 16.0f);
	ctx.Update();
	EXPECT_TRUE(ctx.HasImage(element));
	EXPECT_FALSE(ctx.HasIcon(element));
	EXPECT_FALSE(ctx.GetIconAsset(element).IsValid());
	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 1u);

	ctx.SetIcon(element, Handle<VectorAsset>(&mark), 16.0f);
	ctx.Update();
	EXPECT_TRUE(ctx.HasIcon(element));
	EXPECT_FALSE(ctx.HasImage(element));
	EXPECT_FALSE(ctx.GetImageAsset(element).IsValid());
}

TEST(UIIconTest, AssetCarryingNoImageReservesNoSpace) {
	TextureAsset empty;
	empty.texture = RHI::TextureHandle{5u, 1u};

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(element, style);
	ctx.SetImage(element, Handle<TextureAsset>(&empty), 24.0f);
	ctx.Update();

	const UI::Rect box = ctx.GetLayout(element).borderBox;
	EXPECT_NEAR(box.w, 0.0f, 0.01f);
	EXPECT_NEAR(box.h, 0.0f, 0.01f);
	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 0u);
}

TEST(UIIconTest, OffGpuPictureMeasures) {
	TextureAsset texture;
	texture.size = {32, 32};

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	UI::Style style;
	style.alignSelf = UI::Align::Start;
	ctx.SetStyle(element, style);
	ctx.SetImage(element, Handle<TextureAsset>(&texture), 24.0f);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(element).borderBox.w, 24.0f, 0.01f);
	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 0u);
}

TEST(UIIconTest, AssetOwnsSamplerNotCallSite) {
	const std::pair<RHI::FilterMode, UI::SamplerKind> cases[] = {
		{RHI::FilterMode::Nearest, UI::SamplerKind::NearestClamp},
		{RHI::FilterMode::Linear, UI::SamplerKind::LinearClamp}};
	for (const auto& [filter, expected] : cases) {
		TextureAsset texture;
		MakeTexture(texture, {32, 32}, filter);

		UI::Context ctx(Desc(), nullptr, nullptr);
		UI::ElementHandle element = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), element);
		ctx.SetImage(element, Handle<TextureAsset>(&texture), 16.0f);
		ctx.Update();

		UI::RenderList list;
		ctx.BuildRenderList(list);
		bool found = false;
		for (const UI::UICommand& command : list.commands) {
			if (command.kind == UI::UICommandKind::Quads && command.texture == texture.texture) {
				EXPECT_EQ(command.sampler, expected);
				found = true;
			}
		}
		EXPECT_TRUE(found);
	}
}

TEST(UIIconTest, PicturePaintsWithoutIconSystem) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetImage(element, Handle<TextureAsset>(&texture), 16.0f);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(element).borderBox.w, 16.0f, 0.01f);
	UI::RenderList list;
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 1u);
}

TEST(UIIconTest, ClearImageRemovesContentAndQuad) {
	TextureAsset texture;
	MakeTexture(texture, {32, 32});

	UI::Context ctx(Desc(), nullptr, nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), element);
	ctx.SetImage(element, Handle<TextureAsset>(&texture), 16.0f);
	ctx.Update();
	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);

	ctx.ClearImage(element);
	ctx.Update();
	EXPECT_FALSE(ctx.HasImage(element));
	list.Clear();
	ctx.BuildRenderList(list);
	EXPECT_EQ(list.quads.size(), 0u);
}
