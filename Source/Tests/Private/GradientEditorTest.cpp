#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GradientEditor.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <cmath>

using namespace Shalltear;

namespace {

	class RampHolder : public Component {
	public:
		SLT_REFLECT_COMPONENT(RampHolder, Component)
		SLT_REFLECT_SUBOBJECT(ramp)
		SLT_REFLECT_SUBOBJECT(falloff)
		SLT_END_REFLECT()

		Gradient ramp{GetMemoryResource(MemTag::Scene)};
		AnimationCurve falloff{{Keyframe{.time = 0.0f, .value = 1.0f}}, GetMemoryResource(MemTag::Scene)};
	};

	SLT_REGISTER_TYPE(RampHolder)

	void Mouse(UI::Context& context, InputEventType type, Vec2 at) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(std::lround(at.x)), static_cast<i32>(std::lround(at.y))};
		context.InjectInput(event);
		context.Update();
	}

	void ClickAt(UI::Context& context, Vec2 at) {
		Mouse(context, InputEventType::MouseMove, at);
		Mouse(context, InputEventType::MouseDown, at);
		Mouse(context, InputEventType::MouseUp, at);
	}

	void Press(UI::Context& context, Key key) {
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

	Vec2 Along(UI::Context& context, UI::ElementHandle strip, f32 time) {
		const UI::Rect box = context.GetLayout(strip).borderBox;
		return Vec2(box.x + box.w * time, box.y + box.h * 0.5f);
	}

	struct Harness {
		UITest::MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle anchor;
		UI::ElementHandle popup;
		Gradient last;
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
			desc.contentScale = 1.25f;
			return desc;
		}

		void Open(const Gradient& gradient) {
			last = gradient;
			popup = GradientEditor::Open(
				context, anchor, gradient,
				[this](const Gradient& edited) {
					last = edited;
					++changes;
				},
				[this]() { ++begins; }, [this]() { ++ends; });
			context.Update();
			context.Update();
		}

		UI::ElementHandle Strip(bool alpha) { return UITest::FindByClass(context, popup, EditorTheme::Class::Clickable, alpha ? 0 : 1); }
	};

	class GradientInspectorFixture : public ::testing::Test {
	protected:
		Handle<Scene> _scene;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			RampHolder::GetReflectType();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			PropertyDrawers::Initialize();
			_scene = SceneSystem::CreateScene("Ramps");
		}

		void TearDown() override {
			PropertyDrawers::Terminate();
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}
	};

} // anonymous namespace

TEST(GradientEditorTest, KeysStaySortedAndCapped) {
	Gradient gradient{{GradientColorKey{Color::Red(), 0.0f}, GradientColorKey{Color::Blue(), 1.0f}}, {GradientAlphaKey{1.0f, 0.0f}}};
	const i32 middle = GradientEditor::AddKey(gradient, false, 0.5f);
	ASSERT_EQ(middle, 1);
	EXPECT_NEAR(gradient.colorKeys[1].color.r, 0.5f, 1e-4f);
	EXPECT_NEAR(gradient.colorKeys[1].color.b, 0.5f, 1e-4f);
	EXPECT_EQ(GradientEditor::MoveKey(gradient, false, 1, 1.5f), 2);
	EXPECT_FLOAT_EQ(gradient.colorKeys[2].time, 1.0f);
	EXPECT_EQ(GradientEditor::MoveKey(gradient, false, 2, 0.25f), 1);
	EXPECT_FLOAT_EQ(gradient.colorKeys[1].time, 0.25f);
	while (gradient.colorKeys.size() < Gradient::MAX_KEYS) {
		ASSERT_GE(GradientEditor::AddKey(gradient, false, 0.75f), 0);
	}
	EXPECT_EQ(GradientEditor::AddKey(gradient, false, 0.1f), -1);
	EXPECT_EQ(GradientEditor::AddKey(gradient, true, 0.5f), 1);
	EXPECT_TRUE(GradientEditor::RemoveKey(gradient, true, 0));
	EXPECT_FALSE(GradientEditor::RemoveKey(gradient, true, 0));
	EXPECT_EQ(gradient.alphaKeys.size(), 1u);
}

