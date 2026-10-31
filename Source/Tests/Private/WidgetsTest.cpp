#include "TestsPCH.h"

#include "UITestUtil.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	InputEvent MouseMove(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MouseDown(i32 x, i32 y, KeyMod mods = KeyMod::None) {
		InputEvent event{};
		event.type = InputEventType::MouseDown;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		event.mods = mods;
		return event;
	}
	InputEvent MouseUp(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseUp;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent KeyPress(Key key, KeyMod mods = KeyMod::None) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = key;
		event.mods = mods;
		return event;
	}
	InputEvent TextChar(char32_t codepoint) {
		InputEvent event{};
		event.type = InputEventType::Text;
		event.codepoint = codepoint;
		return event;
	}

	void Click(UI::Context& ctx, Vec2 point, KeyMod mods = KeyMod::None) {
		ctx.InjectInput(MouseDown(static_cast<i32>(point.x), static_cast<i32>(point.y), mods));
		ctx.InjectInput(MouseUp(static_cast<i32>(point.x), static_cast<i32>(point.y)));
		ctx.Update();
	}

	Vec2 Center(UI::Context& ctx, UI::ElementHandle handle) {
		const UI::Rect& box = ctx.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	bool TickIsVisible(UI::Context& ctx, UI::ElementHandle row) {
		UI::ElementHandle gutter = NthChildOf(ctx, row, 0);
		UI::ElementHandle tick = NthChildOf(ctx, gutter, 0);
		return ctx.IsAlive(tick) && ctx.GetStyle(tick).display != UI::Display::None;
	}

	std::pair<UI::Rect, UI::Rect> PaintedGlyphSpan(UI::Context& ctx) {
		UI::RenderList list;
		ctx.BuildRenderList(list);
		UI::Rect first{};
		UI::Rect last{};
		for (const UI::UIQuad& quad : list.quads) {
			if ((quad.flags & UI::QUAD_FLAG_GLYPH) == 0u) {
				continue;
			}
			if (first.w == 0.0f) {
				first = quad.rect;
			}
			last = quad.rect;
		}
		return {first, last};
	}

	u32 CaretsPainted(UI::Context& ctx, const UI::Rect& content) {
		UI::RenderList list;
		ctx.BuildRenderList(list);
		u32 carets = 0;
		for (const UI::UIQuad& quad : list.quads) {
			if ((quad.flags & UI::QUAD_FLAG_GLYPH) != 0u || quad.rect.w <= 0.0f || quad.rect.w > 1.5f) {
				continue;
			}
			if (quad.rect.x >= content.x - 1.0f && quad.rect.x <= content.x + content.w + 1.0f) {
				++carets;
			}
		}
		return carets;
	}

	void TypeReplace(UI::Context& ctx, std::string_view text) {
		ctx.InjectInput(KeyPress(Key::A, KeyMod::Ctrl));
		for (char c : text) {
			ctx.InjectInput(TextChar(static_cast<char32_t>(c)));
		}
		ctx.Update();
	}

	struct Rig {
		MockFontSystem font;
		UI::Context ctx{Desc(), &font};

		Rig() {
			UI::Style rootStyle;
			rootStyle.alignItems = UI::Align::Start;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);
		}
	};

} // anonymous namespace

TEST(WidgetsTest, ButtonClickFires) {
	Rig rig;
	int clicks = 0;
	UI::Widgets::ButtonDesc desc;
	desc.label = "OK";
	desc.onClick = [&] { ++clicks; };
	UI::ElementHandle button = UI::Widgets::Button(rig.ctx, rig.ctx.GetRoot(), desc);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, button));
	EXPECT_EQ(clicks, 1);
}

TEST(WidgetsTest, CheckboxTogglesVisualAndReports) {
	Rig rig;
	bool reported = false;
	int changes = 0;
	auto parts = UI::Widgets::Checkbox(rig.ctx, rig.ctx.GetRoot(), false, [&](bool value) {
		reported = value;
		++changes;
	});
	rig.ctx.Update();

	EXPECT_FALSE(HasFlag(rig.ctx.GetElement(parts.mark)->flags, UI::ElementFlags::Visible));
	Click(rig.ctx, Center(rig.ctx, parts.box));
	EXPECT_TRUE(reported);
	EXPECT_TRUE(HasFlag(rig.ctx.GetElement(parts.mark)->flags, UI::ElementFlags::Visible));

	Click(rig.ctx, Center(rig.ctx, parts.box));
	EXPECT_FALSE(reported);
	EXPECT_EQ(changes, 2);

	UI::Widgets::SetCheckboxValue(rig.ctx, parts, true);
	EXPECT_TRUE(HasFlag(rig.ctx.GetElement(parts.mark)->flags, UI::ElementFlags::Visible));
}

TEST(WidgetsTest, ToggleReportsNewValueAndShowsIt) {
	Rig rig;
	bool reported = false;
	int changes = 0;
	auto parts = UI::Widgets::Toggle(rig.ctx, rig.ctx.GetRoot(), false, [&](bool value) {
		reported = value;
		++changes;
	});
	rig.ctx.Update();
	EXPECT_FALSE(UI::Widgets::GetToggleValue(rig.ctx, parts));

	Click(rig.ctx, Center(rig.ctx, parts.track));
	EXPECT_TRUE(reported);
	EXPECT_TRUE(UI::Widgets::GetToggleValue(rig.ctx, parts));

	Click(rig.ctx, Center(rig.ctx, parts.track));
	EXPECT_FALSE(reported);
	EXPECT_EQ(changes, 2);
	EXPECT_FALSE(UI::Widgets::GetToggleValue(rig.ctx, parts));

	UI::Widgets::SetToggleValue(rig.ctx, parts, true);
	rig.ctx.Update();
	EXPECT_TRUE(UI::Widgets::GetToggleValue(rig.ctx, parts));
	Click(rig.ctx, Center(rig.ctx, parts.track));
	EXPECT_FALSE(reported);
	EXPECT_EQ(changes, 3);
}

TEST(WidgetsTest, TogglesKnobParksAtEndStateNames) {
	Rig rig;
	auto parts = UI::Widgets::Toggle(rig.ctx, rig.ctx.GetRoot(), false, {});
	rig.ctx.Update();

	auto knobCentre = [&]() { return Center(rig.ctx, parts.knob).x; };
	auto trackCentre = [&]() {
		const UI::Rect box = rig.ctx.GetLayout(parts.track).borderBox;
		return box.x + box.w * 0.5f;
	};
	EXPECT_LT(knobCentre(), trackCentre());

	UI::Widgets::SetToggleValue(rig.ctx, parts, true);
	rig.ctx.Update();
	EXPECT_GT(knobCentre(), trackCentre());

	UI::Widgets::SetToggleValue(rig.ctx, parts, false);
	rig.ctx.Update();
	EXPECT_LT(knobCentre(), trackCentre());
}

TEST(WidgetsTest, TogglePressThroughKnobStillWorksSwitch) {
	Rig rig;
	int changes = 0;
	auto parts = UI::Widgets::Toggle(rig.ctx, rig.ctx.GetRoot(), false, [&](bool) { ++changes; });
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, parts.knob));
	EXPECT_EQ(changes, 1);
	rig.ctx.Update();
	Click(rig.ctx, Center(rig.ctx, parts.knob));
	EXPECT_EQ(changes, 2);
	rig.ctx.Update();

	const UI::Rect knob = rig.ctx.GetLayout(parts.knob).borderBox;
	const UI::Rect track = rig.ctx.GetLayout(parts.track).borderBox;
	ASSERT_GT(knob.h, track.h);
	Click(rig.ctx, {knob.x + knob.w * 0.5f, knob.y + (track.y - knob.y) * 0.5f});
	EXPECT_EQ(changes, 3);
}

TEST(WidgetsTest, SwitchHoverLightsKnob) {
	Rig rig;
	auto parts = UI::Widgets::Toggle(rig.ctx, rig.ctx.GetRoot(), false, {});
	rig.ctx.Update();
	auto fill = [&](UI::ElementHandle element) { return rig.ctx.GetComputedStyle(element).background.solid; };
	auto pointAt = [&](UI::ElementHandle element) {
		const UI::Rect box = rig.ctx.GetLayout(element).borderBox;
		return Vec2{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	};
	auto unhover = [&]() {
		const UI::Rect box = rig.ctx.GetLayout(parts.track).borderBox;
		rig.ctx.InjectInput(MouseMove(static_cast<i32>(box.x + box.w + 40.0f),
									  static_cast<i32>(box.y + box.h + 40.0f)));
		rig.ctx.Update();
	};
	auto hover = [&](UI::ElementHandle element) {
		const Vec2 point = pointAt(element);
		rig.ctx.InjectInput(MouseMove(static_cast<i32>(point.x), static_cast<i32>(point.y)));
		rig.ctx.Update();
	};

	unhover();
	const Color trackRest = fill(parts.track);
	const Color knobRest = fill(parts.knob);

	const UI::Rect knobBox = rig.ctx.GetLayout(parts.knob).borderBox;
	const UI::Rect trackBox = rig.ctx.GetLayout(parts.track).borderBox;
	ASSERT_GT(knobBox.h, trackBox.h);
	rig.ctx.InjectInput(MouseMove(static_cast<i32>(knobBox.x + knobBox.w * 0.5f),
								  static_cast<i32>(knobBox.y + (trackBox.y - knobBox.y) * 0.5f)));
	rig.ctx.Update();
	EXPECT_NE(fill(parts.knob), knobRest);
	EXPECT_EQ(fill(parts.track), trackRest);

	unhover();
	EXPECT_EQ(fill(parts.track), trackRest);
	EXPECT_EQ(fill(parts.knob), knobRest);

	const Vec2 farEnd{trackBox.x + trackBox.w * 0.9f, trackBox.y + trackBox.h * 0.5f};
	rig.ctx.InjectInput(MouseMove(static_cast<i32>(farEnd.x), static_cast<i32>(farEnd.y)));
	rig.ctx.Update();
	EXPECT_NE(fill(parts.knob), knobRest);
	EXPECT_EQ(fill(parts.track), trackRest);
}

TEST(WidgetsTest, HoveredSwitchKeepsState) {
	Rig rig;
	auto parts = UI::Widgets::Toggle(rig.ctx, rig.ctx.GetRoot(), false, {});
	rig.ctx.Update();
	auto fill = [&]() { return rig.ctx.GetComputedStyle(parts.track).background.solid; };
	const UI::Rect box = rig.ctx.GetLayout(parts.track).borderBox;
	auto point = [&](bool on, bool onSwitch) {
		return onSwitch ? Vec2{box.x + box.w * (on ? 0.1f : 0.9f), box.y + box.h * 0.5f}
						: Vec2{box.x + box.w + 40.0f, box.y + box.h + 40.0f};
	};
	auto sample = [&](bool on, bool hovered) {
		UI::Widgets::SetToggleValue(rig.ctx, parts, on);
		const Vec2 at = point(on, hovered);
		rig.ctx.InjectInput(MouseMove(static_cast<i32>(at.x), static_cast<i32>(at.y)));
		rig.ctx.Update();
		return fill();
	};

	const Color offRest = sample(false, false);
	const Color onRest = sample(true, false);
	const Color offHover = sample(false, true);
	const Color onHover = sample(true, true);

	ASSERT_NE(offRest, onRest);
	ASSERT_EQ(rig.ctx.GetHovered(), parts.track);
	EXPECT_EQ(offHover, offRest);
	EXPECT_EQ(onHover, onRest);
}

namespace {

	struct SqueezedRow {
		MockFontSystem font;
		UI::Context ctx{Desc(), &font};
		UI::Widgets::CheckboxParts check;
		UI::ElementHandle button;
		UI::Widgets::ColorFieldParts color;

		explicit SqueezedRow(f32 widthPx) {
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.alignItems = UI::Align::Center;
			rowStyle.width = UI::Length::Pixel(widthPx);
			UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

			check = UI::Widgets::Checkbox(ctx, row, true, {});
			UI::Widgets::ButtonDesc clear;
			clear.label = "Clear";
			button = UI::Widgets::Button(ctx, row, clear);
			color = UI::Widgets::ColorSwatchField(ctx, row, Color::White(), {}, 12.0f, {});
			UI::Style labelStyle;
			ctx.SetText(MakeChild(ctx, row, labelStyle), {}, 12.0f, Color::White(),
						"autoscroll, and a counts readout after it");
			ctx.Update();
		}

		f32 CheckWidth() { return ctx.GetLayout(check.box).borderBox.w; }
		f32 ButtonWidth() { return ctx.GetLayout(button).borderBox.w; }
		f32 SwatchWidth() { return ctx.GetLayout(color.swatch).borderBox.w; }
	};

} // namespace

TEST(WidgetsTest, CheckboxAndButtonAreNeverFlexDonors) {
	SqueezedRow resting(600.0f);
	SqueezedRow pressured(120.0f);
	ASSERT_GT(resting.CheckWidth(), 0.0f);
	ASSERT_GT(resting.ButtonWidth(), 0.0f);

	EXPECT_FLOAT_EQ(pressured.CheckWidth(), resting.CheckWidth());
	EXPECT_FLOAT_EQ(pressured.ButtonWidth(), resting.ButtonWidth());
	EXPECT_FLOAT_EQ(pressured.SwatchWidth(), resting.SwatchWidth());
}

TEST(WidgetsTest, SliderJumpsAndDrags) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 20.0f));

	f32 value = 0.0f;
	int begins = 0;
	int ends = 0;
	UI::Widgets::SliderDesc desc;
	desc.minValue = 0.0f;
	desc.maxValue = 100.0f;
	desc.value = 0.0f;
	desc.onChange = [&](f32 v) { value = v; };
	desc.onDragBegin = [&] { ++begins; };
	desc.onDragEnd = [&] { ++ends; };
	auto parts = UI::Widgets::Slider(rig.ctx, container, desc);
	rig.ctx.Update();

	const UI::Rect& track = rig.ctx.GetLayout(parts.track).content;
	ASSERT_GT(track.w, 0.0f);
	f32 y = track.y + track.h * 0.5f;

	rig.ctx.InjectInput(MouseDown(static_cast<i32>(track.x + track.w * 0.25f), static_cast<i32>(y)));
	rig.ctx.Update();
	EXPECT_NEAR(value, 25.0f, 1.5f);
	EXPECT_EQ(begins, 1);

	rig.ctx.InjectInput(MouseMove(static_cast<i32>(track.x + track.w * 0.75f), static_cast<i32>(y)));
	rig.ctx.InjectInput(MouseUp(static_cast<i32>(track.x + track.w * 0.75f), static_cast<i32>(y)));
	rig.ctx.Update();
	EXPECT_NEAR(value, 75.0f, 1.5f);
	EXPECT_EQ(ends, 1);
}

