#include "TestsPCH.h"

#include "TestPlatform.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#ifndef SHALLTEAR_SHARED

#include "PhysicsSettings.h"
#include "PhysicsWorld.h"

#include "Shalltear/Physics3D/CharacterController.h"
#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Physics.h"
#include "Shalltear/Physics3D/PhysicsListener.h"
#include "Shalltear/Physics3D/Rigidbody.h"
SLT_DECLARE_LINKED_PACKAGE(Physics3D)

using namespace Shalltear;

#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
#define SLT_SKIP_WITHOUT_GEOMETRY() GTEST_SKIP() << "stub backend has no real geometry"
#else
#define SLT_SKIP_WITHOUT_GEOMETRY() ((void)0)
#endif

namespace {

	constexpr f32 FRAME_STEP = 1.0f / 60.0f;

	constexpr f32 MOVER_SLOP = 0.005f;
	constexpr f32 SKIN = 0.08f;
	constexpr f32 REST_Y = 1.0f + SKIN - MOVER_SLOP;

	constexpr const char* LISTENER_MODULE_DLL = "ShalltearPhysicsListenerModule" SLT_TEST_DLL_SUFFIX;

	class ControllerProbe : public Behavior, public PhysicsListener {
	public:
		SLT_REFLECT_COMPONENT(ControllerProbe, Behavior)
		SLT_END_REFLECT()

		std::vector<ControllerColliderHit> hits;
		i32 triggerEnter = 0;
		bool destroyController = false;

		void OnControllerColliderHit(const ControllerColliderHit& hit) override {
			hits.push_back(hit);
			if (destroyController) {
				SceneSystem::Destroy(Handle<Component>(hit.controller.Get()));
			}
		}

		void OnTriggerEnter(Handle<Collider>) override {
			++triggerEnter;
		}
	};

	SLT_REGISTER_TYPE(ControllerProbe)

	class CharacterControllerFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 unique = counter.fetch_add(1);
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_controller_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			SLT_CALL_LINKED_PACKAGE(Physics3D, &_token);
			ASSERT_TRUE(GetPhysicsWorld().IsUp());

			_scene = SceneSystem::CreateScene("physics-controller");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		PhysicsSettings* MutableSettings() {
			return const_cast<PhysicsSettings*>(GetPhysicsWorld().GetSettings());
		}

		Handle<SceneObject> MakeObject(const char* name, const Vec3& position, const Quat& rotation = Quat::Identity()) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			if (object.IsValid()) {
				object->SetWorldPosition(position);
				object->SetWorldRotation(rotation);
			}
			return object;
		}

		Handle<BoxCollider> MakeBox(const char* name, const Vec3& center, const Vec3& size,
									const Quat& rotation = Quat::Identity()) {
			Handle<SceneObject> object = MakeObject(name, center, rotation);
			if (!object.IsValid()) {
				return {};
			}
			Handle<BoxCollider> box = object->AddComponent<BoxCollider>();
			if (box.IsValid()) {
				box->SetSize(size);
			}
			return box;
		}

		Handle<BoxCollider> MakeFloor() {
			return MakeBox("floor", Vec3{0.0f, -0.5f, 0.0f}, Vec3{40.0f, 1.0f, 40.0f});
		}

		Handle<BoxCollider> MakeRamp(f32 degrees, f32 startX) {
			const f32 radians = degrees * Math::DEG_TO_RAD;
			const Vec3 up{-std::sin(radians), std::cos(radians), 0.0f};
			const Vec3 along{std::cos(radians), std::sin(radians), 0.0f};
			const Vec3 center = Vec3{startX, 0.0f, 0.0f} - up * 0.5f + along * 6.0f;
			return MakeBox("ramp", center, Vec3{12.0f, 1.0f, 6.0f}, Quat::AngleAxis(degrees, Vec3{0.0f, 0.0f, 1.0f}));
		}

		Handle<CharacterController> MakeController(const char* name, const Vec3& position) {
			Handle<SceneObject> object = MakeObject(name, position);
			return object.IsValid() ? object->AddComponent<CharacterController>() : Handle<CharacterController>{};
		}

		static Vec3 PositionOf(Handle<CharacterController> controller) {
			return controller->GetOwner()->GetWorldPosition();
		}

		static FrameTime Frame() {
			FrameTime frame;
			frame.deltaTime = FRAME_STEP;
			frame.unscaledDeltaTime = FRAME_STEP;
			frame.fixedDeltaTime = FRAME_STEP;
			return frame;
		}

		static CollisionFlags MoveFrame(Handle<CharacterController> controller, const Vec3& motion) {
			Time::FrameScope clock{Frame()};
			return controller->Move(motion);
		}

		static bool SimpleFrame(Handle<CharacterController> controller, const Vec3& speed) {
			Time::FrameScope clock{Frame()};
			return controller->SimpleMove(speed);
		}

		static void Settle(Handle<CharacterController> controller, u32 frames = 120) {
			for (u32 frame = 0; frame < frames; ++frame) {
				SimpleFrame(controller, Vec3{});
			}
		}

		static void RunFixedSteps(u32 count) {
			FrameTime clockState;
			clockState.fixedDeltaTime = FRAME_STEP;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(FRAME_STEP);

			FrameStageArgs args;
			for (u32 index = 0; index < count; ++index) {
				FrameStages::Run(FrameStage::FixedUpdate, args);
			}
		}
	};

} // anonymous namespace

