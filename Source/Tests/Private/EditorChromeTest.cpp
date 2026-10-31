#include "TestsPCH.h"

#include "Shalltear/Core/UI/VectorAsset.h"

#include "UITestUtil.h"

#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Editor/EditorChromeOps.h"
#include "Shalltear/Editor/EditorTheme.h"

using namespace Shalltear;

namespace {

	UI::ContextDesc Desc(i32 width = 800, i32 height = 600, f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {width, height};
		desc.contentScale = scale;
		return desc;
	}

	struct Chrome {
		UI::ElementHandle bar;
		UI::ElementHandle button;
	};

	Chrome BuildBar(UI::Context& ctx, f32 barHeightPx = 32.0f, f32 buttonWidthPx = 40.0f) {
		Chrome chrome;
		chrome.bar = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), chrome.bar);
		ctx.AddClass(chrome.bar, EditorChromeOps::CaptionClass);
		UI::Style barStyle;
		barStyle.width = UI::Length::Percent(100.0f);
		barStyle.height = UI::Length::Pixel(barHeightPx);
		barStyle.justifyContent = UI::Align::End;
		ctx.SetStyle(chrome.bar, barStyle);

		chrome.button = ctx.CreateElement();
		ctx.AppendChild(chrome.bar, chrome.button);
		UI::Style buttonStyle;
		buttonStyle.width = UI::Length::Pixel(buttonWidthPx);
		buttonStyle.height = UI::Length::Percent(100.0f);
		ctx.SetStyle(chrome.button, buttonStyle);
		return chrome;
	}

} // anonymous namespace

TEST(EditorChromeTest, EmptyCaptionSpaceDragsTheWindow) {
	UI::Context ctx(Desc(), nullptr);
	BuildBar(ctx);
	ctx.Update();

	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {200, 16}), HitTestResult::Caption);
}

TEST(EditorChromeTest, ControlInsideCaptionIsNotCaption) {
	UI::Context ctx(Desc(800, 600), nullptr);
	Chrome chrome = BuildBar(ctx, 32.0f, 40.0f);
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(chrome.button).borderBox;
	ASSERT_NEAR(box.w, 40.0f, 0.01f);

	i32 insideButton = static_cast<i32>(box.x + box.w * 0.5f);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {insideButton, 16}), HitTestResult::Client);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {static_cast<i32>(box.x) - 20, 16}),
			  HitTestResult::Caption);
}

TEST(EditorChromeTest, NonHoverableLabelInsideCaptionStillDrags) {
	UI::Context ctx(Desc(), nullptr);
	Chrome chrome = BuildBar(ctx);

	UI::ElementHandle label = ctx.CreateElement();
	ctx.AppendChild(chrome.bar, label);
	UI::Style labelStyle;
	labelStyle.width = UI::Length::Pixel(120.0f);
	labelStyle.height = UI::Length::Percent(100.0f);
	ctx.SetStyle(label, labelStyle);
	ctx.SetHoverable(label, false);
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(label).borderBox;
	i32 insideLabel = static_cast<i32>(box.x + box.w * 0.5f);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {insideLabel, 16}), HitTestResult::Caption);
}

TEST(EditorChromeTest, OutsideTheCaptionIsClient) {
	UI::Context ctx(Desc(), nullptr);
	BuildBar(ctx, 32.0f);
	ctx.Update();

	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {200, 300}), HitTestResult::Client);
}

TEST(EditorChromeTest, OpenPopupOverCaptionIsClient) {
	UI::Context ctx(Desc(), nullptr);
	BuildBar(ctx);

	UI::ElementHandle popup = ctx.CreateElement();
	UI::Style popupStyle;
	popupStyle.width = UI::Length::Pixel(160.0f);
	popupStyle.height = UI::Length::Pixel(200.0f);
	ctx.SetStyle(popup, popupStyle);
	ctx.OpenPopup(popup, {100.0f, 0.0f});
	ctx.Update();

	ASSERT_EQ(ctx.GetOpenPopupCount(), 1u);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {120, 16}), HitTestResult::Client);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {400, 16}), HitTestResult::Caption);
}

TEST(EditorChromeTest, ChromeHitTestIsInPhysicalPixels) {
	UI::Context ctx(Desc(800, 600, 2.0f), nullptr);
	BuildBar(ctx, 32.0f);
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(ctx.GetRoot()).borderBox;
	ASSERT_NEAR(box.h, 600.0f, 0.01f);

	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {200, 48}), HitTestResult::Caption);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {200, 80}), HitTestResult::Client);
}