TEST(WidgetsTest, ThumbReactsToHoverAndDrag) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 20.0f));

	UI::Widgets::SliderDesc desc;
	desc.minValue = 0.0f;
	desc.maxValue = 100.0f;
	desc.value = 50.0f;
	int ends = 0;
	desc.onDragEnd = [&] { ++ends; };
	auto parts = UI::Widgets::Slider(rig.ctx, container, desc);
	rig.ctx.Update();

	auto knobWidth = [&] { return rig.ctx.GetComputedStyle(parts.knob).width.value; };
	auto knobFill = [&] { return rig.ctx.GetComputedStyle(parts.knob).background.solid; };

	const UI::Rect& track = rig.ctx.GetLayout(parts.track).content;
	ASSERT_GT(track.w, 0.0f);
	const i32 x = static_cast<i32>(track.x + track.w * 0.5f);
	const i32 y = static_cast<i32>(track.y + track.h * 0.5f);

	const f32 restWidth = knobWidth();
	const Color restFill = knobFill();
	ASSERT_GT(restWidth, 0.0f);

	rig.ctx.InjectInput(MouseMove(x, y));
	rig.ctx.Update();
	EXPECT_GT(knobWidth(), restWidth);
	const Color hoverFill = knobFill();
	EXPECT_NE(hoverFill.r, restFill.r);

	rig.ctx.InjectInput(MouseDown(x, y));
	rig.ctx.Update();
	EXPECT_NE(knobFill().r, hoverFill.r);

	rig.ctx.InjectInput(MouseMove(x, y + 200));
	rig.ctx.Update();
	EXPECT_GT(knobWidth(), restWidth);

	rig.ctx.InjectInput(MouseUp(x, y + 200));
	rig.ctx.Update();
	EXPECT_EQ(ends, 1);
	EXPECT_NEAR(knobWidth(), restWidth, 0.001f);
	EXPECT_NEAR(knobFill().r, restFill.r, 0.001f);
}

TEST(WidgetsTest, SliderStaysLinearBesideALabel) {
	Rig rig;
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.alignItems = UI::Align::Center;
	rowStyle.width = UI::Length::Pixel(300.0f);
	UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), rowStyle);
	MakeChild(rig.ctx, row, FixedBox(100.0f, 14.0f));

	f32 value = 0.0f;
	UI::Widgets::SliderDesc desc;
	desc.minValue = 0.0f;
	desc.maxValue = 100.0f;
	desc.onChange = [&](f32 v) { value = v; };
	auto parts = UI::Widgets::Slider(rig.ctx, row, desc);
	rig.ctx.Update();

	const UI::Rect firstRoot = rig.ctx.GetLayout(parts.root).content;
	const UI::Rect firstTrack = rig.ctx.GetLayout(parts.track).content;
	ASSERT_NEAR(firstRoot.w, 200.0f, 0.5f);
	ASSERT_GT(firstTrack.w, 0.0f);
	ASSERT_LT(firstTrack.w, firstRoot.w);

	for (f32 fraction : {0.1f, 0.5f, 0.65f, 0.8f, 0.95f}) {
		const UI::Rect& track = rig.ctx.GetLayout(parts.track).content;
		f32 x = track.x + track.w * fraction;
		rig.ctx.InjectInput(MouseDown(static_cast<i32>(x), static_cast<i32>(track.y + track.h * 0.5f)));
		rig.ctx.InjectInput(MouseUp(static_cast<i32>(x), static_cast<i32>(track.y + track.h * 0.5f)));
		rig.ctx.Update();

		EXPECT_NEAR(value, fraction * 100.0f, 1.0f) << "fraction " << fraction;
		EXPECT_NEAR(rig.ctx.GetLayout(parts.track).content.w, firstTrack.w, 0.5f) << "fraction " << fraction;
		EXPECT_NEAR(rig.ctx.GetLayout(parts.fill).borderBox.w, firstTrack.w * fraction, 1.0f)
			<< "fraction " << fraction;
	}
}

TEST(WidgetsTest, ThumbCentresOnFillEnd) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 20.0f));

	UI::Widgets::SliderDesc desc;
	desc.minValue = 0.0f;
	desc.maxValue = 1.0f;
	auto parts = UI::Widgets::Slider(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_TRUE(rig.ctx.IsAlive(parts.knob));

	for (f32 value : {0.0f, 0.25f, 0.5f, 1.0f}) {
		UI::Widgets::SetSliderValue(rig.ctx, parts, 0.0f, 1.0f, value);
		rig.ctx.Update();

		const UI::Rect fill = rig.ctx.GetLayout(parts.fill).borderBox;
		const UI::Rect knob = rig.ctx.GetLayout(parts.knob).borderBox;
		const UI::Rect rail = rig.ctx.GetLayout(parts.root).borderBox;

		EXPECT_NEAR(knob.x + knob.w * 0.5f, fill.x + fill.w, 0.5f);
		EXPECT_GE(knob.x, rail.x - 0.5f) << value;
		EXPECT_LE(knob.x + knob.w, rail.x + rail.w + 0.5f) << value;

		const UI::Rect track = rig.ctx.GetLayout(parts.track).borderBox;
		EXPECT_NEAR(track.y + track.h * 0.5f, rail.y + rail.h * 0.5f, 0.5f) << value;
		EXPECT_NEAR(knob.y + knob.h * 0.5f, rail.y + rail.h * 0.5f, 0.5f) << value;
	}
}

TEST(WidgetsTest, SliderFieldAndThumbSync) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));

	f32 reported = -1.0f;
	UI::Widgets::SliderDesc desc;
	desc.minValue = 0.0f;
	desc.maxValue = 100.0f;
	desc.value = 0.0f;
	desc.showValueField = true;
	desc.decimals = 0;
	desc.onChange = [&](f32 v) { reported = v; };
	auto parts = UI::Widgets::Slider(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_TRUE(rig.ctx.IsAlive(parts.value.field));

	const UI::Rect track = rig.ctx.GetLayout(parts.track).content;
	const i32 y = static_cast<i32>(track.y + track.h * 0.5f);
	rig.ctx.InjectInput(MouseDown(static_cast<i32>(track.x + track.w * 0.5f), y));
	rig.ctx.InjectInput(MouseUp(static_cast<i32>(track.x + track.w * 0.5f), y));
	rig.ctx.Update();
	EXPECT_NEAR(reported, 50.0f, 1.5f);
	EXPECT_NEAR(UI::Widgets::GetNumberFieldValue(rig.ctx, parts.value), 50.0, 1.5);

	const f32 beforeTyping = rig.ctx.GetLayout(parts.knob).borderBox.x;
	Click(rig.ctx, Center(rig.ctx, parts.value.field));
	rig.ctx.InjectInput(TextChar('9'));
	rig.ctx.InjectInput(TextChar('0'));
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_NEAR(reported, 90.0f, 0.001f);
	EXPECT_GT(rig.ctx.GetLayout(parts.knob).borderBox.x, beforeTyping);

	UI::Widgets::SetSliderValue(rig.ctx, parts, 0.0f, 100.0f, 10.0f);
	rig.ctx.Update();
	EXPECT_NEAR(UI::Widgets::GetNumberFieldValue(rig.ctx, parts.value), 10.0, 0.001);
	EXPECT_LT(rig.ctx.GetLayout(parts.knob).borderBox.x, beforeTyping + 1.0f);

	UI::Widgets::SliderDesc bare;
	auto plain = UI::Widgets::Slider(rig.ctx, container, bare);
	rig.ctx.Update();
	EXPECT_FALSE(plain.value.field.IsValid());
}

TEST(WidgetsTest, SliderRailGrows) {
	Rig rig;
	auto build = [&](f32 width) {
		UI::ElementHandle host = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(width, 24.0f));
		UI::Widgets::SliderDesc desc;
		desc.showValueField = true;
		return UI::Widgets::Slider(rig.ctx, host, desc);
	};
	auto narrow = build(160.0f);
	auto wide = build(400.0f);
	rig.ctx.Update();

	const f32 narrowField = rig.ctx.GetLayout(narrow.value.root).borderBox.w;
	const f32 wideField = rig.ctx.GetLayout(wide.value.root).borderBox.w;
	ASSERT_GT(narrowField, 0.0f);
	EXPECT_NEAR(narrowField, wideField, 0.5f);
	EXPECT_GT(rig.ctx.GetLayout(wide.track).borderBox.w,
			  rig.ctx.GetLayout(narrow.track).borderBox.w);
}

TEST(WidgetsTest, ClickSelectsFieldForReplace) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	UI::Widgets::TextFieldDesc desc;
	desc.text = "12345";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, field));
	ASSERT_EQ(rig.ctx.GetFocused(), field);
	rig.ctx.InjectInput(TextChar('9'));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(field), "9");

	const UI::Rect& box = rig.ctx.GetLayout(field).content;
	rig.ctx.InjectInput(TextChar('8'));
	rig.ctx.InjectInput(TextChar('7'));
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetTextString(field), "987");
	Click(rig.ctx, {box.x + 25.0f, box.y + box.h * 0.5f});
	rig.ctx.InjectInput(TextChar('0'));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(field), "9807");
}

TEST(WidgetsTest, DragSelectsRange) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	UI::Widgets::TextFieldDesc desc;
	desc.text = "12345";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();

	const UI::Rect& box = rig.ctx.GetLayout(field).content;
	const i32 y = static_cast<i32>(box.y + box.h * 0.5f);
	rig.ctx.InjectInput(MouseDown(static_cast<i32>(box.x + 25.0f), y));
	rig.ctx.InjectInput(MouseMove(static_cast<i32>(box.x + 45.0f), y));
	rig.ctx.InjectInput(MouseUp(static_cast<i32>(box.x + 45.0f), y));
	rig.ctx.Update();

	rig.ctx.InjectInput(TextChar('x'));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(field), "12x5");
}

TEST(WidgetsTest, ShorteningValueScrollsFieldBackToStart) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(60.0f, 24.0f));
	UI::Widgets::TextFieldDesc desc;
	desc.text = "123456789012345";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();

	const UI::Rect& content = rig.ctx.GetLayout(field).content;
	Click(rig.ctx, Center(rig.ctx, field));
	ASSERT_EQ(rig.ctx.GetFocused(), field);

	rig.ctx.Update();
	auto [head, tail] = PaintedGlyphSpan(rig.ctx);
	ASSERT_GT(tail.w, 0.0f);
	EXPECT_LE(tail.x + tail.w, content.x + content.w + 0.5f);
	EXPECT_LT(head.x, content.x);

	rig.ctx.InjectInput(TextChar('1'));
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetTextString(field), "1");

	UI::Rect shortened = PaintedGlyphSpan(rig.ctx).first;
	ASSERT_GT(shortened.w, 0.0f);
	EXPECT_NEAR(shortened.x, content.x + 1.0f, 0.5f);
}

TEST(WidgetsTest, LeavingFieldScrollsItBackToStart) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(60.0f, 24.0f));
	UI::Widgets::TextFieldDesc desc;
	desc.text = "123456789012345";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();
	const UI::Rect& content = rig.ctx.GetLayout(field).content;

	Click(rig.ctx, Center(rig.ctx, field));
	ASSERT_EQ(rig.ctx.GetFocused(), field);
	rig.ctx.Update();
	ASSERT_LT(PaintedGlyphSpan(rig.ctx).first.x, content.x);

	Click(rig.ctx, {350.0f, 250.0f});
	ASSERT_FALSE(rig.ctx.GetFocused().IsValid());
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetTextString(field), "123456789012345");
	const UI::Rect head = PaintedGlyphSpan(rig.ctx).first;
	ASSERT_GT(head.w, 0.0f);
	EXPECT_NEAR(head.x, content.x + 1.0f, 0.5f);

	Click(rig.ctx, Center(rig.ctx, field));
	rig.ctx.Update();
	EXPECT_LT(PaintedGlyphSpan(rig.ctx).first.x, content.x);
}

TEST(WidgetsTest, ASelectedRangeDrawsNoCaret) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	UI::Widgets::TextFieldDesc desc;
	desc.text = "12345";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();
	const UI::Rect& content = rig.ctx.GetLayout(field).content;
	auto caretsDrawn = [&]() { return CaretsPainted(rig.ctx, content); };

	Click(rig.ctx, Center(rig.ctx, field));
	ASSERT_EQ(rig.ctx.GetFocused(), field);
	EXPECT_EQ(caretsDrawn(), 0u);

	rig.ctx.InjectInput(KeyPress(Key::End));
	rig.ctx.Update();
	ASSERT_EQ(caretsDrawn(), 1u);

	rig.ctx.InjectInput(KeyPress(Key::A, KeyMod::Ctrl));
	rig.ctx.Update();
	EXPECT_EQ(caretsDrawn(), 0u);

	rig.ctx.InjectInput(KeyPress(Key::Home));
	rig.ctx.InjectInput(KeyPress(Key::Right, KeyMod::Shift));
	rig.ctx.Update();
	EXPECT_EQ(caretsDrawn(), 0u);

	rig.ctx.InjectInput(KeyPress(Key::Right));
	rig.ctx.Update();
	EXPECT_EQ(caretsDrawn(), 1u);
}

TEST(WidgetsTest, MovingTheCaretRestartsItsBlink) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	UI::Widgets::TextFieldDesc desc;
	desc.text = "12345";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();
	const UI::Rect& content = rig.ctx.GetLayout(field).content;

	Click(rig.ctx, Center(rig.ctx, field));
	ASSERT_EQ(rig.ctx.GetFocused(), field);
	rig.ctx.InjectInput(KeyPress(Key::End));
	rig.ctx.Update();
	ASSERT_EQ(CaretsPainted(rig.ctx, content), 1u);

	auto waitForTheDark = [&](const char* when) {
		const f64 deadline = Time::GetRealtimeSinceStartup() + 1.5;
		while (CaretsPainted(rig.ctx, content) != 0u) {
			if (Time::GetRealtimeSinceStartup() > deadline) {
				ADD_FAILURE() << "caret never blinked off " << when;
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return true;
	};

	ASSERT_TRUE(waitForTheDark("before the key"));
	rig.ctx.InjectInput(KeyPress(Key::Left));
	rig.ctx.Update();
	EXPECT_EQ(CaretsPainted(rig.ctx, content), 1u);

	ASSERT_TRUE(waitForTheDark("after the key"));
	Click(rig.ctx, {content.x + 1.0f, content.y + content.h * 0.5f});
	EXPECT_EQ(CaretsPainted(rig.ctx, content), 1u);

	EXPECT_TRUE(waitForTheDark("after the click"));
}

TEST(WidgetsTest, TextFieldCommitsOnEnterAndBlur) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	str committed;
	int commits = 0;
	UI::Widgets::TextFieldDesc desc;
	desc.text = "start";
	desc.onCommit = [&](std::string_view text) {
		committed = str(text);
		++commits;
	};
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, field));
	ASSERT_EQ(rig.ctx.GetFocused(), field);
	TypeReplace(rig.ctx, "hello");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(committed, "hello");
	EXPECT_EQ(commits, 1);

	TypeReplace(rig.ctx, "world");
	Click(rig.ctx, {350.0f, 250.0f});
	EXPECT_EQ(committed, "world");
	EXPECT_EQ(commits, 2);

	Click(rig.ctx, Center(rig.ctx, field));
	TypeReplace(rig.ctx, "discarded");
	rig.ctx.InjectInput(KeyPress(Key::Escape));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(field), "world");
	EXPECT_EQ(commits, 2);
}

