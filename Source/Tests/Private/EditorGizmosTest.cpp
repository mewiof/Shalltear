#include "TestsPCH.h"

#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include <cmath>
#include <mutex>
#include <vector>

using namespace Shalltear;

namespace {

	class EditorGizmosFixture : public ::testing::Test {
	protected:
		Handle<Scene> _scene;
		Vector<DebugLine> _lines;
		Vector<DebugLine> _overlay;
		Vector<SceneIcon> _icons;

		void SetUp() override {
			SceneSystem::Initialize();
			EditorSelection::Initialize();
			EditorGizmos::Initialize();
			_scene = SceneSystem::CreateScene("Gizmos");
		}

		void TearDown() override {
			EditorGizmos::Terminate();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
		}

		Handle<SceneObject> MakeObject(const char* name) {
			return SceneSystem::CreateObject(name, _scene);
		}

		void Draw() {
			_lines.clear();
			_overlay.clear();
			_icons.clear();
			GizmoDrawContext context;
			context.lines = &_lines;
			context.overlayLines = &_overlay;
			context.icons = &_icons;
			context.cameraPosition = Vec3(0.0f, 5.0f, -10.0f);
			context.viewAspect = 16.0f / 9.0f;
			EditorGizmos::DrawSceneGizmos(context);
			SceneSystem::EndFrame();
		}

		void DrawLinesOnly() {
			_lines.clear();
			_overlay.clear();
			_icons.clear();
			GizmoDrawContext context;
			context.lines = &_lines;
			context.overlayLines = &_overlay;
			context.cameraPosition = Vec3(0.0f, 5.0f, -10.0f);
			context.viewAspect = 16.0f / 9.0f;
			EditorGizmos::DrawSceneGizmos(context);
			SceneSystem::EndFrame();
		}

		u32 IconsFor(const Handle<SceneObject>& object) const {
			u32 count = 0;
			for (const SceneIcon& icon : _icons) {
				if (icon.object == object) {
					++count;
				}
			}
			return count;
		}

		f32 Reach() const {
			f32 reach = 0.0f;
			for (const DebugLine& line : _lines) {
				reach = Math::Max(reach, Math::Max(line.a.Length(), line.b.Length()));
			}
			return reach;
		}
	};

} // anonymous namespace

TEST_F(EditorGizmosFixture, OnlyRegisteredGizmosDraw) {
	Handle<SceneObject> object = MakeObject("Probe");
	object->AddComponent<Camera>();
	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	const size_t withCamera = _lines.size();
	EXPECT_GT(withCamera, 0u);

	MakeObject("Empty");
	Draw();
	EXPECT_EQ(_lines.size(), withCamera);
}

TEST_F(EditorGizmosFixture, UnselectedCameraIsIconOnly) {
	Handle<SceneObject> object = MakeObject("Camera");
	object->AddComponent<Camera>();
	Draw();
	EXPECT_EQ(_lines.size(), 0u);
	EXPECT_EQ(IconsFor(object), 1u);

	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	EXPECT_GT(_lines.size(), 0u);
	EXPECT_EQ(IconsFor(object), 1u);
}

TEST_F(EditorGizmosFixture, IconOnlyForOptedInType) {
	Handle<SceneObject> bare = MakeObject("Bare");
	Handle<SceneObject> light = MakeObject("Light");
	light->AddComponent<Light>();
	Draw();
	EXPECT_EQ(IconsFor(bare), 0u);
	EXPECT_EQ(IconsFor(light), 1u);
	EXPECT_EQ(_icons.size(), 1u);
}

TEST_F(EditorGizmosFixture, TwoComponentsEmitTwoIcons) {
	Handle<SceneObject> object = MakeObject("Both");
	object->AddComponent<Camera>();
	object->AddComponent<Light>();
	Draw();
	EXPECT_EQ(IconsFor(object), 2u);
	ASSERT_EQ(_icons.size(), 2u);
	EXPECT_EQ(_icons[0].object, _icons[1].object);
	EXPECT_NE(_icons[0].type, _icons[1].type);
}

