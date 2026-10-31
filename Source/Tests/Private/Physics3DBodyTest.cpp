#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
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
#include "PhysicsWorld.h"

#include "Shalltear/Physics3D/Rigidbody.h"

#include "Shalltear/Physics3D/Joints.h"
SLT_DECLARE_LINKED_PACKAGE(Physics3D)
#endif

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_TARGET = "Physics3D";
	constexpr const char* SETTINGS_CATEGORY_PATH = "Physics";
	constexpr const char* RIGIDBODY_TYPE_NAME = "Rigidbody";

	constexpr f32 FIXED_STEP = 1.0f / 60.0f;

	constexpr u32 STEPS_TO_SLEEP = 40;

	const FieldInfo* FindBodyField(const TypeInfo* type, std::string_view fieldName) {
		if (!type) {
			return nullptr;
		}
		StringHash wanted(fieldName);
		for (u32 index = 0; index < type->fieldCount; ++index) {
			if (type->fields[index].name == wanted) {
				return &type->fields[index];
			}
		}
		return nullptr;
	}

	template <typename T>
	bool ReadField(const TypeInfo* type, const void* instance, std::string_view fieldName, T& out) {
		const FieldInfo* field = FindBodyField(type, fieldName);
		if (!field || !instance || !field->addrFn) {
			return false;
		}
		out = *static_cast<const T*>(field->addrFn(const_cast<void*>(instance)));
		return true;
	}

	template <typename T>
	bool WriteField(const TypeInfo* type, void* instance, std::string_view fieldName, const T& value) {
		const FieldInfo* field = FindBodyField(type, fieldName);
		if (!field || !instance || !field->addrFn) {
			return false;
		}
		*static_cast<T*>(field->addrFn(instance)) = value;
		return true;
	}

	bool FindSettingsCategory(std::string_view path, SettingsCategoryInfo& out) {
		Vector<SettingsCategoryInfo> categories;
		Settings::GetCategories(categories);
		for (const SettingsCategoryInfo& category : categories) {
			if (category.path == path) {
				out = category;
				return true;
			}
		}
		return false;
	}

	class Physics3DBodyFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		const TypeInfo* _rigidbodyType = nullptr;
#ifdef SHALLTEAR_SHARED
		ModuleHandle _module;
#else
		char _token = 0;
#endif

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_body_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			ASSERT_TRUE(StandUp());

			_rigidbodyType = TypeRegistry::FindType(StringHash(RIGIDBODY_TYPE_NAME));
			ASSERT_NE(_rigidbodyType, nullptr);

			_scene = SceneSystem::CreateScene("physics-body");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
#ifndef SHALLTEAR_SHARED
			FrameStages::UnregisterOwnedBy(&_token);
#endif
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StandUp() {
#ifdef SHALLTEAR_SHARED
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			return ModuleSystem::IsLoaded(_module);
#else
			SLT_CALL_LINKED_PACKAGE(Physics3D, &_token);
			return true;
#endif
		}

		Handle<SceneObject> MakeBody(const Vec3& position = Vec3{}, Handle<Scene> scene = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject("body", scene.IsValid() ? scene : _scene);
			if (!object.IsValid()) {
				return {};
			}
			object->SetWorldPosition(position);
			object->AddComponent(_rigidbodyType);
			return object;
		}

		void* BodyOf(Handle<SceneObject> object) const {
			Handle<Component> component = object->GetComponentAt(0);
			return component.Get();
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

		static void RunUpdate(f32 alpha) {
			FrameTime clockState;
			clockState.fixedDeltaTime = FIXED_STEP;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(static_cast<f64>(FIXED_STEP) * static_cast<f64>(alpha));

			FrameStageArgs args;
			FrameStages::Run(FrameStage::Update, args);
		}
	};

} // anonymous namespace