TEST(WidgetsTest, NumberFieldParsesClampsAndFormats) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	f64 value = 0.0;
	UI::Widgets::NumberFieldDesc desc;
	desc.value = 1.0;
	desc.minValue = -10.0;
	desc.maxValue = 10.0;
	desc.decimals = 2;
	desc.onChange = [&](f64 v) { value = v; };
	auto parts = UI::Widgets::NumberField(rig.ctx, container, desc);
	rig.ctx.Update();

	EXPECT_EQ(rig.ctx.GetTextString(parts.field), "1");

	Click(rig.ctx, Center(rig.ctx, parts.field));
	TypeReplace(rig.ctx, "5.5");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_DOUBLE_EQ(value, 5.5);
	EXPECT_EQ(rig.ctx.GetTextString(parts.field), "5.5");

	TypeReplace(rig.ctx, "99");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_DOUBLE_EQ(value, 10.0);
	EXPECT_DOUBLE_EQ(UI::Widgets::GetNumberFieldValue(rig.ctx, parts), 10.0);

	UI::Widgets::SetNumberFieldValue(rig.ctx, parts, -3.25, 2);
	EXPECT_EQ(rig.ctx.GetTextString(parts.field), "-3.25");
	EXPECT_DOUBLE_EQ(UI::Widgets::GetNumberFieldValue(rig.ctx, parts), -3.25);
}

TEST(WidgetsTest, NumberFieldRootNeverShrinksBelowInput) {
	for (f32 containerWidth : {400.0f, 200.0f, 120.0f, 60.0f, 30.0f, 12.0f}) {
		Rig rig;
		UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(containerWidth, 24.0f));
		UI::Widgets::NumberFieldDesc desc;
		desc.value = 33.0;
		auto parts = UI::Widgets::NumberField(rig.ctx, row, desc);
		rig.ctx.Update();

		const UI::Rect& root = rig.ctx.GetLayout(parts.root).borderBox;
		const UI::Rect& input = rig.ctx.GetLayout(parts.field).borderBox;
		EXPECT_GE(root.w, input.w) << containerWidth << " the root (" << root.w << ' ' << input.w << ")";
		EXPECT_LE(input.x + input.w, root.x + root.w + 0.01f) << containerWidth;
	}
}

TEST(WidgetsTest, NumberFieldStaysInBox) {
	for (f32 pageWidth : {1200.0f, 800.0f, 500.0f, 300.0f, 260.0f, 220.0f, 180.0f, 140.0f}) {
		Rig rig;
		UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(pageWidth, 24.0f));
		UI::Style rowStyle = rig.ctx.GetStyle(row);
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.alignItems = UI::Align::Center;
		rig.ctx.SetStyle(row, rowStyle);

		UI::ElementHandle label = MakeChild(rig.ctx, row, FixedBox(150.0f, 16.0f));
		UI::Style labelStyle = rig.ctx.GetStyle(label);
		labelStyle.flexShrink = 0.0f;
		rig.ctx.SetStyle(label, labelStyle);

		UI::Widgets::NumberFieldDesc desc;
		desc.value = 33.0;
		auto parts = UI::Widgets::NumberField(rig.ctx, row, desc);

		UI::ElementHandle badge = MakeChild(rig.ctx, row, FixedBox(90.0f, 16.0f));
		UI::Style badgeStyle = rig.ctx.GetStyle(badge);
		badgeStyle.flexShrink = 0.0f;
		rig.ctx.SetStyle(badge, badgeStyle);
		rig.ctx.Update();

		const UI::Rect& input = rig.ctx.GetLayout(parts.field).borderBox;
		const UI::Rect& after = rig.ctx.GetLayout(badge).borderBox;
		EXPECT_LE(input.x + input.w, after.x + 0.01f);
	}
}

TEST(WidgetsTest, NumberFieldShowsNoTrailingZeros) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	UI::Widgets::NumberFieldDesc desc;
	auto parts = UI::Widgets::NumberField(rig.ctx, container, desc);
	rig.ctx.Update();

	auto shows = [&](f64 value) {
		UI::Widgets::SetNumberFieldValue(rig.ctx, parts, value, UI::Widgets::DEFAULT_DECIMALS);
		return rig.ctx.GetTextString(parts.field);
	};

	EXPECT_EQ(shows(1.0), "1");
	EXPECT_EQ(shows(0.0), "0");
	EXPECT_EQ(shows(-2.0), "-2");
	EXPECT_EQ(shows(1.5), "1.5");
	EXPECT_EQ(shows(120.25), "120.25");

	EXPECT_EQ(shows(1.4285), "1.4285");

	EXPECT_EQ(shows(1.23456789), "1.23457");

	EXPECT_EQ(shows(-0.0), "0");
	EXPECT_EQ(shows(-0.000001), "0");

	UI::Widgets::SetNumberFieldValue(rig.ctx, parts, 4200.0, 0);
	EXPECT_EQ(rig.ctx.GetTextString(parts.field), "4200");

	UI::Widgets::SetNumberFieldValue(rig.ctx, parts, 1.4285, UI::Widgets::DEFAULT_DECIMALS);
	EXPECT_DOUBLE_EQ(UI::Widgets::GetNumberFieldValue(rig.ctx, parts), 1.4285);
}

TEST(WidgetsTest, HugeNumberIsScientific) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	UI::Widgets::NumberFieldDesc desc;
	auto parts = UI::Widgets::NumberField(rig.ctx, container, desc);
	rig.ctx.Update();

	auto shows = [&](f64 value) {
		UI::Widgets::SetNumberFieldValue(rig.ctx, parts, value, UI::Widgets::DEFAULT_DECIMALS);
		return rig.ctx.GetTextString(parts.field);
	};

	const str never = shows(static_cast<f64>(3.4028234663852886e+38f));
	EXPECT_NE(never.find('e'), str::npos) << "shown as '" << never << "'";
	EXPECT_LT(never.size(), 26u) << "shown as '" << never << "'";

	EXPECT_DOUBLE_EQ(std::stod(never), static_cast<f64>(3.4028234663852886e+38f));

	EXPECT_EQ(shows(9007199254740991.0), "9007199254740991");
	EXPECT_EQ(shows(1000000.0), "1000000");
	EXPECT_EQ(shows(-123456.75), "-123456.75");

	EXPECT_EQ(shows(std::numeric_limits<f64>::infinity()), "inf");
	EXPECT_EQ(shows(-std::numeric_limits<f64>::infinity()), "-inf");
}

TEST(WidgetsTest, VectorFieldSplitsEvenly) {
	Rig rig;
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.width = UI::Length::Pixel(360.0f);
	rowStyle.height = UI::Length::Pixel(24.0f);
	UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), rowStyle);

	UI::Widgets::VectorFieldDesc desc;
	desc.componentCount = 3;
	auto parts = UI::Widgets::VectorField(rig.ctx, row, desc);
	rig.ctx.Update();

	auto widthOf = [&](u32 i) { return rig.ctx.GetLayout(parts.components[i].field).borderBox.w; };

	f32 even0 = widthOf(0);
	f32 even1 = widthOf(1);
	f32 even2 = widthOf(2);
	EXPECT_FLOAT_EQ(even0, even1);
	EXPECT_FLOAT_EQ(even1, even2);
	ASSERT_GT(even0, 0.0f);

	f64 lopsided[4] = {-123.45678, 0.0, 7.0, 0.0};
	UI::Widgets::SetVectorFieldValues(rig.ctx, parts, 3, lopsided, UI::Widgets::DEFAULT_DECIMALS);
	rig.ctx.Update();

	EXPECT_FLOAT_EQ(widthOf(0), even0);
	EXPECT_FLOAT_EQ(widthOf(1), even1);
	EXPECT_FLOAT_EQ(widthOf(2), even2);
}

TEST(WidgetsTest, ColorChannelsAreBytes) {
	Rig rig;
	UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(360.0f, 24.0f));
	Color reported = Color::Black();
	auto parts = UI::Widgets::ColorSwatchField(
		rig.ctx, row, Color(0.815f, 0.54f, 0.12f, 1.0f), Handle<FontAsset>{}, 13.0f,
		[&](Color value) { reported = value; });
	rig.ctx.Update();

	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[0].field), "208");
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[1].field), "138");
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[2].field), "31");
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[3].field), "255");

	Click(rig.ctx, Center(rig.ctx, parts.vector.components[0].field));
	TypeReplace(rig.ctx, "-5");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[0].field), "0");
	EXPECT_FLOAT_EQ(reported.r, 0.0f);

	TypeReplace(rig.ctx, "300");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[0].field), "255");
	EXPECT_FLOAT_EQ(reported.r, 1.0f);
	EXPECT_NEAR(reported.g, 0.54f, 1.0f / 510.0f);
	EXPECT_NEAR(reported.b, 0.12f, 1.0f / 510.0f);
	EXPECT_FLOAT_EQ(reported.a, 1.0f);
}

TEST(WidgetsTest, ColorFieldResyncs) {
	Rig rig;
	UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(360.0f, 24.0f));
	Color reported = Color::Black();
	auto parts = UI::Widgets::ColorSwatchField(
		rig.ctx, row, Color::White(), Handle<FontAsset>{}, 13.0f,
		[&](Color value) { reported = value; });
	rig.ctx.Update();

	UI::Widgets::SetColorSwatchFieldValue(rig.ctx, parts, Color(0.0f, 0.4f, 0.8f, 1.0f));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[1].field), "102");
	EXPECT_EQ(rig.ctx.GetTextString(parts.vector.components[2].field), "204");

	Click(rig.ctx, Center(rig.ctx, parts.vector.components[0].field));
	TypeReplace(rig.ctx, "51");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_FLOAT_EQ(reported.r, 0.2f);
	EXPECT_NEAR(reported.g, 0.4f, 1.0f / 510.0f);
	EXPECT_NEAR(reported.b, 0.8f, 1.0f / 510.0f);
}

TEST(WidgetsTest, NumberFieldScrubsByPixels) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	f64 value = 0.0;
	int begins = 0;
	int ends = 0;
	UI::Widgets::NumberFieldDesc desc;
	desc.value = 5.0;
	desc.dragScale = 0.1;
	desc.decimals = 1;
	desc.label = "X";
	desc.onChange = [&](f64 v) { value = v; };
	desc.onScrubBegin = [&] { ++begins; };
	desc.onScrubEnd = [&] { ++ends; };
	auto parts = UI::Widgets::NumberField(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_TRUE(parts.labelElement.IsValid());

	Vec2 start = Center(rig.ctx, parts.labelElement);
	rig.ctx.InjectInput(MouseDown(static_cast<i32>(start.x), static_cast<i32>(start.y)));
	rig.ctx.InjectInput(MouseMove(static_cast<i32>(start.x) + 100, static_cast<i32>(start.y)));
	rig.ctx.InjectInput(MouseUp(static_cast<i32>(start.x) + 100, static_cast<i32>(start.y)));
	rig.ctx.Update();

	EXPECT_NEAR(value, 15.0, 0.11);
	EXPECT_EQ(begins, 1);
	EXPECT_EQ(ends, 1);
	EXPECT_EQ(rig.ctx.GetTextString(parts.field), "15");

	Click(rig.ctx, start);
	EXPECT_EQ(begins, 1);
	EXPECT_EQ(ends, 1);
}

TEST(WidgetsTest, ScrubWrapsPointer) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	UI::Widgets::NumberFieldDesc desc;
	desc.value = 5.0;
	desc.label = "X";
	auto parts = UI::Widgets::NumberField(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_TRUE(parts.labelElement.IsValid());
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);

	Vec2 start = Center(rig.ctx, parts.labelElement);
	rig.ctx.InjectInput(MouseDown(static_cast<i32>(start.x), static_cast<i32>(start.y)));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);

	rig.ctx.InjectInput(MouseMove(static_cast<i32>(start.x) + 40, static_cast<i32>(start.y)));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Wrapped);

	rig.ctx.InjectInput(MouseUp(static_cast<i32>(start.x) + 40, static_cast<i32>(start.y)));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);
}

TEST(WidgetsTest, DropdownOpensSelectsAndUpdatesLabel) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Alpha", "Beta", "Gamma"};
	u32 selected = 0;
	UI::Widgets::DropdownDesc desc;
	desc.items = {ITEMS, 3};
	desc.selected = 0;
	desc.onSelect = [&](u32 index) { selected = index; };
	UI::Widgets::DropdownParts dropdown = UI::Widgets::Dropdown(rig.ctx, container, desc);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, dropdown.root));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);

	UI::ElementHandle row = NthChildOf(rig.ctx, content, 1);
	ASSERT_TRUE(row.IsValid());
	Click(rig.ctx, Center(rig.ctx, row));

	EXPECT_EQ(selected, 1u);
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 0u);
	EXPECT_EQ(rig.ctx.GetTextString(dropdown.label), "Beta");
	rig.ctx.Update();
	EXPECT_FALSE(rig.ctx.IsAlive(content));
}

TEST(WidgetsTest, DropdownWidthFollowsItsSelection) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(400.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"S", "MMMMMMMMMMMM"};
	UI::Widgets::DropdownDesc desc;
	desc.items = {ITEMS, 2};
	desc.selected = 0;
	UI::Widgets::DropdownParts dropdown = UI::Widgets::Dropdown(rig.ctx, container, desc);
	for (int i = 0; i < 3; ++i) {
		rig.ctx.Update();
	}
	f32 before = rig.ctx.GetLayout(dropdown.root).borderBox.w;

	Click(rig.ctx, Center(rig.ctx, dropdown.root));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, rig.ctx.GetOpenPopup(0), 1)));

	rig.ctx.Update();
	EXPECT_GT(rig.ctx.GetLayout(dropdown.root).borderBox.w, before);
}

