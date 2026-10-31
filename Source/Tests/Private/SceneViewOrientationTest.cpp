#include "TestsPCH.h"

#include "EditorDockRig.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

using namespace Shalltear;
using namespace UITest;
namespace Ops = EditorViewportOps;

namespace {

	class SceneViewOrientationTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PanelManager::Initialize();
			_scene = SceneSystem::CreateScene("Fixture");
		}
		void TearDown() override {
			PanelManager::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}
		Handle<Scene> _scene;
	};

	Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	Vec2 LaidOutKnob(UI::Context& context, UI::ElementHandle canvas, const Quat& rotation, u32 index) {
		const UI::Rect box = context.GetLayout(canvas).content;
		Ops::OrientationKnob knobs[Ops::ORIENTATION_KNOB_COUNT];
		Ops::LayOutOrientationKnobs(rotation, Vec2{box.w * 0.5f, box.h * 0.5f},
									box.w * Ops::ORIENTATION_ARM_FRACTION, knobs);
		return Vec2{box.x + knobs[index].centerPx.x, box.y + knobs[index].centerPx.y};
	}

	bool LettersSitOnKnobs(UI::Context& context, UI::ElementHandle canvas, const Quat& rotation) {
		static constexpr std::string_view LETTERS[3] = {"X", "Y", "Z"};
		static constexpr u32 POSITIVE[3] = {0, 2, 4};
		for (u32 i = 0; i < 3; ++i) {
			UI::ElementHandle label = FindByText(context, canvas, LETTERS[i]);
			if (!label.IsValid()) {
				return false;
			}
			if ((CenterOf(context, label) - LaidOutKnob(context, canvas, rotation, POSITIVE[i])).Length() > 1.5f) {
				return false;
			}
		}
		return true;
	}

	struct OrientationRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		OrientationRig()
			: context(Desc(), &fonts) {
			EditorTheme::ApplyToContext(context);
			panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("SceneViewPanel")));
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			context.SetStyle(root, fill);
			panel.Get()->BuildContent(context, root);
			context.Update();
			Frame();
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {600, 400};
			return desc;
		}

		void Frame(f32 deltaSeconds = 0.016f) {
			PanelManager::TickAll(deltaSeconds);
			context.Update();
			PanelManager::SubmitViews(deltaSeconds);
		}

		f32 Field(StringHash name) {
			Variant value;
			GetField(panel.Get(), panel.Get()->GetType(), name, &value);
			return value.As<f32>();
		}
		bool Flag(StringHash name) {
			Variant value;
			GetField(panel.Get(), panel.Get()->GetType(), name, &value);
			return value.As<bool>();
		}
		Vec3 CameraPosition() {
			Variant value;
			GetField(panel.Get(), panel.Get()->GetType(), "cameraPosition"_hash, &value);
			return value.As<Vec3>();
		}
		Quat CameraRotation() {
			return Ops::BuildCameraRotation(Field("cameraYawDeg"_hash), Field("cameraPitchDeg"_hash));
		}
		f32 AheadOf(const Vec3& point) {
			Vec3 toPoint = (point - CameraPosition()).Normalized();
			return Vec3::Dot(toPoint, CameraRotation() * Vec3::Forward());
		}

		UI::ElementHandle Overlay() { return FindByClass(context, root, EditorTheme::Class::SceneOrientation, 0); }
		UI::ElementHandle Canvas() { return FindByClass(context, root, EditorTheme::Class::SceneOrientationCube, 0); }
		UI::ElementHandle Viewport() { return FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0); }

		Vec2 KnobAt(u32 index) { return LaidOutKnob(context, Canvas(), CameraRotation(), index); }

		void ClickAt(Vec2 point, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.mods = mods;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			context.Update();
		}
		void PressKey(Key key) {
			InputEvent event{};
			event.type = InputEventType::KeyDown;
			event.key = key;
			context.InjectInput(event);
			context.Update();
		}
	};

	struct DockedOrientationRig : EditorTest::DockedPanelRig {
		DockedOrientationRig() : DockedPanelRig("SceneViewPanel") {}

		UI::ElementHandle Canvas() {
			return FindByClass(context, Root(), EditorTheme::Class::SceneOrientationCube, 0);
		}
		Quat CameraRotation() {
			Variant yaw;
			Variant pitch;
			GetField(panel.Get(), panel.Get()->GetType(), "cameraYawDeg"_hash, &yaw);
			GetField(panel.Get(), panel.Get()->GetType(), "cameraPitchDeg"_hash, &pitch);
			return Ops::BuildCameraRotation(yaw.As<f32>(), pitch.As<f32>());
		}
	};

} // anonymous namespace