namespace {

	f32 CenterMidpointWithRightGroup(f32 rightWidthPx, f32 viewportPx = 1550.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {static_cast<i32>(viewportPx), 830};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, nullptr);
		EditorTheme::ApplyToContext(ctx);

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		ctx.SetStyle(ctx.GetRoot(), rootStyle);

		UI::ElementHandle bar = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), bar);
		ctx.AddClass(bar, EditorTheme::Class::TitleBar);

		auto zone = [&](bool end) {
			UI::ElementHandle z = ctx.CreateElement();
			ctx.AppendChild(bar, z);
			ctx.AddClass(z, EditorTheme::Class::TitleZone);
			if (end) {
				UI::Style s;
				s.justifyContent = UI::Align::End;
				ctx.SetStyle(z, s);
			}
			return z;
		};
		auto block = [&](UI::ElementHandle parent, f32 w) {
			UI::ElementHandle e = ctx.CreateElement();
			ctx.AppendChild(parent, e);
			UI::Style s;
			s.width = UI::Length::Pixel(w);
			s.height = UI::Length::Pixel(32.0f);
			ctx.SetStyle(e, s);
		};

		block(zone(false), 860.0f);

		UI::ElementHandle center = ctx.CreateElement();
		ctx.AppendChild(bar, center);
		UI::Style centerStyle;
		centerStyle.alignItems = UI::Align::Center;
		centerStyle.flexShrink = 0.0f;
		ctx.SetStyle(center, centerStyle);
		block(center, 190.0f);

		block(zone(true), rightWidthPx);

		ctx.Update();
		const UI::Rect& r = ctx.GetLayout(center).borderBox;
		return r.x + r.w * 0.5f;
	}

} // anonymous namespace

TEST(EditorChromeTest, CenteredTitleStaysPut) {
	const f32 viewport = 1550.0f;
	f32 narrow = CenterMidpointWithRightGroup(250.0f, viewport);
	f32 wide = CenterMidpointWithRightGroup(520.0f, viewport);

	EXPECT_NEAR(narrow, viewport * 0.5f, 6.0f);
	EXPECT_NEAR(wide, narrow, 0.01f);
}

TEST(EditorChromeTest, FlankGetsHalfLeftover) {
	const f32 viewport = 900.0f;
	f32 mid = CenterMidpointWithRightGroup(520.0f, viewport);
	EXPECT_NEAR(mid, viewport * 0.5f, 6.0f);
}

TEST(EditorChromeTest, WindowControlsClipLast) {
	UI::ContextDesc desc;
	desc.sizePx = {400, 120};
	desc.contentScale = 1.0f;
	UI::Context ctx(desc, nullptr);
	EditorTheme::ApplyToContext(ctx);

	UI::Style rootStyle;
	rootStyle.flexDirection = UI::FlexDirection::Column;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);

	UI::ElementHandle bar = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), bar);
	ctx.AddClass(bar, EditorTheme::Class::TitleBar);

	UI::ElementHandle zone = ctx.CreateElement();
	ctx.AppendChild(bar, zone);
	ctx.AddClass(zone, EditorTheme::Class::TitleZone);
	UI::Style zoneStyle;
	zoneStyle.justifyContent = UI::Align::End;
	ctx.SetStyle(zone, zoneStyle);

	auto captionButton = [&](f32 widthPx) {
		UI::ElementHandle e = ctx.CreateElement();
		ctx.AppendChild(zone, e);
		ctx.AddClass(e, EditorTheme::Class::ToolButton);
		UI::Style s;
		s.width = UI::Length::Pixel(widthPx);
		s.height = UI::Length::Pixel(32.0f);
		ctx.SetStyle(e, s);
		return e;
	};
	UI::ElementHandle transport = captionButton(300.0f);
	UI::ElementHandle controls = captionButton(120.0f);
	ctx.Update();

	const UI::Rect& zoneBox = ctx.GetLayout(zone).borderBox;
	const UI::Rect& transportBox = ctx.GetLayout(transport).borderBox;
	const UI::Rect& controlsBox = ctx.GetLayout(controls).borderBox;

	EXPECT_NEAR(transportBox.w, 300.0f, 0.5f);
	EXPECT_NEAR(controlsBox.w, 120.0f, 0.5f);

	EXPECT_NEAR(controlsBox.x + controlsBox.w, zoneBox.x + zoneBox.w, 0.5f);
	EXPECT_LT(transportBox.x, zoneBox.x);
	EXPECT_GE(controlsBox.x, 0.0f);
	EXPECT_LE(controlsBox.x + controlsBox.w, static_cast<f32>(desc.sizePx.x));
}