TEST(WidgetsTest, DropdownFillsSlotOnRequest) {
	Rig rig;
	static constexpr std::string_view ITEMS[] = {"S"};
	UI::Widgets::DropdownDesc desc;
	desc.items = {ITEMS, 1};

	UI::ElementHandle bare = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(400.0f, 24.0f));
	UI::Widgets::DropdownParts loose = UI::Widgets::Dropdown(rig.ctx, bare, desc);

	UI::ElementHandle slot = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(400.0f, 24.0f));
	UI::Widgets::DropdownParts filled = UI::Widgets::Dropdown(rig.ctx, slot, desc);
	rig.ctx.AddClass(filled.root, StringHash("slt-fill"));
	for (int i = 0; i < 3; ++i) {
		rig.ctx.Update();
	}

	const f32 looseWidth = rig.ctx.GetLayout(loose.root).borderBox.w;
	const f32 filledWidth = rig.ctx.GetLayout(filled.root).borderBox.w;
	const f32 slotWidth = rig.ctx.GetLayout(slot).borderBox.w;
	EXPECT_GT(filledWidth, looseWidth);
	EXPECT_NEAR(filledWidth, slotWidth, 0.01f);
	EXPECT_LT(looseWidth, slotWidth);
}

TEST(WidgetsTest, DropdownTicksLiveSelection) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Alpha", "Beta", "Gamma"};
	UI::Widgets::DropdownDesc desc;
	desc.items = {ITEMS, 3};
	desc.selected = 0;
	UI::Widgets::DropdownParts dropdown = UI::Widgets::Dropdown(rig.ctx, container, desc);
	rig.ctx.Update();

	UI::Widgets::SetDropdownSelection(rig.ctx, dropdown, {ITEMS, 3}, 2);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(dropdown.label), "Gamma");

	Click(rig.ctx, Center(rig.ctx, dropdown.root));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);
	EXPECT_FALSE(TickIsVisible(rig.ctx, NthChildOf(rig.ctx, content, 0)));
	EXPECT_TRUE(TickIsVisible(rig.ctx, NthChildOf(rig.ctx, content, 2)));
}

TEST(WidgetsTest, UnnamedSelectionClearsLabel) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(200.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Alpha", "Beta"};
	UI::Widgets::DropdownDesc desc;
	desc.items = {ITEMS, 2};
	desc.selected = 1;
	UI::Widgets::DropdownParts dropdown = UI::Widgets::Dropdown(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetTextString(dropdown.label), "Beta");

	UI::Widgets::SetDropdownSelection(rig.ctx, dropdown, {ITEMS, 2}, 7);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(dropdown.label), "");
}

TEST(WidgetsTest, MaskDropdownTogglesBits) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Info", "Warn", "Error"};
	u64 reported = 0;
	int calls = 0;
	UI::Widgets::MaskDropdownDesc desc;
	desc.items = {ITEMS, 3};
	desc.mask = 0b001;
	desc.onChange = [&](u64 mask) {
		reported = mask;
		++calls;
	};
	UI::Widgets::DropdownParts picker = UI::Widgets::MaskDropdown(rig.ctx, container, desc);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Info");

	Click(rig.ctx, Center(rig.ctx, picker.root));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);

	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 4)));
	EXPECT_EQ(reported, 0b011u);
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 1u);

	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 3)));
	EXPECT_EQ(reported, 0b010u);
	EXPECT_EQ(calls, 2);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Warn");
}

TEST(WidgetsTest, MaskDropdownEndsReadNothingAndEverything) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Info", "Warn", "Error"};
	u64 reported = 0;
	UI::Widgets::MaskDropdownDesc desc;
	desc.items = {ITEMS, 3};
	desc.mask = 0b101;
	desc.onChange = [&](u64 mask) { reported = mask; };
	UI::Widgets::DropdownParts picker = UI::Widgets::MaskDropdown(rig.ctx, container, desc);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Info, Error");

	Click(rig.ctx, Center(rig.ctx, picker.root));
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);
	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 1)));
	EXPECT_EQ(reported, 0b111u);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Everything");

	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 0)));
	EXPECT_EQ(reported, 0u);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Nothing");
}

TEST(WidgetsTest, ResyncedMaskMovesValue) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Info", "Warn", "Error"};
	u64 reported = 0;
	UI::Widgets::MaskDropdownDesc desc;
	desc.items = {ITEMS, 3};
	desc.mask = 0b011;
	desc.onChange = [&](u64 mask) { reported = mask; };
	UI::Widgets::DropdownParts picker = UI::Widgets::MaskDropdown(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetTextString(picker.label), "Info, Warn");

	UI::Widgets::SetMaskDropdownValue(rig.ctx, picker, {ITEMS, 3}, 0b001);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Info");

	Click(rig.ctx, Center(rig.ctx, picker.root));
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);
	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 5)));
	EXPECT_EQ(reported, 0b101u);
}

TEST(WidgetsTest, ResyncedMaskLabelMatches) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Info", "Warn", "Error"};
	UI::Widgets::MaskDropdownDesc desc;
	desc.items = {ITEMS, 3};
	desc.mask = 0b111;
	UI::Widgets::DropdownParts picker = UI::Widgets::MaskDropdown(rig.ctx, container, desc);
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetTextString(picker.label), "Everything");

	UI::Widgets::SetMaskDropdownValue(rig.ctx, picker, {ITEMS, 3}, 0u);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Nothing");

	UI::Widgets::SetMaskDropdownValue(rig.ctx, picker, {ITEMS, 3}, 0b101);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Info, Error");

	UI::Widgets::SetMaskDropdownValue(rig.ctx, picker, {ITEMS, 3}, 0b111);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(picker.label), "Everything");
}

TEST(WidgetsTest, MaskDropdownReticksEveryRowAfterPick) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	static constexpr std::string_view ITEMS[] = {"Info", "Warn"};
	UI::Widgets::MaskDropdownDesc desc;
	desc.items = {ITEMS, 2};
	desc.mask = 0b11;
	UI::Widgets::DropdownParts picker = UI::Widgets::MaskDropdown(rig.ctx, container, desc);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, picker.root));
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);
	ASSERT_TRUE(TickIsVisible(rig.ctx, NthChildOf(rig.ctx, content, 1)));

	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 4)));
	EXPECT_FALSE(TickIsVisible(rig.ctx, NthChildOf(rig.ctx, content, 1)));
	EXPECT_TRUE(TickIsVisible(rig.ctx, NthChildOf(rig.ctx, content, 3)));
	EXPECT_FALSE(TickIsVisible(rig.ctx, NthChildOf(rig.ctx, content, 4)));
}

TEST(WidgetsTest, MaskDropdownDropsItemsMaskCannotAddress) {
	Rig rig;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(300.0f, 24.0f));
	Vector<std::string_view> many{GetMemoryResource(MemTag::UI)};
	static str storage[70];
	for (int i = 0; i < 70; ++i) {
		storage[i] = std::format("Item{}", i);
		many.push_back(storage[i]);
	}
	UI::Widgets::MaskDropdownDesc desc;
	desc.items = {many.data(), many.size()};
	UI::Widgets::DropdownParts picker = UI::Widgets::MaskDropdown(rig.ctx, container, desc);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, picker.root));
	UI::ElementHandle content = rig.ctx.GetOpenPopup(0);
	EXPECT_EQ(ChildCountOf(rig.ctx, content), 3u + 64u);
}

TEST(WidgetsTest, MenuSelectionClosesAndFires) {
	Rig rig;
	int fired = 0;
	Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
	items.emplace_back();
	items.back().label = "First";
	items.back().onSelect = [&] { ++fired; };
	items.emplace_back();
	items.back().label = "Disabled";
	items.back().enabled = false;
	items.emplace_back();
	items.back().label = "Second";
	items.back().separatorBefore = true;
	items.back().onSelect = [&] { fired += 10; };

	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {50.0f, 50.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);

	UI::ElementHandle second = NthChildOf(rig.ctx, content, 3);
	ASSERT_TRUE(second.IsValid());
	Click(rig.ctx, Center(rig.ctx, second));
	EXPECT_EQ(fired, 10);
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 0u);
	rig.ctx.Update();
	EXPECT_FALSE(rig.ctx.IsAlive(content));
}

TEST(WidgetsTest, MenuSizesToItems) {
	Rig rig;
	Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
	items.emplace_back();
	items.back().label = "Open";
	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {50.0f, 50.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();

	f32 popupWidth = rig.ctx.GetLayout(content).borderBox.w;
	EXPECT_NEAR(popupWidth, 160.0f, 0.5f);
	UI::ElementHandle row = NthChildOf(rig.ctx, content, 0);
	ASSERT_TRUE(row.IsValid());
	EXPECT_NEAR(rig.ctx.GetLayout(row).borderBox.w, popupWidth, 0.5f);
}

TEST(WidgetsTest, MenuLabelsLineUp) {
	Rig rig;
	Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
	items.emplace_back();
	items.back().label = "On";
	items.back().checked = true;
	items.emplace_back();
	items.back().label = "Off";
	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {50.0f, 50.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();
	const UI::ElementHandle on = NthChildOf(rig.ctx, content, 0);
	const UI::ElementHandle off = NthChildOf(rig.ctx, content, 1);
	ASSERT_TRUE(on.IsValid() && off.IsValid());
	EXPECT_TRUE(FindByClass(rig.ctx, on, StringHash("slt-check-mark"), 0).IsValid());
	EXPECT_FALSE(FindByClass(rig.ctx, off, StringHash("slt-check-mark"), 0).IsValid());
	const UI::ElementHandle onLabel = FindByText(rig.ctx, on, "On");
	const UI::ElementHandle offLabel = FindByText(rig.ctx, off, "Off");
	ASSERT_TRUE(onLabel.IsValid() && offLabel.IsValid());
	EXPECT_NEAR(rig.ctx.GetLayout(onLabel).borderBox.x, rig.ctx.GetLayout(offLabel).borderBox.x, 0.5f);
}

namespace {

	Vector<UI::Widgets::MenuItem> SubmenuItems(int& childFired, int& siblingFired) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		items.emplace_back();
		items.back().label = "Meshes";
		items.back().children.emplace_back();
		items.back().children.back().label = "Cube";
		items.back().children.back().onSelect = [&childFired] { ++childFired; };
		items.emplace_back();
		items.back().label = "Camera";
		items.back().onSelect = [&siblingFired] { ++siblingFired; };
		return items;
	}

	void Hover(UI::Context& ctx, Vec2 point) {
		ctx.InjectInput(MouseMove(static_cast<i32>(point.x), static_cast<i32>(point.y)));
		ctx.Update();
	}

} // anonymous namespace

TEST(WidgetsTest, SubmenuFollowsHover) {
	Rig rig;
	int childFired = 0;
	int siblingFired = 0;
	Vector<UI::Widgets::MenuItem> items = SubmenuItems(childFired, siblingFired);
	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {20.0f, 20.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();

	UI::ElementHandle submenuRow = NthChildOf(rig.ctx, content, 0);
	UI::ElementHandle siblingRow = NthChildOf(rig.ctx, content, 1);
	ASSERT_TRUE(submenuRow.IsValid());
	ASSERT_TRUE(siblingRow.IsValid());

	Hover(rig.ctx, Center(rig.ctx, submenuRow));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 2u);
	UI::ElementHandle child = rig.ctx.GetOpenPopup(1);
	EXPECT_NEAR(rig.ctx.GetLayout(child).borderBox.x,
				rig.ctx.GetLayout(submenuRow).borderBox.x + rig.ctx.GetLayout(submenuRow).borderBox.w, 0.5f);

	Hover(rig.ctx, Center(rig.ctx, siblingRow));
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	EXPECT_TRUE(rig.ctx.IsPopupOpen(content));
}

TEST(WidgetsTest, EdgeSubmenuOpensLeft) {
	Rig rig;
	int childFired = 0;
	int siblingFired = 0;
	Vector<UI::Widgets::MenuItem> items = SubmenuItems(childFired, siblingFired);
	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {390.0f, 20.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();
	UI::ElementHandle submenuRow = NthChildOf(rig.ctx, content, 0);
	ASSERT_TRUE(submenuRow.IsValid());
	const UI::Rect row = rig.ctx.GetLayout(submenuRow).borderBox;
	ASSERT_GT(row.x + row.w + 20.0f, 400.0f);

	Hover(rig.ctx, Center(rig.ctx, submenuRow));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 2u);
	UI::ElementHandle child = rig.ctx.GetOpenPopup(1);
	const UI::Rect box = rig.ctx.GetLayout(child).borderBox;
	EXPECT_NEAR(box.x + box.w, row.x, 0.5f);
	EXPECT_NEAR(box.y, row.y, 0.5f);

	Click(rig.ctx, Center(rig.ctx, submenuRow));
	EXPECT_EQ(childFired, 0);
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 2u);
}

TEST(WidgetsTest, RehoverDoesNotStack) {
	Rig rig;
	int childFired = 0;
	int siblingFired = 0;
	Vector<UI::Widgets::MenuItem> items = SubmenuItems(childFired, siblingFired);
	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {20.0f, 20.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();
	UI::ElementHandle submenuRow = NthChildOf(rig.ctx, content, 0);

	Hover(rig.ctx, Center(rig.ctx, submenuRow));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 2u);
	UI::ElementHandle child = rig.ctx.GetOpenPopup(1);

	Hover(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, child, 0)));
	Hover(rig.ctx, Center(rig.ctx, submenuRow));
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 2u);
	EXPECT_EQ(rig.ctx.GetOpenPopup(1), child);
}

TEST(WidgetsTest, SubmenuSelectionClosesTheWholeChain) {
	Rig rig;
	int childFired = 0;
	int siblingFired = 0;
	Vector<UI::Widgets::MenuItem> items = SubmenuItems(childFired, siblingFired);
	UI::ElementHandle content = UI::Widgets::OpenMenu(rig.ctx, {20.0f, 20.0f}, {items.data(), items.size()}, {}, 12.0f);
	rig.ctx.Update();

	Hover(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, content, 0)));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 2u);
	UI::ElementHandle child = rig.ctx.GetOpenPopup(1);

	Click(rig.ctx, Center(rig.ctx, NthChildOf(rig.ctx, child, 0)));
	EXPECT_EQ(childFired, 1);
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 0u);
	rig.ctx.Update();
	EXPECT_FALSE(rig.ctx.IsAlive(content));
	EXPECT_FALSE(rig.ctx.IsAlive(child));
}