TEST_F(Physics3DBodyFixture, TheComponentCarriesItsNamesAndDefaults) {
	Handle<SceneObject> object = MakeBody();
	ASSERT_TRUE(object.IsValid());
	void* body = BodyOf(object);
	ASSERT_NE(body, nullptr);

	ASSERT_NE(_rigidbodyType->parent, nullptr);
	EXPECT_EQ(_rigidbodyType->parent->name, StringHash("Component"));

	f32 mass = 0.0f;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_mass", mass));
	EXPECT_FLOAT_EQ(mass, 1.0f);

	f32 angularDamping = 0.0f;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_angularDamping", angularDamping));
	EXPECT_FLOAT_EQ(angularDamping, 0.05f);

	f32 linearDamping = -1.0f;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_linearDamping", linearDamping));
	EXPECT_FLOAT_EQ(linearDamping, 0.0f);

	bool useGravity = false;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_useGravity", useGravity));
	EXPECT_TRUE(useGravity);

	bool isKinematic = true;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_isKinematic", isKinematic));
	EXPECT_FALSE(isKinematic);

	bool automaticCenterOfMass = false;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_automaticCenterOfMass", automaticCenterOfMass));
	EXPECT_TRUE(automaticCenterOfMass);

	f32 sleepThreshold = 0.0f;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_sleepThreshold", sleepThreshold));
	EXPECT_FLOAT_EQ(sleepThreshold, 0.05f);

	for (const char* name : {"_freezePositionX", "_freezePositionY", "_freezePositionZ",
							 "_freezeRotationX", "_freezeRotationY", "_freezeRotationZ"}) {
		bool frozen = true;
		ASSERT_TRUE(ReadField(_rigidbodyType, body, name, frozen)) << name;
		EXPECT_FALSE(frozen) << name;
	}

	const FieldInfo* interpolation = FindBodyField(_rigidbodyType, "_interpolation");
	ASSERT_NE(interpolation, nullptr);
	ASSERT_NE(interpolation->fieldType, nullptr);
	EXPECT_NE(interpolation->fieldType->flags & TypeFlagEnum, 0u);
	EXPECT_EQ(interpolation->fieldType->enumConstantCount, 3u);

	EXPECT_EQ(FindBodyField(_rigidbodyType, "drag"), nullptr);
	EXPECT_EQ(FindBodyField(_rigidbodyType, "angularDrag"), nullptr);
	EXPECT_EQ(FindBodyField(_rigidbodyType, "velocity"), nullptr);

	EXPECT_EQ(FindBodyField(_rigidbodyType, "maxLinearVelocity"), nullptr);
	EXPECT_EQ(FindBodyField(_rigidbodyType, "maxAngularVelocity"), nullptr);
}

TEST_F(Physics3DBodyFixture, CollisionDetectionHasTwoModes) {
	Handle<SceneObject> object = MakeBody();
	void* body = BodyOf(object);
	ASSERT_NE(body, nullptr);

	const FieldInfo* mode = FindBodyField(_rigidbodyType, "_collisionDetectionMode");
	ASSERT_NE(mode, nullptr);
	ASSERT_NE(mode->fieldType, nullptr);
	EXPECT_NE(mode->fieldType->flags & TypeFlagEnum, 0u);
	EXPECT_EQ(mode->fieldType->enumConstantCount, 2u);

	u8 raw = 0xFF;
	ASSERT_TRUE(ReadField(_rigidbodyType, body, "_collisionDetectionMode", raw));
	EXPECT_EQ(raw, 0u);

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory(SETTINGS_CATEGORY_PATH, physics));
	ASSERT_NE(physics.type, nullptr);
	bool enableContinuous = false;
	ASSERT_TRUE(ReadField(physics.type, physics.live, "enableContinuous", enableContinuous));
	EXPECT_TRUE(enableContinuous);
}

TEST_F(Physics3DBodyFixture, BodyWithNoColliderFallsUnderGravity) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	ASSERT_TRUE(object.IsValid());

	RunFixedSteps(30);

	EXPECT_LT(object->GetWorldPosition().y, 10.0f);
	EXPECT_FLOAT_EQ(object->GetWorldPosition().x, 0.0f);
	EXPECT_FLOAT_EQ(object->GetWorldPosition().z, 0.0f);
}

TEST_F(Physics3DBodyFixture, GravityOffHoldsBody) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	ASSERT_TRUE(WriteField(_rigidbodyType, BodyOf(object), "_useGravity", false));

	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);

	RunFixedSteps(30);

	EXPECT_FLOAT_EQ(object->GetWorldPosition().y, 10.0f);
}