namespace {

	struct FloatingRig {
		UI::Context ctx{Desc(480, 360), nullptr};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		EditorChromeOps::FloatingChromeParts parts;

		explicit FloatingRig(bool markTabBar = true) {
			EditorTheme::ApplyToContext(ctx);
			parts = EditorChromeOps::BuildFloatingChrome(ctx, Handle<Window>{}, []() {});
			if (markTabBar) {
				dock.SetTabBarClass(EditorChromeOps::CaptionStripClass);
			}
			dock.RegisterPanel("panel.a"_hash, "Inspector", [](UI::Context&, UI::ElementHandle) {});
			dock.RegisterPanel("panel.b"_hash, "Console", [](UI::Context&, UI::ElementHandle) {});
			dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel("panel.b"_hash, dock.GetRootNode(), UI::DockZone::Center);
			dock.Build(parts.dockHost);
			ctx.Update();
		}

		UI::ElementHandle Find(StringHash cls, UI::ElementHandle from = {}) const {
			UI::ElementHandle node = from.IsValid() ? from : ctx.GetRoot();
			if (from.IsValid() && ctx.HasClass(node, cls)) {
				return node;
			}
			const UI::Element* element = ctx.GetElement(node);
			if (!element) {
				return {};
			}
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = ctx.MakeHandle(child);
				if (!handle.IsValid()) {
					break;
				}
				if (UI::ElementHandle hit = Find(cls, handle); hit.IsValid()) {
					return hit;
				}
				const UI::Element* childElement = ctx.GetElement(handle);
				child = childElement ? childElement->nextSibling : UI::INVALID_UI_INDEX;
			}
			return {};
		}

		UI::ElementHandle LastTab(UI::ElementHandle tabBar) const {
			const UI::Element* element = ctx.GetElement(tabBar);
			return element ? ctx.MakeHandle(element->lastChild) : UI::ElementHandle{};
		}
	};

} // anonymous namespace

TEST(EditorChromeTest, TheFloatingTabStripDragsTheWindow) {
	FloatingRig rig;

	UI::ElementHandle tabBar = rig.Find(StringHash("dock-tab-bar"));
	ASSERT_TRUE(tabBar.IsValid());
	ASSERT_TRUE(rig.ctx.HasClass(tabBar, EditorChromeOps::CaptionStripClass));
	EXPECT_FALSE(rig.ctx.HasClass(tabBar, EditorChromeOps::CaptionClass));

	const UI::Rect& bar = rig.ctx.GetLayout(tabBar).borderBox;
	const UI::Rect& lastTab = rig.ctx.GetLayout(rig.LastTab(tabBar)).borderBox;
	const UI::Rect& buttons = rig.ctx.GetLayout(rig.parts.controls).borderBox;
	ASSERT_LT(lastTab.x + lastTab.w, buttons.x);

	i32 emptyStrip = static_cast<i32>((lastTab.x + lastTab.w + buttons.x) * 0.5f);
	i32 stripY = static_cast<i32>(bar.y + bar.h * 0.5f);

	EXPECT_EQ(EditorChromeOps::HitTestChrome(rig.ctx, {emptyStrip, stripY}), HitTestResult::Caption);
}

TEST(EditorChromeTest, TabInFloatingStripIsNotCaption) {
	FloatingRig rig;

	UI::ElementHandle tab = rig.Find(StringHash("dock-tab"));
	ASSERT_TRUE(tab.IsValid());
	const UI::Rect& box = rig.ctx.GetLayout(tab).borderBox;
	ASSERT_GT(box.w, 0.0f);

	EXPECT_EQ(EditorChromeOps::HitTestChrome(
				  rig.ctx, {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)}),
			  HitTestResult::Client);
}

TEST(EditorChromeTest, TabLabelClickReachesTab) {
	FloatingRig rig;

	UI::ElementHandle label = rig.Find(StringHash("dock-tab-label"));
	ASSERT_TRUE(label.IsValid());
	const UI::Rect& box = rig.ctx.GetLayout(label).borderBox;

	EXPECT_EQ(EditorChromeOps::HitTestChrome(rig.ctx, {static_cast<i32>(box.x), static_cast<i32>(box.y + 1.0f)}),
			  HitTestResult::Client);
}

