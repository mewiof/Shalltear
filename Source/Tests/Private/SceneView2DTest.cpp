#include "TestsPCH.h"

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

	class SceneView2DTest : public ::testing::Test {
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

	struct Rig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		Rig()
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

		template <typename T>
		T Field(StringHash name) {
			Variant value;
			GetField(panel.Get(), panel.Get()->GetType(), name, &value);
			return value.As<T>();
		}
		template <typename T>
		void SetFieldValue(StringHash name, T value) {
			Variant boxed(value);
			SetField(panel.Get(), panel.Get()->GetType(), name, &boxed);
		}

		UI::ElementHandle Button2D() {
			return FindByTextContaining(context, root, "2D");
		}
		UI::ElementHandle Overlay() { return FindByClass(context, root, EditorTheme::Class::SceneOrientation, 0); }

		void Press2D() {
			const UI::ElementHandle face = Button2D();
			EXPECT_TRUE(face.IsValid());
			ClickElement(context, face);
			Frame();
		}
	};

} // anonymous namespace

TEST_F(SceneView2DTest, ToggleFacesXYOrthographic) {
	Rig rig;
	EXPECT_FALSE(rig.Field<bool>("mode2D"_hash));
	EXPECT_FALSE(rig.Field<bool>("orthographic"_hash));

	rig.Press2D();
	EXPECT_TRUE(rig.Field<bool>("mode2D"_hash));
	EXPECT_TRUE(rig.Field<bool>("orthographic"_hash));
	EXPECT_FLOAT_EQ(rig.Field<f32>("cameraYawDeg"_hash), 0.0f);
	EXPECT_FLOAT_EQ(rig.Field<f32>("cameraPitchDeg"_hash), 0.0f);
}

TEST_F(SceneView2DTest, Leaving2DRestores3DView) {
	Rig rig;
	rig.SetFieldValue<Vec3>("cameraPosition"_hash, Vec3(11.0f, 22.0f, -33.0f));
	rig.SetFieldValue<f32>("cameraYawDeg"_hash, 123.0f);
	rig.SetFieldValue<f32>("cameraPitchDeg"_hash, -41.0f);
	rig.SetFieldValue<bool>("orthographic"_hash, false);

	rig.Press2D();
	ASSERT_TRUE(rig.Field<bool>("mode2D"_hash));
	EXPECT_FLOAT_EQ(rig.Field<f32>("cameraYawDeg"_hash), 0.0f);

	rig.Press2D();
	EXPECT_FALSE(rig.Field<bool>("mode2D"_hash));
	EXPECT_FLOAT_EQ(rig.Field<f32>("cameraYawDeg"_hash), 123.0f);
	EXPECT_FLOAT_EQ(rig.Field<f32>("cameraPitchDeg"_hash), -41.0f);
	EXPECT_EQ(rig.Field<Vec3>("cameraPosition"_hash), Vec3(11.0f, 22.0f, -33.0f));
	EXPECT_FALSE(rig.Field<bool>("orthographic"_hash));
}

TEST_F(SceneView2DTest, OrthographicViewRestored) {
	Rig rig;
	rig.SetFieldValue<bool>("orthographic"_hash, true);
	rig.Press2D();
	ASSERT_TRUE(rig.Field<bool>("mode2D"_hash));
	rig.Press2D();
	EXPECT_FALSE(rig.Field<bool>("mode2D"_hash));
	EXPECT_TRUE(rig.Field<bool>("orthographic"_hash));
}

TEST_F(SceneView2DTest, NoOrientationOverlayIn2D) {
	Rig rig;
	ASSERT_TRUE(rig.Overlay().IsValid());
	EXPECT_NE(rig.context.GetStyle(rig.Overlay()).display, UI::Display::None);

	rig.Press2D();
	ASSERT_TRUE(rig.Overlay().IsValid());
	EXPECT_EQ(rig.context.GetStyle(rig.Overlay()).display, UI::Display::None);

	const UI::ElementHandle cube = FindByClass(rig.context, rig.root, EditorTheme::Class::SceneOrientationCube, 0);
	ASSERT_TRUE(cube.IsValid());
	const UI::Rect cubeBox = rig.context.GetLayout(cube).borderBox;
	const Vec2 centre{cubeBox.x + cubeBox.w * 0.5f, cubeBox.y + cubeBox.h * 0.5f};
	EXPECT_NE(rig.context.HitTest(centre), cube);
	ClickElement(rig.context, cube);
	rig.Frame();
	EXPECT_TRUE(rig.Field<bool>("orthographic"_hash));
	EXPECT_TRUE(rig.Field<bool>("mode2D"_hash));

	rig.Press2D();
	EXPECT_NE(rig.context.GetStyle(rig.Overlay()).display, UI::Display::None);
}

TEST_F(SceneView2DTest, GridFollowsViewTo2D) {
	Rig rig;
	auto planeNow = [&]() {
		Ops::Viewpoint viewpoint;
		viewpoint.position = rig.Field<Vec3>("cameraPosition"_hash);
		viewpoint.rotation = Ops::BuildCameraRotation(rig.Field<f32>("cameraYawDeg"_hash),
													  rig.Field<f32>("cameraPitchDeg"_hash));
		viewpoint.orthographic = rig.Field<bool>("orthographic"_hash);
		viewpoint.orthographicSize = rig.Field<f32>("orthographicSize"_hash);
		return Ops::GridPlaneFor(Ops::BuildViewMatrix(viewpoint), Ops::BuildProjection(viewpoint));
	};
	EXPECT_EQ(planeNow(), Ops::GridPlane::XZ);

	rig.Press2D();
	EXPECT_EQ(planeNow(), Ops::GridPlane::XY);

	rig.Press2D();
	EXPECT_EQ(planeNow(), Ops::GridPlane::XZ);
}

TEST_F(SceneView2DTest, SameModeParksNothing) {
	Rig rig;
	rig.SetFieldValue<f32>("cameraYawDeg"_hash, 77.0f);
	rig.Press2D();
	ASSERT_TRUE(rig.Field<bool>("mode2D"_hash));

	rig.SetFieldValue<bool>("mode2D"_hash, true);
	rig.Frame();

	rig.Press2D();
	EXPECT_FALSE(rig.Field<bool>("mode2D"_hash));
	EXPECT_FLOAT_EQ(rig.Field<f32>("cameraYawDeg"_hash), 77.0f);
}