TEST_F(EditorGizmosFixture, NoIconSinkStillDrawsLines) {
	Handle<SceneObject> object = MakeObject("Camera");
	object->AddComponent<Camera>();
	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	const size_t withSink = _lines.size();
	ASSERT_GT(withSink, 0u);

	DrawLinesOnly();
	EXPECT_EQ(_lines.size(), withSink);
	EXPECT_TRUE(_icons.empty());
}

TEST_F(EditorGizmosFixture, SweepTakesModuleIcons) {
	int moduleImage = 0;
	ASSERT_TRUE(EditorGizmos::RegisterSceneIcon(StringHash("Camera"), &moduleImage));
	EXPECT_EQ(EditorGizmos::UnregisterOwnedBy(&moduleImage), 1u);

	Handle<SceneObject> camera = MakeObject("Camera");
	camera->AddComponent<Camera>();
	Handle<SceneObject> light = MakeObject("Light");
	light->AddComponent<Light>();
	Draw();
	EXPECT_EQ(IconsFor(camera), 0u);
	EXPECT_EQ(IconsFor(light), 1u);
}

TEST_F(EditorGizmosFixture, TheLastRegistrationForAKeyWins) {
	u32 first = 0;
	u32 second = 0;
	EditorGizmos::RegisterComponentGizmo(StringHash("Camera"),
										 [&first](GizmoDrawContext&, Handle<Component>) { ++first; });
	EditorGizmos::RegisterComponentGizmo(StringHash("Camera"),
										 [&second](GizmoDrawContext&, Handle<Component>) { ++second; });

	Handle<SceneObject> object = MakeObject("Probe");
	object->AddComponent<Camera>();
	Draw();

	EXPECT_EQ(first, 0u);
	EXPECT_EQ(second, 1u);
}

TEST_F(EditorGizmosFixture, SweepTakesModuleGizmos) {
	int moduleImageA = 0;
	int moduleImageB = 0;
	EditorGizmos::RegisterComponentGizmo(StringHash("FakeA"), [](GizmoDrawContext&, Handle<Component>) {}, &moduleImageA);
	EditorGizmos::RegisterComponentGizmo(StringHash("FakeB"), [](GizmoDrawContext&, Handle<Component>) {}, &moduleImageB);
	ASSERT_NE(EditorGizmos::FindComponentGizmo(StringHash("FakeA")), nullptr);

	EXPECT_EQ(EditorGizmos::UnregisterOwnedBy(&moduleImageA), 1u);
	EXPECT_EQ(EditorGizmos::FindComponentGizmo(StringHash("FakeA")), nullptr);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("FakeB")), nullptr);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("Camera")), nullptr);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("Light")), nullptr);

	EXPECT_EQ(EditorGizmos::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("Camera")), nullptr);
}

TEST_F(EditorGizmosFixture, InactiveObjectDrawsNothing) {
	Handle<SceneObject> object = MakeObject("Probe");
	object->AddComponent<Camera>();
	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	ASSERT_GT(_lines.size(), 0u);
	ASSERT_EQ(IconsFor(object), 1u);

	object->SetActive(false);
	Draw();
	EXPECT_EQ(_lines.size(), 0u);
	EXPECT_EQ(IconsFor(object), 0u);
}

TEST_F(EditorGizmosFixture, CameraGrowsFrustumOnlyWhenSelected) {
	Handle<SceneObject> object = MakeObject("Camera");
	Handle<Camera> camera = object->AddComponent<Camera>();
	camera->SetNearPlane(0.1f);
	camera->SetFarPlane(20.0f);
	Draw();
	EXPECT_EQ(_lines.size(), 0u);

	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	EXPECT_GT(Reach(), 10.0f);
}