TEST_F(SceneViewOrientationTest, UpKnobLooksDown) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Framed", _scene);
	object.Get()->SetLocalPosition({4.0f, 1.0f, 9.0f});
	EditorSelection::Select(Handle<Object>(object.Get()));
	OrientationRig rig;
	ClickElement(rig.context, rig.Viewport());
	rig.PressKey(Key::F);
	rig.Frame();
	ASSERT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);

	rig.ClickAt(rig.KnobAt(2));
	rig.Frame();
	EXPECT_NEAR(rig.Field("cameraPitchDeg"_hash), 90.0f, 1.0e-3f);
	EXPECT_GT(rig.CameraPosition().y, object.Get()->GetWorldPosition().y);
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
}

TEST_F(SceneViewOrientationTest, EndOnKnobTurnsToSide) {
	OrientationRig rig;
	const Vec2 before = Vec2{rig.Field("cameraYawDeg"_hash), rig.Field("cameraPitchDeg"_hash)};
	rig.ClickAt(rig.KnobAt(1));
	rig.Frame();
	const Vec3 forward = rig.CameraRotation() * Vec3::Forward();
	EXPECT_NEAR(forward.x, 1.0f, 1.0e-3f);
	EXPECT_NE(rig.Field("cameraYawDeg"_hash), before.x);
}

TEST_F(SceneViewOrientationTest, CubeMenuFreeRestoresAngle) {
	OrientationRig rig;
	Handle<EditorPanel> fresh = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("SceneViewPanel")));
	Variant bootYaw;
	Variant bootPitch;
	GetField(fresh.Get(), fresh.Get()->GetType(), "cameraYawDeg"_hash, &bootYaw);
	GetField(fresh.Get(), fresh.Get()->GetType(), "cameraPitchDeg"_hash, &bootPitch);

	RightClickElement(rig.context, rig.Canvas());
	rig.context.Update();
	for (std::string_view view : {"Free", "Top", "Bottom", "Left", "Right", "Front", "Back"}) {
		EXPECT_TRUE(FindByText(rig.context, rig.context.GetRoot(), view).IsValid()) << view;
	}
	UI::ElementHandle top = FindByText(rig.context, rig.context.GetRoot(), "Top");
	ASSERT_TRUE(top.IsValid());
	ClickElement(rig.context, top);
	rig.Frame();
	EXPECT_NEAR(rig.Field("cameraPitchDeg"_hash), 90.0f, 1.0e-3f);

	RightClickElement(rig.context, rig.Canvas());
	rig.context.Update();
	UI::ElementHandle free = FindByText(rig.context, rig.context.GetRoot(), "Free");
	ASSERT_TRUE(free.IsValid());
	ClickElement(rig.context, free);
	rig.Frame();
	EXPECT_NEAR(rig.Field("cameraYawDeg"_hash), bootYaw.As<f32>(), 1.0e-3f);
	EXPECT_NEAR(rig.Field("cameraPitchDeg"_hash), bootPitch.As<f32>(), 1.0e-3f);
}

TEST_F(SceneViewOrientationTest, ShiftClickingCubeRestoresBootAngle) {
	OrientationRig rig;
	const f32 bootPitch = rig.Field("cameraPitchDeg"_hash);
	rig.ClickAt(rig.KnobAt(2));
	rig.Frame();
	ASSERT_NEAR(rig.Field("cameraPitchDeg"_hash), 90.0f, 1.0e-3f);

	rig.ClickAt(CenterOf(rig.context, rig.Canvas()), KeyMod::Shift);
	rig.Frame();
	EXPECT_NEAR(rig.Field("cameraPitchDeg"_hash), bootPitch, 1.0e-3f);
}

