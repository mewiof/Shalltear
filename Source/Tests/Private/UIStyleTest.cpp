#include "TestsPCH.h"

#include "UIStyleVocabulary.h"
#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

} // anonymous namespace

TEST(UIStyleTest, ParsesSelectorsAndSortsBySpecificity) {
	UI::StyleSheet sheet;
	sheet.Parse(R"css(
		#main { width: 10px; }
		button { width: 20px; }
		.wide { width: 30px; }
	)css");
	const auto& rules = sheet.GetRules();
	ASSERT_EQ(rules.size(), 3u);
	EXPECT_EQ(rules[0].targetTag, "button"_hash);
	EXPECT_EQ(rules[1].targetClass, "wide"_hash);
	EXPECT_EQ(rules[2].targetId, "main"_hash);
}

TEST(UIStyleTest, AppliesPropertiesToMatchingElements) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		.panel {
			width: 120px;
			height: 50%;
			padding: 4 8;
			background-color: #ff0000;
			border-radius: 3 4 5 6;
			opacity: 0.5;
			overflow: hidden;
			flex-direction: column;
		}
	)css");

	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "panel"_hash);
	const UI::Style& computed = ctx.GetComputedStyle(element);
	EXPECT_EQ(computed.width, UI::Length::Pixel(120.0f));
	EXPECT_EQ(computed.height, UI::Length::Percent(0.5f));
	EXPECT_EQ(computed.padding, (UI::Edges{8.0f, 4.0f, 8.0f, 4.0f}));
	EXPECT_EQ(computed.background.type, UI::BrushType::Solid);
	EXPECT_NEAR(computed.background.solid.r, 1.0f, 0.005f);
	EXPECT_NEAR(computed.background.solid.g, 0.0f, 0.005f);
	EXPECT_EQ(computed.borderRadius, (UI::Radii{3.0f, 4.0f, 5.0f, 6.0f}));
	EXPECT_NEAR(computed.opacity, 0.5f, 0.001f);
	EXPECT_EQ(computed.overflow, UI::Overflow::Hidden);
	EXPECT_EQ(computed.flexDirection, UI::FlexDirection::Column);
}

TEST(UIStyleTest, EdgeShorthandUsesCssOrder) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		.four { padding: 1 2 3 4; }
		.three { padding: 1 2 3; }
		.one { padding: 5; }
	)css");

	auto paddingOf = [&ctx](StringHash cls) {
		UI::ElementHandle element = ctx.CreateElement();
		ctx.AddClass(element, cls);
		return ctx.GetComputedStyle(element).padding;
	};
	EXPECT_EQ(paddingOf("four"_hash), (UI::Edges{4.0f, 1.0f, 2.0f, 3.0f}));
	EXPECT_EQ(paddingOf("three"_hash), (UI::Edges{2.0f, 1.0f, 2.0f, 3.0f}));
	EXPECT_EQ(paddingOf("one"_hash), UI::Edges::All(5.0f));
}

TEST(UIStyleTest, TagAndIdSelectorsMatch) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		label { opacity: 0.25; }
		#unique { opacity: 0.75; }
	)css");

	UI::ElementHandle tagged = ctx.CreateElement();
	ctx.SetTypeTag(tagged, "label"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(tagged).opacity, 0.25f, 0.001f);

	UI::ElementHandle identified = ctx.CreateElement();
	ctx.SetId(identified, "unique"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(identified).opacity, 0.75f, 0.001f);

	UI::ElementHandle both = ctx.CreateElement();
	ctx.SetTypeTag(both, "label"_hash);
	ctx.SetId(both, "unique"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(both).opacity, 0.75f, 0.001f);
}

TEST(UIStyleTest, LetterSpacingParsesIncludingNegativeTracking) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		.wide { letter-spacing: 1.5; }
		.tight { letter-spacing: -0.5; }
	)css");

	UI::ElementHandle wide = ctx.CreateElement();
	ctx.AddClass(wide, "wide"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(wide).letterSpacing, 1.5f, 0.001f);

	UI::ElementHandle tight = ctx.CreateElement();
	ctx.AddClass(tight, "tight"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(tight).letterSpacing, -0.5f, 0.001f);

	UI::ElementHandle untouched = ctx.CreateElement();
	EXPECT_NEAR(ctx.GetComputedStyle(untouched).letterSpacing, 0.0f, 0.001f);
}