TEST_F(EditorGizmosFixture, CameraFrustumFollowsCamerasProjection) {
	Handle<SceneObject> object = MakeObject("Camera");
	Handle<Camera> camera = object->AddComponent<Camera>();
	camera->SetNearPlane(0.1f);
	camera->SetFarPlane(20.0f);
	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	const f32 nearFar = Reach();

	camera->SetFarPlane(40.0f);
	Draw();
	const f32 farther = Reach();
	EXPECT_GT(farther, nearFar * 1.8f);

	camera->SetFieldOfViewDeg(120.0f);
	Draw();
	EXPECT_GT(Reach(), farther);
}

TEST_F(EditorGizmosFixture, LightsSelectedGizmoShowsExtentTypeMeans) {
	Handle<SceneObject> object = MakeObject("Light");
	Handle<Light> light = object->AddComponent<Light>();
	light->SetLightType(LightType::Point);
	light->SetRange(7.0f);
	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	EXPECT_NEAR(Reach(), 7.0f, 0.5f);

	light->SetRange(14.0f);
	Draw();
	EXPECT_NEAR(Reach(), 14.0f, 0.5f);

	light->SetLightType(LightType::Spot);
	light->SetSpotAngleDeg(20.0f);
	Draw();
	const f32 narrow = Reach();
	light->SetSpotAngleDeg(120.0f);
	Draw();
	EXPECT_GT(Reach(), narrow);
}

TEST_F(EditorGizmosFixture, LocalEnvironmentDrawsBlendBox) {
	Handle<SceneObject> object = MakeObject("Zone");
	object->SetLocalScale(Vec3(2.0f, 1.0f, 1.0f));
	Handle<Environment> environment = object->AddComponent<Environment>();
	environment->SetIsGlobal(false);
	environment->SetSize(Vec3(4.0f, 6.0f, 8.0f));
	Draw();
	EXPECT_EQ(_lines.size(), 0u);

	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	EXPECT_EQ(_lines.size(), 12u);
	EXPECT_NEAR(Reach(), std::sqrt(41.0f), 1e-3f);

	environment->SetIsGlobal(true);
	Draw();
	EXPECT_EQ(_lines.size(), 0u);
}

TEST_F(EditorGizmosFixture, SelectingComponentCountsAsSelectingOwner) {
	Handle<SceneObject> object = MakeObject("Camera");
	Handle<Camera> camera = object->AddComponent<Camera>();
	EditorSelection::Select(Handle<Object>(object.Get()));
	Draw();
	const f32 viaObject = Reach();

	EditorSelection::Select(Handle<Object>(camera.Get()));
	Draw();
	EXPECT_NEAR(Reach(), viaObject, 1.0e-3f);
}

namespace {

	Mat4 IconViewProjection(const Vec2& sizePx) {
		return EditorViewportOps::BuildProjection(sizePx.x / sizePx.y) *
			   EditorViewportOps::BuildViewMatrix(Vec3::Zero(), Quat::Identity());
	}

	constexpr f32 ICON_PX = 20.0f;

} // anonymous namespace

TEST_F(EditorGizmosFixture, IconBehindCameraIsNotPlaced) {
	const Vec2 sizePx{800.0f, 600.0f};
	Handle<SceneObject> ahead = MakeObject("Ahead");
	Handle<SceneObject> behind = MakeObject("Behind");
	const SceneIcon icons[] = {
		{ahead, nullptr, Vec3(0.0f, 0.0f, 10.0f)},
		{behind, nullptr, Vec3(0.0f, 0.0f, -10.0f)},
	};
	Vector<SceneIconPlacement> placed;
	EditorGizmos::LayOutSceneIcons(icons, IconViewProjection(sizePx), sizePx, ICON_PX, placed);

	ASSERT_EQ(placed.size(), 1u);
	EXPECT_EQ(placed[0].object, ahead);
	EXPECT_NEAR(placed[0].centerPx.x, sizePx.x * 0.5f, 0.5f);
	EXPECT_NEAR(placed[0].centerPx.y, sizePx.y * 0.5f, 0.5f);
}

