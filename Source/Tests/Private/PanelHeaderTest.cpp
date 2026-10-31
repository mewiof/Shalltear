#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Editor/EditorTheme.h"

#include "../../Editor/Private/Panels/PanelHeader.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {320, 200};
		desc.contentScale = 1.0f;
		return desc;
	}

	Vector<UI::ElementHandle> ToolButtons(UI::Context& context, UI::ElementHandle root) {
		Vector<UI::ElementHandle> out{GetMemoryResource(MemTag::Core)};
		for (u32 skip = 0;; ++skip) {
			UI::ElementHandle found = FindByClass(context, root, EditorTheme::Class::ToolButton, skip);
			if (!found.IsValid()) {
				break;
			}
			out.push_back(found);
		}
		return out;
	}

} // anonymous namespace

TEST(PanelHeaderTest, CallbacklessControlNotBuilt) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	PanelHeader::Desc empty;
	PanelHeader::Parts bare = PanelHeader::Build(context, context.GetRoot(), empty);
	context.Update();
	ASSERT_TRUE(bare.root.IsValid());
	EXPECT_TRUE(context.HasClass(bare.root, EditorTheme::Class::PanelHeader));
	EXPECT_EQ(ToolButtons(context, bare.root).size(), 0u);
	EXPECT_FALSE(bare.searchField.IsValid());

	PanelHeader::Desc addOnly;
	addOnly.onAdd = []() {};
	PanelHeader::Parts one = PanelHeader::Build(context, context.GetRoot(), addOnly);
	context.Update();
	EXPECT_EQ(ToolButtons(context, one.root).size(), 1u);
	EXPECT_FALSE(one.searchField.IsValid());

	PanelHeader::Desc searchOnly;
	searchOnly.onSearchChanged = [](std::string_view) {};
	PanelHeader::Parts other = PanelHeader::Build(context, context.GetRoot(), searchOnly);
	context.Update();
	EXPECT_EQ(ToolButtons(context, other.root).size(), 1u);
}

namespace {

	UI::ElementHandle DropOf(UI::Context& context, UI::ElementHandle strip) {
		return FindByClass(context, strip, EditorTheme::Class::PanelHeaderDrop, 0);
	}

	void ClickAt(UI::Context& context, i32 x, i32 y) {
		for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
			InputEvent event{};
			event.type = type;
			event.button = MouseButton::Left;
			event.mousePosPx = {x, y};
			context.InjectInput(event);
		}
		context.Update();
	}

} // anonymous namespace

TEST(PanelHeaderTest, StripShadowOverlaysContent) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);
	EditorTheme::ApplyToContext(context);

	UI::Style column;
	column.flexDirection = UI::FlexDirection::Column;
	column.width = UI::Length::Pixel(320.0f);
	UI::ElementHandle host = MakeChild(context, context.GetRoot(), column);

	UI::ElementHandle strip = PanelHeader::BuildStrip(context, host);
	UI::ElementHandle body = MakeChild(context, host, FixedBox(320.0f, 60.0f));
	u32 bodyClicks = 0;
	context.Events(body).onClick = [&bodyClicks]() { ++bodyClicks; };
	context.Update();

	const UI::Style& style = context.GetComputedStyle(strip);
	EXPECT_TRUE(style.background.type == UI::BrushType::None ||
				(style.background.type == UI::BrushType::Solid && style.background.solid.a == 0.0f));
	for (u8 i = 0; i < style.boxShadowCount; ++i) {
		EXPECT_TRUE(style.boxShadows[i].inset);
	}

	UI::ElementHandle drop = DropOf(context, strip);
	ASSERT_TRUE(context.IsAlive(drop));
	const UI::Brush& ramp = context.GetComputedStyle(drop).background;
	ASSERT_EQ(ramp.type, UI::BrushType::LinearGradient);
	EXPECT_GT(ramp.gradient0.a, 0.0f);
	EXPECT_EQ(ramp.gradient1.a, 0.0f);
	EXPECT_GT(ramp.gradientDirection.y, 0.0f);

	EXPECT_GT(style.zIndex, context.GetComputedStyle(body).zIndex);
	const UI::Rect stripBox = context.GetLayout(strip).borderBox;
	const UI::Rect dropBox = context.GetLayout(drop).borderBox;
	const UI::Rect bodyBox = context.GetLayout(body).borderBox;
	EXPECT_NEAR(bodyBox.y, stripBox.y + stripBox.h, 0.5f);
	EXPECT_GT(dropBox.h, 0.0f);
	EXPECT_NEAR(dropBox.y, bodyBox.y, 0.5f);

	ClickAt(context, static_cast<i32>(dropBox.x + dropBox.w * 0.5f), static_cast<i32>(dropBox.y + dropBox.h * 0.5f));
	EXPECT_EQ(bodyClicks, 1u);
}