TEST_F(CharacterControllerFixture, FallsThenRestsOnGround) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 3.0f, 0.0f});
	ASSERT_TRUE(controller.IsValid());

	EXPECT_FALSE(SimpleFrame(controller, Vec3{}));
	EXPECT_LT(PositionOf(controller).y, 3.0f);

	Settle(controller, 180);
	EXPECT_TRUE(controller->IsGrounded());
	EXPECT_EQ(controller->GetCollisionFlags(), CollisionFlags::Below);
	const Vec3 rest = PositionOf(controller);
	EXPECT_NEAR(rest.y, REST_Y, 2.0e-3f);
	EXPECT_NEAR(rest.x, 0.0f, 1.0e-4f);
	EXPECT_NEAR(rest.z, 0.0f, 1.0e-4f);

	Settle(controller, 60);
	EXPECT_NEAR(PositionOf(controller).y, rest.y, 1.0e-3f);
	EXPECT_TRUE(controller->IsGrounded());
}

TEST_F(CharacterControllerFixture, GroundedOnlyWhilePushingDown) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller);
	ASSERT_TRUE(controller->IsGrounded());

	EXPECT_EQ(MoveFrame(controller, Vec3{0.05f, 0.0f, 0.0f}), CollisionFlags::None);
	EXPECT_FALSE(controller->IsGrounded());

	EXPECT_EQ(MoveFrame(controller, Vec3{0.0f, -0.01f, 0.0f}), CollisionFlags::Below);
	EXPECT_TRUE(controller->IsGrounded());

	EXPECT_EQ(MoveFrame(controller, Vec3{}), CollisionFlags::None);
}

TEST_F(CharacterControllerFixture, SlidesAlongWall) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeBox("wall", Vec3{2.0f, 2.0f, 0.0f}, Vec3{1.0f, 4.0f, 20.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);

	const Vec3 velocity{4.0f, -2.0f, 4.0f};
	for (u32 frame = 0; frame < 60; ++frame) {
		MoveFrame(controller, velocity * FRAME_STEP);
	}

	const Vec3 end = PositionOf(controller);
	EXPECT_LE(end.x, 1.5f - 0.5f);
	EXPECT_GT(end.x, 0.9f);
	EXPECT_GT(end.z, 3.9f);
	EXPECT_NEAR(end.y, REST_Y, 2.0e-3f);
	EXPECT_EQ(controller->GetCollisionFlags(), CollisionFlags::Sides | CollisionFlags::Below);
	EXPECT_NEAR(controller->GetVelocity().x, 0.0f, 0.01f);
	EXPECT_NEAR(controller->GetVelocity().z, 4.0f, 0.05f);
}

