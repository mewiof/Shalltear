#include "TestsPCH.h"

#include "EditorDockRig.h"
#include "UITestUtil.h"

#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <windows.h>

using namespace Shalltear;
using namespace UITest;

namespace {

	class SceneViewFollowTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			WindowSystem::Initialize();
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
			WindowSystem::Terminate();
		}
		Handle<Scene> _scene;
	};

	struct FollowRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		FollowRig()
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

		Vec3 CameraPosition() {
			Variant value;
			GetField(panel.Get(), panel.Get()->GetType(), "cameraPosition"_hash, &value);
			return value.As<Vec3>();
		}
		Quat CameraRotation() {
			Variant yaw;
			Variant pitch;
			GetField(panel.Get(), panel.Get()->GetType(), "cameraYawDeg"_hash, &yaw);
			GetField(panel.Get(), panel.Get()->GetType(), "cameraPitchDeg"_hash, &pitch);
			return EditorViewportOps::BuildCameraRotation(yaw.As<f32>(), pitch.As<f32>());
		}
		void SetYaw(f32 degrees) {
			Variant value(degrees);
			SetField(panel.Get(), panel.Get()->GetType(), "cameraYawDeg"_hash, &value);
		}
		f32 DistanceTo(const Vec3& point) { return (point - CameraPosition()).Length(); }
		f32 AheadOf(const Vec3& point) {
			Vec3 toPoint = (point - CameraPosition()).Normalized();
			return Vec3::Dot(toPoint, CameraRotation() * Vec3::Forward());
		}

		UI::ElementHandle Chip() { return FindByClass(context, root, EditorTheme::Class::SceneFollow, 0); }
		UI::ElementHandle Viewport() { return FindByClass(context, root, EditorTheme::Class::ViewportPlaceholder, 0); }
	};

	struct DockedFollowRig : EditorTest::DockedPanelRig {
		DockedFollowRig() : DockedPanelRig("SceneViewPanel") {}

		UI::ElementHandle Chip() { return FindByClass(context, Root(), EditorTheme::Class::SceneFollow, 0); }
	};

	void Follow(Handle<SceneObject> object) {
		EditorEvents::FollowRequested.Publish(EventBus::GetGlobal(), {object.GetInstanceId()});
	}

	Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	class HeldInput {
	public:
		HeldInput() {
			WindowDesc desc;
			desc.title = "FollowInput";
			desc.clientSize = {64, 64};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
		}
		~HeldInput() {
			if (_window.IsValid()) {
				::PostMessageW(Hwnd(), WM_KEYUP, 'W', KeyLParam(true));
				::PostMessageW(Hwnd(), WM_RBUTTONUP, 0, MAKELPARAM(10, 10));
				WindowSystem::PumpEvents();
			}
		}
		HeldInput(const HeldInput&) = delete;
		HeldInput& operator=(const HeldInput&) = delete;

		bool IsValid() const { return _window.IsValid(); }
		void HoldRightButtonAndW() {
			::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
			::PostMessageW(Hwnd(), WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(10, 10));
			::PostMessageW(Hwnd(), WM_KEYDOWN, 'W', KeyLParam(false));
			WindowSystem::PumpEvents();
		}
	private:
		HWND Hwnd() const { return static_cast<HWND>(_window->GetNativeHandle().hwnd); }
		static LPARAM KeyLParam(bool up) {
			LPARAM lParam = 1;
			lParam |= static_cast<LPARAM>(0x11) << 16;
			if (up) {
				lParam |= static_cast<LPARAM>(1) << 30;
				lParam |= static_cast<LPARAM>(1) << 31;
			}
			return lParam;
		}
		Handle<Window> _window;
	};

} // anonymous namespace