TEST(UIStyleTest, TextOverflowParsesAll3) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		.trim { text-overflow: ellipsis; }
		.cut { text-overflow: clip; }
		.trim.plain { text-overflow: normal; }
	)css");

	UI::ElementHandle trimmed = ctx.CreateElement();
	ctx.AddClass(trimmed, "trim"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(trimmed).textOverflow, UI::TextOverflow::Ellipsis);

	UI::ElementHandle cut = ctx.CreateElement();
	ctx.AddClass(cut, "cut"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(cut).textOverflow, UI::TextOverflow::Clip);

	UI::ElementHandle untouched = ctx.CreateElement();
	EXPECT_EQ(ctx.GetComputedStyle(untouched).textOverflow, UI::TextOverflow::Wrap);

	ctx.RemoveClass(trimmed, "trim"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(trimmed).textOverflow, UI::TextOverflow::Wrap);
}

TEST(UIStyleTest, UniversalSelectorLosesToAll) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		* { text-outline-width: 1; opacity: 0.5; }
		label { opacity: 0.25; }
		.loud { opacity: 0.75; }
	)css");

	UI::ElementHandle bare = ctx.CreateElement();
	EXPECT_NEAR(ctx.GetComputedStyle(bare).textOutlineWidth, 1.0f, 0.001f);
	EXPECT_NEAR(ctx.GetComputedStyle(bare).opacity, 0.5f, 0.001f);

	UI::ElementHandle tagged = ctx.CreateElement();
	ctx.SetTypeTag(tagged, "label"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(tagged).opacity, 0.25f, 0.001f);
	EXPECT_NEAR(ctx.GetComputedStyle(tagged).textOutlineWidth, 1.0f, 0.001f);

	UI::ElementHandle classed = ctx.CreateElement();
	ctx.AddClass(classed, "loud"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(classed).opacity, 0.75f, 0.001f);
}

TEST(UIStyleTest, ComputedStyleRevertsWhenClassesLeave) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".hot { opacity: 0.1; background-color: #00ff00; }");

	UI::ElementHandle element = ctx.CreateElement();
	UI::Style inlineStyle;
	inlineStyle.opacity = 0.9f;
	inlineStyle.background = UI::Brush::SolidColor(Color(0.2f, 0.2f, 0.2f, 1.0f));
	ctx.SetStyle(element, inlineStyle);

	EXPECT_NEAR(ctx.GetComputedStyle(element).opacity, 0.9f, 0.001f);

	ctx.AddClass(element, "hot"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(element).opacity, 0.1f, 0.001f);
	EXPECT_NEAR(ctx.GetComputedStyle(element).background.solid.g, 1.0f, 0.005f);

	ctx.RemoveClass(element, "hot"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(element).opacity, 0.9f, 0.001f);
	EXPECT_NEAR(ctx.GetComputedStyle(element).background.solid.g, 0.2f, 0.005f);
}

TEST(UIStyleTest, HoverPseudoAppliesAndReverts) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".button { opacity: 1.0; } .button:hover { opacity: 0.5; }");

	UI::ElementHandle button = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), button);
	ctx.SetStyle(button, FixedBox(100.0f, 50.0f));
	ctx.AddClass(button, "button"_hash);
	ctx.Update();
	EXPECT_NEAR(ctx.GetComputedStyle(button).opacity, 1.0f, 0.001f);

	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {50, 25};
	ctx.InjectInput(move);
	ctx.Update();
	EXPECT_EQ(ctx.GetHovered(), button);
	EXPECT_NEAR(ctx.GetComputedStyle(button).opacity, 0.5f, 0.001f);

	move.mousePosPx = {350, 250};
	ctx.InjectInput(move);
	ctx.Update();
	EXPECT_NEAR(ctx.GetComputedStyle(button).opacity, 1.0f, 0.001f);
}

namespace {

	constexpr const char* PART_SHEET =
		".part { opacity: 1.0; } .part:hover { opacity: 0.5; } .part:active { opacity: 0.25; } .part:focus { opacity: 0.1; }";