TEST_F(CharacterControllerFixture, CeilingStopsRise) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeBox("ceiling", Vec3{0.0f, 3.5f, 0.0f}, Vec3{10.0f, 1.0f, 10.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);

	EXPECT_EQ(MoveFrame(controller, Vec3{0.0f, 1.0f, 0.0f}), CollisionFlags::Above);
	const f32 y = PositionOf(controller).y;
	EXPECT_LE(y, 3.0f - 1.0f);
	EXPECT_GT(y, REST_Y + 0.5f);
}

TEST_F(CharacterControllerFixture, StepsOntoLowLedge) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeBox("ledge", Vec3{2.0f, 0.125f, 0.0f}, Vec3{2.0f, 0.25f, 4.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);

	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
	}

	const Vec3 end = PositionOf(controller);
	EXPECT_NEAR(end.y, 0.25f + REST_Y, 0.01f);
	EXPECT_GT(end.x, 1.0f);
	EXPECT_TRUE(controller->IsGrounded());
}

TEST_F(CharacterControllerFixture, TallLedgeBlocks) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeBox("ledge", Vec3{2.0f, 0.225f, 0.0f}, Vec3{2.0f, 0.45f, 4.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);

	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
	}

	const Vec3 end = PositionOf(controller);
	EXPECT_LT(end.x, 1.0f - 0.3f);
	EXPECT_NEAR(end.y, REST_Y, 5.0e-3f);
}

TEST_F(CharacterControllerFixture, ZeroStepOffsetBlocksLowLedge) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeBox("ledge", Vec3{2.0f, 0.125f, 0.0f}, Vec3{2.0f, 0.25f, 4.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	controller->SetStepOffset(0.0f);
	Settle(controller, 10);

	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_LT(PositionOf(controller).x, 1.0f - 0.3f);
	EXPECT_NEAR(PositionOf(controller).y, REST_Y, 5.0e-3f);
}

TEST_F(CharacterControllerFixture, ClimbsGentleRamp) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeRamp(30.0f, 2.0f).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);

	for (u32 frame = 0; frame < 90; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_GT(PositionOf(controller).y, REST_Y + 1.0f);
	EXPECT_TRUE(controller->IsGrounded());
}

TEST_F(CharacterControllerFixture, SteepRampBlocks) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeRamp(60.0f, 2.0f).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);

	for (u32 frame = 0; frame < 90; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_LT(PositionOf(controller).x, 2.0f);
	EXPECT_LT(PositionOf(controller).y, REST_Y + 0.05f);
}

TEST_F(CharacterControllerFixture, SlopeLimitDecidesClimb) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeRamp(60.0f, 2.0f).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	controller->SetSlopeLimit(70.0f);
	Settle(controller, 10);

	for (u32 frame = 0; frame < 90; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_GT(PositionOf(controller).y, REST_Y + 1.0f);
}

TEST_F(CharacterControllerFixture, StandsStillOnGentleRamp) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeRamp(30.0f, 2.0f).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{5.0f, 4.0f, 0.0f});
	Settle(controller, 180);

	EXPECT_TRUE(controller->IsGrounded());
	EXPECT_NEAR(PositionOf(controller).x, 5.0f, 0.02f);
	EXPECT_GT(PositionOf(controller).y, 2.0f);
}

TEST_F(CharacterControllerFixture, SlidesDownSteepRamp) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeRamp(60.0f, 2.0f).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{3.5f, 4.5f, 0.0f});
	for (u32 frame = 0; frame < 60; ++frame) {
		MoveFrame(controller, Vec3{0.0f, -0.1f, 0.0f});
	}

	EXPECT_LT(PositionOf(controller).x, 3.0f);
	EXPECT_LT(PositionOf(controller).y, REST_Y + 1.0f);
	EXPECT_TRUE(controller->IsGrounded());
}