TEST(EditorChromeTest, AnUnmarkedTabStripIsClient) {
	FloatingRig rig(false);

	UI::ElementHandle tabBar = rig.Find(StringHash("dock-tab-bar"));
	ASSERT_TRUE(tabBar.IsValid());
	EXPECT_FALSE(rig.ctx.HasClass(tabBar, EditorChromeOps::CaptionStripClass));

	const UI::Rect& bar = rig.ctx.GetLayout(tabBar).borderBox;
	const UI::Rect& lastTab = rig.ctx.GetLayout(rig.LastTab(tabBar)).borderBox;
	const UI::Rect& buttons = rig.ctx.GetLayout(rig.parts.controls).borderBox;
	i32 emptyStrip = static_cast<i32>((lastTab.x + lastTab.w + buttons.x) * 0.5f);

	EXPECT_EQ(EditorChromeOps::HitTestChrome(rig.ctx, {emptyStrip, static_cast<i32>(bar.y + bar.h * 0.5f)}),
			  HitTestResult::Client);
}

TEST(EditorChromeTest, FloatingWindowControlsAreClientOverStrip) {
	FloatingRig rig;

	const UI::Rect& buttons = rig.ctx.GetLayout(rig.parts.controls).borderBox;
	ASSERT_GT(buttons.w, 0.0f);
	EXPECT_NEAR(buttons.y, 0.0f, 0.01f);
	EXPECT_NEAR(buttons.x + buttons.w, 480.0f, 0.01f);

	UI::ElementHandle tabBar = rig.Find(StringHash("dock-tab-bar"));
	ASSERT_TRUE(tabBar.IsValid());
	const UI::Rect& bar = rig.ctx.GetLayout(tabBar).borderBox;
	ASSERT_LT(buttons.y, bar.y + bar.h);

	EXPECT_EQ(EditorChromeOps::HitTestChrome(
				  rig.ctx, {static_cast<i32>(buttons.x + buttons.w * 0.5f), static_cast<i32>(buttons.h * 0.5f)}),
			  HitTestResult::Client);
}

TEST(EditorChromeTest, FloatingWindowGetsAll3WindowControls) {
	FloatingRig rig;

	EXPECT_TRUE(rig.parts.maximizeFace.IsValid());

	u32 count = 0;
	const UI::Element* controls = rig.ctx.GetElement(rig.parts.controls);
	for (u32 child = controls ? controls->firstChild : UI::INVALID_UI_INDEX;
		 child != UI::INVALID_UI_INDEX;) {
		++count;
		const UI::Element* childElement = rig.ctx.GetElement(rig.ctx.MakeHandle(child));
		child = childElement ? childElement->nextSibling : UI::INVALID_UI_INDEX;
	}
	EXPECT_EQ(count, 3u);
}

TEST(EditorChromeTest, TheMaximizeGlyphSwapsAndReturns) {
	FloatingRig rig;
	ASSERT_TRUE(rig.parts.maximizeFace.IsValid());

	Handle<VectorAsset> atRest = rig.ctx.GetIconAsset(rig.parts.maximizeFace);
	ASSERT_TRUE(atRest.IsValid());

	EditorChromeOps::SetMaximizeGlyph(rig.ctx, rig.parts.maximizeFace, true);
	Handle<VectorAsset> zoomed = rig.ctx.GetIconAsset(rig.parts.maximizeFace);
	EXPECT_NE(zoomed, atRest);

	EditorChromeOps::SetMaximizeGlyph(rig.ctx, rig.parts.maximizeFace, false);
	EXPECT_EQ(rig.ctx.GetIconAsset(rig.parts.maximizeFace), atRest);

	EditorChromeOps::SetMaximizeGlyph(rig.ctx, UI::ElementHandle{}, true);
}