TEST_F(Physics3DBodyFixture, KinematicFollowsScene) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	ASSERT_TRUE(WriteField(_rigidbodyType, BodyOf(object), "_isKinematic", true));
	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);

	for (u32 step = 0; step < 20; ++step) {
		const Vec3 wanted{static_cast<f32>(step) * 0.25f, 10.0f, 0.0f};
		object->SetWorldPosition(wanted);
		RunFixedSteps(1);
		EXPECT_FLOAT_EQ(object->GetWorldPosition().x, wanted.x) << "at step " << step;
		EXPECT_FLOAT_EQ(object->GetWorldPosition().y, wanted.y) << "at step " << step;
	}
}

TEST_F(Physics3DBodyFixture, TransformWriteTeleports) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 100.0f, 0.0f});

	RunFixedSteps(30);
	const f32 fallenTo = object->GetWorldPosition().y;
	ASSERT_LT(fallenTo, 100.0f);

	const f32 beforeProbe = object->GetWorldPosition().y;
	RunFixedSteps(1);
	const f32 stepDrop = beforeProbe - object->GetWorldPosition().y;
	ASSERT_GT(stepDrop, 0.0f);

	object->SetWorldPosition(Vec3{5.0f, 500.0f, -3.0f});
	RunFixedSteps(1);

	const Vec3 after = object->GetWorldPosition();
	EXPECT_FLOAT_EQ(after.x, 5.0f);
	EXPECT_FLOAT_EQ(after.z, -3.0f);
	EXPECT_LT(after.y, 500.0f);

	const f32 dropAfterTeleport = 500.0f - after.y;
	EXPECT_NEAR(dropAfterTeleport, stepDrop, stepDrop * 0.25f);
}

TEST_F(Physics3DBodyFixture, FreezingAxisStopsBodyMovingAlongIt) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	ASSERT_TRUE(WriteField(_rigidbodyType, BodyOf(object), "_freezePositionY", true));
	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);

	RunFixedSteps(30);

	EXPECT_FLOAT_EQ(object->GetWorldPosition().y, 10.0f);
}

TEST_F(Physics3DBodyFixture, EditingGravityWhileSimulationRunsReachesIt) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 100.0f, 0.0f});

	RunFixedSteps(10);
	const f32 underDefaultGravity = 100.0f - object->GetWorldPosition().y;
	ASSERT_GT(underDefaultGravity, 0.0f);

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory(SETTINGS_CATEGORY_PATH, physics));
	ASSERT_NE(physics.type, nullptr);
	ASSERT_TRUE(WriteField(physics.type, physics.live, "gravityY", 20.0f));

	const f32 before = object->GetWorldPosition().y;
	RunFixedSteps(30);
	EXPECT_GT(object->GetWorldPosition().y, before);
}

TEST_F(Physics3DBodyFixture, InterpolationBlendsLastSteps) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 100.0f, 0.0f});

	RunFixedSteps(4);
	const Vec3 unsmoothed = object->GetWorldPosition();
	RunUpdate(0.5f);
	EXPECT_FLOAT_EQ(object->GetWorldPosition().y, unsmoothed.y);

	ASSERT_TRUE(WriteField(_rigidbodyType, BodyOf(object), "_interpolation", static_cast<u8>(1)));

	RunFixedSteps(1);
	const f32 previousStep = object->GetWorldPosition().y;
	RunFixedSteps(1);
	const f32 latestStep = object->GetWorldPosition().y;
	ASSERT_LT(latestStep, previousStep);

	RunUpdate(0.5f);
	EXPECT_NEAR(object->GetWorldPosition().y, (previousStep + latestStep) * 0.5f, 1e-4f);

	RunUpdate(0.0f);
	EXPECT_NEAR(object->GetWorldPosition().y, previousStep, 1e-4f);
	RunUpdate(1.0f);
	EXPECT_NEAR(object->GetWorldPosition().y, latestStep, 1e-4f);
}

TEST_F(Physics3DBodyFixture, InterpolatedPoseNotScriptWrite) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 100.0f, 0.0f});
	ASSERT_TRUE(WriteField(_rigidbodyType, BodyOf(object), "_interpolation", static_cast<u8>(1)));

	RunFixedSteps(10);
	const f32 beforeSmoothing = object->GetWorldPosition().y;

	RunUpdate(0.5f);
	const f32 smoothed = object->GetWorldPosition().y;
	ASSERT_GT(smoothed, beforeSmoothing);

	RunFixedSteps(1);
	EXPECT_LT(object->GetWorldPosition().y, beforeSmoothing);
}