TEST_F(CharacterControllerFixture, TriggersReportButNeverBlock) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<BoxCollider> trigger = MakeBox("zone", Vec3{2.0f, 1.0f, 0.0f}, Vec3{1.0f, 2.0f, 4.0f});
	ASSERT_TRUE(trigger.IsValid());
	trigger->SetTrigger(true);
	Handle<ControllerProbe> zoneProbe = trigger->GetOwner()->AddComponent<ControllerProbe>();

	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Handle<ControllerProbe> heroProbe = controller->GetOwner()->AddComponent<ControllerProbe>();
	ASSERT_TRUE(zoneProbe.IsValid() && heroProbe.IsValid());

	for (u32 frame = 0; frame < 60; ++frame) {
		SimpleFrame(controller, Vec3{3.0f, 0.0f, 0.0f});
		RunFixedSteps(1);
	}

	EXPECT_GT(PositionOf(controller).x, 2.5f);
	EXPECT_EQ(heroProbe->triggerEnter, 1);
	EXPECT_EQ(zoneProbe->triggerEnter, 1);
	for (const ControllerColliderHit& hit : heroProbe->hits) {
		EXPECT_NE(hit.collider.GetInstanceId(), trigger.GetInstanceId());
	}
}

TEST_F(CharacterControllerFixture, CategoriesDecideBlocking) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	MutableSettings()->categories = "Default,Ghost";
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<BoxCollider> wall = MakeBox("ghost wall", Vec3{2.0f, 2.0f, 0.0f}, Vec3{1.0f, 4.0f, 40.0f});
	ASSERT_TRUE(wall.IsValid());
	wall->SetCategories("Ghost");

	Handle<CharacterController> solid = MakeController("solid", Vec3{0.0f, REST_Y, -5.0f});
	Handle<CharacterController> ghost = MakeController("ghost", Vec3{0.0f, REST_Y, 5.0f});
	ghost->SetCollidesWithMode(PhysicsCategoryMatch::AllExcept);
	ghost->SetCollidesWith("Ghost");

	for (u32 frame = 0; frame < 60; ++frame) {
		SimpleFrame(solid, Vec3{3.0f, 0.0f, 0.0f});
		SimpleFrame(ghost, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_LT(PositionOf(solid).x, 1.5f - 0.5f);
	EXPECT_GT(PositionOf(ghost).x, 2.5f);
	EXPECT_TRUE(ghost->IsGrounded());
}

TEST_F(CharacterControllerFixture, VocabularyChangeReachesController) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	MutableSettings()->categories = "Default,Ghost";
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<BoxCollider> wall = MakeBox("ghost wall", Vec3{2.0f, 2.0f, 0.0f}, Vec3{1.0f, 4.0f, 40.0f});
	wall->SetCategories("Ghost");
	Handle<CharacterController> ghost = MakeController("ghost", Vec3{0.0f, REST_Y, 0.0f});
	ghost->SetCollidesWith("Ghost");
	Settle(ghost, 10);
	ASSERT_TRUE(ghost->IsGrounded());

	const u64 lowestKnown = std::min(StringHash("Default").GetValue(), StringHash("Ghost").GetValue());
	std::string shifting;
	for (u32 candidate = 0; candidate < 1000 && shifting.empty(); ++candidate) {
		const std::string name = "Shift" + std::to_string(candidate);
		if (StringHash(name).GetValue() < lowestKnown) {
			shifting = name;
		}
	}
	ASSERT_FALSE(shifting.empty());
	MutableSettings()->categories = str(("Default,Ghost," + shifting).c_str());
	for (u32 frame = 0; frame < 60; ++frame) {
		SimpleFrame(ghost, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_GT(PositionOf(ghost).x, 2.5f);
	EXPECT_NEAR(PositionOf(ghost).y, REST_Y, 2.0e-3f);
	EXPECT_TRUE(ghost->IsGrounded());
}

TEST_F(CharacterControllerFixture, StepPlacesMovedController) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 5.0f, 0.0f});
	controller->GetOwner()->SetWorldPosition(Vec3{10.0f, 5.0f, 0.0f});
	RunFixedSteps(1);

	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(Ray{Vec3{10.0f, 20.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}}, hit));
	EXPECT_EQ(hit.collider.GetInstanceId(), controller.GetInstanceId());
	EXPECT_NEAR(hit.point.y, 6.0f, 1.0e-3f);
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{0.0f, 20.0f, 0.0f}, Vec3{0.0f, -1.0f, 0.0f}}, hit));
}