	struct ControlRig {
		UI::Context ctx{Desc(), nullptr};
		UI::ElementHandle control;

		ControlRig() {
			ctx.LoadStyleSheet(PART_SHEET);
			control = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 50.0f));
		}

		UI::ElementHandle Part(UI::ElementHandle parent, bool inherits) {
			UI::ElementHandle part = MakeChild(ctx, parent, FixedBox(20.0f, 20.0f));
			ctx.AddClass(part, "part"_hash);
			ctx.SetHoverable(part, false);
			ctx.SetInheritsPointerState(part, inherits);
			return part;
		}

		void Send(InputEventType type, i32 x, i32 y) {
			InputEvent event{};
			event.type = type;
			event.button = MouseButton::Left;
			event.mousePosPx = {x, y};
			ctx.InjectInput(event);
			ctx.Update();
		}
		void Over() { Send(InputEventType::MouseMove, 60, 25); }
		void Away() { Send(InputEventType::MouseMove, 350, 250); }
		f32 Opacity(UI::ElementHandle element) { return ctx.GetComputedStyle(element).opacity; }
	};

} // anonymous namespace

TEST(UIStyleTest, PartTakesControlStates) {
	ControlRig rig;
	UI::ElementHandle part = rig.Part(rig.control, true);
	UI::ElementHandle plain = rig.Part(rig.control, false);
	rig.ctx.Update();
	EXPECT_NEAR(rig.Opacity(part), 1.0f, 0.001f);

	rig.Over();
	ASSERT_EQ(rig.ctx.GetHovered(), rig.control);
	EXPECT_NEAR(rig.Opacity(part), 0.5f, 0.001f);
	EXPECT_NEAR(rig.Opacity(plain), 1.0f, 0.001f);

	rig.Send(InputEventType::MouseDown, 60, 25);
	EXPECT_NEAR(rig.Opacity(part), 0.25f, 0.001f);
	EXPECT_NEAR(rig.Opacity(plain), 1.0f, 0.001f);

	rig.Send(InputEventType::MouseUp, 60, 25);
	EXPECT_NEAR(rig.Opacity(part), 0.5f, 0.001f);

	rig.Away();
	EXPECT_NEAR(rig.Opacity(part), 1.0f, 0.001f);
}

TEST(UIStyleTest, PartKeepsPressAfterLeave) {
	ControlRig rig;
	UI::ElementHandle part = rig.Part(rig.control, true);
	rig.Over();
	rig.Send(InputEventType::MouseDown, 60, 25);
	rig.Away();
	EXPECT_NE(rig.ctx.GetHovered(), rig.control);
	EXPECT_NEAR(rig.Opacity(part), 0.25f, 0.001f);

	rig.Send(InputEventType::MouseUp, 350, 250);
	EXPECT_NEAR(rig.Opacity(part), 1.0f, 0.001f);
}

TEST(UIStyleTest, PointerStateFollowsInheritance) {
	ControlRig rig;
	UI::ElementHandle middle = MakeChild(rig.ctx, rig.control, FixedBox(30.0f, 30.0f));
	rig.ctx.SetHoverable(middle, false);
	UI::ElementHandle leaf = rig.Part(middle, true);

	rig.Over();
	ASSERT_EQ(rig.ctx.GetHovered(), rig.control);
	EXPECT_NEAR(rig.Opacity(leaf), 1.0f, 0.001f);

	rig.ctx.SetInheritsPointerState(middle, true);
	EXPECT_NEAR(rig.Opacity(leaf), 0.5f, 0.001f);

	rig.ctx.SetInheritsPointerState(middle, false);
	EXPECT_NEAR(rig.Opacity(leaf), 1.0f, 0.001f);
}

TEST(UIStyleTest, HoverablePartAnswersBoth) {
	ControlRig rig;
	UI::ElementHandle grip = rig.Part(rig.control, true);
	rig.ctx.SetHoverable(grip, true);
	rig.ctx.Update();

	rig.Send(InputEventType::MouseMove, 10, 10);
	ASSERT_EQ(rig.ctx.GetHovered(), grip);
	EXPECT_NEAR(rig.Opacity(grip), 0.5f, 0.001f);

	rig.Over();
	ASSERT_EQ(rig.ctx.GetHovered(), rig.control);
	EXPECT_NEAR(rig.Opacity(grip), 0.5f, 0.001f);

	rig.Away();
	EXPECT_NEAR(rig.Opacity(grip), 1.0f, 0.001f);
}