TEST_F(SceneViewFollowTest, FollowKeepsObjectFramed) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Runner", _scene);
	object.Get()->SetLocalPosition({10.0f, 0.0f, 5.0f});
	FollowRig rig;
	const Vec3 before = rig.CameraPosition();

	Follow(object);
	rig.Frame();
	const f32 expected = EditorViewportOps::FrameDistanceFor(EditorViewportOps::GetObjectWorldBounds(object));
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
	EXPECT_NEAR(rig.DistanceTo(object.Get()->GetWorldPosition()), expected, 1.0e-3f);
	EXPECT_GT((rig.CameraPosition() - before).Length(), 0.1f);

	object.Get()->SetLocalPosition({10.0f, 0.0f, 25.0f});
	rig.Frame();
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
	EXPECT_NEAR(rig.DistanceTo(object.Get()->GetWorldPosition()), expected, 1.0e-3f);
}

TEST_F(SceneViewFollowTest, LookingWhileFollowingOrbitsTheObject) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Runner", _scene);
	object.Get()->SetLocalPosition({3.0f, 1.0f, 8.0f});
	FollowRig rig;
	Follow(object);
	rig.Frame();
	const Vec3 fromTheFront = rig.CameraPosition();
	const f32 distance = rig.DistanceTo(object.Get()->GetWorldPosition());

	rig.SetYaw(90.0f);
	rig.Frame();
	EXPECT_GT((rig.CameraPosition() - fromTheFront).Length(), 1.0f);
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
	EXPECT_NEAR(rig.DistanceTo(object.Get()->GetWorldPosition()), distance, 1.0e-3f);
}

TEST_F(SceneViewFollowTest, FlyingTheCameraReleasesTheFollow) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Runner", _scene);
	object.Get()->SetLocalPosition({0.0f, 0.0f, 20.0f});
	FollowRig rig;
	Follow(object);
	rig.Frame();
	ASSERT_TRUE(rig.Chip().IsValid());

	HeldInput held;
	ASSERT_TRUE(held.IsValid());
	const Vec2 inside = CenterOf(rig.context, rig.Viewport());
	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {static_cast<i32>(inside.x), static_cast<i32>(inside.y)};
	rig.context.InjectInput(move);
	rig.context.Update();
	held.HoldRightButtonAndW();
	ASSERT_TRUE(Input::IsMouseButtonDown(MouseButton::Right));
	ASSERT_TRUE(Input::IsKeyDown(Key::W));
	rig.Frame(0.1f);
	rig.Frame(0.1f);

	EXPECT_FALSE(rig.Chip().IsValid());
	const Vec3 flown = rig.CameraPosition();
	object.Get()->SetLocalPosition({50.0f, 0.0f, 20.0f});
	rig.Frame(0.1f);
	EXPECT_GT(rig.DistanceTo(object.Get()->GetWorldPosition()), 30.0f);
	EXPECT_GT((rig.CameraPosition() - flown).Length(), 0.0f);
}

TEST_F(SceneViewFollowTest, ChipClickReleasesFollow) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Main Camera", _scene);
	FollowRig rig;
	ASSERT_FALSE(rig.Chip().IsValid());

	Follow(object);
	rig.Frame();
	UI::ElementHandle chip = rig.Chip();
	ASSERT_TRUE(chip.IsValid());
	EXPECT_TRUE(FindByTextContaining(rig.context, chip, "Main Camera").IsValid());

	ClickElement(rig.context, chip);
	rig.Frame();
	EXPECT_FALSE(rig.Chip().IsValid());
	const Vec3 released = rig.CameraPosition();
	object.Get()->SetLocalPosition({0.0f, 0.0f, 40.0f});
	rig.Frame();
	EXPECT_NEAR((rig.CameraPosition() - released).Length(), 0.0f, 1.0e-4f);
}

TEST_F(SceneViewFollowTest, TheChipFollowsARename) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Before", _scene);
	FollowRig rig;
	Follow(object);
	rig.Frame();
	ASSERT_TRUE(FindByTextContaining(rig.context, rig.Chip(), "Before").IsValid());

	object.Get()->name = "After";
	rig.Frame();
	EXPECT_TRUE(FindByTextContaining(rig.context, rig.Chip(), "After").IsValid());
	EXPECT_FALSE(FindByTextContaining(rig.context, rig.Chip(), "Before").IsValid());
}