TEST(GradientEditorTest, ClickAddsDragMovesDeleteRemoves) {
	Harness harness;
	harness.Open(Gradient{{GradientColorKey{Color::Red(), 0.0f}, GradientColorKey{Color::Blue(), 1.0f}}, {GradientAlphaKey{1.0f, 0.0f}}});
	UI::ElementHandle colors = harness.Strip(false);
	UI::ElementHandle alphas = harness.Strip(true);
	ASSERT_TRUE(colors.IsValid());
	ASSERT_TRUE(alphas.IsValid());

	ClickAt(harness.context, Along(harness.context, colors, 0.5f));
	ASSERT_EQ(harness.last.colorKeys.size(), 3u);
	EXPECT_NEAR(harness.last.colorKeys[1].time, 0.5f, 0.01f);
	EXPECT_NEAR(harness.last.colorKeys[1].color.r, 0.5f, 0.02f);
	EXPECT_EQ(harness.begins, harness.ends);

	const Vec2 from = Along(harness.context, colors, harness.last.colorKeys[1].time);
	const Vec2 to = Along(harness.context, colors, 0.2f);
	Mouse(harness.context, InputEventType::MouseMove, from);
	Mouse(harness.context, InputEventType::MouseDown, from);
	for (i32 step = 1; step <= 3; ++step) {
		Mouse(harness.context, InputEventType::MouseMove, from + (to - from) * (static_cast<f32>(step) / 3.0f));
	}
	Mouse(harness.context, InputEventType::MouseUp, to);
	ASSERT_EQ(harness.last.colorKeys.size(), 3u);
	EXPECT_NEAR(harness.last.colorKeys[1].time, 0.2f, 0.01f);
	EXPECT_EQ(harness.begins, harness.ends);

	Press(harness.context, Key::Delete);
	EXPECT_EQ(harness.last.colorKeys.size(), 2u);

	ClickAt(harness.context, Along(harness.context, alphas, 0.5f));
	EXPECT_EQ(harness.last.alphaKeys.size(), 2u);
	EXPECT_EQ(harness.last.colorKeys.size(), 2u);
	EXPECT_EQ(harness.begins, harness.ends);
}

TEST_F(GradientInspectorFixture, RowEditsWithUndo) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Holder", _scene);
	Handle<RampHolder> holder = object->AddComponent<RampHolder>();
	EditorSelection::Select(Handle<Object>(object.Get()));
	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
	ASSERT_TRUE(panel.IsValid());

	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 900};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	UI::ElementHandle ramp = UITest::FindByClass(context, root, EditorTheme::Class::CurveEditorPlot, 0);
	UI::ElementHandle falloff = UITest::FindByClass(context, root, EditorTheme::Class::CurveEditorPlot, 1);
	ASSERT_TRUE(ramp.IsValid());
	ASSERT_TRUE(falloff.IsValid());

	UITest::ClickElement(context, ramp);
	context.Update();
	UI::ElementHandle popup = UITest::FindByClass(context, context.GetRoot(), EditorTheme::Class::CurveEditor, 0);
	ASSERT_TRUE(popup.IsValid());
	UI::ElementHandle colors = UITest::FindByClass(context, popup, EditorTheme::Class::Clickable, 1);
	ASSERT_TRUE(colors.IsValid());
	ClickAt(context, Along(context, colors, 0.5f));
	EXPECT_EQ(holder->ramp.colorKeys.size(), 3u);
	EXPECT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(holder->ramp.colorKeys.size(), 2u);
	context.CloseAllPopups();
	context.Update();

	UITest::ClickElement(context, falloff);
	context.Update();
	EXPECT_TRUE(UITest::FindByClass(context, context.GetRoot(), EditorTheme::Class::CurveEditor, 0).IsValid());
	PanelManager::ClosePanel(panel);
}