TEST_F(Physics3DBodyFixture, RigidbodySceneRoundTrips) {
	Handle<SceneObject> object = MakeBody(Vec3{1.0f, 2.0f, 3.0f});
	void* body = BodyOf(object);
	ASSERT_NE(body, nullptr);

	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_mass", 7.5f));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_linearDamping", 0.25f));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_angularDamping", 0.5f));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_useGravity", false));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_isKinematic", true));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_interpolation", static_cast<u8>(2)));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_sleepThreshold", 0.125f));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_automaticCenterOfMass", false));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_centerOfMass", Vec3{0.5f, -0.25f, 0.75f}));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_freezePositionZ", true));
	ASSERT_TRUE(WriteField(_rigidbodyType, body, "_freezeRotationY", true));

	const Path first((_root / "first.scene").generic_string());
	ASSERT_TRUE(SceneSystem::SaveScene(_scene, first));
	str firstText = FileSystem::ReadTextFile(first);
	ASSERT_FALSE(firstText.empty());
	EXPECT_NE(firstText.find(RIGIDBODY_TYPE_NAME), str::npos);

	Handle<Scene> reloaded = SceneSystem::LoadScene(first);
	ASSERT_TRUE(reloaded.IsValid());

	std::filesystem::create_directories(_root / "again");
	const Path second((_root / "again" / "first.scene").generic_string());
	ASSERT_TRUE(SceneSystem::SaveScene(reloaded, second));
	EXPECT_EQ(FileSystem::ReadTextFile(second), firstText);

	Handle<SceneObject> loadedObject = reloaded->FindObjectByName("body");
	ASSERT_TRUE(loadedObject.IsValid());
	void* loadedBody = loadedObject->GetComponentAt(0).Get();
	ASSERT_NE(loadedBody, nullptr);

	f32 mass = 0.0f;
	ASSERT_TRUE(ReadField(_rigidbodyType, loadedBody, "_mass", mass));
	EXPECT_FLOAT_EQ(mass, 7.5f);
	u8 interpolation = 0;
	ASSERT_TRUE(ReadField(_rigidbodyType, loadedBody, "_interpolation", interpolation));
	EXPECT_EQ(interpolation, 2);
	Vec3 centerOfMass{};
	ASSERT_TRUE(ReadField(_rigidbodyType, loadedBody, "_centerOfMass", centerOfMass));
	EXPECT_FLOAT_EQ(centerOfMass.x, 0.5f);
	bool freezeRotationY = false;
	ASSERT_TRUE(ReadField(_rigidbodyType, loadedBody, "_freezeRotationY", freezeRotationY));
	EXPECT_TRUE(freezeRotationY);
}

#ifndef SHALLTEAR_SHARED

TEST_F(Physics3DBodyFixture, RigidbodyIsOneBody) {
	PhysicsWorld& world = GetPhysicsWorld();
	ASSERT_TRUE(world.IsUp());
	const u32 before = world.GetBodyCount();

	Handle<SceneObject> object = MakeBody();
	EXPECT_EQ(world.GetBodyCount(), before + 1);

	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	EXPECT_EQ(world.GetBodyCount(), before);
	component->SetEnabled(true);
	EXPECT_EQ(world.GetBodyCount(), before + 1);

	object->SetActive(false);
	EXPECT_EQ(world.GetBodyCount(), before);
	object->SetActive(true);
	EXPECT_EQ(world.GetBodyCount(), before + 1);

	SceneSystem::Destroy(object);
	EXPECT_EQ(world.GetBodyCount(), before);
}

TEST_F(Physics3DBodyFixture, RigidbodyInIsolatedSceneIsNeverSimulated) {
	PhysicsWorld& world = GetPhysicsWorld();
	const u32 before = world.GetBodyCount();

	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("preview");
	ASSERT_TRUE(isolated.IsValid());

	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f}, isolated);
	ASSERT_TRUE(object.IsValid());
	EXPECT_EQ(world.GetBodyCount(), before);

	RunFixedSteps(30);
	EXPECT_FLOAT_EQ(object->GetWorldPosition().y, 10.0f);
}