TEST_F(CharacterControllerFixture, HitsOncePerColliderPerMove) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> floor = MakeFloor();
	Handle<BoxCollider> wall = MakeBox("wall", Vec3{2.0f, 2.0f, 0.0f}, Vec3{1.0f, 4.0f, 20.0f});
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.9f, REST_Y, 0.0f});
	Handle<ControllerProbe> probe = controller->GetOwner()->AddComponent<ControllerProbe>();
	ASSERT_TRUE(floor.IsValid() && wall.IsValid() && probe.IsValid());
	Settle(controller, 10);
	probe->hits.clear();

	MoveFrame(controller, Vec3{0.1f, -0.05f, 0.0f});

	ASSERT_EQ(probe->hits.size(), 2u);
	const ControllerColliderHit* floorHit = nullptr;
	const ControllerColliderHit* wallHit = nullptr;
	for (const ControllerColliderHit& hit : probe->hits) {
		EXPECT_EQ(hit.controller.GetInstanceId(), controller.GetInstanceId());
		EXPECT_FALSE(hit.rigidbody.IsValid());
		EXPECT_GE(hit.moveLength, 0.0f);
		if (hit.collider.GetInstanceId() == floor.GetInstanceId()) {
			floorHit = &hit;
		} else if (hit.collider.GetInstanceId() == wall.GetInstanceId()) {
			wallHit = &hit;
		}
	}
	ASSERT_NE(floorHit, nullptr);
	ASSERT_NE(wallHit, nullptr);
	EXPECT_EQ(floorHit->object.GetInstanceId(), floor->GetOwner().GetInstanceId());
	EXPECT_NEAR(floorHit->moveDirection.y, -1.0f, 1.0e-5f);
	EXPECT_NEAR(floorHit->normal.y, 1.0f, 1.0e-3f);
	EXPECT_NEAR(wallHit->moveDirection.x, 1.0f, 1.0e-5f);
	EXPECT_NEAR(wallHit->moveDirection.y, 0.0f, 1.0e-5f);
	EXPECT_NEAR(wallHit->normal.x, -1.0f, 1.0e-3f);
	EXPECT_NEAR(wallHit->point.x, 1.5f, 0.01f);
}

TEST_F(CharacterControllerFixture, OverlapRecoveryIsNotHit) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> floor = MakeFloor();
	ASSERT_TRUE(MakeBox("wall", Vec3{2.0f, 2.0f, 0.0f}, Vec3{1.0f, 4.0f, 20.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.95f, REST_Y + 0.05f, 0.0f});
	Handle<ControllerProbe> probe = controller->GetOwner()->AddComponent<ControllerProbe>();

	EXPECT_EQ(MoveFrame(controller, Vec3{0.0f, -0.1f, 0.0f}), CollisionFlags::Below);

	ASSERT_EQ(probe->hits.size(), 1u);
	EXPECT_EQ(probe->hits[0].collider.GetInstanceId(), floor.GetInstanceId());
	EXPECT_NEAR(PositionOf(controller).x, 1.5f - 0.5f - SKIN + MOVER_SLOP, 2.0e-3f);
	EXPECT_NEAR(PositionOf(controller).y, REST_Y, 2.0e-3f);
}

TEST_F(CharacterControllerFixture, ListenerMayDestroyController) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	ASSERT_TRUE(MakeBox("wall", Vec3{2.0f, 2.0f, 0.0f}, Vec3{1.0f, 4.0f, 20.0f}).IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.9f, REST_Y, 0.0f});
	Handle<ControllerProbe> probe = controller->GetOwner()->AddComponent<ControllerProbe>();
	Settle(controller, 10);
	probe->hits.clear();
	probe->destroyController = true;

	MoveFrame(controller, Vec3{0.1f, -0.05f, 0.0f});

	EXPECT_EQ(probe->hits.size(), 1u);
	EXPECT_TRUE(controller->IsPendingDestroy());
}