TEST_F(SceneViewOrientationTest, PressingKnobDrawsItPressedBeforeRelease) {
	OrientationRig rig;
	const Vec2 knob = rig.KnobAt(2);
	const f32 reach = rig.context.GetLayout(rig.Canvas()).content.w * Ops::ORIENTATION_KNOB_FRACTION;
	auto colorsAtKnob = [&]() {
		UI::RenderList list;
		rig.context.BuildRenderList(list);
		Vector<Color> seen{GetMemoryResource(MemTag::Editor)};
		for (const UI::UIQuad& quad : list.quads) {
			const Vec2 centre(quad.rect.x + quad.rect.w * 0.5f, quad.rect.y + quad.rect.h * 0.5f);
			if ((centre - knob).Length() <= reach && std::find(seen.begin(), seen.end(), quad.color0) == seen.end()) {
				seen.push_back(quad.color0);
			}
		}
		return seen;
	};
	auto send = [&](InputEventType type, Vec2 point) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		rig.context.InjectInput(event);
	};

	const Vector<Color> resting = colorsAtKnob();
	ASSERT_FALSE(resting.empty());
	send(InputEventType::MouseMove, knob);
	rig.Frame();
	rig.Frame();
	const Vector<Color> hovered = colorsAtKnob();
	ASSERT_NE(hovered, resting);

	send(InputEventType::MouseDown, knob);
	rig.context.Update();
	const Vector<Color> pressed = colorsAtKnob();
	EXPECT_NE(pressed, hovered);
	EXPECT_NE(pressed, resting);

	send(InputEventType::MouseUp, knob);
	rig.Frame();
	EXPECT_NEAR(rig.Field("cameraPitchDeg"_hash), 90.0f, 1.0e-3f);
}

TEST_F(SceneViewOrientationTest, LettersFollowPositiveKnobs) {
	OrientationRig rig;
	rig.Frame();
	EXPECT_TRUE(LettersSitOnKnobs(rig.context, rig.Canvas(), rig.CameraRotation()));

	rig.ClickAt(rig.KnobAt(1));
	rig.Frame();
	rig.Frame();
	EXPECT_TRUE(LettersSitOnKnobs(rig.context, rig.Canvas(), rig.CameraRotation()));
}

TEST_F(SceneViewOrientationTest, LettersFindKnobsAgainAfterTabSwitch) {
	DockedOrientationRig rig;
	ASSERT_TRUE(rig.Canvas().IsValid());
	ASSERT_TRUE(LettersSitOnKnobs(rig.context, rig.Canvas(), rig.CameraRotation()));

	rig.ShowTab(DockedOrientationRig::OTHER_TAB);
	EXPECT_FALSE(rig.Canvas().IsValid());

	rig.ShowTab(DockedOrientationRig::PANEL_TAB);
	rig.Frame();
	ASSERT_TRUE(rig.Canvas().IsValid());
	EXPECT_TRUE(LettersSitOnKnobs(rig.context, rig.Canvas(), rig.CameraRotation()));
}

TEST_F(SceneViewOrientationTest, IdleViewDoesNotRestyleLetters) {
	OrientationRig rig;
	rig.Frame();
	rig.Frame();
	const u64 before = rig.context.GetStyleStats().recalcs;
	for (u32 i = 0; i < 10; ++i) {
		rig.Frame();
	}
	EXPECT_EQ(rig.context.GetStyleStats().recalcs, before);

	Variant yaw(rig.Field("cameraYawDeg"_hash) + 30.0f);
	SetField(rig.panel.Get(), rig.panel.Get()->GetType(), "cameraYawDeg"_hash, &yaw);
	rig.Frame();
	EXPECT_GT(rig.context.GetStyleStats().recalcs, before);
}