TEST_F(EditorGizmosFixture, AnIconOffTheViewportIsDropped) {
	const Vec2 sizePx{800.0f, 600.0f};
	Handle<SceneObject> object = MakeObject("FarLeft");
	const SceneIcon icons[] = {{object, nullptr, Vec3(-200.0f, 0.0f, 10.0f)}};
	Vector<SceneIconPlacement> placed;
	EditorGizmos::LayOutSceneIcons(icons, IconViewProjection(sizePx), sizePx, ICON_PX, placed);
	EXPECT_TRUE(placed.empty());
}

TEST_F(EditorGizmosFixture, SharedIconsSitSideBySide) {
	const Vec2 sizePx{800.0f, 600.0f};
	Handle<SceneObject> object = MakeObject("Both");
	const SceneIcon icons[] = {
		{object, nullptr, Vec3(0.0f, 0.0f, 10.0f)},
		{object, nullptr, Vec3(0.0f, 0.0f, 10.0f)},
	};
	Vector<SceneIconPlacement> placed;
	EditorGizmos::LayOutSceneIcons(icons, IconViewProjection(sizePx), sizePx, ICON_PX, placed);

	ASSERT_EQ(placed.size(), 2u);
	EXPECT_NEAR(placed[0].centerPx.y, placed[1].centerPx.y, 1.0e-3f);
	EXPECT_NEAR(std::abs(placed[1].centerPx.x - placed[0].centerPx.x), ICON_PX, 1.0e-3f);
	EXPECT_NEAR((placed[0].centerPx.x + placed[1].centerPx.x) * 0.5f, sizePx.x * 0.5f, 0.5f);

	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, placed[0].centerPx, ICON_PX), 0u);
	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, placed[1].centerPx, ICON_PX), 1u);
}

TEST_F(EditorGizmosFixture, NEARERIconTakesClickWhere2Overlap) {
	const Vec2 sizePx{800.0f, 600.0f};
	Handle<SceneObject> near = MakeObject("Near");
	Handle<SceneObject> far = MakeObject("Far");
	const SceneIcon icons[] = {
		{far, nullptr, Vec3(0.0f, 0.0f, 30.0f)},
		{near, nullptr, Vec3(0.0f, 0.0f, 6.0f)},
	};
	Vector<SceneIconPlacement> placed;
	EditorGizmos::LayOutSceneIcons(icons, IconViewProjection(sizePx), sizePx, ICON_PX, placed);

	ASSERT_EQ(placed.size(), 2u);
	EXPECT_EQ(placed[0].object, far);
	EXPECT_EQ(placed[1].object, near);

	const u32 hit = EditorGizmos::HitSceneIcon(placed, Vec2{sizePx.x * 0.5f, sizePx.y * 0.5f}, ICON_PX);
	ASSERT_NE(hit, SCENE_ICON_NONE);
	EXPECT_EQ(placed[hit].object, near);
}

TEST_F(EditorGizmosFixture, NearerIconWinsInOrthographic) {
	const Vec2 sizePx{800.0f, 600.0f};
	EditorViewportOps::Viewpoint viewpoint;
	viewpoint.orthographic = true;
	viewpoint.orthographicSize = 20.0f;
	viewpoint.aspect = sizePx.x / sizePx.y;
	const Mat4 viewProjection = EditorViewportOps::BuildProjection(viewpoint) *
								EditorViewportOps::BuildViewMatrix(viewpoint);

	Handle<SceneObject> near = MakeObject("Near");
	Handle<SceneObject> far = MakeObject("Far");
	const SceneIcon icons[] = {
		{far, nullptr, Vec3(0.0f, 0.0f, 30.0f)},
		{near, nullptr, Vec3(0.0f, 0.0f, 6.0f)},
	};
	Vector<SceneIconPlacement> placed;
	EditorGizmos::LayOutSceneIcons(icons, viewProjection, sizePx, ICON_PX, placed);

	ASSERT_EQ(placed.size(), 2u);
	EXPECT_GT(placed[1].sortDepth, placed[0].sortDepth);
	EXPECT_EQ(placed[0].object, far);
	EXPECT_EQ(placed[1].object, near);

	const u32 hit = EditorGizmos::HitSceneIcon(placed, Vec2{sizePx.x * 0.5f, sizePx.y * 0.5f}, ICON_PX);
	ASSERT_NE(hit, SCENE_ICON_NONE);
	EXPECT_EQ(placed[hit].object, near);
}