TEST(UIStyleTest, PartDoesNotWearControlsFocus) {
	ControlRig rig;
	UI::ElementHandle part = rig.Part(rig.control, true);
	rig.ctx.SetFocusable(rig.control, true);
	rig.ctx.SetFocus(rig.control);
	ASSERT_EQ(rig.ctx.GetFocused(), rig.control);
	EXPECT_NEAR(rig.Opacity(part), 1.0f, 0.001f);
}

TEST(UIStyleTest, PartMovedUnderEngagedControlTakesState) {
	ControlRig rig;
	UI::ElementHandle part = rig.Part(rig.ctx.GetRoot(), true);
	rig.Over();
	ASSERT_EQ(rig.ctx.GetHovered(), rig.control);
	EXPECT_NEAR(rig.Opacity(part), 1.0f, 0.001f);

	rig.ctx.AppendChild(rig.control, part);
	EXPECT_NEAR(rig.Opacity(part), 0.5f, 0.001f);
}

TEST(UIStyleTest, BoxShadowListParses) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".shadowed { box-shadow: 0 4px 10px #00000080, inset 0 1px 0 #ffffff20; }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "shadowed"_hash);
	const UI::Style& computed = ctx.GetComputedStyle(element);
	ASSERT_EQ(computed.boxShadowCount, 2u);
	EXPECT_NEAR(computed.boxShadows[0].y, 4.0f, 0.001f);
	EXPECT_NEAR(computed.boxShadows[0].blur, 10.0f, 0.001f);
	EXPECT_FALSE(computed.boxShadows[0].inset);
	EXPECT_TRUE(computed.boxShadows[1].inset);
	EXPECT_NEAR(computed.boxShadows[1].y, 1.0f, 0.001f);
}

TEST(UIStyleTest, MalformedInputIsTolerated) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		/* comment */ .ok { unknown-prop: whatever; width: 42px; broken decl }
		.unterminated { height: 7px;
	)css");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "ok"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).width, UI::Length::Pixel(42.0f));

	UI::ElementHandle other = ctx.CreateElement();
	ctx.AddClass(other, "unterminated"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(other).height, UI::Length::Pixel(7.0f));
}

TEST(UIStyleTest, CursorAndPointerEventsParse) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".ghost { cursor: pointer; pointer-events: none; }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "ghost"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).cursor, SystemCursor::Hand);
	EXPECT_FALSE(ctx.GetComputedStyle(element).pointerEvents);
}

TEST(UIStyleTest, FilterSpellsDropShadow) {
	auto apply = [](std::string_view value) {
		UI::Style style;
		EXPECT_TRUE(UI::ApplyStyleDeclaration(style, StringHash("filter"), value, nullptr));
		return style.dropShadow;
	};
	EXPECT_EQ(apply("drop-shadow(1px 2px 3px #00000080)"), (UI::DropShadow{1.0f, 2.0f, 3.0f, Color::FromHex("#00000080")}));
	EXPECT_EQ(apply("drop-shadow(#ff000080 4 5)"), (UI::DropShadow{4.0f, 5.0f, 0.0f, Color::FromHex("#ff000080")}));
	EXPECT_EQ(apply("drop-shadow(4 5)").color, Color::Black());
	EXPECT_EQ(apply("none"), UI::DropShadow{});
	EXPECT_EQ(apply("blur(5px)"), UI::DropShadow{});
	EXPECT_EQ(apply("drop-shadow(4)"), UI::DropShadow{});

	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".lift { filter: drop-shadow(0 4 12 #00000066); }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "lift"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).dropShadow, (UI::DropShadow{0.0f, 4.0f, 12.0f, Color::FromHex("#00000066")}));
	ctx.RemoveClass(element, "lift"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).dropShadow, UI::DropShadow{});
}