TEST(EditorChromeTest, FloatingWindowControlsShareStripsBand) {
	FloatingRig rig;

	UI::ElementHandle tabBar = rig.Find(StringHash("dock-tab-bar"));
	ASSERT_TRUE(tabBar.IsValid());
	const UI::Rect& bar = rig.ctx.GetLayout(tabBar).borderBox;
	const UI::Rect& buttons = rig.ctx.GetLayout(rig.parts.controls).borderBox;

	f32 band = EditorTheme::GetMetric(EditorTheme::MetricToken::CaptionStripHeight);
	EXPECT_FLOAT_EQ(buttons.h, band);
	EXPECT_FLOAT_EQ(bar.h, band);

	f32 gapAbove = buttons.y - bar.y;
	f32 gapBelow = (bar.y + bar.h) - (buttons.y + buttons.h);
	EXPECT_FLOAT_EQ(gapAbove, gapBelow);
	EXPECT_FLOAT_EQ(gapBelow, 0.0f);
}

TEST(EditorChromeTest, ChromeControlsFitTheReservedStrip) {
	FloatingRig rig;

	UI::ElementHandle tabBar = rig.Find(StringHash("dock-tab-bar"));
	ASSERT_TRUE(tabBar.IsValid());

	const UI::Rect& bar = rig.ctx.GetLayout(tabBar).borderBox;
	const UI::Rect& content = rig.ctx.GetLayout(tabBar).content;
	const UI::Rect& buttons = rig.ctx.GetLayout(rig.parts.controls).borderBox;

	f32 reserved = (bar.x + bar.w) - (content.x + content.w);
	EXPECT_GT(reserved, 0.0f);
	EXPECT_GE(reserved, buttons.w);

	UI::Context plain(Desc(), nullptr);
	EditorTheme::ApplyToContext(plain);
	UI::ElementHandle bareBar = plain.CreateElement();
	plain.AppendChild(plain.GetRoot(), bareBar);
	plain.AddClass(bareBar, EditorChromeOps::CaptionClass);
	plain.Update();
	const UI::LayoutData& bareLayout = plain.GetLayout(bareBar);
	EXPECT_NEAR(bareLayout.borderBox.x + bareLayout.borderBox.w,
				bareLayout.content.x + bareLayout.content.w, 0.01f);
}

namespace {

	ConsoleEntry NoticeEntry(u64 sequence, LogLevel level, std::string_view category, std::string_view message) {
		ConsoleEntry entry;
		entry.sequence = sequence;
		entry.level = level;
		entry.category = str(category);
		entry.message = str(message);
		return entry;
	}

	struct NoticeBar {
		UI::ElementHandle bar;
		EditorChromeOps::ConsoleNoticeParts parts;
	};

	NoticeBar BuildNoticeBar(UI::Context& ctx, std::function<void()> onActivate = {}) {
		NoticeBar built;
		built.bar = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), built.bar);
		UI::Style barStyle;
		barStyle.flexDirection = UI::FlexDirection::Row;
		barStyle.width = UI::Length::Percent(100.0f);
		barStyle.height = UI::Length::Pixel(24.0f);
		ctx.SetStyle(built.bar, barStyle);
		built.parts = EditorChromeOps::BuildConsoleNotice(ctx, built.bar, std::move(onActivate));
		return built;
	}

	void ClickAt(UI::Context& ctx, Vec2 point) {
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = MouseButton::Left;
		down.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		InputEvent up = down;
		up.type = InputEventType::MouseUp;
		ctx.InjectInput(down);
		ctx.InjectInput(up);
		ctx.Update();
	}

	Vec2 CenterOf(UI::Context& ctx, UI::ElementHandle handle) {
		const UI::Rect& box = ctx.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

} // anonymous namespace

TEST(EditorChromeTest, NoticeShowsNewestLine) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	NoticeBar built = BuildNoticeBar(ctx);

	ConsoleSummary summary;
	summary.newest = NoticeEntry(9, LogLevel::Info, "Editor", "saved Main.scene");
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();

	EXPECT_EQ(ctx.GetTextString(built.parts.message), "[Editor] saved Main.scene");
	EXPECT_TRUE(ctx.GetTextString(built.parts.errors).empty());
	EXPECT_TRUE(ctx.GetTextString(built.parts.warnings).empty());
}

