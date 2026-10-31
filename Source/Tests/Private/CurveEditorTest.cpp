#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/CurveEditor.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <cmath>

using namespace Shalltear;

namespace {

	constexpr f32 SCALE = 1.25f;

	Keyframe MakeKey(f32 time, f32 value, f32 slope) {
		Keyframe key;
		key.time = time;
		key.value = value;
		key.inTangent = slope;
		key.outTangent = slope;
		return key;
	}

	AnimationCurve Line() {
		AnimationCurve curve;
		curve.keys.push_back(MakeKey(0.0f, 0.0f, 1.0f));
		curve.keys.push_back(MakeKey(1.0f, 1.0f, 1.0f));
		return curve;
	}

	struct Harness {
		UITest::MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle anchor;
		UI::ElementHandle popup;
		CurveEditor::View view;
		AnimationCurve last;
		u32 changes = 0;
		u32 begins = 0;
		u32 ends = 0;

		Harness() : context(Desc(), &fonts) {
			EditorTheme::ApplyToContext(context);
			anchor = UITest::MakeChild(context, context.GetRoot(), UITest::FixedBox(60.0f, 16.0f));
			context.Update();
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {1000, 700};
			desc.contentScale = SCALE;
			return desc;
		}

		void Open(const AnimationCurve& curve) {
			view = CurveEditor::Frame(curve);
			last = curve;
			popup = CurveEditor::Open(
				context, anchor, curve,
				[this](const AnimationCurve& edited) {
					last = edited;
					++changes;
				},
				[this]() { ++begins; }, [this]() { ++ends; });
			context.Update();
			context.Update();
		}

		UI::Rect Plot() { return context.GetLayout(UITest::FindByClass(context, popup, EditorTheme::Class::CurveEditorPlot, 0)).borderBox; }

		Vec2 At(f32 time, f32 value) {
			const UI::Rect box = Plot();
			return Vec2(box.x + (time - view.timeMin) / (view.timeMax - view.timeMin) * box.w,
						box.y + (1.0f - (value - view.valueMin) / (view.valueMax - view.valueMin)) * box.h);
		}

		void Mouse(InputEventType type, Vec2 at, MouseButton button = MouseButton::Left) {
			InputEvent event{};
			event.type = type;
			event.button = button;
			event.mousePosPx = {static_cast<i32>(std::lround(at.x)), static_cast<i32>(std::lround(at.y))};
			context.InjectInput(event);
			context.Update();
		}

		void Click(Vec2 at) {
			Mouse(InputEventType::MouseMove, at);
			Mouse(InputEventType::MouseDown, at);
			Mouse(InputEventType::MouseUp, at);
		}

		void Drag(Vec2 from, Vec2 to) {
			Mouse(InputEventType::MouseMove, from);
			Mouse(InputEventType::MouseDown, from);
			for (i32 step = 1; step <= 3; ++step) {
				Mouse(InputEventType::MouseMove, from + (to - from) * (static_cast<f32>(step) / 3.0f));
			}
			Mouse(InputEventType::MouseUp, to);
		}

		void Press(Key key) {
			InputEvent down{};
			down.type = InputEventType::KeyDown;
			down.key = key;
			context.InjectInput(down);
			context.Update();
			InputEvent up = down;
			up.type = InputEventType::KeyUp;
			context.InjectInput(up);
			context.Update();
		}

		f32 TimePerPx() { return (view.timeMax - view.timeMin) / Plot().w; }
		f32 ValuePerPx() { return (view.valueMax - view.valueMin) / Plot().h; }
	};

} // anonymous namespace

TEST(CurveEditorTest, FrameIsKeysAndValuesPadded) {
	AnimationCurve rising;
	rising.keys.push_back(MakeKey(0.0f, 0.0f, 2.0f));
	rising.keys.push_back(MakeKey(2.0f, 4.0f, 2.0f));
	const CurveEditor::View view = CurveEditor::Frame(rising);
	EXPECT_FLOAT_EQ(view.timeMin, -0.2f);
	EXPECT_FLOAT_EQ(view.timeMax, 2.2f);
	EXPECT_NEAR(view.valueMin, -0.4f, 1e-5f);
	EXPECT_NEAR(view.valueMax, 4.4f, 1e-5f);

	AnimationCurve point;
	point.keys.push_back(MakeKey(1.0f, 3.0f, 0.0f));
	const CurveEditor::View one = CurveEditor::Frame(point);
	EXPECT_LT(one.timeMin, 1.0f);
	EXPECT_GT(one.timeMax, 1.0f);
	EXPECT_LT(one.valueMin, 3.0f);
	EXPECT_GT(one.valueMax, 3.0f);

	AnimationCurve bump;
	bump.keys.push_back(MakeKey(0.0f, 0.0f, 10.0f));
	bump.keys.push_back(MakeKey(1.0f, 0.0f, 0.0f));
	EXPECT_GT(CurveEditor::Frame(bump).valueMax, 1.0f);
}

