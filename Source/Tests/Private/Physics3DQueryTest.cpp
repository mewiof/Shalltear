#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include <atomic>
#include <chrono>
#include <filesystem>

#ifndef SHALLTEAR_SHARED

#include "PhysicsSettings.h"
#include "PhysicsWorld.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Physics.h"
#include "Shalltear/Physics3D/Rigidbody.h"
SLT_DECLARE_LINKED_PACKAGE(Physics3D)

using namespace Shalltear;

namespace {

	constexpr f32 FIXED_STEP = 1.0f / 60.0f;

	constexpr f32 NEAR_TOLERANCE = 1.0e-3f;

	class Physics3DQueryFixture : public ::testing::Test {
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
					("shalltear_physics3d_query_" + std::to_string(stamp) + "_" + std::to_string(unique));
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

			_scene = SceneSystem::CreateScene("physics-query");
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

		Handle<SceneObject> MakeObject(const char* name, const Vec3& position, Handle<SceneObject> parent = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			if (!object.IsValid()) {
				return {};
			}
			if (parent.IsValid()) {
				object->SetParent(parent);
			}
			object->SetWorldPosition(position);
			return object;
		}

		Handle<SphereCollider> MakeSphere(const char* name, const Vec3& position, f32 radius = 1.0f) {
			Handle<SceneObject> object = MakeObject(name, position);
			if (!object.IsValid()) {
				return {};
			}
			Handle<SphereCollider> collider = object->AddComponent<SphereCollider>();
			if (collider.IsValid()) {
				collider->SetRadius(radius);
				collider->SetEnabled(false);
				collider->SetEnabled(true);
			}
			return collider;
		}

		PhysicsSettings* MutableSettings() {
			return const_cast<PhysicsSettings*>(GetPhysicsWorld().GetSettings());
		}

		static void RunFixedSteps(u32 count) {
			FrameTime clockState;
			clockState.fixedDeltaTime = FIXED_STEP;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(FIXED_STEP);

			FrameStageArgs args;
			for (u32 index = 0; index < count; ++index) {
				FrameStages::Run(FrameStage::FixedUpdate, args);
			}
		}
	};

} // anonymous namespace

TEST_F(Physics3DQueryFixture, HitNamesOwners) {
	Handle<SceneObject> body = MakeObject("body", Vec3{0.0f, 0.0f, 10.0f});
	Handle<Rigidbody> rigidbody = body->AddComponent<Rigidbody>();
	ASSERT_TRUE(rigidbody.IsValid());
	rigidbody->SetKinematic(true);

	Handle<SceneObject> child = MakeObject("shape", Vec3{0.0f, 0.0f, 10.0f}, body);
	Handle<SphereCollider> collider = child->AddComponent<SphereCollider>();
	ASSERT_TRUE(collider.IsValid());

	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
	EXPECT_EQ(hit.collider.GetInstanceId(), collider.GetInstanceId());
	EXPECT_EQ(hit.object.GetInstanceId(), child.GetInstanceId());
	EXPECT_EQ(hit.rigidbody.GetInstanceId(), rigidbody.GetInstanceId());
}

TEST_F(Physics3DQueryFixture, StaticHitHasNoRigidbody) {
	Handle<SphereCollider> collider = MakeSphere("ground", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(collider.IsValid());

	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
	EXPECT_EQ(hit.collider.GetInstanceId(), collider.GetInstanceId());
	EXPECT_FALSE(hit.rigidbody.IsValid());
}

TEST_F(Physics3DQueryFixture, ARaysDistanceAndItsPointAgree) {
	ASSERT_TRUE(MakeSphere("target", Vec3{0.0f, 0.0f, 5.0f}).IsValid());

	const Ray ray{Vec3{}, Vec3::Forward()};
	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(ray, hit));

	EXPECT_NEAR(hit.distance, 4.0f, NEAR_TOLERANCE);
	const Vec3 reconstructed = ray.GetPoint(hit.distance);
	EXPECT_NEAR((reconstructed - hit.point).Length(), 0.0f, NEAR_TOLERANCE);
	EXPECT_FLOAT_EQ(Physics::MAX_QUERY_DISTANCE, 1.0e6f);
}

TEST_F(Physics3DQueryFixture, RayReportsNearest) {
	Handle<SphereCollider> near = MakeSphere("near", Vec3{0.0f, 0.0f, 5.0f});
	Handle<SphereCollider> far = MakeSphere("far", Vec3{0.0f, 0.0f, 20.0f});
	ASSERT_TRUE(near.IsValid() && far.IsValid());

	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
	EXPECT_EQ(hit.collider.GetInstanceId(), near.GetInstanceId());

	near->SetEnabled(false);
	ASSERT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
	EXPECT_EQ(hit.collider.GetInstanceId(), far.GetInstanceId());
}