TEST_F(EditorGizmosFixture, ClickTargetIsSquareLayoutWasGiven) {
	const Vec2 sizePx{800.0f, 600.0f};
	Handle<SceneObject> object = MakeObject("One");
	const SceneIcon icons[] = {{object, nullptr, Vec3(0.0f, 0.0f, 10.0f)}};
	Vector<SceneIconPlacement> placed;
	EditorGizmos::LayOutSceneIcons(icons, IconViewProjection(sizePx), sizePx, ICON_PX, placed);
	ASSERT_EQ(placed.size(), 1u);

	const Vec2 center = placed[0].centerPx;
	const f32 half = ICON_PX * 0.5f;
	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, center + Vec2{half - 1.0f, half - 1.0f}, ICON_PX), 0u);
	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, center - Vec2{half - 1.0f, half - 1.0f}, ICON_PX), 0u);
	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, center + Vec2{half + 1.0f, 0.0f}, ICON_PX), SCENE_ICON_NONE);
	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, center + Vec2{0.0f, half + 1.0f}, ICON_PX), SCENE_ICON_NONE);
	EXPECT_EQ(EditorGizmos::HitSceneIcon(placed, center - Vec2{half + 1.0f, 0.0f}, ICON_PX), SCENE_ICON_NONE);
}

namespace {

	f32 ExtentAlong(const Vector<DebugLine>& lines, const Vec3& origin, const Vec3& axis) {
		f32 extent = 0.0f;
		for (const DebugLine& line : lines) {
			extent = Math::Max(extent, Math::Abs(Vec3::Dot(line.a - origin, axis)));
			extent = Math::Max(extent, Math::Abs(Vec3::Dot(line.b - origin, axis)));
		}
		return extent;
	}

} // anonymous namespace

TEST(EditorGizmoShapesTest, CircleLiesInPlaneNormalNames) {
	Vector<DebugLine> lines;
	EditorGizmos::AppendCircle(lines, Vec3(1.0f, 2.0f, 3.0f), Vec3::Up(), 2.0f, Color32::White());
	ASSERT_FALSE(lines.empty());
	for (const DebugLine& line : lines) {
		EXPECT_NEAR(line.a.y, 2.0f, 1.0e-4f);
		const Vec3 planar(line.a.x - 1.0f, 0.0f, line.a.z - 3.0f);
		EXPECT_NEAR(planar.Length(), 2.0f, 1.0e-3f);
	}
	EXPECT_NEAR((lines.back().b - lines.front().a).Length(), 0.0f, 1.0e-4f);
}

TEST(EditorGizmoShapesTest, OrientedBoxTurnsWithRotation) {
	Vector<DebugLine> upright;
	EditorGizmos::AppendOrientedBox(upright, Vec3(), Quat::Identity(), Vec3(2.0f, 1.0f, 1.0f), Color32::White());
	EXPECT_EQ(upright.size(), 12u);
	EXPECT_NEAR(ExtentAlong(upright, Vec3(), Vec3::Right()), 2.0f, 1.0e-4f);
	EXPECT_NEAR(ExtentAlong(upright, Vec3(), Vec3::Forward()), 1.0f, 1.0e-4f);

	Vector<DebugLine> turned;
	EditorGizmos::AppendOrientedBox(turned, Vec3(), Quat::FromEuler(Vec3(0.0f, 90.0f, 0.0f)), Vec3(2.0f, 1.0f, 1.0f), Color32::White());
	EXPECT_NEAR(ExtentAlong(turned, Vec3(), Vec3::Right()), 1.0f, 1.0e-3f);
	EXPECT_NEAR(ExtentAlong(turned, Vec3(), Vec3::Forward()), 2.0f, 1.0e-3f);
}