TEST_F(Physics3DBodyFixture, RestingBodySleepsAndWakes) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);

	body->SetUseGravity(false);
	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);
	body = static_cast<Rigidbody*>(BodyOf(object));

	EXPECT_FALSE(body->IsSleeping());

	RunFixedSteps(STEPS_TO_SLEEP);
	EXPECT_TRUE(body->IsSleeping());

	body->WakeUp();
	EXPECT_FALSE(body->IsSleeping());

	body->SetLinearVelocity(Vec3{0.0f, 5.0f, 0.0f});
	RunFixedSteps(10);
	EXPECT_FALSE(body->IsSleeping());
	EXPECT_GT(object->GetWorldPosition().y, 10.0f);
}

TEST_F(Physics3DBodyFixture, RotatedParentBodySettles) {
	Handle<SceneObject> parent = SceneSystem::CreateObject("carrier", _scene);
	ASSERT_TRUE(parent.IsValid());
	parent->SetWorldPosition(Vec3{3.5f, -7.25f, 11.125f});
	parent->SetWorldRotation(Quat::FromEuler(23.0f, 47.0f, 71.0f));
	parent->SetLocalScale(Vec3{0.3f, 1.7f, 2.9f});

	Handle<SceneObject> object = SceneSystem::CreateObject("body", _scene);
	ASSERT_TRUE(object.IsValid());
	object->SetParent(parent);
	object->SetWorldPosition(Vec3{1.0f, 2.0f, 3.0f});
	object->AddComponent(_rigidbodyType);

	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	body->SetUseGravity(false);
	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);
	body = static_cast<Rigidbody*>(BodyOf(object));

	body->SetAngularVelocity(Vec3{0.7f, -1.3f, 0.4f});
	RunFixedSteps(12);
	ASSERT_FALSE(body->IsSleeping());
	body->SetAngularVelocity(Vec3{});

	RunFixedSteps(STEPS_TO_SLEEP);

	EXPECT_TRUE(body->IsSleeping());
}

TEST_F(Physics3DBodyFixture, EarlyVelocityApplied) {
	Handle<SceneObject> object = SceneSystem::CreateObject("pending", _scene);
	ASSERT_TRUE(object.IsValid());
	object->SetActive(false);
	object->SetWorldPosition(Vec3{0.0f, 10.0f, 0.0f});
	object->AddComponent(_rigidbodyType);

	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	body->SetUseGravity(false);
	body->SetLinearVelocity(Vec3{1.0f, 0.0f, 0.0f});
	body->SetAngularVelocity(Vec3{0.0f, 3.0f, 0.0f});
	EXPECT_FLOAT_EQ(body->GetLinearVelocity().x, 1.0f);
	EXPECT_FLOAT_EQ(body->GetAngularVelocity().y, 3.0f);

	object->SetActive(true);
	RunFixedSteps(10);

	EXPECT_GT(object->GetWorldPosition().x, 0.0f);
	EXPECT_GT(Math::Abs(object->GetWorldRotation().y), 0.0f);
}

TEST_F(Physics3DBodyFixture, SpinTurnsColliderlessBody) {
	Handle<SceneObject> object = MakeBody();
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	body->SetUseGravity(false);
	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);
	body = static_cast<Rigidbody*>(BodyOf(object));

	EXPECT_FLOAT_EQ(object->GetWorldRotation().y, 0.0f);

	body->SetAngularVelocity(Vec3{0.0f, 2.0f, 0.0f});
	EXPECT_GT(body->GetAngularVelocity().y, 0.0f);

	RunFixedSteps(10);

	EXPECT_GT(Math::Abs(object->GetWorldRotation().y), 0.0f);
}

TEST_F(Physics3DBodyFixture, ExtrapolationProjectsSpinAndNotOnlyPosition) {
	Handle<SceneObject> object = MakeBody();
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	body->SetUseGravity(false);
	body->SetInterpolation(RigidbodyInterpolation::Extrapolate);
	Handle<Component> component = object->GetComponentAt(0);
	component->SetEnabled(false);
	component->SetEnabled(true);
	body = static_cast<Rigidbody*>(BodyOf(object));

	body->SetAngularVelocity(Vec3{0.0f, 2.0f, 0.0f});
	RunFixedSteps(4);
	const Quat settled = object->GetWorldRotation();

	RunUpdate(0.0f);
	EXPECT_NEAR(object->GetWorldRotation().y, settled.y, 1e-6f);

	RunUpdate(1.0f);
	EXPECT_GT(Math::Abs(object->GetWorldRotation().y), Math::Abs(settled.y));
}