TEST_F(Physics3DQueryFixture, InsideRayMissesCollider) {
	Handle<SphereCollider> around = MakeSphere("around", Vec3{}, 2.0f);
	ASSERT_TRUE(around.IsValid());

	RaycastHit hit;
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));

	Handle<SphereCollider> beyond = MakeSphere("beyond", Vec3{0.0f, 0.0f, 10.0f});
	ASSERT_TRUE(beyond.IsValid());
	ASSERT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
	EXPECT_EQ(hit.collider.GetInstanceId(), beyond.GetInstanceId());
}

TEST_F(Physics3DQueryFixture, MaxDistanceBoundsTheQuery) {
	ASSERT_TRUE(MakeSphere("target", Vec3{0.0f, 0.0f, 50.0f}).IsValid());

	RaycastHit hit;
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, 10.0f));
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, 100.0f));
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, 0.0f));
}

TEST_F(Physics3DQueryFixture, QueryAgainstEmptyWorldMissesNotFailing) {
	RaycastHit hit;
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));

	Vector<Handle<Collider>> results;
	EXPECT_EQ(Physics::OverlapSphere(Vec3{}, 100.0f, results), 0u);
	EXPECT_TRUE(results.empty());
}

TEST_F(Physics3DQueryFixture, ZeroRadiusQueryIsRefusedNotUndefined) {
	ASSERT_TRUE(MakeSphere("target", Vec3{0.0f, 0.0f, 5.0f}).IsValid());

	RaycastHit hit;
	EXPECT_FALSE(Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.0f, hit));
	EXPECT_FALSE(Physics::CapsuleCast(Vec3{}, Vec3{0.0f, 1.0f, 0.0f}, 0.0f, Vec3::Forward(), hit));

	Vector<Handle<Collider>> results;
	EXPECT_EQ(Physics::OverlapSphere(Vec3{0.0f, 0.0f, 5.0f}, 0.0f, results), 0u);

	EXPECT_TRUE(Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.25f, hit));
}

TEST_F(Physics3DQueryFixture, SphereCastWiderThanRay) {
	ASSERT_TRUE(MakeSphere("target", Vec3{1.4f, 0.0f, 6.0f}).IsValid());

	RaycastHit hit;
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
	EXPECT_TRUE(Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.5f, hit));
}

TEST_F(Physics3DQueryFixture, CapsuleCastSweepsBothOfPoints) {
	ASSERT_TRUE(MakeSphere("target", Vec3{0.0f, 4.0f, 6.0f}).IsValid());

	RaycastHit hit;
	EXPECT_FALSE(Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.5f, hit));
	EXPECT_TRUE(Physics::CapsuleCast(Vec3{}, Vec3{0.0f, 4.0f, 0.0f}, 0.5f, Vec3::Forward(), hit));
}

TEST_F(Physics3DQueryFixture, OverlapSphereReportsTouches) {
	ASSERT_TRUE(MakeSphere("a", Vec3{0.0f, 0.0f, 0.0f}, 0.5f).IsValid());
	ASSERT_TRUE(MakeSphere("b", Vec3{2.0f, 0.0f, 0.0f}, 0.5f).IsValid());
	ASSERT_TRUE(MakeSphere("far", Vec3{50.0f, 0.0f, 0.0f}, 0.5f).IsValid());

	Vector<Handle<Collider>> results;
	EXPECT_EQ(Physics::OverlapSphere(Vec3{1.0f, 0.0f, 0.0f}, 1.0f, results), 2u);
	EXPECT_EQ(results.size(), 2u);

	EXPECT_EQ(Physics::OverlapSphere(Vec3{0.2f, 0.0f, 0.0f}, 0.3f, results), 1u);
	EXPECT_EQ(results.size(), 1u);
}

TEST_F(Physics3DQueryFixture, FilteredQueryMissesWhatUnfilteredOneHits) {
	MutableSettings()->categories = "Default, Ground, Enemy";

	Handle<SphereCollider> enemy = MakeSphere("enemy", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(enemy.IsValid());
	enemy->SetCategories("Enemy");
	enemy->SetEnabled(false);
	enemy->SetEnabled(true);

	RaycastHit hit;
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));

	PhysicsQueryFilter onlyGround;
	onlyGround.collidesWithMode = PhysicsCategoryMatch::Only;
	onlyGround.collidesWith = "Ground";
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, onlyGround));

	PhysicsQueryFilter onlyEnemy;
	onlyEnemy.collidesWithMode = PhysicsCategoryMatch::Only;
	onlyEnemy.collidesWith = "Enemy";
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, onlyEnemy));
}