TEST(WidgetsTest, MenuBarOpensAndHoverSwitches) {
	Rig rig;
	int fileNew = 0;
	int editUndo = 0;
	Vector<UI::Widgets::MenuBarItemDesc> menus{GetMemoryResource(MemTag::UI)};
	menus.emplace_back();
	menus.back().label = "File";
	menus.back().fill = [&](Vector<UI::Widgets::MenuItem>& items) {
		items.emplace_back();
		items.back().label = "New";
		items.back().onSelect = [&] { ++fileNew; };
	};
	menus.emplace_back();
	menus.back().label = "Edit";
	menus.back().fill = [&](Vector<UI::Widgets::MenuItem>& items) {
		items.emplace_back();
		items.back().label = "Undo";
		items.back().onSelect = [&] { ++editUndo; };
	};

	UI::ElementHandle bar = UI::Widgets::MenuBar(rig.ctx, rig.ctx.GetRoot(), {menus.data(), menus.size()}, {}, 12.0f);
	rig.ctx.Update();

	UI::ElementHandle fileItem = NthChildOf(rig.ctx, bar, 0);
	UI::ElementHandle editItem = NthChildOf(rig.ctx, bar, 1);
	ASSERT_TRUE(fileItem.IsValid());
	ASSERT_TRUE(editItem.IsValid());

	Click(rig.ctx, Center(rig.ctx, fileItem));
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);

	Vec2 editCenter = Center(rig.ctx, editItem);
	rig.ctx.InjectInput(MouseMove(static_cast<i32>(editCenter.x), static_cast<i32>(editCenter.y)));
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	UI::ElementHandle editMenu = rig.ctx.GetOpenPopup(0);

	UI::ElementHandle undoRow = NthChildOf(rig.ctx, editMenu, 0);
	ASSERT_TRUE(undoRow.IsValid());
	Click(rig.ctx, Center(rig.ctx, undoRow));
	EXPECT_EQ(editUndo, 1);
	EXPECT_EQ(fileNew, 0);
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 0u);
}

namespace {

	struct ScrollRig {
		UI::Widgets::ScrollViewParts parts;
		f32 maxScroll = 0.0f;
		UI::Rect track;
	};

	ScrollRig MakeScrollRig(UI::Context& ctx, f32 contentWidth = 150.0f) {
		ScrollRig scroll;
		UI::ElementHandle container = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
		scroll.parts = UI::Widgets::ScrollView(ctx, container);
		UI::Style rootStyle = ctx.GetStyle(scroll.parts.root);
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		ctx.SetStyle(scroll.parts.root, rootStyle);
		for (int i = 0; i < 10; ++i) {
			MakeChild(ctx, scroll.parts.viewport, FixedBox(contentWidth, 50.0f));
		}
		ctx.Update();
		const UI::Element* viewport = ctx.GetElement(scroll.parts.viewport);
		scroll.maxScroll = viewport ? viewport->maxScroll.y : 0.0f;
		scroll.track = ctx.GetLayout(scroll.parts.track).content;
		return scroll;
	}

	f32 ScrollOffset(const UI::Context& ctx, UI::ElementHandle viewport) {
		const UI::Element* element = ctx.GetElement(viewport);
		return element ? element->scrollOffset.y : 0.0f;
	}

	std::optional<Color> ThumbFill(UI::Context& ctx, const ScrollRig& scroll) {
		UI::RenderList list;
		ctx.BuildRenderList(list);
		for (const UI::UIQuad& quad : list.quads) {
			if (std::abs(quad.rect.x - (scroll.track.x + 2.0f)) < 0.6f &&
				std::abs(quad.rect.w - (scroll.track.w - 4.0f)) < 0.6f) {
				return quad.color0;
			}
		}
		return std::nullopt;
	}

} // namespace

TEST(WidgetsTest, ScrollTrackKeepsWidth) {
	Rig resting;
	ScrollRig narrow = MakeScrollRig(resting.ctx);
	f32 restingWidth = resting.ctx.GetLayout(narrow.parts.track).borderBox.w;
	ASSERT_GT(restingWidth, 0.0f);

	Rig pressured;
	ScrollRig wide = MakeScrollRig(pressured.ctx, 400.0f);
	EXPECT_FLOAT_EQ(pressured.ctx.GetLayout(wide.parts.track).borderBox.w, restingWidth);
}

TEST(WidgetsTest, TrackClickJumpsThumb) {
	Rig rig;
	ScrollRig scroll = MakeScrollRig(rig.ctx);
	ASSERT_GT(scroll.maxScroll, 0.0f);
	ASSERT_GT(scroll.track.h, 0.0f);

	i32 x = static_cast<i32>(scroll.track.x + scroll.track.w * 0.5f);
	Click(rig.ctx, {static_cast<f32>(x), scroll.track.y + scroll.track.h - 1.0f});
	EXPECT_NEAR(ScrollOffset(rig.ctx, scroll.parts.viewport), scroll.maxScroll, 1.0f);

	Click(rig.ctx, {static_cast<f32>(x), scroll.track.y + 1.0f});
	EXPECT_NEAR(ScrollOffset(rig.ctx, scroll.parts.viewport), 0.0f, 1.0f);
}

TEST(WidgetsTest, ThumbDragsByGrabPoint) {
	Rig rig;
	ScrollRig scroll = MakeScrollRig(rig.ctx);
	ASSERT_GT(scroll.maxScroll, 0.0f);

	f32 half = scroll.maxScroll * 0.5f;
	f32 center = scroll.track.y + scroll.track.h * 0.5f;
	i32 x = static_cast<i32>(scroll.track.x + scroll.track.w * 0.5f);
	constexpr f32 OFF_CENTER = 4.0f;
	constexpr f32 DRAG = 20.0f;

	rig.ctx.SetScrollOffset(scroll.parts.viewport, {0.0f, half});
	rig.ctx.Update();
	rig.ctx.InjectInput(MouseDown(x, static_cast<i32>(center + OFF_CENTER)));
	rig.ctx.Update();
	EXPECT_NEAR(ScrollOffset(rig.ctx, scroll.parts.viewport), half, 0.5f);

	rig.ctx.InjectInput(MouseMove(x, static_cast<i32>(center + OFF_CENTER + DRAG)));
	rig.ctx.InjectInput(MouseUp(x, static_cast<i32>(center + OFF_CENTER + DRAG)));
	rig.ctx.Update();
	f32 draggedFromBelowCenter = ScrollOffset(rig.ctx, scroll.parts.viewport);
	EXPECT_GT(draggedFromBelowCenter, half);

	rig.ctx.SetScrollOffset(scroll.parts.viewport, {0.0f, half});
	rig.ctx.Update();
	rig.ctx.InjectInput(MouseDown(x, static_cast<i32>(center - OFF_CENTER)));
	rig.ctx.InjectInput(MouseMove(x, static_cast<i32>(center - OFF_CENTER + DRAG)));
	rig.ctx.InjectInput(MouseUp(x, static_cast<i32>(center - OFF_CENTER + DRAG)));
	rig.ctx.Update();
	EXPECT_NEAR(ScrollOffset(rig.ctx, scroll.parts.viewport), draggedFromBelowCenter, 0.5f);
}

TEST(WidgetsTest, ThumbReactsToHoverAndPress) {
	Rig rig;
	ScrollRig scroll = MakeScrollRig(rig.ctx);
	ASSERT_GT(scroll.maxScroll, 0.0f);
	ASSERT_GT(scroll.track.h, 0.0f);

	rig.ctx.SetScrollOffset(scroll.parts.viewport, {0.0f, scroll.maxScroll * 0.5f});
	rig.ctx.Update();
	i32 x = static_cast<i32>(scroll.track.x + scroll.track.w * 0.5f);
	i32 onThumb = static_cast<i32>(scroll.track.y + scroll.track.h * 0.5f);
	i32 offBar = static_cast<i32>(scroll.track.x - 20.0f);

	rig.ctx.InjectInput(MouseMove(offBar, onThumb));
	rig.ctx.Update();
	std::optional<Color> idle = ThumbFill(rig.ctx, scroll);
	ASSERT_TRUE(idle.has_value());

	rig.ctx.InjectInput(MouseMove(x, onThumb));
	rig.ctx.Update();
	std::optional<Color> hovered = ThumbFill(rig.ctx, scroll);
	ASSERT_TRUE(hovered.has_value());
	EXPECT_NE(hovered, idle);

	rig.ctx.InjectInput(MouseMove(x, static_cast<i32>(scroll.track.y + 1.0f)));
	rig.ctx.Update();
	EXPECT_EQ(ThumbFill(rig.ctx, scroll), idle);

	rig.ctx.InjectInput(MouseMove(x, onThumb));
	rig.ctx.InjectInput(MouseDown(x, onThumb));
	rig.ctx.Update();
	std::optional<Color> pressed = ThumbFill(rig.ctx, scroll);
	ASSERT_TRUE(pressed.has_value());
	EXPECT_NE(pressed, hovered);
	EXPECT_NE(pressed, idle);

	rig.ctx.InjectInput(MouseMove(offBar, onThumb));
	rig.ctx.Update();
	EXPECT_EQ(ThumbFill(rig.ctx, scroll), pressed);
	EXPECT_NEAR(ScrollOffset(rig.ctx, scroll.parts.viewport), scroll.maxScroll * 0.5f, 0.5f);

	rig.ctx.InjectInput(MouseUp(offBar, onThumb));
	rig.ctx.Update();
	EXPECT_EQ(ThumbFill(rig.ctx, scroll), idle);
}

TEST(WidgetsTest, TreeRowIndentsWithItsDepth) {
	Rig rig;
	auto rowAt = [&rig](u32 depth, std::string_view label) {
		UI::Widgets::TreeRowDesc desc;
		desc.depth = depth;
		desc.hasChildren = true;
		desc.label = label;
		return UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	};
	UI::Widgets::TreeRowParts root = rowAt(0, "Root");
	UI::Widgets::TreeRowParts child = rowAt(1, "Child");
	UI::Widgets::TreeRowParts grandchild = rowAt(2, "Grandchild");
	rig.ctx.Update();

	f32 step = rig.ctx.GetStyleVarFloat("--slt-tree-indent"_hash, 0.0f);
	ASSERT_GT(step, 0.0f);

	auto labelX = [&rig](const UI::Widgets::TreeRowParts& parts) {
		return rig.ctx.GetLayout(parts.labelElement).borderBox.x;
	};
	auto arrowX = [&rig](const UI::Widgets::TreeRowParts& parts) {
		return rig.ctx.GetLayout(parts.arrow).borderBox.x;
	};
	EXPECT_FLOAT_EQ(arrowX(child) - arrowX(root), step);
	EXPECT_FLOAT_EQ(arrowX(grandchild) - arrowX(root), step * 2.0f);
	EXPECT_FLOAT_EQ(labelX(child) - labelX(root), step);
	EXPECT_FLOAT_EQ(labelX(grandchild) - labelX(root), step * 2.0f);

	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(child.row).borderBox.x, rig.ctx.GetLayout(root.row).borderBox.x);
	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(child.row).borderBox.w, rig.ctx.GetLayout(root.row).borderBox.w);
}

TEST(WidgetsTest, TreeRowDrawsElbow) {
	Rig rig;
	auto rowAt = [&rig](u32 depth, u32 guideMask) {
		UI::Widgets::TreeRowDesc desc;
		desc.depth = depth;
		desc.guideMask = guideMask;
		desc.hasChildren = true;
		desc.label = "Row";
		return UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	};
	UI::Widgets::TreeRowParts root = rowAt(0, 0);
	UI::Widgets::TreeRowParts middle = rowAt(2, 0x3);
	UI::Widgets::TreeRowParts last = rowAt(2, 0x1);
	rig.ctx.Update();

	const Color guide = rig.ctx.GetStyleVarColor("--slt-tree-guide"_hash, Color::Magenta());
	ASSERT_NE(guide, Color::Magenta());

	UI::RenderList list;
	rig.ctx.BuildRenderList(list);
	struct Marks {
		Vector<UI::Rect> stems{GetMemoryResource(MemTag::Editor)};
		Vector<UI::Rect> arms{GetMemoryResource(MemTag::Editor)};
	};
	auto marksIn = [&](const UI::Widgets::TreeRowParts& parts) {
		const UI::Rect& row = rig.ctx.GetLayout(parts.row).borderBox;
		Marks found;
		for (const UI::UIQuad& quad : list.quads) {
			if (quad.rect.y < row.y || quad.rect.y >= row.y + row.h) {
				continue;
			}
			const bool vertical = quad.rect.w == 1.0f;
			const bool horizontal = quad.rect.h == 1.0f && quad.rect.w > 1.0f;
			if (!vertical && !horizontal) {
				continue;
			}
			EXPECT_EQ(quad.color0, guide);
			(vertical ? found.stems : found.arms).push_back(quad.rect);
		}
		std::sort(found.stems.begin(), found.stems.end(),
				  [](const UI::Rect& a, const UI::Rect& b) { return a.x < b.x; });
		return found;
	};

	EXPECT_TRUE(marksIn(root).stems.empty());
	EXPECT_TRUE(marksIn(root).arms.empty());

	auto indentOf = [&rig](const UI::Widgets::TreeRowParts& parts) {
		const UI::Element* row = rig.ctx.GetElement(parts.row);
		return rig.ctx.GetLayout(rig.ctx.MakeHandle(row->firstChild)).borderBox;
	};

	const f32 step = rig.ctx.GetStyleVarFloat("--slt-tree-indent"_hash, 0.0f);
	ASSERT_GT(step, 0.0f);

	Marks mid = marksIn(middle);
	ASSERT_EQ(mid.stems.size(), 2u);
	ASSERT_EQ(mid.arms.size(), 1u);
	const UI::Rect midRow = rig.ctx.GetLayout(middle.row).borderBox;
	const UI::Rect midIndent = indentOf(middle);
	for (u32 level = 0; level < 2; ++level) {
		const f32 wanted = std::round(midIndent.x + (static_cast<f32>(level) + 0.5f) * step - 0.5f);
		EXPECT_FLOAT_EQ(mid.stems[level].x, wanted) << "stem " << level;
	}
	EXPECT_FLOAT_EQ(mid.stems[0].h, midRow.h);
	EXPECT_FLOAT_EQ(mid.stems[1].h, midRow.h);
	EXPECT_FLOAT_EQ(mid.arms[0].x, mid.stems[1].x);
	EXPECT_FLOAT_EQ(mid.arms[0].x + mid.arms[0].w, midIndent.x + midIndent.w);
	EXPECT_GT(mid.arms[0].y, midRow.y);
	EXPECT_LT(mid.arms[0].y, midRow.y + midRow.h);

	Marks tail = marksIn(last);
	ASSERT_EQ(tail.stems.size(), 2u);
	ASSERT_EQ(tail.arms.size(), 1u);
	const UI::Rect lastRow = rig.ctx.GetLayout(last.row).borderBox;
	EXPECT_FLOAT_EQ(tail.stems[0].h, lastRow.h);
	EXPECT_LT(tail.stems[1].h, lastRow.h);
	EXPECT_NEAR(tail.stems[1].y + tail.stems[1].h, tail.arms[0].y + tail.arms[0].h, 1.5f);
}