TEST_F(CharacterControllerFixture, DetectCollisionsDecidesPassage) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Settle(controller, 10);
	EXPECT_TRUE(controller->IsInSimulation());

	Handle<SceneObject> ball = MakeObject("ball", Vec3{0.0f, 3.0f, 0.0f});
	ball->AddComponent<Rigidbody>();
	Handle<SphereCollider> sphere = ball->AddComponent<SphereCollider>();
	sphere->SetRadius(0.25f);

	RunFixedSteps(90);
	EXPECT_GT(ball->GetWorldPosition().y, REST_Y + 1.0f);

	controller->SetDetectCollisions(false);
	EXPECT_FALSE(controller->IsInSimulation());
	RunFixedSteps(120);
	EXPECT_LT(ball->GetWorldPosition().y, 0.5f);

	controller->GetOwner()->SetWorldPosition(Vec3{5.0f, REST_Y + 0.5f, 0.0f});
	EXPECT_EQ(MoveFrame(controller, Vec3{0.0f, -1.0f, 0.0f}), CollisionFlags::Below);
	EXPECT_NEAR(PositionOf(controller).y, REST_Y, 2.0e-3f);
}

TEST_F(CharacterControllerFixture, MovingPlatformCarries) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> deck = MakeBox("deck", Vec3{0.0f, 0.25f, 0.0f}, Vec3{4.0f, 0.5f, 4.0f});
	Handle<Rigidbody> platform = deck->GetOwner()->AddComponent<Rigidbody>();
	platform->SetKinematic(true);
	platform->SetCarryRiders(true);
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 0.5f + REST_Y, 0.0f});
	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}
	ASSERT_TRUE(controller->IsGrounded());

	for (u32 frame = 1; frame <= 30; ++frame) {
		deck->GetOwner()->SetWorldPosition(Vec3{2.0f * FRAME_STEP * static_cast<f32>(frame), 0.25f, 0.0f});
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}

	EXPECT_NEAR(PositionOf(controller).x, 1.0f, 0.01f);
	EXPECT_NEAR(PositionOf(controller).y, 0.5f + REST_Y, 2.0e-3f);
	EXPECT_NEAR(controller->GetVelocity().x, 0.0f, 0.01f);
	EXPECT_TRUE(controller->IsGrounded());
}

TEST_F(CharacterControllerFixture, StillPlatformLeavesRider) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> deck = MakeBox("deck", Vec3{0.0f, 0.25f, 0.0f}, Vec3{4.0f, 0.5f, 4.0f});
	Handle<Rigidbody> platform = deck->GetOwner()->AddComponent<Rigidbody>();
	platform->SetKinematic(true);
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 0.5f + REST_Y, 0.0f});
	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}

	for (u32 frame = 1; frame <= 30; ++frame) {
		deck->GetOwner()->SetWorldPosition(Vec3{2.0f * FRAME_STEP * static_cast<f32>(frame), 0.25f, 0.0f});
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}

	EXPECT_NEAR(PositionOf(controller).x, 0.0f, 0.01f);
}

TEST_F(CharacterControllerFixture, TurntableTurnsYawOnly) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> deck = MakeBox("turntable", Vec3{0.0f, 0.25f, 0.0f}, Vec3{6.0f, 0.5f, 6.0f});
	Handle<Rigidbody> platform = deck->GetOwner()->AddComponent<Rigidbody>();
	platform->SetKinematic(true);
	platform->SetCarryRiders(true);
	Handle<CharacterController> controller = MakeController("hero", Vec3{1.0f, 0.5f + REST_Y, 0.0f});
	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}

	for (u32 frame = 1; frame <= 60; ++frame) {
		deck->GetOwner()->SetWorldRotation(Quat::AngleAxis(1.5f * static_cast<f32>(frame), Vec3::Up()));
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}

	const Vec3 end = PositionOf(controller);
	EXPECT_NEAR(end.x, 0.0f, 0.02f);
	EXPECT_NEAR(end.z, -1.0f, 0.02f);
	const Quat rotation = controller->GetOwner()->GetWorldRotation();
	EXPECT_NEAR((rotation * Vec3::Up()).y, 1.0f, 1.0e-4f);
	EXPECT_NEAR((rotation * Vec3{1.0f, 0.0f, 0.0f}).z, -1.0f, 0.01f);
}