TEST(PanelHeaderTest, ShadowIsDropOnly) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);
	EditorTheme::ApplyToContext(context);

	UI::Style column;
	column.flexDirection = UI::FlexDirection::Column;
	column.width = UI::Length::Pixel(320.0f);
	UI::Style inset = column;
	inset.padding = {40.0f, 40.0f, 40.0f, 40.0f};
	UI::ElementHandle frame = MakeChild(context, context.GetRoot(), inset);
	UI::ElementHandle strip = PanelHeader::BuildStrip(context, frame);
	context.Update();

	const UI::Rect stripBox = context.GetLayout(strip).borderBox;
	UI::RenderList list;
	context.BuildRenderList(list);
	ASSERT_FALSE(list.quads.empty());

	bool below = false;
	for (const UI::UIQuad& quad : list.quads) {
		const UI::Rect& ink = quad.rect;
		EXPECT_GE(ink.y, stripBox.y - 0.01f) << stripBox.y - ink.y;
		EXPECT_GE(ink.x, stripBox.x - 0.01f);
		EXPECT_LE(ink.x + ink.w, stripBox.x + stripBox.w + 0.01f);
		if (ink.y + 0.01f >= stripBox.y + stripBox.h) {
			below = true;
			EXPECT_NEAR(ink.x, stripBox.x, 0.01f);
			EXPECT_NEAR(ink.x + ink.w, stripBox.x + stripBox.w, 0.01f);
		}
	}
	EXPECT_TRUE(below);
}

TEST(PanelHeaderTest, RefillingAStripKeepsItsDrop) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);
	EditorTheme::ApplyToContext(context);

	UI::ElementHandle strip = PanelHeader::BuildStrip(context, context.GetRoot());
	PanelHeader::ToolButtonDesc button;
	button.fallback = "Lock";
	button.onClick = []() {};
	PanelHeader::ToolButton(context, strip, button);
	PanelHeader::ToolButton(context, strip, button);
	context.Update();
	ASSERT_TRUE(context.IsAlive(DropOf(context, strip)));

	PanelHeader::ClearStrip(context, strip);
	context.Update();
	EXPECT_TRUE(context.IsAlive(DropOf(context, strip)));
	EXPECT_EQ(ToolButtons(context, strip).size(), 0u);
}

TEST(PanelHeaderTest, AddRunsSearchOpens) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	u32 added = 0;
	i32 toggles = 0;
	bool open = false;
	PanelHeader::Desc desc;
	desc.onAdd = [&added]() { ++added; };
	desc.onSearchChanged = [](std::string_view) {};
	desc.onSearchToggled = [&toggles, &open](bool value) {
		++toggles;
		open = value;
	};
	PanelHeader::Parts parts = PanelHeader::Build(context, context.GetRoot(), desc);
	context.Update();

	Vector<UI::ElementHandle> buttons = ToolButtons(context, parts.root);
	ASSERT_EQ(buttons.size(), 2u);

	ClickElement(context, buttons[0]);
	EXPECT_EQ(added, 1u);
	EXPECT_EQ(toggles, 0);

	ClickElement(context, buttons[1]);
	EXPECT_EQ(toggles, 1);
	EXPECT_TRUE(open);
	EXPECT_EQ(added, 1u);
}