TEST_F(SceneViewFollowTest, AFollowedObjectDyingReleasesTheFollow) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Doomed", _scene);
	FollowRig rig;
	Follow(object);
	rig.Frame();
	ASSERT_TRUE(rig.Chip().IsValid());

	SceneSystem::Destroy(object);
	SceneSystem::EndFrame();
	rig.Frame();
	rig.Frame();
	EXPECT_FALSE(rig.Chip().IsValid());
}

TEST_F(SceneViewFollowTest, ShiftFLocksFFrames) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Picked", _scene);
	object.Get()->SetLocalPosition({0.0f, 2.0f, 12.0f});
	EditorSelection::Select(Handle<Object>(object.Get()));
	FollowRig rig;
	ClickElement(rig.context, rig.Viewport());

	auto press = [&](KeyMod mods) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = Key::F;
		event.mods = mods;
		rig.context.InjectInput(event);
		rig.context.Update();
	};

	press(KeyMod::Shift);
	rig.Frame();
	ASSERT_TRUE(rig.Chip().IsValid());
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);

	press(KeyMod::None);
	rig.Frame();
	EXPECT_FALSE(rig.Chip().IsValid());
	EXPECT_NEAR(rig.AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
}

TEST_F(SceneViewFollowTest, FollowSurvivesStopSwapThroughRemapTable) {
	Handle<SceneObject> old = SceneSystem::CreateObject("Old", _scene);
	Handle<SceneObject> fresh = SceneSystem::CreateObject("Fresh", _scene);
	old.Get()->SetLocalPosition({0.0f, 0.0f, 10.0f});
	fresh.Get()->SetLocalPosition({20.0f, 0.0f, 10.0f});
	FollowRig rig;
	Follow(old);
	rig.Frame();

	const EditorIdRemap rows[] = {{old.GetInstanceId(), fresh.GetInstanceId()}};
	EditorEvents::WorldRemapped.Publish(EventBus::GetGlobal(), {std::span<const EditorIdRemap>(rows, 1)});
	rig.Frame();
	EXPECT_NEAR(rig.AheadOf(fresh.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
	EXPECT_TRUE(FindByTextContaining(rig.context, rig.Chip(), "Fresh").IsValid());

	const EditorIdRemap unrelated[] = {{old.GetInstanceId(), old.GetInstanceId()}};
	EditorEvents::WorldRemapped.Publish(EventBus::GetGlobal(), {std::span<const EditorIdRemap>(unrelated, 1)});
	rig.Frame();
	rig.Frame();
	EXPECT_FALSE(rig.Chip().IsValid());
}

TEST_F(SceneViewFollowTest, FollowRequestForNothingLeavesViewAlone) {
	FollowRig rig;
	const Vec3 before = rig.CameraPosition();
	EditorEvents::FollowRequested.Publish(EventBus::GetGlobal(), {0xDEADBEEFull});
	rig.Frame();
	EXPECT_NEAR((rig.CameraPosition() - before).Length(), 0.0f, 1.0e-4f);
	EXPECT_FALSE(rig.Chip().IsValid());
}

TEST_F(SceneViewFollowTest, ViewsShareFollowRequest) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Runner", _scene);
	object.Get()->SetLocalPosition({5.0f, 0.0f, 15.0f});
	FollowRig first;
	FollowRig second;
	Follow(object);
	first.Frame();
	second.Frame();
	for (FollowRig* rig : {&first, &second}) {
		EXPECT_TRUE(rig->Chip().IsValid());
		EXPECT_NEAR(rig->AheadOf(object.Get()->GetWorldPosition()), 1.0f, 1.0e-3f);
	}
}

TEST_F(SceneViewFollowTest, ChipComesBackWhenTabDoes) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Runner", _scene);
	object.Get()->SetLocalPosition({5.0f, 0.0f, 15.0f});
	DockedFollowRig rig;
	Follow(object);
	rig.Frame();
	ASSERT_TRUE(rig.Chip().IsValid());

	rig.SwitchAwayAndBack();
	EXPECT_TRUE(rig.Chip().IsValid());
	EXPECT_TRUE(FindByTextContaining(rig.context, rig.Chip(), "Runner").IsValid());
}