TEST(WidgetsTest, AClosedAncestorDrawsNoThroughLine) {
	Rig rig;
	UI::Widgets::TreeRowDesc desc;
	desc.depth = 3;
	desc.guideMask = 0;
	desc.hasChildren = false;
	desc.label = "Leaf";
	UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	rig.ctx.Update();

	auto stemCount = [&rig]() {
		UI::RenderList list;
		rig.ctx.BuildRenderList(list);
		u32 count = 0;
		for (const UI::UIQuad& quad : list.quads) {
			if (quad.rect.w == 1.0f) {
				++count;
			}
		}
		return count;
	};
	EXPECT_EQ(stemCount(), 1u);

	UI::Widgets::TreeRowDesc open = desc;
	open.guideMask = 0x7;
	UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), open);
	rig.ctx.Update();
	EXPECT_EQ(stemCount(), 4u);
}

TEST(WidgetsTest, TreeGuideIsToken) {
	Rig rig;
	UI::Widgets::TreeRowDesc desc;
	desc.depth = 3;
	desc.guideMask = 0x7;
	desc.hasChildren = true;
	desc.label = "Row";
	UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	rig.ctx.Update();

	auto connectorCount = [&rig]() {
		UI::RenderList list;
		rig.ctx.BuildRenderList(list);
		u32 count = 0;
		for (const UI::UIQuad& quad : list.quads) {
			if (quad.rect.w == 1.0f || (quad.rect.h == 1.0f && quad.rect.w > 1.0f)) {
				++count;
			}
		}
		return count;
	};
	ASSERT_EQ(connectorCount(), 4u);

	rig.ctx.SetStyleVar("--slt-tree-guide"_hash, "#00000000");
	rig.ctx.Update();
	EXPECT_EQ(connectorCount(), 0u);
}

TEST(WidgetsTest, CaretGutterNeverShrinks) {
	Rig rig;
	UI::ElementHandle host = rig.ctx.CreateElement();
	UI::Style hostStyle;
	hostStyle.width = UI::Length::Pixel(200.0f);
	hostStyle.flexDirection = UI::FlexDirection::Column;
	rig.ctx.SetStyle(host, hostStyle);
	rig.ctx.AppendChild(rig.ctx.GetRoot(), host);

	UI::Widgets::TreeRowDesc shallow;
	shallow.hasChildren = true;
	shallow.label = "Deeply nested object";
	UI::Widgets::TreeRowParts shallowRow = UI::Widgets::TreeRow(rig.ctx, host, shallow);

	UI::Widgets::TreeRowDesc deep = shallow;
	deep.depth = 12;
	UI::Widgets::TreeRowParts deepRow = UI::Widgets::TreeRow(rig.ctx, host, deep);
	rig.ctx.Update();

	f32 gutter = rig.ctx.GetStyleVarFloat("--slt-tree-gutter"_hash, 0.0f);
	ASSERT_GT(gutter, 0.0f);

	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(shallowRow.arrow).borderBox.w, gutter);
	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(deepRow.arrow).borderBox.w, gutter);
	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(deepRow.arrow).borderBox.x -
						rig.ctx.GetLayout(shallowRow.arrow).borderBox.x,
					12.0f * rig.ctx.GetStyleVarFloat("--slt-tree-indent"_hash, 0.0f));
}

TEST(WidgetsTest, IndentFollowsTheme) {
	Rig rig;
	rig.ctx.SetStyleVar("--slt-tree-indent"_hash, "24");
	rig.ctx.LoadStyleSheet(".slt-tree-row { padding: 2 4 2 4; }");

	UI::Widgets::TreeRowDesc desc;
	desc.label = "Root";
	UI::Widgets::TreeRowParts root = UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	desc.depth = 1;
	desc.label = "Child";
	UI::Widgets::TreeRowParts child = UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	rig.ctx.Update();

	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(child.labelElement).borderBox.x -
						rig.ctx.GetLayout(root.labelElement).borderBox.x,
					24.0f);
}

TEST(WidgetsTest, TreeRowSelectExpandAndContextActions) {
	Rig rig;
	KeyMod selectMods = KeyMod::None;
	int selects = 0;
	int toggles = 0;
	int doubles = 0;
	int rights = 0;
	UI::Widgets::TreeRowDesc desc;
	desc.hasChildren = true;
	desc.label = "Node";
	desc.userTag = 77;
	desc.onSelect = [&](KeyMod mods) {
		selectMods = mods;
		++selects;
	};
	desc.onToggleExpand = [&] { ++toggles; };
	desc.onDoubleClick = [&] { ++doubles; };
	desc.onRightClick = [&] { ++rights; };
	auto parts = UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	rig.ctx.Update();

	EXPECT_EQ(rig.ctx.GetElement(parts.row)->userTag, 77u);

	Click(rig.ctx, Center(rig.ctx, parts.labelElement), KeyMod::Ctrl);
	EXPECT_EQ(selects, 1);
	EXPECT_EQ(selectMods, KeyMod::Ctrl);

	Click(rig.ctx, Center(rig.ctx, parts.arrow));
	EXPECT_EQ(toggles, 1);
	EXPECT_EQ(selects, 1);

	Vec2 rowPoint = Center(rig.ctx, parts.labelElement);
	Click(rig.ctx, rowPoint);
	Click(rig.ctx, rowPoint);
	EXPECT_GE(doubles, 1);

	InputEvent rightDown{};
	rightDown.type = InputEventType::MouseDown;
	rightDown.button = MouseButton::Right;
	rightDown.mousePosPx = {static_cast<i32>(rowPoint.x), static_cast<i32>(rowPoint.y)};
	InputEvent rightUp = rightDown;
	rightUp.type = InputEventType::MouseUp;
	rig.ctx.InjectInput(rightDown);
	rig.ctx.InjectInput(rightUp);
	rig.ctx.Update();
	EXPECT_EQ(rights, 1);
}

TEST(WidgetsTest, TreeRowSelectsOnClickNotPress) {
	Rig rig;
	int selects = 0;
	UI::Widgets::TreeRowDesc desc;
	desc.label = "Node";
	desc.onSelect = [&](KeyMod) { ++selects; };
	desc.onDragRow = [](Vec2) {};
	auto parts = UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);

	UI::ElementHandle other = rig.ctx.CreateElement();
	UI::Style otherStyle;
	otherStyle.width = UI::Length::Pixel(120.0f);
	otherStyle.height = UI::Length::Pixel(40.0f);
	rig.ctx.SetStyle(other, otherStyle);
	rig.ctx.AppendChild(rig.ctx.GetRoot(), other);
	rig.ctx.Update();

	Vec2 point = Center(rig.ctx, parts.labelElement);
	ASSERT_EQ(rig.ctx.HitTest(point), parts.row);
	ASSERT_EQ(rig.ctx.HitTest(Center(rig.ctx, other)), other);
	rig.ctx.InjectInput(MouseDown(static_cast<i32>(point.x), static_cast<i32>(point.y)));
	rig.ctx.Update();
	EXPECT_EQ(selects, 0);

	rig.ctx.InjectInput(MouseUp(static_cast<i32>(point.x), static_cast<i32>(point.y)));
	rig.ctx.Update();
	EXPECT_EQ(selects, 1);

	Vec2 elsewhere = Center(rig.ctx, other);
	rig.ctx.InjectInput(MouseDown(static_cast<i32>(point.x), static_cast<i32>(point.y)));
	rig.ctx.InjectInput(MouseMove(static_cast<i32>(elsewhere.x), static_cast<i32>(elsewhere.y)));
	rig.ctx.InjectInput(MouseUp(static_cast<i32>(elsewhere.x), static_cast<i32>(elsewhere.y)));
	rig.ctx.Update();
	EXPECT_EQ(selects, 1);
}

TEST(WidgetsTest, TreeRowActsAsDropTarget) {
	Rig rig;
	i32 dropped = 0;
	UI::Widgets::TreeRowDesc desc;
	desc.label = "Target";
	desc.onDragOver = [](const UI::DragPayload& payload) { return payload.type == "SceneObject"_hash; };
	desc.onDrop = [&](const UI::DragPayload& payload, Vec2) { dropped = payload.data.As<i32>(); };
	auto parts = UI::Widgets::TreeRow(rig.ctx, rig.ctx.GetRoot(), desc);
	rig.ctx.Update();

	UI::DragPayload payload;
	payload.type = "SceneObject"_hash;
	payload.data = Variant(i32(123));

	EXPECT_EQ(rig.ctx.UpdateDragTarget(payload, Center(rig.ctx, parts.labelElement)), parts.row);
	EXPECT_TRUE(rig.ctx.DeliverDrop(payload, Center(rig.ctx, parts.labelElement)));
	EXPECT_EQ(dropped, 123);
}

TEST(WidgetsTest, SkinLoadsOncePerContext) {
	Rig rig;
	EXPECT_TRUE(rig.ctx.LoadStyleSheetOnce("test-sheet"_hash, ".x { opacity: 0.5; }"));
	EXPECT_FALSE(rig.ctx.LoadStyleSheetOnce("test-sheet"_hash, ".x { opacity: 0.5; }"));

	UI::Widgets::EnsureSkin(rig.ctx);
	UI::Widgets::EnsureSkin(rig.ctx);
	rig.ctx.ClearStyleSheets();
	EXPECT_TRUE(rig.ctx.LoadStyleSheetOnce("test-sheet"_hash, ".x { opacity: 0.5; }"));
}

TEST(WidgetsTest, SeparatorFollowsTheParentFlowDirection) {
	Rig rig;
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.width = UI::Length::Pixel(200.0f);
	UI::ElementHandle column = MakeChild(rig.ctx, rig.ctx.GetRoot(), columnStyle);
	UI::ElementHandle rule = UI::Widgets::Separator(rig.ctx, column);

	UI::Style toolbarStyle;
	toolbarStyle.flexDirection = UI::FlexDirection::Row;
	toolbarStyle.alignItems = UI::Align::Center;
	toolbarStyle.width = UI::Length::Pixel(200.0f);
	UI::ElementHandle toolbar = MakeChild(rig.ctx, rig.ctx.GetRoot(), toolbarStyle);
	UI::Widgets::ButtonDesc button;
	button.label = "Play";
	UI::ElementHandle play = UI::Widgets::Button(rig.ctx, toolbar, button);
	UI::ElementHandle bar = UI::Widgets::Separator(rig.ctx, toolbar);
	button.label = "Stop";
	UI::ElementHandle stop = UI::Widgets::Button(rig.ctx, toolbar, button);
	rig.ctx.Update();

	EXPECT_NEAR(rig.ctx.GetLayout(rule).borderBox.w, 200.0f, 0.5f);
	EXPECT_NEAR(rig.ctx.GetLayout(rule).borderBox.h, 1.0f, 0.5f);

	const UI::Rect& barBox = rig.ctx.GetLayout(bar).borderBox;
	EXPECT_NEAR(barBox.w, 1.0f, 0.5f);
	EXPECT_GT(barBox.h, 10.0f);
	f32 playWidth = rig.ctx.GetLayout(play).borderBox.w;
	f32 stopWidth = rig.ctx.GetLayout(stop).borderBox.w;
	EXPECT_NEAR(playWidth, 4.0f * MockFontSystem::ADVANCE + 24.0f, 0.5f);
	EXPECT_NEAR(stopWidth, playWidth, 0.5f);
}

TEST(WidgetsTest, TextFieldNeverWrapsHoweverNarrow) {
	Rig rig;
	UI::Style containerStyle = FixedBox(70.0f, 40.0f);
	containerStyle.alignItems = UI::Align::Start;
	UI::ElementHandle container = MakeChild(rig.ctx, rig.ctx.GetRoot(), containerStyle);
	UI::Widgets::TextFieldDesc desc;
	desc.text = "a much longer value than fits";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, container, desc);
	rig.ctx.Update();

	const UI::Rect& box = rig.ctx.GetLayout(field).borderBox;
	EXPECT_NEAR(box.h, MockFontSystem::LINE_HEIGHT + 6.0f, 0.5f);
	EXPECT_LE(box.x + box.w, 70.01f);

	rig.ctx.InjectInput(MouseDown(static_cast<i32>(box.x + box.w - 4.0f), static_cast<i32>(box.y + box.h * 0.5f)));
	rig.ctx.InjectInput(MouseUp(static_cast<i32>(box.x + box.w - 4.0f), static_cast<i32>(box.y + box.h * 0.5f)));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetFocused(), field);
	TypeReplace(rig.ctx, "x");
	EXPECT_EQ(rig.ctx.GetTextString(field), "x");
}

namespace {

	struct OpenPicker {
		UI::ElementHandle content;
		UI::ElementHandle square;
		UI::ElementHandle hue;
		UI::ElementHandle alpha;
		UI::ElementHandle hex;
	};

	OpenPicker FindOpenPicker(UI::Context& ctx) {
		OpenPicker found;
		for (u32 i = 0; i < ctx.GetOpenPopupCount(); ++i) {
			UI::ElementHandle popup = ctx.GetOpenPopup(i);
			if (ctx.HasClass(popup, StringHash("slt-color-picker"))) {
				found.content = popup;
			}
		}
		if (!found.content.IsValid()) {
			return found;
		}
		found.square = FindByClass(ctx, found.content, StringHash("slt-color-sv"), 0);
		found.hue = FindByClass(ctx, found.content, StringHash("slt-color-strip"), 0);
		found.alpha = FindByClass(ctx, found.content, StringHash("slt-color-strip"), 1);
		found.hex = FindByClass(ctx, found.content, StringHash("slt-color-hex"), 0);
		return found;
	}