TEST(EditorChromeTest, UnseenErrorCountedNotShown) {
	ConsoleSummary summary;
	summary.newest = NoticeEntry(120, LogLevel::Info, "Game", "spawned 3 chunks");
	summary.newestUnseenError = NoticeEntry(41, LogLevel::Error, "Game", "null component on 'Player'");
	summary.unseenErrors = 1;

	EditorChromeOps::ConsoleNoticeState state = EditorChromeOps::DescribeConsoleNotice(summary);
	EXPECT_EQ(state.message, "[Game] spawned 3 chunks");
	EXPECT_EQ(state.level, LogLevel::Info);
	EXPECT_EQ(state.errors, "1 error");
	EXPECT_NE(state.tooltip.find("[Game] null component on 'Player'"), str::npos);

	ConsoleSummary alone;
	alone.newest = summary.newestUnseenError;
	alone.newestUnseenError = summary.newestUnseenError;
	alone.unseenErrors = 1;
	EditorChromeOps::ConsoleNoticeState once = EditorChromeOps::DescribeConsoleNotice(alone);
	EXPECT_EQ(once.tooltip.find("newest error:"), str::npos);

	ConsoleSummary seen;
	seen.newest = summary.newest;
	EditorChromeOps::ConsoleNoticeState after = EditorChromeOps::DescribeConsoleNotice(seen);
	EXPECT_EQ(after.message, "[Game] spawned 3 chunks");
	EXPECT_TRUE(after.errors.empty());
}

TEST(EditorChromeTest, WarningIsCountedButNeverTakesMessage) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	NoticeBar built = BuildNoticeBar(ctx);

	ConsoleSummary summary;
	summary.newest = NoticeEntry(30, LogLevel::Info, "Assets", "imported Rock.glb");
	summary.unseenWarnings = 4;
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();

	EXPECT_EQ(ctx.GetTextString(built.parts.message), "[Assets] imported Rock.glb");
	EXPECT_EQ(ctx.GetTextString(built.parts.warnings), "4 warnings");
	EXPECT_TRUE(ctx.GetTextString(built.parts.errors).empty());
}

TEST(EditorChromeTest, CountsReadAsEnglishAndStopWidening) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	NoticeBar built = BuildNoticeBar(ctx);

	ConsoleSummary one;
	one.newest = NoticeEntry(1, LogLevel::Error, "Game", "boom");
	one.newestUnseenError = one.newest;
	one.unseenErrors = 1;
	one.unseenWarnings = 1;
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, one);
	EXPECT_EQ(ctx.GetTextString(built.parts.errors), "1 error");
	EXPECT_EQ(ctx.GetTextString(built.parts.warnings), "1 warning");

	ConsoleSummary flood = one;
	flood.unseenErrors = 41'000;
	flood.unseenWarnings = 1'000;
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, flood);
	EXPECT_EQ(ctx.GetTextString(built.parts.errors), "999+ errors");
	EXPECT_EQ(ctx.GetTextString(built.parts.warnings), "999+ warnings");
}

TEST(EditorChromeTest, OnlyFirstLineOfMessageReachesBar) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	NoticeBar built = BuildNoticeBar(ctx);

	ConsoleSummary summary;
	summary.newest = NoticeEntry(7, LogLevel::Error, "Build",
								 "expected ';' after expression\r\n    return x\n            ^");
	summary.newestUnseenError = summary.newest;
	summary.unseenErrors = 1;
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();

	EXPECT_EQ(ctx.GetTextString(built.parts.message), "[Build] expected ';' after expression");
	EXPECT_NEAR(ctx.GetLayout(built.parts.message).borderBox.h,
				UITest::MockFontSystem::LINE_HEIGHT, 0.01f);
}

TEST(EditorChromeTest, ALongMessageTruncatesInsteadOfWrapping) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(240, 100), &fonts);
	EditorTheme::ApplyToContext(ctx);
	NoticeBar built = BuildNoticeBar(ctx);

	ConsoleSummary summary;
	summary.newest = NoticeEntry(3, LogLevel::Info, "Assets",
								 "imported Assets/Environment/Rocks/Cliffs/LargeCliffFaceVariantB.glb");
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();

	EXPECT_EQ(ctx.GetComputedStyle(built.parts.message).textOverflow, UI::TextOverflow::Ellipsis);
	EXPECT_NEAR(ctx.GetLayout(built.parts.message).borderBox.h,
				UITest::MockFontSystem::LINE_HEIGHT, 0.01f);
	EXPECT_LE(ctx.GetLayout(built.parts.message).borderBox.w, 240.0f);
}

TEST(EditorChromeTest, ClickingTheNoticeAsksForTheConsole) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	int asked = 0;
	NoticeBar built = BuildNoticeBar(ctx, [&asked]() { ++asked; });

	ConsoleSummary summary;
	summary.newest = NoticeEntry(2, LogLevel::Warn, "Scene", "no camera in the loaded scenes");
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();

	ClickAt(ctx, CenterOf(ctx, built.parts.message));
	EXPECT_EQ(asked, 1);
}