TEST_F(SceneViewOrientationTest, PadlockTogglesRotationLockAndShowsIt) {
	OrientationRig rig;
	auto padlock = [&]() { return FindByClass(rig.context, rig.Overlay(), EditorTheme::Class::ToolButton, 0); };
	ASSERT_TRUE(padlock().IsValid());
	EXPECT_FALSE(rig.context.HasClass(padlock(), EditorTheme::Class::ToolButtonOn));

	ClickElement(rig.context, padlock());
	rig.Frame();
	EXPECT_TRUE(rig.context.HasClass(padlock(), EditorTheme::Class::ToolButtonOn));

	ClickElement(rig.context, padlock());
	rig.Frame();
	EXPECT_FALSE(rig.context.HasClass(padlock(), EditorTheme::Class::ToolButtonOn));
}

TEST_F(SceneViewOrientationTest, OverlayNamesProjectionAndWordTogglesIt) {
	OrientationRig rig;
	auto projection = [&]() {
		return FindByTextContaining(rig.context, rig.Overlay(), rig.Flag("orthographic"_hash) ? "Iso" : "Persp");
	};
	ASSERT_TRUE(projection().IsValid());
	EXPECT_FALSE(rig.Flag("orthographic"_hash));
	EXPECT_TRUE(FindByTextContaining(rig.context, rig.Overlay(), "Persp").IsValid());

	ClickElement(rig.context, projection());
	rig.Frame();
	EXPECT_TRUE(rig.Flag("orthographic"_hash));
	EXPECT_TRUE(FindByTextContaining(rig.context, rig.Overlay(), "Iso").IsValid());

	ClickElement(rig.context, projection());
	rig.Frame();
	EXPECT_FALSE(rig.Flag("orthographic"_hash));
	EXPECT_TRUE(FindByTextContaining(rig.context, rig.Overlay(), "Persp").IsValid());
}

TEST_F(SceneViewOrientationTest, PressOnCubeItselfTogglesProjection) {
	OrientationRig rig;
	Variant yaw(45.0f);
	Variant pitch(35.0f);
	SetField(rig.panel.Get(), rig.panel.Get()->GetType(), "cameraYawDeg"_hash, &yaw);
	SetField(rig.panel.Get(), rig.panel.Get()->GetType(), "cameraPitchDeg"_hash, &pitch);
	rig.Frame();

	const UI::Rect box = rig.context.GetLayout(rig.Canvas()).content;
	Ops::OrientationKnob knobs[Ops::ORIENTATION_KNOB_COUNT];
	Ops::LayOutOrientationKnobs(rig.CameraRotation(), Vec2{box.w * 0.5f, box.h * 0.5f},
								box.w * Ops::ORIENTATION_ARM_FRACTION, knobs);
	ASSERT_EQ(Ops::HitOrientationKnob(knobs, Vec2{box.w * 0.5f, box.h * 0.5f},
									  box.w * Ops::ORIENTATION_KNOB_FRACTION),
			  Ops::ORIENTATION_KNOB_NONE);

	const f32 yawBefore = rig.Field("cameraYawDeg"_hash);
	rig.ClickAt(Vec2{box.x + box.w * 0.5f, box.y + box.h * 0.5f});
	rig.Frame();
	EXPECT_TRUE(rig.Flag("orthographic"_hash));
	EXPECT_EQ(rig.Field("cameraYawDeg"_hash), yawBefore);
}

TEST_F(SceneViewOrientationTest, OrthographicFramingSetsSize) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Framed", _scene);
	object.Get()->SetLocalPosition({0.0f, 0.0f, 30.0f});
	EditorSelection::Select(Handle<Object>(object.Get()));

	OrientationRig rig;
	ClickElement(rig.context, rig.Viewport());
	ClickElement(rig.context, FindByTextContaining(rig.context, rig.Overlay(), "Persp"));
	rig.Frame();
	ASSERT_TRUE(rig.Flag("orthographic"_hash));

	const f32 before = 500.0f;
	Variant wide(before);
	SetField(rig.panel.Get(), rig.panel.Get()->GetType(), "orthographicSize"_hash, &wide);

	rig.PressKey(Key::F);
	rig.Frame();
	const f32 after = rig.Field("orthographicSize"_hash);
	EXPECT_LT(after, before);
	EXPECT_NEAR(after, Ops::FrameSizeFor(Ops::GetSelectionWorldBounds()), 1.0e-3f);
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
}