TEST_F(CharacterControllerFixture, TiltingCarrierKeepsRiderUpright) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> deck = MakeBox("seesaw", Vec3{0.0f, 0.25f, 0.0f}, Vec3{6.0f, 0.5f, 6.0f});
	Handle<Rigidbody> platform = deck->GetOwner()->AddComponent<Rigidbody>();
	platform->SetKinematic(true);
	platform->SetCarryRiders(true);
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 0.5f + REST_Y, 0.0f});
	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}
	const f32 startZ = PositionOf(controller).z;

	for (u32 frame = 1; frame <= 30; ++frame) {
		deck->GetOwner()->SetWorldRotation(Quat::AngleAxis(0.3f * static_cast<f32>(frame), Vec3{1.0f, 0.0f, 0.0f}));
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}

	const Quat rotation = controller->GetOwner()->GetWorldRotation();
	EXPECT_NEAR((rotation * Vec3::Up()).y, 1.0f, 1.0e-5f);
	EXPECT_NEAR((rotation * Vec3::Up()).z, 0.0f, 1.0e-4f);
	EXPECT_NEAR(PositionOf(controller).z, startZ + 1.325f * std::sin(9.0f * Math::DEG_TO_RAD), 0.01f);
	EXPECT_TRUE(controller->IsGrounded());
}

TEST_F(CharacterControllerFixture, TeleportDropsCarry) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<BoxCollider> deck = MakeBox("deck", Vec3{0.0f, 0.25f, 0.0f}, Vec3{4.0f, 0.5f, 4.0f});
	Handle<Rigidbody> platform = deck->GetOwner()->AddComponent<Rigidbody>();
	platform->SetKinematic(true);
	platform->SetCarryRiders(true);
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 0.5f + REST_Y, 0.0f});
	for (u32 frame = 0; frame < 30; ++frame) {
		SimpleFrame(controller, Vec3{});
		RunFixedSteps(1);
	}
	ASSERT_TRUE(controller->IsGrounded());

	controller->GetOwner()->SetWorldPosition(Vec3{10.0f, 5.0f, 0.0f});
	deck->GetOwner()->SetWorldPosition(Vec3{0.5f, 0.25f, 0.0f});
	MoveFrame(controller, Vec3{0.0f, -0.01f, 0.0f});

	EXPECT_NEAR(PositionOf(controller).x, 10.0f, 1.0e-5f);
	EXPECT_NEAR(PositionOf(controller).y, 4.99f, 1.0e-4f);
}

TEST_F(CharacterControllerFixture, ControllersBlockEachOther) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<CharacterController> mover = MakeController("mover", Vec3{0.0f, REST_Y, 0.0f});
	Handle<CharacterController> blocker = MakeController("blocker", Vec3{3.0f, REST_Y, 0.0f});
	Settle(mover, 10);
	Settle(blocker, 10);

	for (u32 frame = 0; frame < 60; ++frame) {
		SimpleFrame(mover, Vec3{3.0f, 0.0f, 0.0f});
	}

	EXPECT_LT(PositionOf(mover).x, 3.0f - 1.0f);
	EXPECT_GT(PositionOf(mover).x, 1.5f);
	EXPECT_NEAR(PositionOf(blocker).x, 3.0f, 1.0e-5f);
}