TEST(PanelHeaderTest, MagnifierAtFarEnd) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	auto magnifierRight = [&](bool searchOpen) {
		PanelHeader::Desc desc;
		desc.onAdd = []() {};
		desc.onSearchChanged = [](std::string_view) {};
		desc.onSearchToggled = [](bool) {};
		desc.searchOpen = searchOpen;
		desc.searchText = "probe";
		PanelHeader::Parts parts = PanelHeader::Build(context, context.GetRoot(), desc);
		context.Update();
		Vector<UI::ElementHandle> buttons = ToolButtons(context, parts.root);
		EXPECT_EQ(buttons.size(), 2u);
		const UI::Rect strip = context.GetLayout(parts.root).borderBox;
		const UI::Rect glass = context.GetLayout(buttons.back()).borderBox;
		return (strip.x + strip.w) - (glass.x + glass.w);
	};

	const f32 closedGap = magnifierRight(false);
	const f32 openGap = magnifierRight(true);
	EXPECT_LT(closedGap, 6.0f);
	EXPECT_NEAR(openGap, closedGap, 0.5f);
}

TEST(PanelHeaderTest, SearchReportsKeystrokes) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	str seen;
	u32 changes = 0;
	PanelHeader::Desc desc;
	desc.searchOpen = true;
	desc.searchText = "cam";
	desc.onSearchChanged = [&seen, &changes](std::string_view text) {
		seen = str(text);
		++changes;
	};
	desc.onSearchToggled = [](bool) {};
	PanelHeader::Parts parts = PanelHeader::Build(context, context.GetRoot(), desc);
	context.Update();

	ASSERT_TRUE(parts.searchField.IsValid());
	EXPECT_EQ(context.GetTextString(parts.searchField), "cam");

	context.SetFocus(parts.searchField);
	InputEvent typed{};
	typed.type = InputEventType::Text;
	typed.codepoint = 'e';
	context.InjectInput(typed);
	context.Update();
	EXPECT_EQ(changes, 1u);
	EXPECT_EQ(seen, "e");
	EXPECT_EQ(context.GetTextString(parts.searchField), "e");
}

TEST(PanelHeaderTest, ClosingSearchClearsQueryItWasHolding) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	str seen = "stale";
	bool open = true;
	PanelHeader::Desc desc;
	desc.searchOpen = true;
	desc.searchText = "stale";
	desc.onAdd = []() {};
	desc.onSearchChanged = [&seen](std::string_view text) { seen = str(text); };
	desc.onSearchToggled = [&open](bool value) { open = value; };
	PanelHeader::Parts parts = PanelHeader::Build(context, context.GetRoot(), desc);
	context.Update();

	Vector<UI::ElementHandle> buttons = ToolButtons(context, parts.root);
	ASSERT_EQ(buttons.size(), 2u);
	ClickElement(context, buttons.back());

	EXPECT_FALSE(open);
	EXPECT_EQ(seen, "");
}

TEST(PanelHeaderTest, EscapeClosesAndClears) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	str seen = "stale";
	bool open = true;
	PanelHeader::Desc desc;
	desc.searchOpen = true;
	desc.searchText = "stale";
	desc.onSearchChanged = [&seen](std::string_view text) { seen = str(text); };
	desc.onSearchToggled = [&open](bool value) { open = value; };
	PanelHeader::Parts parts = PanelHeader::Build(context, context.GetRoot(), desc);
	context.Update();
	ASSERT_TRUE(parts.searchField.IsValid());

	context.SetFocus(parts.searchField);
	InputEvent escape{};
	escape.type = InputEventType::KeyDown;
	escape.key = Key::Escape;
	context.InjectInput(escape);
	context.Update();

	EXPECT_FALSE(open);
	EXPECT_EQ(seen, "");
}

TEST(PanelHeaderTest, FieldFocusOnlyWhenAsked) {
	MockFontSystem font;
	UI::Context context(Desc(), &font);

	PanelHeader::Desc desc;
	desc.searchOpen = true;
	desc.searchText = "cam";
	desc.onSearchChanged = [](std::string_view) {};
	desc.onSearchToggled = [](bool) {};

	PanelHeader::Parts quiet = PanelHeader::Build(context, context.GetRoot(), desc);
	context.Update();
	EXPECT_NE(context.GetFocused(), quiet.searchField);

	desc.focusSearch = true;
	PanelHeader::Parts opened = PanelHeader::Build(context, context.GetRoot(), desc);
	context.Update();
	EXPECT_EQ(context.GetFocused(), opened.searchField);
}