TEST_F(Physics3DBodyFixture, LiveBodyFieldsReachSimulation) {
	using Property = PhysicsBackend::BodyProperty;

	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 4.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	PhysicsWorld& world = GetPhysicsWorld();
	ASSERT_TRUE(world.IsUp());

	body->SetLinearDamping(0.25f);
	EXPECT_FLOAT_EQ(world.ReadBodyProperty(*body, Property::LinearDamping).AsScalar(), 0.25f);
	body->SetAngularDamping(0.75f);
	EXPECT_FLOAT_EQ(world.ReadBodyProperty(*body, Property::AngularDamping).AsScalar(), 0.75f);
	body->SetSleepThreshold(0.5f);
	EXPECT_FLOAT_EQ(world.ReadBodyProperty(*body, Property::SleepThreshold).AsScalar(), 0.5f);

	body->SetUseGravity(false);
	EXPECT_FLOAT_EQ(world.ReadBodyProperty(*body, Property::GravityScale).AsScalar(), 0.0f);
	body->SetUseGravity(true);
	EXPECT_FLOAT_EQ(world.ReadBodyProperty(*body, Property::GravityScale).AsScalar(), 1.0f);
	body->SetCollisionDetectionMode(RigidbodyCollisionDetection::ContinuousDynamic);
	EXPECT_TRUE(world.ReadBodyProperty(*body, Property::IsBullet).AsFlag());
	body->SetCollisionDetectionMode(RigidbodyCollisionDetection::Continuous);
	EXPECT_FALSE(world.ReadBodyProperty(*body, Property::IsBullet).AsFlag());

	body->SetFreezePositionX(true);
	body->SetFreezeRotationZ(true);
	EXPECT_TRUE(world.ReadBodyProperty(*body, Property::FreezePositionX).AsFlag());
	EXPECT_TRUE(world.ReadBodyProperty(*body, Property::FreezeRotationZ).AsFlag());
	EXPECT_FALSE(world.ReadBodyProperty(*body, Property::FreezePositionY).AsFlag());
	EXPECT_FALSE(world.ReadBodyProperty(*body, Property::FreezeRotationX).AsFlag());
	body->SetFreezePositionX(false);
	EXPECT_FALSE(world.ReadBodyProperty(*body, Property::FreezePositionX).AsFlag());
	EXPECT_TRUE(world.ReadBodyProperty(*body, Property::FreezeRotationZ).AsFlag());

	body->SetKinematic(true);
	EXPECT_EQ(world.ReadBodyKind(*body), PhysicsBackend::BodyKind::Kinematic);
	body->SetKinematic(false);
	EXPECT_EQ(world.ReadBodyKind(*body), PhysicsBackend::BodyKind::Dynamic);

	body->SetMass(4.0f);
	EXPECT_NEAR(world.ReadBodyInverseMass(*body), 0.25f, 1e-5f);
}

TEST_F(Physics3DBodyFixture, KindChangeDoesNotDiscardAuthoredMass) {
	const TypeInfo* boxType = TypeRegistry::FindType(StringHash("BoxCollider"));
	ASSERT_NE(boxType, nullptr);

	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 4.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	ASSERT_NE(object->AddComponent(boxType).Get(), nullptr);

	PhysicsWorld& world = GetPhysicsWorld();
	body->SetMass(70.0f);
	ASSERT_NEAR(world.ReadBodyInverseMass(*body), 1.0f / 70.0f, 1e-6f);

	body->SetKinematic(true);
	body->SetKinematic(false);

	EXPECT_EQ(world.ReadBodyKind(*body), PhysicsBackend::BodyKind::Dynamic);
	EXPECT_NEAR(world.ReadBodyInverseMass(*body), 1.0f / 70.0f, 1e-6f);
}

TEST_F(Physics3DBodyFixture, KinematicToDynamicFalls) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	body->SetKinematic(true);

	RunFixedSteps(30);
	ASSERT_FLOAT_EQ(object->GetWorldPosition().y, 10.0f);

	body->SetKinematic(false);
	RunFixedSteps(30);

	EXPECT_LT(object->GetWorldPosition().y, 9.9f);
}