TEST(EditorChromeTest, EmptyNoticeIsNotClickTarget) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	int asked = 0;
	NoticeBar built = BuildNoticeBar(ctx, [&asked]() { ++asked; });

	EditorChromeOps::SetConsoleNotice(ctx, built.parts, ConsoleSummary{});
	ctx.Update();

	EXPECT_TRUE(ctx.GetTextString(built.parts.message).empty());
	EXPECT_FALSE(ctx.GetStyle(built.parts.root).pointerEvents);
	ClickAt(ctx, CenterOf(ctx, built.parts.root));
	EXPECT_EQ(asked, 0);

	ConsoleSummary summary;
	summary.newest = NoticeEntry(1, LogLevel::Info, "Editor", "ready");
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();
	ClickAt(ctx, CenterOf(ctx, built.parts.message));
	EXPECT_EQ(asked, 1);
}

TEST(EditorChromeTest, StandingNoticeDownTakesPictureWithIt) {
	UITest::MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	EditorTheme::ApplyToContext(ctx);
	NoticeBar built = BuildNoticeBar(ctx);

	ConsoleSummary summary;
	summary.newest = NoticeEntry(4, LogLevel::Error, "Module", "module binary not found");
	summary.newestUnseenError = summary.newest;
	summary.unseenErrors = 1;
	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();
	ASSERT_TRUE(ctx.HasIcon(built.parts.icon));

	EditorChromeOps::SetConsoleNotice(ctx, built.parts, ConsoleSummary{});
	ctx.Update();
	EXPECT_FALSE(ctx.HasIcon(built.parts.icon));
	EXPECT_TRUE(ctx.GetTextString(built.parts.icon).empty());

	EditorChromeOps::SetConsoleNotice(ctx, built.parts, summary);
	ctx.Update();
	EXPECT_TRUE(ctx.HasIcon(built.parts.icon));
}

TEST(EditorChromeTest, ContextWithNoChromeIsAllClient) {
	UI::Context ctx(Desc(), nullptr);
	ctx.Update();

	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {200, 16}), HitTestResult::Client);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {0, 0}), HitTestResult::Client);
}

TEST(EditorChromeTest, ScrollTrackOwnsItsPixels) {
	UI::Context ctx(Desc(), nullptr);
	UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(ctx, ctx.GetRoot());
	UI::Style rootStyle = ctx.GetStyle(scroll.root);
	rootStyle.width = UI::Length::Percent(1.0f);
	rootStyle.height = UI::Length::Percent(1.0f);
	ctx.SetStyle(scroll.root, rootStyle);
	ctx.Update();

	const UI::Rect& track = ctx.GetLayout(scroll.track).borderBox;
	ASSERT_GT(track.w, 0.0f);
	const Vec2I onTrack{static_cast<i32>(track.x + track.w * 0.5f), static_cast<i32>(track.y + track.h * 0.5f)};
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, onTrack), HitTestResult::Content);

	const UI::Rect& viewport = ctx.GetLayout(scroll.viewport).borderBox;
	ASSERT_GT(viewport.w, 0.0f);
	const Vec2I beside{static_cast<i32>(viewport.x + viewport.w * 0.5f),
					   static_cast<i32>(viewport.y + viewport.h * 0.5f)};
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, beside), HitTestResult::Client);
}

TEST(EditorChromeTest, EdgeControlParentNotPromoted) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle pane = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), pane);
	UI::Style paneStyle;
	paneStyle.width = UI::Length::Pixel(200.0f);
	paneStyle.height = UI::Length::Pixel(100.0f);
	paneStyle.justifyContent = UI::Align::End;
	ctx.SetStyle(pane, paneStyle);

	UI::ElementHandle grip = ctx.CreateElement();
	ctx.AppendChild(pane, grip);
	ctx.AddClass(grip, StringHash(UI::Widgets::EdgeControlClass));
	UI::Style gripStyle;
	gripStyle.width = UI::Length::Pixel(10.0f);
	gripStyle.height = UI::Length::Percent(1.0f);
	ctx.SetStyle(grip, gripStyle);
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(grip).borderBox;
	ASSERT_GT(box.w, 0.0f);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(
				  ctx, {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)}),
			  HitTestResult::Content);
	EXPECT_EQ(EditorChromeOps::HitTestChrome(ctx, {20, 50}), HitTestResult::Client);
}