TEST_F(CharacterControllerFixture, MinMoveDistanceIgnoresSmallMotion) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 5.0f, 0.0f});
	controller->SetMinMoveDistance(0.01f);

	EXPECT_EQ(MoveFrame(controller, Vec3{0.005f, 0.0f, 0.0f}), CollisionFlags::None);
	EXPECT_EQ(PositionOf(controller).x, 0.0f);
	EXPECT_EQ(controller->GetVelocity(), Vec3{});

	MoveFrame(controller, Vec3{0.02f, 0.0f, 0.0f});
	EXPECT_NEAR(PositionOf(controller).x, 0.02f, 1.0e-6f);
}

TEST_F(CharacterControllerFixture, VelocityIsDisplacementOverTime) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 5.0f, 0.0f});

	MoveFrame(controller, Vec3{0.1f, 0.0f, 0.05f});

	EXPECT_NEAR(controller->GetVelocity().x, 6.0f, 1.0e-3f);
	EXPECT_NEAR(controller->GetVelocity().y, 0.0f, 1.0e-3f);
	EXPECT_NEAR(controller->GetVelocity().z, 3.0f, 1.0e-3f);
}

TEST_F(CharacterControllerFixture, ScaleStretchesCapsule) {
	SLT_SKIP_WITHOUT_GEOMETRY();
	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<SceneObject> object = MakeObject("tall", Vec3{0.0f, 4.0f, 0.0f});
	object->SetLocalScale(Vec3{1.0f, 2.0f, 1.0f});
	Handle<CharacterController> controller = object->AddComponent<CharacterController>();
	Settle(controller, 180);

	EXPECT_NEAR(PositionOf(controller).y, 2.0f + SKIN - MOVER_SLOP, 2.0e-3f);
}

TEST_F(CharacterControllerFixture, DisabledControllerLeavesWorld) {
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, 5.0f, 0.0f});
	ASSERT_TRUE(controller.IsValid());
	EXPECT_EQ(GetPhysicsWorld().GetControllerCount(), 1u);
	EXPECT_TRUE(controller->IsInSimulation());

	controller->SetEnabled(false);
	EXPECT_EQ(GetPhysicsWorld().GetControllerCount(), 0u);
	EXPECT_FALSE(controller->IsInSimulation());
	EXPECT_EQ(controller->Move(Vec3{1.0f, 0.0f, 0.0f}), CollisionFlags::None);
	EXPECT_EQ(PositionOf(controller).x, 0.0f);

	controller->SetEnabled(true);
	EXPECT_EQ(GetPhysicsWorld().GetControllerCount(), 1u);
	EXPECT_TRUE(controller->IsInSimulation());
}

TEST_F(CharacterControllerFixture, ModuleListenerHearsHit) {
	SLT_SKIP_WITHOUT_GEOMETRY();
#ifndef SHALLTEAR_SHARED
	if (!TestPlatform::StaticModulesShareTypeInfo()) {
		GTEST_SKIP() << "listener module has its copy of the interface's type info in a static tree";
	}
#endif
	ModuleHandle module = ModuleSystem::Load(Path(LISTENER_MODULE_DLL));
	ASSERT_TRUE(ModuleSystem::IsLoaded(module));
	const TypeInfo* probeType = TypeRegistry::FindType(StringHash("ContactProbe"));
	ASSERT_NE(probeType, nullptr);

	ASSERT_TRUE(MakeFloor().IsValid());
	Handle<CharacterController> controller = MakeController("hero", Vec3{0.0f, REST_Y, 0.0f});
	Handle<Component> probe = controller->GetOwner()->AddComponent(probeType);
	ASSERT_TRUE(probe.IsValid());

	MoveFrame(controller, Vec3{0.0f, -0.05f, 0.0f});

	const FieldInfo* field = nullptr;
	for (u32 index = 0; index < probeType->fieldCount; ++index) {
		if (probeType->fields[index].name == StringHash("controllerHit")) {
			field = &probeType->fields[index];
		}
	}
	ASSERT_NE(field, nullptr);
	EXPECT_EQ(*static_cast<const i32*>(field->addrFn(probe.Get())), 1);
}

#endif