TEST_F(Physics3DBodyFixture, GravityWakesSleepingBody) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);

	body->SetUseGravity(false);
	RunFixedSteps(STEPS_TO_SLEEP);
	ASSERT_TRUE(body->IsSleeping());
	const f32 settled = object->GetWorldPosition().y;

	body->SetUseGravity(true);
	EXPECT_FALSE(body->IsSleeping());
	RunFixedSteps(20);
	EXPECT_LT(object->GetWorldPosition().y, settled - 0.01f);
}

TEST_F(Physics3DBodyFixture, InertWriteKeepsBodyAsleep) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);

	body->SetUseGravity(false);
	RunFixedSteps(STEPS_TO_SLEEP);
	ASSERT_TRUE(body->IsSleeping());

	body->SetLinearDamping(0.5f);
	body->SetSleepThreshold(0.2f);
	body->SetCollisionDetectionMode(RigidbodyCollisionDetection::ContinuousDynamic);
	EXPECT_TRUE(body->IsSleeping());

	EXPECT_FLOAT_EQ(GetPhysicsWorld().ReadBodyProperty(*body, PhysicsBackend::BodyProperty::LinearDamping).AsScalar(), 0.5f);
}

TEST_F(Physics3DBodyFixture, OfflineBodySetterWritesMember) {
	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 10.0f, 0.0f});
	Handle<Component> component = object->GetComponentAt(0);
	ASSERT_TRUE(component.IsValid());
	component->SetEnabled(false);

	Rigidbody* body = static_cast<Rigidbody*>(component.Get());
	ASSERT_NE(body, nullptr);
	ASSERT_EQ(GetPhysicsWorld().ReadBodyInverseMass(*body), 0.0f);

	body->SetMass(50.0f);
	body->SetKinematic(true);
	body->SetUseGravity(false);
	body->SetFreezePositionY(true);
	EXPECT_FLOAT_EQ(body->GetMass(), 50.0f);
	EXPECT_TRUE(body->IsKinematic());
	EXPECT_FALSE(body->GetUseGravity());
	EXPECT_TRUE(body->GetFreezePositionY());

	component->SetEnabled(true);
	body = static_cast<Rigidbody*>(component.Get());
	ASSERT_NE(body, nullptr);
	PhysicsWorld& world = GetPhysicsWorld();
	EXPECT_EQ(world.ReadBodyKind(*body), PhysicsBackend::BodyKind::Kinematic);
	EXPECT_FLOAT_EQ(world.ReadBodyProperty(*body, PhysicsBackend::BodyProperty::GravityScale).AsScalar(), 0.0f);
	EXPECT_TRUE(world.ReadBodyProperty(*body, PhysicsBackend::BodyProperty::FreezePositionY).AsFlag());
}

TEST_F(Physics3DBodyFixture, MassWriteRebuildsJoints) {
	const TypeInfo* boxType = TypeRegistry::FindType(StringHash("BoxCollider"));
	const TypeInfo* hingeType = TypeRegistry::FindType(StringHash("HingeJoint"));
	ASSERT_NE(boxType, nullptr);
	ASSERT_NE(hingeType, nullptr);

	Handle<SceneObject> object = MakeBody(Vec3{0.0f, 4.0f, 0.0f});
	Rigidbody* body = static_cast<Rigidbody*>(BodyOf(object));
	ASSERT_NE(body, nullptr);
	ASSERT_NE(object->AddComponent(boxType).Get(), nullptr);
	Handle<Component> jointComponent = object->AddComponent(hingeType);
	auto* hinge = static_cast<HingeJoint*>(jointComponent.Get());
	ASSERT_NE(hinge, nullptr);
	hinge->SetUseSpring(true);
	hinge->SetSpring(500.0f);

	PhysicsWorld& world = GetPhysicsWorld();
	const f32 before = world.ReadJointProperty(*hinge, PhysicsBackend::JointProperty::SpringHertz).AsScalar();
	ASSERT_GT(before, 0.0f);

	body->SetMass(100.0f);
	const f32 after = world.ReadJointProperty(*hinge, PhysicsBackend::JointProperty::SpringHertz).AsScalar();
	EXPECT_NE(after, before);
}

#endif