TEST(CurveEditorTest, DoubleClickAddsKeyInOneStep) {
	Harness harness;
	harness.Open(Line());
	ASSERT_GT(harness.Plot().w, 100.0f);

	harness.Click(harness.At(0.5f, 0.9f));
	harness.Click(harness.At(0.5f, 0.9f));
	EXPECT_EQ(harness.changes, 0u);

	harness.Click(harness.At(0.5f, 0.5f));
	harness.Click(harness.At(0.5f, 0.5f));
	ASSERT_EQ(harness.changes, 1u);
	ASSERT_EQ(harness.last.keys.size(), 3u);
	EXPECT_NEAR(harness.last.keys[1].time, 0.5f, harness.TimePerPx());
	EXPECT_NEAR(harness.last.keys[1].value, harness.last.keys[1].time, 1e-4f);
	EXPECT_EQ(harness.begins, 1u);
	EXPECT_EQ(harness.ends, 1u);
}

TEST(CurveEditorTest, KeyDragIsOneStep) {
	Harness harness;
	harness.Open(Line());
	harness.Drag(harness.At(1.0f, 1.0f), harness.At(0.8f, 0.5f));
	ASSERT_GE(harness.changes, 2u);
	ASSERT_EQ(harness.last.keys.size(), 2u);
	EXPECT_NEAR(harness.last.keys[1].time, 0.8f, harness.TimePerPx() * 1.5f);
	EXPECT_NEAR(harness.last.keys[1].value, 0.5f, harness.ValuePerPx() * 1.5f);
	EXPECT_EQ(harness.begins, 1u);
	EXPECT_EQ(harness.ends, 1u);
}

TEST(CurveEditorTest, ASelectedKeysHandleSetsItsSlope) {
	Harness harness;
	harness.Open(Line());
	harness.Click(harness.At(0.0f, 0.0f));
	ASSERT_EQ(harness.changes, 0u);
	const UI::Rect box = harness.Plot();
	Vec2 direction(1.0f, -(box.h / (harness.view.valueMax - harness.view.valueMin)) / (box.w / (harness.view.timeMax - harness.view.timeMin)));
	direction = direction * (1.0f / direction.Length());
	const Vec2 handle = harness.At(0.0f, 0.0f) + direction * (36.0f * SCALE);
	harness.Drag(handle, handle + Vec2(0.0f, -30.0f * SCALE));
	ASSERT_GE(harness.changes, 1u);
	EXPECT_GT(harness.last.keys[0].outTangent, 1.5f);
	EXPECT_EQ(harness.last.keys[0].outTangentMode, KeyframeTangentMode::Free);
	EXPECT_EQ(harness.last.keys[1], Line().keys[1]);
}

TEST(CurveEditorTest, DeleteRemovesTheSelectedKey) {
	Harness harness;
	AnimationCurve three = Line();
	three.keys.push_back(MakeKey(2.0f, 0.0f, 0.0f));
	harness.Open(three);
	harness.Click(harness.At(1.0f, 1.0f));
	harness.Press(Key::Delete);
	ASSERT_EQ(harness.changes, 1u);
	ASSERT_EQ(harness.last.keys.size(), 2u);
	EXPECT_EQ(harness.last.keys[1].time, 2.0f);
	harness.Press(Key::Delete);
	EXPECT_EQ(harness.changes, 1u);
}

TEST(CurveEditorTest, WrapModeIsSetFromDropdown) {
	Harness harness;
	harness.Open(Line());
	const UI::ElementHandle label = UITest::FindByText(harness.context, harness.popup, "Post Wrap");
	ASSERT_TRUE(harness.context.IsAlive(label));
	const UI::Element* pod = harness.context.GetElement(label);
	const UI::ElementHandle dropdown = UITest::NthChildOf(harness.context, harness.context.MakeHandle(pod->parent), 1);
	UITest::ClickElement(harness.context, dropdown);
	const u32 popups = harness.context.GetOpenPopupCount();
	ASSERT_GE(popups, 2u);
	const UI::ElementHandle pingPong = UITest::FindByText(harness.context, harness.context.GetOpenPopup(popups - 1), "Ping Pong");
	ASSERT_TRUE(harness.context.IsAlive(pingPong));
	UITest::ClickElement(harness.context, pingPong);
	ASSERT_EQ(harness.changes, 1u);
	EXPECT_EQ(harness.last.postWrapMode, AnimationCurveWrapMode::PingPong);
	EXPECT_EQ(harness.last.preWrapMode, AnimationCurveWrapMode::Clamp);
}

TEST(CurveEditorTest, ClosingEditorMidDragClosesStep) {
	Harness harness;
	harness.Open(Line());
	const Vec2 from = harness.At(1.0f, 1.0f);
	harness.Mouse(InputEventType::MouseMove, from);
	harness.Mouse(InputEventType::MouseDown, from);
	harness.Mouse(InputEventType::MouseMove, from + Vec2(-20.0f, 20.0f));
	ASSERT_EQ(harness.begins, 1u);
	harness.context.ClosePopup(harness.popup);
	harness.context.Update();
	EXPECT_EQ(harness.ends, 1u);
}