TEST(EditorGizmoShapesTest, CapsuleEndsAtDomes) {
	Vector<DebugLine> lines;
	EditorGizmos::AppendCapsule(lines, Vec3(0.0f, -0.5f, 0.0f), Vec3(0.0f, 0.5f, 0.0f), 0.5f, Color32::White());
	ASSERT_FALSE(lines.empty());
	EXPECT_NEAR(ExtentAlong(lines, Vec3(), Vec3::Up()), 1.0f, 1.0e-3f);
	EXPECT_NEAR(ExtentAlong(lines, Vec3(), Vec3::Right()), 0.5f, 1.0e-3f);
}

TEST(EditorGizmoShapesTest, DegenerateCapsuleDrawsSphereItBecame) {
	Vector<DebugLine> lines;
	EditorGizmos::AppendCapsule(lines, Vec3(2.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), 0.75f, Color32::White());
	ASSERT_FALSE(lines.empty());
	const Vec3 center(2.0f, 0.0f, 0.0f);
	EXPECT_NEAR(ExtentAlong(lines, center, Vec3::Up()), 0.75f, 1.0e-3f);
	EXPECT_NEAR(ExtentAlong(lines, center, Vec3::Right()), 0.75f, 1.0e-3f);
	EXPECT_NEAR(ExtentAlong(lines, center, Vec3::Forward()), 0.75f, 1.0e-3f);
	for (const DebugLine& line : lines) {
		EXPECT_TRUE(std::isfinite(line.a.x) && std::isfinite(line.a.y) && std::isfinite(line.a.z));
	}
}

TEST_F(EditorGizmosFixture, SceneGizmoDrawsUnselected) {
	u32 calls = 0;
	bool sawSelected = true;
	EditorGizmos::RegisterSceneGizmo(StringHash("Probe"), [&](GizmoDrawContext& context) {
		++calls;
		sawSelected = context.selected;
		context.lines->push_back({Vec3(), Vec3(0.0f, 3.0f, 0.0f), Color32::White()});
	});

	Draw();
	EXPECT_EQ(calls, 1u);
	EXPECT_FALSE(sawSelected);
	EXPECT_NEAR(Reach(), 3.0f, 1.0e-4f);

	Handle<SceneObject> object = MakeObject("Probe");
	object->AddComponent<Camera>();
	Draw();
	EXPECT_EQ(calls, 2u);
}

TEST_F(EditorGizmosFixture, SweepTakesSceneGizmo) {
	int moduleImage = 0;
	EditorGizmos::RegisterComponentGizmo(StringHash("FakeA"), [](GizmoDrawContext&, Handle<Component>) {}, &moduleImage);
	EditorGizmos::RegisterSceneGizmo(StringHash("FakeScene"), [](GizmoDrawContext& context) { context.lines->push_back({Vec3(), Vec3(0.0f, 5.0f, 0.0f), Color32::White()}); }, &moduleImage);

	Draw();
	ASSERT_NEAR(Reach(), 5.0f, 1.0e-4f);

	EXPECT_EQ(EditorGizmos::UnregisterOwnedBy(&moduleImage), 2u);
	Draw();
	EXPECT_EQ(_lines.size(), 0u);
}

namespace {

	class GizmoCapturingSink : public ILogSink {
	public:
		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			_messages.push_back(str(event.message));
		}
		void Flush() override {}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
	};

} // anonymous namespace