	void DragAcross(UI::Context& ctx, UI::ElementHandle element, f32 across, f32 down = 0.5f) {
		const UI::Rect& box = ctx.GetLayout(element).content;
		i32 x = static_cast<i32>(box.x + box.w * across);
		i32 y = static_cast<i32>(box.y + box.h * down);
		ctx.InjectInput(MouseMove(static_cast<i32>(box.x + 1.0f), static_cast<i32>(box.y + 1.0f)));
		ctx.InjectInput(MouseDown(static_cast<i32>(box.x + 1.0f), static_cast<i32>(box.y + 1.0f)));
		ctx.Update();
		ctx.InjectInput(MouseMove(x, y));
		ctx.Update();
		ctx.InjectInput(MouseUp(x, y));
		ctx.Update();
	}

	struct ColorRig {
		Rig rig;
		UI::Widgets::ColorFieldParts parts;
		Color reported = Color::Black();
		u32 changes = 0;
		u32 begins = 0;
		u32 ends = 0;

		explicit ColorRig(Color initial) {
			UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(360.0f, 24.0f));
			parts = UI::Widgets::ColorSwatchField(
				rig.ctx, row, initial, Handle<FontAsset>{}, 13.0f,
				[this](Color value) { reported = value; ++changes; },
				[this]() { ++begins; }, [this]() { ++ends; });
			rig.ctx.Update();
		}

		OpenPicker Open() {
			Click(rig.ctx, Center(rig.ctx, parts.swatch));
			rig.ctx.Update();
			return FindOpenPicker(rig.ctx);
		}
	};

} // anonymous namespace

TEST(WidgetsTest, SwatchOpensPicker) {
	ColorRig rig{Color(1.0f, 0.0f, 0.0f, 1.0f)};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.content.IsValid());
	ASSERT_TRUE(picker.square.IsValid());
	ASSERT_TRUE(picker.hue.IsValid());

	DragAcross(rig.rig.ctx, picker.hue, 1.0f / 3.0f);
	ASSERT_GT(rig.changes, 0u);
	EXPECT_GT(rig.reported.g, 0.9f) << "hue " << rig.reported.ToHex();
	EXPECT_LT(rig.reported.r, 0.1f);
	EXPECT_LT(rig.reported.b, 0.1f);

	const f32 channels[4] = {rig.reported.r, rig.reported.g, rig.reported.b, rig.reported.a};
	for (u32 i = 0; i < 4; ++i) {
		EXPECT_EQ(rig.rig.ctx.GetTextString(rig.parts.vector.components[i].field),
				  std::to_string(static_cast<i32>(std::lround(channels[i] * 255.0f))));
	}
}

TEST(WidgetsTest, DraggingTheSquareMovesSaturationAndValue) {
	ColorRig rig{Color(0.0f, 0.4f, 0.8f, 1.0f)};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.square.IsValid());

	DragAcross(rig.rig.ctx, picker.square, 0.0f, 1.0f);
	ASSERT_GT(rig.changes, 0u);
	EXPECT_NEAR(rig.reported.r, 0.0f, 0.01f);
	EXPECT_NEAR(rig.reported.g, 0.0f, 0.01f);
	EXPECT_NEAR(rig.reported.b, 0.0f, 0.01f);

	DragAcross(rig.rig.ctx, picker.square, 1.0f, 0.0f);
	EXPECT_GT(rig.reported.b, rig.reported.g) << rig.reported.ToHex();
	EXPECT_GT(rig.reported.g, rig.reported.r);
}

TEST(WidgetsTest, ThePickerKeepsItsHueThroughBlack) {
	ColorRig rig{Color(0.0f, 0.4f, 0.8f, 1.0f)};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.square.IsValid());

	DragAcross(rig.rig.ctx, picker.square, 0.0f, 1.0f);
	ASSERT_NEAR(rig.reported.r + rig.reported.g + rig.reported.b, 0.0f, 0.02f);

	DragAcross(rig.rig.ctx, picker.square, 1.0f, 0.0f);
	EXPECT_GT(rig.reported.b, 0.9f) << rig.reported.ToHex();
	EXPECT_LT(rig.reported.r, 0.1f);
}

TEST(WidgetsTest, HexFieldTakesRGBAStringIncludingAlpha) {
	ColorRig rig{Color::Black()};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.hex.IsValid());

	Click(rig.rig.ctx, Center(rig.rig.ctx, picker.hex));
	TypeReplace(rig.rig.ctx, "#ffffffaa");
	rig.rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.rig.ctx.Update();

	ASSERT_GT(rig.changes, 0u);
	EXPECT_FLOAT_EQ(rig.reported.r, 1.0f);
	EXPECT_FLOAT_EQ(rig.reported.g, 1.0f);
	EXPECT_FLOAT_EQ(rig.reported.b, 1.0f);
	EXPECT_NEAR(rig.reported.a, 170.0f / 255.0f, 1.0f / 510.0f);
	EXPECT_EQ(rig.rig.ctx.GetTextString(rig.parts.vector.components[3].field), "170");
}

TEST(WidgetsTest, UnparseableHexStringLeavesColorAlone) {
	ColorRig rig{Color(0.0f, 0.4f, 0.8f, 1.0f)};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.hex.IsValid());
	const u32 before = rig.changes;

	Click(rig.rig.ctx, Center(rig.rig.ctx, picker.hex));
	TypeReplace(rig.rig.ctx, "not a color");
	rig.rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.rig.ctx.Update();

	EXPECT_EQ(rig.changes, before);
	EXPECT_EQ(rig.rig.ctx.GetTextString(picker.hex), Color(0.0f, 0.4f, 0.8f, 1.0f).ToHex());
}

TEST(WidgetsTest, PickerOpensOnValueFieldHoldsNow) {
	ColorRig rig{Color::White()};
	UI::Widgets::SetColorSwatchFieldValue(rig.rig.ctx, rig.parts, Color(0.0f, 0.4f, 0.8f, 1.0f));
	rig.rig.ctx.Update();

	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.hex.IsValid());
	EXPECT_EQ(rig.rig.ctx.GetTextString(picker.hex), Color(0.0f, 0.4f, 0.8f, 1.0f).ToHex());
}

TEST(WidgetsTest, PickerUndoGroups) {
	ColorRig rig{Color(1.0f, 0.0f, 0.0f, 1.0f)};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.hue.IsValid());

	DragAcross(rig.rig.ctx, picker.hue, 0.5f);
	EXPECT_EQ(rig.begins, 1u);
	EXPECT_EQ(rig.ends, 1u);
	EXPECT_GT(rig.changes, 0u);

	DragAcross(rig.rig.ctx, picker.square, 0.25f, 0.25f);
	EXPECT_EQ(rig.begins, 2u);
	EXPECT_EQ(rig.ends, 2u);

	Click(rig.rig.ctx, Center(rig.rig.ctx, picker.hex));
	TypeReplace(rig.rig.ctx, "#102030");
	rig.rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.rig.ctx.Update();
	EXPECT_EQ(rig.begins, 3u);
	EXPECT_EQ(rig.ends, 3u);
}

TEST(WidgetsTest, EscapeRestoresPickerColor) {
	const Color red(1.0f, 0.0f, 0.0f, 1.0f);
	ColorRig rig{red};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.hue.IsValid());
	DragAcross(rig.rig.ctx, picker.hue, 0.5f);
	ASSERT_FALSE(rig.reported == red);

	rig.rig.ctx.InjectInput(KeyPress(Key::Escape));
	rig.rig.ctx.Update();
	EXPECT_FALSE(rig.rig.ctx.IsPopupOpen(picker.content));
	EXPECT_TRUE(rig.reported == red) << rig.reported.ToHex();
	EXPECT_EQ(rig.rig.ctx.GetTextString(rig.parts.vector.components[0].field), "255");
	EXPECT_EQ(rig.rig.ctx.GetTextString(rig.parts.vector.components[2].field), "0");
	EXPECT_EQ(rig.begins, rig.ends);
}

TEST(WidgetsTest, OrphanPickerCloses) {
	for (const bool escape : {false, true}) {
		ColorRig rig{Color(1.0f, 0.0f, 0.0f, 1.0f)};
		OpenPicker picker = rig.Open();
		ASSERT_TRUE(picker.hue.IsValid());
		DragAcross(rig.rig.ctx, picker.hue, 0.5f);
		const u32 changes = rig.changes;
		ASSERT_GT(changes, 0u);

		rig.rig.ctx.DestroyElement(rig.parts.root);
		rig.rig.ctx.Update();
		if (escape) {
			rig.rig.ctx.InjectInput(KeyPress(Key::Escape));
			rig.rig.ctx.Update();
		} else {
			DragAcross(rig.rig.ctx, picker.hue, 0.25f);
		}
		EXPECT_FALSE(rig.rig.ctx.IsPopupOpen(picker.content)) << (escape ? "Escape" : "a drag");
		EXPECT_EQ(rig.changes, changes) << (escape ? "Escape" : "a drag");
		EXPECT_EQ(rig.begins, rig.ends);
	}
}

TEST(WidgetsTest, EnterClosesThePickerKeepingItsColor) {
	ColorRig rig{Color(1.0f, 0.0f, 0.0f, 1.0f)};
	OpenPicker picker = rig.Open();
	DragAcross(rig.rig.ctx, picker.hue, 0.5f);
	const Color picked = rig.reported;
	const u32 changes = rig.changes;

	rig.rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.rig.ctx.Update();
	EXPECT_FALSE(rig.rig.ctx.IsPopupOpen(picker.content));
	EXPECT_TRUE(rig.reported == picked);
	EXPECT_EQ(rig.changes, changes);
}

TEST(WidgetsTest, HexEscapeCancels) {
	const Color blue(0.0f, 0.4f, 0.8f, 1.0f);
	ColorRig rig{blue};
	OpenPicker picker = rig.Open();
	ASSERT_TRUE(picker.hex.IsValid());
	Click(rig.rig.ctx, Center(rig.rig.ctx, picker.hex));
	TypeReplace(rig.rig.ctx, "#00ff00");
	rig.rig.ctx.InjectInput(KeyPress(Key::Escape));
	rig.rig.ctx.Update();
	EXPECT_FALSE(rig.rig.ctx.IsPopupOpen(picker.content));
	EXPECT_TRUE(rig.reported == blue) << rig.reported.ToHex();

	ColorRig untouched{blue};
	OpenPicker quiet = untouched.Open();
	untouched.rig.ctx.InjectInput(KeyPress(Key::Escape));
	untouched.rig.ctx.Update();
	EXPECT_FALSE(untouched.rig.ctx.IsPopupOpen(quiet.content));
	EXPECT_EQ(untouched.changes, 0u);
	EXPECT_EQ(untouched.begins, 0u);
}

namespace {

	struct DialogRig {
		Rig rig;
		UI::ElementHandle card;
		UI::ElementHandle cancel;
		UI::ElementHandle accept;
		int accepts = 0;
		int cancels = 0;

		DialogRig() {
			card = rig.ctx.CreateElement();
			UI::Widgets::ButtonDesc cancelDesc;
			cancelDesc.label = "Cancel";
			cancelDesc.onClick = [this] { ++cancels; };
			cancel = UI::Widgets::Button(rig.ctx, card, cancelDesc);

			UI::Widgets::ButtonDesc acceptDesc;
			acceptDesc.label = "OK";
			acceptDesc.onClick = [this] { ++accepts; };
			acceptDesc.defaultForPopup = card;
			accept = UI::Widgets::Button(rig.ctx, card, acceptDesc);

			rig.ctx.OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center);
			rig.ctx.Update();
		}

		void Press(Key key) {
			rig.ctx.InjectInput(KeyPress(key));
			rig.ctx.Update();
		}
	};

} // anonymous namespace

TEST(WidgetsTest, EnterPressesTheDialogsDefaultButton) {
	DialogRig dialog;
	dialog.Press(Key::Enter);

	EXPECT_EQ(dialog.accepts, 1);
	EXPECT_EQ(dialog.cancels, 0);
}

TEST(WidgetsTest, EnterNeedsDefault) {
	Rig rig;
	UI::ElementHandle card = rig.ctx.CreateElement();
	int clicks = 0;
	UI::Widgets::ButtonDesc desc;
	desc.label = "OK";
	desc.onClick = [&] { ++clicks; };
	UI::Widgets::Button(rig.ctx, card, desc);
	rig.ctx.OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss);
	rig.ctx.Update();

	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(clicks, 0);
}

TEST(WidgetsTest, DefaultButtonMarked) {
	DialogRig dialog;
	EXPECT_TRUE(dialog.rig.ctx.HasClass(dialog.accept, StringHash("slt-button-default")));
	EXPECT_FALSE(dialog.rig.ctx.HasClass(dialog.cancel, StringHash("slt-button-default")));
}

TEST(WidgetsTest, EnterInTextFieldCommitsThenSubmits) {
	Rig rig;
	UI::ElementHandle card = rig.ctx.CreateElement();

	str committed;
	str seenByAccept;
	UI::Widgets::TextFieldDesc fieldDesc;
	fieldDesc.text = "";
	fieldDesc.onCommit = [&](std::string_view text) { committed = str(text); };
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, card, fieldDesc);

	UI::Widgets::ButtonDesc acceptDesc;
	acceptDesc.label = "Create";
	acceptDesc.defaultForPopup = card;
	acceptDesc.onClick = [&] { seenByAccept = rig.ctx.GetTextString(field); };
	UI::Widgets::Button(rig.ctx, card, acceptDesc);

	rig.ctx.OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss);
	rig.ctx.Update();

	Click(rig.ctx, Center(rig.ctx, field));
	TypeReplace(rig.ctx, "MyGame");
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();

	EXPECT_EQ(committed, "MyGame");
	EXPECT_EQ(seenByAccept, "MyGame");
}

TEST(WidgetsTest, GuardedDefaultRefusesEnter) {
	Rig rig;
	UI::ElementHandle card = rig.ctx.CreateElement();
	bool armed = false;
	int deletes = 0;
	UI::Widgets::ButtonDesc desc;
	desc.label = "Delete";
	desc.defaultForPopup = card;
	desc.onClick = [&] {
		if (!armed) {
			return;
		}
		++deletes;
		rig.ctx.ClosePopup(card);
	};
	UI::Widgets::Button(rig.ctx, card, desc);
	rig.ctx.OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss);
	rig.ctx.Update();

	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(deletes, 0);
	EXPECT_TRUE(rig.ctx.IsPopupOpen(card));

	armed = true;
	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(deletes, 1);
	EXPECT_FALSE(rig.ctx.IsPopupOpen(card));
}