TEST(UIStyleTest, FilterReadsBackAsFieldHoldsIt) {
	UI::Style style;
	style.dropShadow = UI::DropShadow{3.0f, -2.0f, 0.0f, Color(0.0f, 0.0f, 0.0f, 0.0f)};
	char buffer[UI::MAX_STYLE_VALUE_TEXT];
	std::string_view text = UI::ReadStyleProperty(style, StringHash("filter"), buffer, sizeof(buffer));
	UI::Style back;
	ASSERT_TRUE(UI::ApplyStyleDeclaration(back, StringHash("filter"), text, nullptr));
	EXPECT_EQ(back.dropShadow, style.dropShadow) << text << "'";

	UI::Style none;
	EXPECT_EQ(UI::ReadStyleProperty(none, StringHash("filter"), buffer, sizeof(buffer)), "none");
}

static_assert(sizeof(UI::Style) == 552, "Style changed shape. See UIStyleVocabulary.h");

TEST(UIStyleTest, EveryStyleFieldHasAStylesheetSpelling) {
	for (const StyleVocabulary::Entry& entry : StyleVocabulary::All) {
		UI::StyleSheet sheet;
		sheet.Parse(str(".p { ") + str(entry.declaration) + str("; }"));
		ASSERT_EQ(sheet.GetRules().size(), 1u) << entry.field;

		UI::Style parsed;
		UI::StyleSheet::ApplyRule(parsed, sheet.GetRules()[0]);

		UI::Style expected;
		entry.mutate(expected);

		EXPECT_NE(expected, UI::Style{}) << entry.field;
		EXPECT_EQ(parsed, expected) << entry.field;
	}
}

TEST(UIStyleTest, LaterEqualSpecificityRuleWins) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".a { opacity: 0.3; } .a { opacity: 0.6; }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "a"_hash);
	EXPECT_NEAR(ctx.GetComputedStyle(element).opacity, 0.6f, 0.001f);
}

TEST(UIStyleTest, VarResolvesColorsLengthsAndShadowLists) {
	UI::Context ctx(Desc(), nullptr);
	ctx.SetStyleVar("--tone"_hash, "#204080");
	ctx.SetStyleVar("--gap"_hash, "17");
	ctx.SetStyleVar("--rim"_hash, "#ffffff20");
	ctx.SetStyleVar("--depth"_hash, "#00000050");
	ctx.LoadStyleSheet(".themed { background-color: var(--tone); width: var(--gap); box-shadow: inset 0 0 0 1 var(--rim), 0 2 0 var(--depth); }");

	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "themed"_hash);
	const UI::Style& computed = ctx.GetComputedStyle(element);
	EXPECT_EQ(computed.background.solid, Color::FromHex("#204080"));
	EXPECT_EQ(computed.width, UI::Length::Pixel(17.0f));
	ASSERT_EQ(computed.boxShadowCount, 2u);
	EXPECT_TRUE(computed.boxShadows[0].inset);
	EXPECT_EQ(computed.boxShadows[0].color, Color::FromHex("#ffffff20"));
	EXPECT_FALSE(computed.boxShadows[1].inset);
	EXPECT_EQ(computed.boxShadows[1].color, Color::FromHex("#00000050"));
}

TEST(UIStyleTest, VarValueMayReferenceAnotherVar) {
	UI::Context ctx(Desc(), nullptr);
	ctx.SetStyleVar("--base"_hash, "#123456");
	ctx.SetStyleVar("--alias"_hash, "var(--base)");
	ctx.LoadStyleSheet(".aliased { background-color: var(--alias); }");

	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "aliased"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).background.solid, Color::FromHex("#123456"));
}

TEST(UIStyleTest, UndefinedVarIsMagenta) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(".broken { background-color: var(--nope); }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "broken"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).background.solid, Color::Magenta());
}