TEST_F(Physics3DQueryFixture, ColliderRefusesExcludedQuery) {
	MutableSettings()->categories = "Default, Bullets";

	Handle<SphereCollider> shield = MakeSphere("shield", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(shield.IsValid());
	shield->SetCollidesWithMode(PhysicsCategoryMatch::AllExcept);
	shield->SetCollidesWith("Bullets");
	shield->SetEnabled(false);
	shield->SetEnabled(true);

	RaycastHit hit;
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));

	PhysicsQueryFilter bullet;
	bullet.categories = "Bullets";
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, bullet));
}

TEST_F(Physics3DQueryFixture, UnnamedQueryIsAllCategories) {
	MutableSettings()->categories = "Default, Ground";

	Handle<SphereCollider> nameless = MakeSphere("nameless", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(nameless.IsValid());
	nameless->SetCategories("");
	nameless->SetEnabled(false);
	nameless->SetEnabled(true);

	RaycastHit hit;
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));

	nameless->SetCategories("Ground");
	nameless->SetEnabled(false);
	nameless->SetEnabled(true);
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
}

TEST_F(Physics3DQueryFixture, QueryUsesEditedVocabulary) {
	MutableSettings()->categories = "Default, Ground";

	Handle<SphereCollider> collider = MakeSphere("ground", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(collider.IsValid());
	collider->SetCategories("Ground");
	collider->SetEnabled(false);
	collider->SetEnabled(true);

	RaycastHit hit;
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));

	MutableSettings()->categories = "Default, Lava";

	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit));
}

TEST_F(Physics3DQueryFixture, TriggerHitRules) {
	Handle<SphereCollider> trigger = MakeSphere("trigger", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(trigger.IsValid());
	trigger->SetTrigger(true);
	trigger->SetEnabled(false);
	trigger->SetEnabled(true);

	RaycastHit hit;
	PhysicsQueryFilter useGlobal;
	PhysicsQueryFilter always;
	always.triggerInteraction = PhysicsQueryTriggers::Collide;
	PhysicsQueryFilter never;
	never.triggerInteraction = PhysicsQueryTriggers::Ignore;

	ASSERT_TRUE(MutableSettings()->queriesHitTriggers);
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, useGlobal));
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, never));

	MutableSettings()->queriesHitTriggers = false;
	EXPECT_FALSE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, useGlobal));
	EXPECT_TRUE(Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit, Physics::MAX_QUERY_DISTANCE, always));
}

TEST_F(Physics3DQueryFixture, TriggerRuleReachesOverlapAndCastsToo) {
	Handle<SphereCollider> trigger = MakeSphere("trigger", Vec3{0.0f, 0.0f, 5.0f});
	ASSERT_TRUE(trigger.IsValid());
	trigger->SetTrigger(true);
	trigger->SetEnabled(false);
	trigger->SetEnabled(true);

	MutableSettings()->queriesHitTriggers = false;

	RaycastHit hit;
	EXPECT_FALSE(Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.25f, hit));
	EXPECT_FALSE(Physics::CapsuleCast(Vec3{}, Vec3{0.0f, 0.5f, 0.0f}, 0.25f, Vec3::Forward(), hit));

	Vector<Handle<Collider>> results;
	EXPECT_EQ(Physics::OverlapSphere(Vec3{0.0f, 0.0f, 5.0f}, 0.5f, results), 0u);

	MutableSettings()->queriesHitTriggers = true;
	EXPECT_TRUE(Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.25f, hit));
	EXPECT_EQ(Physics::OverlapSphere(Vec3{0.0f, 0.0f, 5.0f}, 0.5f, results), 1u);
}

TEST_F(Physics3DQueryFixture, AQueryAdvancesNothing) {
	Handle<SceneObject> object = MakeObject("falling", Vec3{0.0f, 0.0f, 5.0f});
	Handle<Rigidbody> body = object->AddComponent<Rigidbody>();
	ASSERT_TRUE(body.IsValid());
	ASSERT_TRUE(object->AddComponent<SphereCollider>().IsValid());

	RunFixedSteps(4);
	const Vec3 settled = object->GetWorldPosition();
	const u32 bodies = GetPhysicsWorld().GetBodyCount();
	const u32 colliders = GetPhysicsWorld().GetColliderCount();

	RaycastHit hit;
	Vector<Handle<Collider>> results;
	for (u32 index = 0; index < 32; ++index) {
		Physics::Raycast(Ray{Vec3{}, Vec3::Forward()}, hit);
		Physics::SphereCast(Ray{Vec3{}, Vec3::Forward()}, 0.5f, hit);
		Physics::OverlapSphere(settled, 2.0f, results);
	}

	const Vec3 after = object->GetWorldPosition();
	EXPECT_FLOAT_EQ(after.x, settled.x);
	EXPECT_FLOAT_EQ(after.y, settled.y);
	EXPECT_FLOAT_EQ(after.z, settled.z);
	EXPECT_EQ(GetPhysicsWorld().GetBodyCount(), bodies);
	EXPECT_EQ(GetPhysicsWorld().GetColliderCount(), colliders);
}

#endif