TEST(WidgetsTest, TheTopmostPopupOwnsEnter) {
	DialogRig under;
	UI::ElementHandle over = under.rig.ctx.CreateElement();
	int overAccepts = 0;
	UI::Widgets::ButtonDesc desc;
	desc.label = "Yes";
	desc.defaultForPopup = over;
	desc.onClick = [&] { ++overAccepts; };
	UI::Widgets::Button(under.rig.ctx, over, desc);
	under.rig.ctx.OpenPopup(over, {}, UI::PopupFlags::NoOutsideDismiss);
	under.rig.ctx.Update();

	under.Press(Key::Enter);
	EXPECT_EQ(overAccepts, 1);
	EXPECT_EQ(under.accepts, 0);

	under.rig.ctx.ClosePopup(over);
	under.rig.ctx.DestroyElement(over);
	under.rig.ctx.Update();
	under.Press(Key::Enter);
	EXPECT_EQ(under.accepts, 1);
}

TEST(WidgetsTest, ADestroyedDefaultButtonLeavesNoRecord) {
	Rig rig;
	UI::ElementHandle card = rig.ctx.CreateElement();
	int accepts = 0;
	UI::Widgets::ButtonDesc desc;
	desc.label = "OK";
	desc.defaultForPopup = card;
	desc.onClick = [&] { ++accepts; };
	UI::ElementHandle accept = UI::Widgets::Button(rig.ctx, card, desc);
	rig.ctx.OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss);
	rig.ctx.Update();

	rig.ctx.DestroyElement(accept);
	rig.ctx.Update();

	int strangerClicks = 0;
	UI::Widgets::ButtonDesc stranger;
	stranger.label = "Not me";
	stranger.onClick = [&] { ++strangerClicks; };
	UI::Widgets::Button(rig.ctx, card, stranger);
	rig.ctx.Update();

	rig.ctx.InjectInput(KeyPress(Key::Enter));
	rig.ctx.Update();
	EXPECT_EQ(accepts, 0);
	EXPECT_EQ(strangerClicks, 0);
}

TEST(WidgetsTest, EscapeStillFoldsDialogThatHasDefault) {
	DialogRig dialog;
	dialog.Press(Key::Escape);
	EXPECT_FALSE(dialog.rig.ctx.IsPopupOpen(dialog.card));
	EXPECT_EQ(dialog.accepts, 0);
}

namespace {

	struct KeyEaterRig {
		Rig rig;
		UI::ElementHandle list;
		int eaten = 0;

		KeyEaterRig() {
			list = rig.ctx.CreateElement();
			rig.ctx.AppendChild(rig.ctx.GetRoot(), list);
			rig.ctx.SetFocusable(list, true);
			rig.ctx.Events(list).onKeyDown = [this](Key, KeyMod) {
				++eaten;
				return true;
			};
			rig.ctx.SetFocus(list);
			rig.ctx.Update();
		}

		UI::ElementHandle OpenDialog(int& accepts, UI::PopupFlags flags) {
			UI::ElementHandle card = rig.ctx.CreateElement();
			UI::Widgets::ButtonDesc desc;
			desc.label = "Delete";
			desc.defaultForPopup = card;
			desc.onClick = [&accepts] { ++accepts; };
			UI::Widgets::Button(rig.ctx, card, desc);
			rig.ctx.OpenPopup(card, {}, flags);
			rig.ctx.Update();
			return card;
		}

		void Press(Key key) {
			rig.ctx.InjectInput(KeyPress(key));
			rig.ctx.Update();
		}
	};

	constexpr UI::PopupFlags DIALOG =
		UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center | UI::PopupFlags::Scrim;

} // anonymous namespace

TEST(WidgetsTest, DialogTakesEnter) {
	KeyEaterRig under;
	int accepts = 0;
	under.OpenDialog(accepts, DIALOG);

	under.Press(Key::Enter);
	EXPECT_EQ(accepts, 1);
	EXPECT_EQ(under.eaten, 0);
}

TEST(WidgetsTest, DialogTakesEscape) {
	KeyEaterRig under;
	int accepts = 0;
	UI::ElementHandle card = under.OpenDialog(accepts, DIALOG);

	under.Press(Key::Escape);
	EXPECT_FALSE(under.rig.ctx.IsPopupOpen(card));
	EXPECT_EQ(under.eaten, 0);
	EXPECT_EQ(accepts, 0);
}

TEST(WidgetsTest, KeysReturnAfterDialog) {
	KeyEaterRig under;
	int accepts = 0;
	UI::ElementHandle card = under.OpenDialog(accepts, DIALOG);
	EXPECT_EQ(under.rig.ctx.GetFocused(), under.list);

	under.rig.ctx.ClosePopup(card);
	under.rig.ctx.Update();

	under.Press(Key::Down);
	EXPECT_EQ(under.eaten, 1);
}

TEST(WidgetsTest, DialogBlocksTyping) {
	Rig rig;
	UI::Widgets::TextFieldDesc fieldDesc;
	fieldDesc.text = "start";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, rig.ctx.GetRoot(), fieldDesc);
	rig.ctx.Update();
	Click(rig.ctx, Center(rig.ctx, field));
	EXPECT_EQ(rig.ctx.GetFocused(), field);

	UI::ElementHandle card = rig.ctx.CreateElement();
	rig.ctx.OpenPopup(card, {}, DIALOG);
	rig.ctx.Update();

	rig.ctx.InjectInput(TextChar(U'x'));
	rig.ctx.InjectInput(KeyPress(Key::Backspace));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(field), "start");
}

TEST(WidgetsTest, UnscrimmedPopupLeavesElementUnderItKeys) {
	KeyEaterRig under;
	int accepts = 0;
	under.OpenDialog(accepts, UI::PopupFlags::None);

	under.Press(Key::Enter);
	EXPECT_EQ(under.eaten, 1);
	EXPECT_EQ(accepts, 0);
}

TEST(WidgetsTest, PressOnScrimLeavesDialogsFieldTyping) {
	Rig rig;
	UI::ElementHandle card = rig.ctx.CreateElement();
	UI::Widgets::TextFieldDesc fieldDesc;
	fieldDesc.text = "seed";
	UI::ElementHandle field = UI::Widgets::TextField(rig.ctx, card, fieldDesc);
	rig.ctx.OpenPopup(card, {}, DIALOG);
	rig.ctx.SetFocus(field);
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.GetFocused(), field);
	const UI::Rect& box = rig.ctx.GetLayout(card).borderBox;
	ASSERT_GT(box.x, 4.0f);

	Click(rig.ctx, {2.0f, 2.0f});
	EXPECT_EQ(rig.ctx.GetFocused(), field);
	rig.ctx.InjectInput(TextChar(U'x'));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetTextString(field), "x");
}

TEST(WidgetsTest, ScrimPressKeepsKeys) {
	KeyEaterRig under;
	int accepts = 0;
	UI::ElementHandle card = under.OpenDialog(accepts, DIALOG);
	const UI::Rect& box = under.rig.ctx.GetLayout(card).borderBox;
	ASSERT_GT(box.x, 4.0f);

	Click(under.rig.ctx, {2.0f, 2.0f});
	EXPECT_TRUE(under.rig.ctx.IsPopupOpen(card));
	under.rig.ctx.ClosePopup(card);
	under.rig.ctx.Update();

	under.Press(Key::Down);
	EXPECT_EQ(under.eaten, 1);
}

TEST(WidgetsTest, PressBesideUnscrimmedPopupStillDropsFocus) {
	KeyEaterRig under;
	int accepts = 0;
	UI::ElementHandle card = under.OpenDialog(accepts, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center);
	const UI::Rect& box = under.rig.ctx.GetLayout(card).borderBox;
	ASSERT_GT(box.x, 4.0f);

	Click(under.rig.ctx, {2.0f, 2.0f});
	EXPECT_TRUE(under.rig.ctx.IsPopupOpen(card));
	EXPECT_FALSE(under.rig.ctx.GetFocused().IsValid());
}

namespace {

	UI::ElementHandle AddClosingButton(Rig& rig, UI::ElementHandle card, int& accepts,
									   std::function<void()> action = {}) {
		UI::Widgets::ButtonDesc desc;
		desc.label = "OK";
		desc.defaultForPopup = card;
		UI::Context* ctx = &rig.ctx;
		desc.onClick = [ctx, card, &accepts, action] {
			++accepts;
			if (action) {
				action();
			}
			ctx->ClosePopup(card);
		};
		return UI::Widgets::Button(rig.ctx, card, desc);
	}

	struct ClosingDialog {
		UI::ElementHandle card;
		UI::ElementHandle button;
	};

	ClosingDialog OpenClosingDialog(Rig& rig, int& accepts, UI::PopupFlags flags,
									std::function<void()> action = {}) {
		ClosingDialog dialog;
		dialog.card = rig.ctx.CreateElement();
		dialog.button = AddClosingButton(rig, dialog.card, accepts, std::move(action));
		rig.ctx.OpenPopup(dialog.card, {}, flags);
		rig.ctx.Update();
		return dialog;
	}

} // anonymous namespace

TEST(WidgetsTest, DialogButtonReturnsKeyboard) {
	KeyEaterRig under;
	int accepts = 0;
	ClosingDialog dialog = OpenClosingDialog(under.rig, accepts, DIALOG);
	ASSERT_EQ(under.rig.ctx.GetFocused(), under.list);

	Click(under.rig.ctx, Center(under.rig.ctx, dialog.button));
	ASSERT_EQ(accepts, 1);
	ASSERT_FALSE(under.rig.ctx.IsPopupOpen(dialog.card));

	EXPECT_EQ(under.rig.ctx.GetFocused(), under.list);
	under.Press(Key::Down);
	EXPECT_EQ(under.eaten, 1);
}

TEST(WidgetsTest, DialogFieldReturnsKeyboard) {
	KeyEaterRig under;
	int accepts = 0;
	UI::ElementHandle card = under.rig.ctx.CreateElement();
	UI::Widgets::TextFieldDesc fieldDesc;
	fieldDesc.text = "name";
	UI::ElementHandle field = UI::Widgets::TextField(under.rig.ctx, card, fieldDesc);
	AddClosingButton(under.rig, card, accepts);
	under.rig.ctx.OpenPopup(card, {}, DIALOG);
	under.rig.ctx.Update();
	Click(under.rig.ctx, Center(under.rig.ctx, field));
	ASSERT_EQ(under.rig.ctx.GetFocused(), field);

	under.Press(Key::Enter);
	ASSERT_EQ(accepts, 1);
	ASSERT_FALSE(under.rig.ctx.IsPopupOpen(card));
	EXPECT_EQ(under.rig.ctx.GetFocused(), under.list);
}

TEST(WidgetsTest, DialogActionKeepsNewFocus) {
	KeyEaterRig under;
	UI::ElementHandle other = under.rig.ctx.CreateElement();
	under.rig.ctx.AppendChild(under.rig.ctx.GetRoot(), other);
	under.rig.ctx.SetFocusable(other, true);
	int accepts = 0;
	UI::Context* ctx = &under.rig.ctx;
	ClosingDialog dialog = OpenClosingDialog(under.rig, accepts, DIALOG, [ctx, other] { ctx->SetFocus(other); });

	Click(under.rig.ctx, Center(under.rig.ctx, dialog.button));
	ASSERT_EQ(accepts, 1);
	EXPECT_EQ(under.rig.ctx.GetFocused(), other);
}

TEST(WidgetsTest, DroppedKeyboardStaysDropped) {
	KeyEaterRig under;
	int accepts = 0;
	ClosingDialog dialog =
		OpenClosingDialog(under.rig, accepts, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center);
	UI::ElementHandle chrome = under.rig.ctx.CreateElement();
	UI::Style strip;
	strip.width = UI::Length::Pixel(80.0f);
	strip.height = UI::Length::Pixel(20.0f);
	under.rig.ctx.SetStyle(chrome, strip);
	under.rig.ctx.AppendChild(dialog.card, chrome);
	under.rig.ctx.Update();
	ASSERT_GT(under.rig.ctx.GetLayout(dialog.card).borderBox.x, 4.0f);

	Click(under.rig.ctx, Center(under.rig.ctx, chrome));
	ASSERT_FALSE(under.rig.ctx.GetFocused().IsValid());
	Click(under.rig.ctx, {2.0f, 2.0f});
	under.rig.ctx.ClosePopup(dialog.card);
	under.rig.ctx.Update();
	EXPECT_FALSE(under.rig.ctx.GetFocused().IsValid());
}

TEST(WidgetsTest, DeadHolderGetsNoKeyboard) {
	KeyEaterRig under;
	int accepts = 0;
	ClosingDialog dialog = OpenClosingDialog(under.rig, accepts, DIALOG);
	const u32 slot = under.list.index;
	under.rig.ctx.DestroyElement(under.list);
	UI::ElementHandle successor = under.rig.ctx.CreateElement();
	under.rig.ctx.AppendChild(under.rig.ctx.GetRoot(), successor);
	under.rig.ctx.SetFocusable(successor, true);
	ASSERT_EQ(successor.index, slot);
	under.rig.ctx.Update();

	Click(under.rig.ctx, Center(under.rig.ctx, dialog.button));
	ASSERT_EQ(accepts, 1);
	EXPECT_FALSE(under.rig.ctx.GetFocused().IsValid());
}

TEST(WidgetsTest, StackedDialogsReturnKeyboard) {
	KeyEaterRig under;
	int firstAccepts = 0;
	UI::ElementHandle first = under.rig.ctx.CreateElement();
	UI::Widgets::TextFieldDesc fieldDesc;
	fieldDesc.text = "name";
	UI::ElementHandle field = UI::Widgets::TextField(under.rig.ctx, first, fieldDesc);
	UI::ElementHandle firstButton = AddClosingButton(under.rig, first, firstAccepts);
	under.rig.ctx.OpenPopup(first, {}, DIALOG);
	under.rig.ctx.Update();
	Click(under.rig.ctx, Center(under.rig.ctx, field));
	ASSERT_EQ(under.rig.ctx.GetFocused(), field);

	int secondAccepts = 0;
	ClosingDialog second = OpenClosingDialog(under.rig, secondAccepts, DIALOG);
	Click(under.rig.ctx, Center(under.rig.ctx, second.button));
	ASSERT_EQ(secondAccepts, 1);
	EXPECT_EQ(under.rig.ctx.GetFocused(), field);

	Click(under.rig.ctx, Center(under.rig.ctx, firstButton));
	ASSERT_EQ(firstAccepts, 1);
	EXPECT_EQ(under.rig.ctx.GetFocused(), under.list);
}