TEST(UIStyleTest, CyclicVarTerminatesInsteadOfHanging) {
	UI::Context ctx(Desc(), nullptr);
	ctx.SetStyleVar("--a"_hash, "var(--b)");
	ctx.SetStyleVar("--b"_hash, "var(--a)");
	ctx.LoadStyleSheet(".cycle { background-color: var(--a); }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "cycle"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(element).background.solid, Color::Magenta());
}

TEST(UIStyleTest, VarChangeRestylesLive) {
	UI::Context ctx(Desc(), nullptr);
	ctx.SetStyleVar("--accent"_hash, "#0078d7");
	ctx.LoadStyleSheet(".accented { background-color: var(--accent); }");
	UI::ElementHandle element = ctx.CreateElement();
	ctx.AddClass(element, "accented"_hash);
	ASSERT_EQ(ctx.GetComputedStyle(element).background.solid, Color::FromHex("#0078d7"));

	ctx.SetStyleVar("--accent"_hash, "#c04040");
	EXPECT_EQ(ctx.GetComputedStyle(element).background.solid, Color::FromHex("#c04040"));
}

TEST(UIStyleTest, DefaultStyleVarsSeedEveryContext) {
	UI::Context ctx(Desc(), nullptr);
	EXPECT_GT(ctx.GetStyleVars().GetCount(), 0u);
	EXPECT_TRUE(ctx.GetStyleVars().Has("--slt-accent"_hash));
	u32 checked = 0;
	for (const UI::StyleVarDesc& var : UI::GetDefaultStyleVars()) {
		EXPECT_TRUE(ctx.GetStyleVars().Has(StringHash(var.name))) << var.name;
		EXPECT_EQ(ctx.GetStyleVars().Get(StringHash(var.name)), var.value) << var.name << " arrived altered";
		++checked;
	}
	EXPECT_GT(checked, 0u);
	EXPECT_EQ(ctx.GetStyleVarColor("--not-a-token"_hash, Color::White()), Color::White());
	EXPECT_FLOAT_EQ(ctx.GetStyleVarFloat("--not-a-token"_hash, 5.0f), 5.0f);
}

TEST(UIStyleTest, SkinVarsInDefaultTable) {
	UI::StyleVars defaults;
	defaults.Set(UI::GetDefaultStyleVars());

	auto checkSheet = [&](std::string_view source, const char* what) {
		size_t cursor = 0;
		u32 checked = 0;
		while ((cursor = source.find("var(", cursor)) != std::string_view::npos) {
			size_t close = source.find(')', cursor);
			ASSERT_NE(close, std::string_view::npos) << what << ": unterminated var()";
			std::string_view name = source.substr(cursor + 4, close - cursor - 4);
			EXPECT_TRUE(defaults.Has(StringHash(name))) << what << " references undefined " << name;
			++checked;
			cursor = close + 1;
		}
		EXPECT_GT(checked, 0u) << what;
	};
	checkSheet(UI::Widgets::GetDefaultSkinSource(), "the widget skin");
	checkSheet(UI::DockManager::GetDefaultSkinSource(), "the dock skin");

	EXPECT_FALSE(defaults.Has("--slt-not-a-real-token"_hash));
}

TEST(UIStyleTest, BackgroundShorthandParsesGradientsAndPlainColors) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		.ramp  { background: linear-gradient(#ff000080, transparent); }
		.down  { background: linear-gradient(#00ff00ff, #0000ffff, to bottom); }
		.plain { background: #123456; }
		.token { background: linear-gradient(var(--probe), transparent); }
	)css");

	UI::ElementHandle ramp = ctx.CreateElement();
	ctx.AddClass(ramp, "ramp"_hash);
	const UI::Style& rampStyle = ctx.GetComputedStyle(ramp);
	EXPECT_EQ(rampStyle.background.type, UI::BrushType::LinearGradient);
	EXPECT_NEAR(rampStyle.background.gradient0.r, 1.0f, 0.005f);
	EXPECT_NEAR(rampStyle.background.gradient0.a, 0.502f, 0.005f);
	EXPECT_NEAR(rampStyle.background.gradient1.a, 0.0f, 0.005f);
	EXPECT_EQ(rampStyle.background.gradientDirection, (Vec2{1.0f, 0.0f}));

	UI::ElementHandle down = ctx.CreateElement();
	ctx.AddClass(down, "down"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(down).background.gradientDirection, (Vec2{0.0f, 1.0f}));

	UI::ElementHandle plain = ctx.CreateElement();
	ctx.AddClass(plain, "plain"_hash);
	EXPECT_EQ(ctx.GetComputedStyle(plain).background.type, UI::BrushType::Solid);
	EXPECT_NEAR(ctx.GetComputedStyle(plain).background.solid.b, 0.337f, 0.005f);

	ctx.SetStyleVar(StringHash("--probe"), "#00ff00ff");
	UI::ElementHandle token = ctx.CreateElement();
	ctx.AddClass(token, "token"_hash);
	const UI::Style& tokenStyle = ctx.GetComputedStyle(token);
	EXPECT_EQ(tokenStyle.background.type, UI::BrushType::LinearGradient);
	EXPECT_NEAR(tokenStyle.background.gradient0.g, 1.0f, 0.005f);
}

TEST(UIStyleTest, GradientAndRuleSwap) {
	UI::Context ctx(Desc(), nullptr);
	ctx.LoadStyleSheet(R"css(
		.tint { background: linear-gradient(#ff0000ff, transparent); }
		.row:hover { background-color: #333333ff; }
		.row.tint:hover { background: linear-gradient(#ff0000ff, transparent); }
	)css");

	UI::ElementHandle plainRow = ctx.CreateElement();
	ctx.AddClass(plainRow, "row"_hash);
	UI::ElementHandle tinted = ctx.CreateElement();
	ctx.AddClass(tinted, "row"_hash);
	ctx.AddClass(tinted, "tint"_hash);

	EXPECT_EQ(ctx.GetComputedStyle(plainRow).background.type, UI::BrushType::None);
	EXPECT_EQ(ctx.GetComputedStyle(tinted).background.type, UI::BrushType::LinearGradient);

	EXPECT_GT(ctx.GetComputedStyle(tinted).background.gradient0.r, 0.9f);
}

TEST(UIStyleTest, SweepPropertiesEnumerated) {
	std::span<const std::string_view> names = UI::GetStylePropertyNames();
	ASSERT_FALSE(names.empty());

	auto lists = [names](std::string_view name) {
		for (std::string_view candidate : names) {
			if (candidate == name) {
				return true;
			}
		}
		return false;
	};

	for (const StyleVocabulary::Entry& entry : StyleVocabulary::All) {
		std::string_view declaration = entry.declaration;
		std::string_view name = declaration.substr(0, declaration.find(':'));
		EXPECT_TRUE(lists(name));
	}

	for (std::string_view alias : {"background-color", "margin-left", "margin-top", "margin-right", "margin-bottom"}) {
		EXPECT_TRUE(lists(alias)) << "'" << alias;
	}
	EXPECT_EQ(names.size(), std::size(StyleVocabulary::All) + 5);

	EXPECT_FALSE(lists("font-family"));
	EXPECT_FALSE(lists("background-image"));
}

TEST(UIStyleTest, AliasesSweepCannotCoverReachFields) {
	auto apply = [](std::string_view declaration) {
		UI::StyleSheet sheet;
		sheet.Parse(str("*{") + str(declaration) + ";}");
		UI::Style style;
		if (!sheet.GetRules().empty()) {
			UI::StyleSheet::ApplyRule(style, sheet.GetRules()[0]);
		}
		return style;
	};

	EXPECT_EQ(apply("background-color: #ff0000").background.solid, Color(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_EQ(apply("background-color: #ff0000").background.type, UI::BrushType::Solid);
	EXPECT_FLOAT_EQ(apply("margin-left: 3px").margin.left, 3.0f);
	EXPECT_FLOAT_EQ(apply("margin-top: 4px").margin.top, 4.0f);
	EXPECT_FLOAT_EQ(apply("margin-right: 5px").margin.right, 5.0f);
	EXPECT_FLOAT_EQ(apply("margin-bottom: 6px").margin.bottom, 6.0f);

	UI::Style left = apply("margin-left: 3px");
	EXPECT_FLOAT_EQ(left.margin.top, 0.0f);
	EXPECT_FLOAT_EQ(left.margin.right, 0.0f);
	EXPECT_FLOAT_EQ(left.margin.bottom, 0.0f);

	EXPECT_FLOAT_EQ(apply("margin-start: 3px").margin.left, 0.0f);
}

TEST(UIStyleTest, SplitAndParseAgree) {
	constexpr const char* SOURCE = R"css(
/* a leading comment */
.row { padding: 8px; color: red }
#hud:hover {
	background-color: var(--slt-accent); /* trailing */
	opacity: 0.5;
}
panel.card { width: 10px; ; broken; height: 2px; }
)css";

	UI::StyleSheet parsed;
	parsed.Parse(SOURCE);
	Vector<UI::StyleRuleSource> split{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, split);

	ASSERT_EQ(split.size(), parsed.GetRules().size());
	ASSERT_EQ(split.size(), 3u);

	for (const UI::StyleRuleSource& rule : split) {
		UI::StyleSheet one;
		one.Parse(str(rule.selector) + "{}");
		ASSERT_EQ(one.GetRules().size(), 1u) << rule.selector;
		const UI::StyleRule& probe = one.GetRules()[0];

		const UI::StyleRule* match = nullptr;
		for (const UI::StyleRule& candidate : parsed.GetRules()) {
			if (candidate.targetTag == probe.targetTag && candidate.targetClass == probe.targetClass &&
				candidate.targetId == probe.targetId && candidate.pseudoState == probe.pseudoState) {
				match = &candidate;
			}
		}
		ASSERT_NE(match, nullptr) << rule.selector;
		EXPECT_EQ(rule.declarations.size(), match->properties.size()) << rule.selector;
		for (size_t i = 0; i < rule.declarations.size() && i < match->properties.size(); ++i) {
			EXPECT_EQ(StringHash(rule.declarations[i].name), match->properties[i].name);
			EXPECT_EQ(rule.declarations[i].value, match->properties[i].value);
		}
	}

	EXPECT_EQ(split[0].selector, ".row");
	EXPECT_EQ(split[1].selector, "#hud:hover");
	EXPECT_EQ(split[2].selector, "panel.card");
	EXPECT_EQ(split[2].declarations.size(), 2u);
}

TEST(UIStyleTest, RuleBodyMatchesDeclarationWalk) {
	constexpr std::string_view SOURCE = ".a { /* } */ color: red; }\n.empty {}\n.tail { color: red";

	Vector<UI::StyleRuleSource> split{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, split);
	ASSERT_EQ(split.size(), 3u);

	EXPECT_EQ(split[0].body, " /* } */ color: red; ");
	EXPECT_LT(SOURCE.find('}'), static_cast<size_t>(split[0].body.data() + split[0].body.size() - SOURCE.data()));

	EXPECT_TRUE(split[1].body.empty());
	ASSERT_NE(split[1].body.data(), nullptr);
	EXPECT_EQ(std::string_view(split[1].body.data(), 1), "}");

	EXPECT_EQ(split[2].body, " color: red");

	EXPECT_GE(split[0].body.data(), SOURCE.data());
	EXPECT_LE(split[2].body.data() + split[2].body.size(), SOURCE.data() + SOURCE.size());
}

TEST(UIStyleTest, CommentBraceOpensRule) {
	constexpr std::string_view SOURCE = ".row /* { */ { width: 10px; }";

	Vector<UI::StyleRuleSource> split{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitSource(SOURCE, split);
	ASSERT_EQ(split.size(), 1u);
	EXPECT_EQ(split[0].selector, ".row /*");
	EXPECT_EQ(split[0].body, " */ { width: 10px; ");

	UI::StyleSheet parsed;
	parsed.Parse(SOURCE);
	ASSERT_EQ(parsed.GetRules().size(), 1u);
	EXPECT_NE(parsed.GetRules()[0].targetClass, StringHash("row"));
}

TEST(UIStyleTest, DeclarationRunSpansTextIncludingSemicolon) {
	constexpr std::string_view FRAGMENT = "width: 10px; height: 4px";
	Vector<UI::StyleDeclarationSource> declarations{GetMemoryResource(MemTag::Debug)};
	UI::StyleSheet::SplitDeclarations(FRAGMENT, declarations);

	ASSERT_EQ(declarations.size(), 2u);
	EXPECT_EQ(declarations[0].name, "width");
	EXPECT_EQ(declarations[0].value, "10px");
	EXPECT_EQ(declarations[0].text, "width: 10px;");
	EXPECT_EQ(declarations[1].text, "height: 4px");

	EXPECT_EQ(declarations[0].text.data(), FRAGMENT.data());
	EXPECT_GE(declarations[1].text.data(), FRAGMENT.data());
	EXPECT_LE(declarations[1].text.data() + declarations[1].text.size(), FRAGMENT.data() + FRAGMENT.size());
}