TEST(EditorGizmoRegistryTest, RegistrationBeforeInitializeIsRefusedAndReported) {
	EditorGizmos::Terminate();

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<GizmoCapturingSink>();
	LogManager::AddSink(sink);

	EXPECT_FALSE(EditorGizmos::RegisterComponentGizmo(StringHash("TooEarly"), [](GizmoDrawContext&, Handle<Component>) {}));
	EXPECT_FALSE(EditorGizmos::RegisterSceneGizmo(StringHash("TooEarly"), [](GizmoDrawContext&) {}));

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);
	EXPECT_EQ(sink->CountMatching("discarded"), 2u);
	sink.reset();
	JobSystem::Terminate();

	EditorGizmos::Initialize();
	EXPECT_TRUE(EditorGizmos::RegisterComponentGizmo(StringHash("InTime"), [](GizmoDrawContext&, Handle<Component>) {}));
	EXPECT_NE(EditorGizmos::FindComponentGizmo(StringHash("InTime")), nullptr);
	EditorGizmos::Terminate();
}

namespace {

	Vector<CascadeFit> BuildDistinctFits(u32 count) {
		Vector<CascadeFit> fits{GetMemoryResource(MemTag::Editor)};
		const Mat4 lightView = Mat4::LookAt(Vec3(40.0f, 60.0f, -30.0f), Vec3::Zero());
		for (u32 i = 0; i < count; ++i) {
			const f32 radius = 5.0f * static_cast<f32>(i + 1);
			CascadeFit& fit = fits.emplace_back();
			fit.viewProjection =
				Mat4::OrthographicReverseZ(-radius, radius, -radius, radius, 0.0f, 200.0f) * lightView;
			fit.worldTexelSize = radius / 512.0f;
			fit.farDepth = radius * 2.0f;
		}
		return fits;
	}

	bool SameLines(std::span<const DebugLine> a, std::span<const DebugLine> b) {
		if (a.size() != b.size()) {
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i].a != b[i].a || a[i].b != b[i].b) {
				return false;
			}
		}
		return true;
	}

} // anonymous namespace

TEST(EditorCascadeFitTest, NegativeIndexDrawsAllCascades) {
	const Vector<CascadeFit> fits = BuildDistinctFits(MAX_SHADOW_CASCADES);

	Vector<DebugLine> all{GetMemoryResource(MemTag::Editor)};
	EditorGizmos::AppendCascadeFitLines(all, {fits.data(), fits.size()}, -1);
	ASSERT_FALSE(all.empty());
	ASSERT_EQ(all.size() % fits.size(), 0u);
	const size_t perCascade = all.size() / fits.size();

	for (u32 i = 0; i < MAX_SHADOW_CASCADES; ++i) {
		Vector<DebugLine> one{GetMemoryResource(MemTag::Editor)};
		EditorGizmos::AppendCascadeFitLines(one, {fits.data(), fits.size()}, static_cast<i32>(i));
		ASSERT_EQ(one.size(), perCascade) << "cascade " << i;
		EXPECT_TRUE(SameLines({one.data(), one.size()},
							  {all.data() + i * perCascade, perCascade}))
			<< "cascade " << i;
	}

	EXPECT_FALSE(SameLines({all.data(), perCascade}, {all.data() + perCascade, perCascade}));
}

TEST(EditorCascadeFitTest, AnIndexPastTheFitDrawsNothing) {
	const Vector<CascadeFit> fits = BuildDistinctFits(2);

	Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
	EditorGizmos::AppendCascadeFitLines(lines, {fits.data(), fits.size()}, 2);
	EXPECT_TRUE(lines.empty());

	EditorGizmos::AppendCascadeFitLines(lines, {fits.data(), fits.size()}, 1);
	EXPECT_FALSE(lines.empty());

	lines.clear();
	EditorGizmos::AppendCascadeFitLines(lines, {}, -1);
	EXPECT_TRUE(lines.empty());
}

TEST(EditorCascadeFitTest, LinesAppendToExisting) {
	const Vector<CascadeFit> fits = BuildDistinctFits(1);
	Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
	lines.push_back(DebugLine{Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, 5.0f, 6.0f), Color32::White()});

	EditorGizmos::AppendCascadeFitLines(lines, {fits.data(), fits.size()}, 0);
	ASSERT_GT(lines.size(), 1u);
	EXPECT_EQ(lines.front().a, Vec3(1.0f, 2.0f, 3.0f));
}

TEST_F(EditorGizmosFixture, SweepTakesModuleRadii) {
	int moduleImage = 0;
	ASSERT_TRUE(EditorGizmos::RegisterRadiusHandles(
		StringHash("Light"),
		[](Handle<Component>, Vector<RadiusHandle>& out) {
			RadiusHandle radius;
			radius.radius = 3.0f;
			radius.field = StringHash("_range");
			out.push_back(radius);
		},
		&moduleImage));
	Handle<SceneObject> lit = MakeObject("Lit");
	Handle<Light> light = lit->AddComponent<Light>();
	Handle<SceneObject> other = MakeObject("Other");
	other->AddComponent<Light>();

	Vector<ComponentRadius> radii{GetMemoryResource(MemTag::Editor)};
	EditorGizmos::GatherRadiusHandles(radii);
	EXPECT_TRUE(radii.empty());

	EditorSelection::Select(Handle<Object>(lit.Get()));
	EditorGizmos::GatherRadiusHandles(radii);
	ASSERT_EQ(radii.size(), 1u);
	EXPECT_EQ(radii[0].component.Get(), static_cast<Component*>(light.Get()));
	EXPECT_EQ(radii[0].handle.radius, 3.0f);

	EditorSelection::ToggleInSelection(Handle<Object>(light.Get()));
	EditorGizmos::GatherRadiusHandles(radii);
	EXPECT_EQ(radii.size(), 1u);
	EditorSelection::Select(Handle<Object>(light.Get()));
	EditorGizmos::GatherRadiusHandles(radii);
	EXPECT_EQ(radii.size(), 1u);

	EditorSelection::Select(Handle<Object>(lit.Get()));
	lit->SetActive(false);
	EditorGizmos::GatherRadiusHandles(radii);
	EXPECT_TRUE(radii.empty());
	lit->SetActive(true);

	EXPECT_EQ(EditorGizmos::UnregisterOwnedBy(&moduleImage), 1u);
	EditorGizmos::GatherRadiusHandles(radii);
	EXPECT_TRUE(radii.empty());
}

TEST_F(EditorGizmosFixture, ToolHandlesWriteBackToSource) {
	int moduleImage = 0;
	SceneHandle written;
	u32 writes = 0;
	ASSERT_TRUE(EditorGizmos::RegisterSceneHandles(
		StringHash("Tool.Shape"),
		[](Vector<SceneHandle>& out) {
			SceneHandle handle;
			handle.id = 7;
			handle.position = Vec3(1.0f, 2.0f, 3.0f);
			out.push_back(handle);
		},
		[&](const SceneHandle& dragged) {
			written = dragged;
			++writes;
		},
		&moduleImage));
	Vector<OfferedSceneHandle> offered{GetMemoryResource(MemTag::Editor)};
	EditorGizmos::GatherSceneHandles(offered);
	ASSERT_EQ(offered.size(), 1u);
	EXPECT_EQ(offered[0].source, StringHash("Tool.Shape"));
	EXPECT_EQ(offered[0].handle.id, 7u);

	SceneHandle moved = offered[0].handle;
	moved.position = Vec3(4.0f, 5.0f, 6.0f);
	EXPECT_TRUE(EditorGizmos::WriteSceneHandle(StringHash("Tool.Shape"), moved));
	ASSERT_EQ(writes, 1u);
	EXPECT_EQ(written.position, Vec3(4.0f, 5.0f, 6.0f));
	EXPECT_EQ(written.id, 7u);
	EXPECT_FALSE(EditorGizmos::WriteSceneHandle(StringHash("Tool.Other"), moved));
	EXPECT_EQ(writes, 1u);

	EXPECT_EQ(EditorGizmos::UnregisterOwnedBy(&moduleImage), 1u);
	EditorGizmos::GatherSceneHandles(offered);
	EXPECT_TRUE(offered.empty());
	EXPECT_FALSE(EditorGizmos::WriteSceneHandle(StringHash("Tool.Shape"), moved));
}
