#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
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
#include <mutex>
#include <vector>

#ifndef SHALLTEAR_SHARED
#include "PhysicsWorld.h"

#include "PhysicsCategories.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/PhysicsMaterial.h"
#include "Shalltear/Physics3D/Rigidbody.h"
SLT_DECLARE_LINKED_PACKAGE(Physics3D)
#endif

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_TARGET = "Physics3D";
	constexpr f32 FIXED_STEP = 1.0f / 60.0f;

	const FieldInfo* FindTypeField(const TypeInfo* type, std::string_view fieldName) {
		const StringHash wanted(fieldName);
		for (const TypeInfo* level = type; level; level = level->parent) {
			for (u32 index = 0; index < level->fieldCount; ++index) {
				if (level->fields[index].name == wanted) {
					return &level->fields[index];
				}
			}
		}
		return nullptr;
	}

	template <typename T>
	bool WriteField(const TypeInfo* type, void* instance, std::string_view fieldName, const T& value) {
		const FieldInfo* field = FindTypeField(type, fieldName);
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

	template <typename T>
	bool ReadField(const TypeInfo* type, const void* instance, std::string_view fieldName, T& out) {
		const FieldInfo* field = FindTypeField(type, fieldName);
		if (!field || !instance || !field->addrFn) {
			return false;
		}
		out = *static_cast<const T*>(field->addrFn(const_cast<void*>(instance)));
		return true;
	}

	class PhysicsCapturingSink : public ILogSink {
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

	class Physics3DColliderFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		const TypeInfo* _rigidbodyType = nullptr;
		const TypeInfo* _boxType = nullptr;
		const TypeInfo* _sphereType = nullptr;
		const TypeInfo* _capsuleType = nullptr;
		const TypeInfo* _meshType = nullptr;
#ifdef SHALLTEAR_SHARED
		ModuleHandle _module;
#else
		char _token = 0;
#endif

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 unique = counter.fetch_add(1);
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_collider_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			ASSERT_TRUE(StandUp());

			_rigidbodyType = TypeRegistry::FindType(StringHash("Rigidbody"));
			_boxType = TypeRegistry::FindType(StringHash("BoxCollider"));
			_sphereType = TypeRegistry::FindType(StringHash("SphereCollider"));
			_capsuleType = TypeRegistry::FindType(StringHash("CapsuleCollider"));
			_meshType = TypeRegistry::FindType(StringHash("MeshCollider"));
			ASSERT_NE(_rigidbodyType, nullptr);
			ASSERT_NE(_boxType, nullptr);
			ASSERT_NE(_sphereType, nullptr);
			ASSERT_NE(_capsuleType, nullptr);
			ASSERT_NE(_meshType, nullptr);

			_scene = SceneSystem::CreateScene("physics-collider");
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

TEST_F(Physics3DColliderFixture, TheCollidersCarryTheirNamesAndDefaults) {
	Handle<SceneObject> object = MakeObject("shapes", Vec3{});
	ASSERT_TRUE(object.IsValid());

	for (const TypeInfo* type : {_boxType, _sphereType, _capsuleType, _meshType}) {
		ASSERT_NE(type->parent, nullptr);
		EXPECT_EQ(type->parent->name, StringHash("Collider"));
	}

	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(box.IsValid());
	Vec3 size{};
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_size", size));
	EXPECT_FLOAT_EQ(size.x, 1.0f);
	EXPECT_FLOAT_EQ(size.y, 1.0f);
	EXPECT_FLOAT_EQ(size.z, 1.0f);
	Vec3 center{1.0f, 1.0f, 1.0f};
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_center", center));
	EXPECT_FLOAT_EQ(center.x, 0.0f);
	bool isTrigger = true;
	ASSERT_TRUE(ReadField(_boxType, box.Get(), "_isTrigger", isTrigger));
	EXPECT_FALSE(isTrigger);

	Handle<Component> sphere = object->AddComponent(_sphereType);
	f32 radius = 0.0f;
	ASSERT_TRUE(ReadField(_sphereType, sphere.Get(), "_radius", radius));
	EXPECT_FLOAT_EQ(radius, 0.5f);
	EXPECT_EQ(FindTypeField(_sphereType, "_size"), nullptr);

	Handle<Component> capsule = object->AddComponent(_capsuleType);
	f32 capsuleRadius = 0.0f;
	f32 height = 0.0f;
	ASSERT_TRUE(ReadField(_capsuleType, capsule.Get(), "_radius", capsuleRadius));
	ASSERT_TRUE(ReadField(_capsuleType, capsule.Get(), "_height", height));
	EXPECT_FLOAT_EQ(capsuleRadius, 0.5f);
	EXPECT_FLOAT_EQ(height, 2.0f);
	const FieldInfo* direction = FindTypeField(_capsuleType, "_direction");
	ASSERT_NE(direction, nullptr);
	ASSERT_NE(direction->fieldType, nullptr);
	EXPECT_NE(direction->fieldType->flags & TypeFlagEnum, 0u);
	EXPECT_EQ(direction->fieldType->enumConstantCount, 3u);

	Handle<Component> mesh = object->AddComponent(_meshType);
	bool convex = true;
	ASSERT_TRUE(ReadField(_meshType, mesh.Get(), "_convex", convex));
	EXPECT_FALSE(convex);
	EXPECT_EQ(FindTypeField(_meshType, "_center"), nullptr);
}

TEST_F(Physics3DColliderFixture, PhysicsMaterialIsCreatableDataAsset) {
	const TypeInfo* material = TypeRegistry::FindType(StringHash("PhysicsMaterial"));
	ASSERT_NE(material, nullptr);
	ASSERT_NE(material->parent, nullptr);
	EXPECT_EQ(material->parent->name, StringHash("DataAsset"));

	EXPECT_NE(FindTypeField(material, "friction"), nullptr);
	EXPECT_NE(FindTypeField(material, "bounciness"), nullptr);
	EXPECT_NE(FindTypeField(material, "rollingResistance"), nullptr);
	EXPECT_EQ(FindTypeField(material, "dynamicFriction"), nullptr);
	EXPECT_EQ(FindTypeField(material, "staticFriction"), nullptr);

	const FieldInfo* frictionCombine = FindTypeField(material, "frictionCombine");
	ASSERT_NE(frictionCombine, nullptr);
	ASSERT_NE(frictionCombine->fieldType, nullptr);
	EXPECT_NE(frictionCombine->fieldType->flags & TypeFlagEnum, 0u);
	EXPECT_EQ(frictionCombine->fieldType->enumConstantCount, 4u);

	IAssetImporter* importer = AssetImporterRegistry::FindByExtension(".physmat");
	ASSERT_NE(importer, nullptr);
	EXPECT_EQ(importer->GetAssetType(), StringHash("PhysicsMaterial"));
	EXPECT_TRUE(importer->CanCreateAsset());
	EXPECT_EQ(importer->GetCreateMenuLabel(), "Physics Material");

	for (const TypeInfo* type : {_boxType, _sphereType, _capsuleType, _meshType}) {
		const FieldInfo* field = FindTypeField(type, "_material");
		ASSERT_NE(field, nullptr);
		EXPECT_NE(field->flags & FieldFlagHandle, 0u);
	}
}

TEST_F(Physics3DColliderFixture, BodyLandsOnStaticCollider) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	ASSERT_TRUE(floor.IsValid());
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(floorBox.IsValid());
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	Handle<SceneObject> faller = MakeObject("faller", Vec3{0.0f, 6.0f, 0.0f});
	faller->AddComponent(_boxType);
	faller->AddComponent(_rigidbodyType);

	RunFixedSteps(180);

	const f32 restingY = faller->GetWorldPosition().y;

	EXPECT_GT(restingY, 0.0f);
	EXPECT_LT(restingY, 6.0f);
}

TEST_F(Physics3DColliderFixture, DisabledFloorLetsBodyThrough) {
	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	floorBox->SetEnabled(false);

	Handle<SceneObject> faller = MakeObject("faller", Vec3{0.0f, 6.0f, 0.0f});
	faller->AddComponent(_boxType);
	faller->AddComponent(_rigidbodyType);

	RunFixedSteps(180);

	EXPECT_LT(faller->GetWorldPosition().y, -5.0f);
}

#ifndef SHALLTEAR_SHARED

TEST_F(Physics3DColliderFixture, MirroredMeshFloorHoldsBody) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	MeshAsset mesh;
	const Vec3 corners[4] = {{-5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, -5.0f}, {5.0f, 0.0f, 5.0f}, {-5.0f, 0.0f, 5.0f}};
	for (const Vec3& corner : corners) {
		MeshVertex vertex;
		vertex.position = corner;
		mesh.vertices.push_back(vertex);
	}
	for (u32 index : {0u, 2u, 1u, 0u, 3u, 2u}) {
		mesh.indices.push_back(index);
	}

	const Vec3 scales[4] = {{1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}};
	Handle<SceneObject> floors[4];
	Handle<SceneObject> fallers[4];
	for (u32 index = 0; index < 4; ++index) {
		const f32 x = 20.0f * static_cast<f32>(index);
		floors[index] = MakeObject("floor", Vec3{x, 0.0f, 0.0f});
		floors[index]->SetLocalScale(scales[index]);
		Handle<Component> soup = floors[index]->AddComponent(_meshType);
		static_cast<MeshCollider*>(soup.Get())->SetMesh(Handle<MeshAsset>(&mesh));
		soup->SetEnabled(false);
		soup->SetEnabled(true);
		ASSERT_TRUE(static_cast<Collider*>(soup.Get())->IsInSimulation());

		fallers[index] = MakeObject("faller", Vec3{x + 0.3f, 2.0f, 0.2f});
		fallers[index]->AddComponent(_boxType);
		fallers[index]->AddComponent(_rigidbodyType);
	}

	RunFixedSteps(120);

	for (u32 index = 0; index < 4; ++index) {
		EXPECT_NEAR(fallers[index]->GetWorldPosition().y, 0.5f, 0.05f) << scales[index].x << " " << scales[index].z;
		SceneSystem::Destroy(fallers[index]);
		SceneSystem::Destroy(floors[index]);
	}
}

TEST_F(Physics3DColliderFixture, CollidersComposeOneBody) {
	PhysicsWorld& world = GetPhysicsWorld();
	ASSERT_TRUE(world.IsUp());

	Handle<SceneObject> object = MakeObject("compound", Vec3{0.0f, 5.0f, 0.0f});
	object->AddComponent(_rigidbodyType);
	object->AddComponent(_boxType);
	object->AddComponent(_sphereType);
	object->AddComponent(_capsuleType);

	EXPECT_EQ(world.GetBodyCount(), 1u);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);

	Collider* box = static_cast<Collider*>(object->GetComponent<BoxCollider>().Get());
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*box), 3u);
}

TEST_F(Physics3DColliderFixture, StaticBodyPerObject) {
	PhysicsWorld& world = GetPhysicsWorld();

	Handle<SceneObject> first = MakeObject("wall", Vec3{});
	first->AddComponent(_boxType);
	first->AddComponent(_sphereType);
	EXPECT_EQ(world.GetStaticBodyCount(), 1u);
	EXPECT_EQ(world.GetBodyCount(), 0u);

	Handle<SceneObject> second = MakeObject("other wall", Vec3{10.0f, 0.0f, 0.0f});
	second->AddComponent(_boxType);
	EXPECT_EQ(world.GetStaticBodyCount(), 2u);

	SceneSystem::Destroy(second);
	EXPECT_EQ(world.GetStaticBodyCount(), 1u);

	Handle<Component> firstBox = first->GetComponentAt(0);
	firstBox->SetEnabled(false);
	EXPECT_EQ(world.GetStaticBodyCount(), 1u);
	first->GetComponentAt(1)->SetEnabled(false);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);
}

TEST_F(Physics3DColliderFixture, StaticBodiesDontAccumulate) {
	if (PhysicsBackend::GetAllocatedBytes() < 0) {
		GTEST_SKIP();
	}

	constexpr u32 COLLIDERS_PER_LEVEL = 64;
	const i32 beforeAnything = PhysicsBackend::GetAllocatedBytes();
	i32 peaks[2] = {0, 0};

	for (u32 cycle = 0; cycle < 2; ++cycle) {
		Handle<SceneObject> objects[COLLIDERS_PER_LEVEL];
		for (u32 index = 0; index < COLLIDERS_PER_LEVEL; ++index) {
			objects[index] = MakeObject("wall", Vec3{static_cast<f32>(index) * 4.0f, 0.0f, 0.0f});
			objects[index]->AddComponent(_boxType);
		}
		ASSERT_EQ(GetPhysicsWorld().GetStaticBodyCount(), COLLIDERS_PER_LEVEL);
		peaks[cycle] = PhysicsBackend::GetAllocatedBytes();

		for (u32 index = 0; index < COLLIDERS_PER_LEVEL; ++index) {
			SceneSystem::Destroy(objects[index]);
		}
		ASSERT_EQ(GetPhysicsWorld().GetStaticBodyCount(), 0u);
	}

	const i32 oneLevel = peaks[0] - beforeAnything;
	ASSERT_GT(oneLevel, 0);
	EXPECT_LT(peaks[1] - peaks[0], oneLevel / 4);
}

TEST_F(Physics3DColliderFixture, ColliderUnderAncestorsRigidbodyJoinsThatBody) {
	PhysicsWorld& world = GetPhysicsWorld();

	Handle<SceneObject> root = MakeObject("vehicle", Vec3{0.0f, 5.0f, 0.0f});
	root->AddComponent(_rigidbodyType);
	root->AddComponent(_boxType);

	Handle<SceneObject> middle = MakeObject("axle", Vec3{0.0f, 5.0f, 0.0f}, root);
	Handle<SceneObject> wheel = MakeObject("wheel", Vec3{2.0f, 5.0f, 0.0f}, middle);
	wheel->AddComponent(_sphereType);

	EXPECT_EQ(world.GetBodyCount(), 1u);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);

	Collider* wheelCollider = static_cast<Collider*>(wheel->GetComponent<SphereCollider>().Get());
	ASSERT_NE(wheelCollider, nullptr);
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*wheelCollider), 2u);
}

TEST_F(Physics3DColliderFixture, ColliderFindsRigidbodyWhicheverIsEnabledFirst) {
	PhysicsWorld& world = GetPhysicsWorld();

	Handle<SceneObject> colliderFirst = MakeObject("collider first", Vec3{0.0f, 5.0f, 0.0f});
	colliderFirst->AddComponent(_boxType);
	EXPECT_EQ(world.GetStaticBodyCount(), 1u);
	colliderFirst->AddComponent(_rigidbodyType);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);
	EXPECT_EQ(world.GetBodyCount(), 1u);

	Collider* box = static_cast<Collider*>(colliderFirst->GetComponent<BoxCollider>().Get());
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*box), 1u);

	Handle<SceneObject> bodyFirst = MakeObject("body first", Vec3{10.0f, 5.0f, 0.0f});
	bodyFirst->AddComponent(_rigidbodyType);
	bodyFirst->AddComponent(_boxType);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);
	EXPECT_EQ(world.GetBodyCount(), 2u);
}

TEST_F(Physics3DColliderFixture, DisablingRigidbodyReturnsCollidersToStaticGeometry) {
	PhysicsWorld& world = GetPhysicsWorld();

	Handle<SceneObject> object = MakeObject("crate", Vec3{0.0f, 5.0f, 0.0f});
	Handle<Component> body = object->AddComponent(_rigidbodyType);
	object->AddComponent(_boxType);
	ASSERT_EQ(world.GetBodyCount(), 1u);
	ASSERT_EQ(world.GetStaticBodyCount(), 0u);

	body->SetEnabled(false);
	EXPECT_EQ(world.GetBodyCount(), 0u);
	EXPECT_EQ(world.GetStaticBodyCount(), 1u);

	Collider* box = static_cast<Collider*>(object->GetComponent<BoxCollider>().Get());
	ASSERT_NE(box, nullptr);
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*box), 1u);

	body->SetEnabled(true);
	EXPECT_EQ(world.GetBodyCount(), 1u);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*box), 1u);
}

TEST_F(Physics3DColliderFixture, EveryTeardownPathTakesCollidersShapeBack) {
	PhysicsWorld& world = GetPhysicsWorld();

	Handle<SceneObject> object = MakeObject("crate", Vec3{0.0f, 5.0f, 0.0f});
	object->AddComponent(_rigidbodyType);
	Handle<Component> box = object->AddComponent(_boxType);
	Collider* collider = static_cast<Collider*>(box.Get());
	ASSERT_NE(collider, nullptr);
	ASSERT_TRUE(collider->IsInSimulation());
	ASSERT_EQ(world.GetColliderCount(), 1u);

	box->SetEnabled(false);
	EXPECT_FALSE(collider->IsInSimulation());
	EXPECT_EQ(world.GetColliderCount(), 0u);
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*collider), 0u);
	box->SetEnabled(true);
	EXPECT_TRUE(collider->IsInSimulation());

	object->SetActive(false);
	EXPECT_FALSE(collider->IsInSimulation());
	object->SetActive(true);
	EXPECT_TRUE(collider->IsInSimulation());

	SceneSystem::Destroy(object);
	EXPECT_EQ(world.GetColliderCount(), 0u);
}

TEST_F(Physics3DColliderFixture, ColliderInIsolatedSceneIsNeverSimulated) {
	PhysicsWorld& world = GetPhysicsWorld();
	const u32 before = world.GetStaticBodyCount();

	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("preview");
	ASSERT_TRUE(isolated.IsValid());
	Handle<SceneObject> object = SceneSystem::CreateObject("preview mesh", isolated);
	ASSERT_TRUE(object.IsValid());
	object->AddComponent(_boxType);

	EXPECT_EQ(world.GetStaticBodyCount(), before);
	EXPECT_EQ(world.GetColliderCount(), 0u);
}

TEST_F(Physics3DColliderFixture, MeshlessColliderHasNoShape) {
	PhysicsWorld& world = GetPhysicsWorld();

	Handle<SceneObject> object = MakeObject("mesh", Vec3{});
	Handle<Component> mesh = object->AddComponent(_meshType);
	Collider* collider = static_cast<Collider*>(mesh.Get());
	ASSERT_NE(collider, nullptr);

	EXPECT_FALSE(collider->IsInSimulation());
	EXPECT_EQ(world.GetColliderCount(), 1u);
	EXPECT_EQ(world.GetStaticBodyCount(), 0u);
}

TEST_F(Physics3DColliderFixture, MeshColliderHullOrRefused) {
	MeshAsset mesh;
	const Vec3 corners[8] = {
		{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}};
	for (const Vec3& corner : corners) {
		MeshVertex vertex;
		vertex.position = corner;
		mesh.vertices.push_back(vertex);
	}
	const u32 faces[36] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
						   3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
	for (u32 index : faces) {
		mesh.indices.push_back(index);
	}

	Handle<SceneObject> dynamicObject = MakeObject("convex", Vec3{0.0f, 5.0f, 0.0f});
	dynamicObject->AddComponent(_rigidbodyType);
	Handle<Component> convex = dynamicObject->AddComponent(_meshType);
	static_cast<MeshCollider*>(convex.Get())->SetMesh(Handle<MeshAsset>(&mesh));
	ASSERT_TRUE(WriteField(_meshType, convex.Get(), "_convex", true));
	convex->SetEnabled(false);
	convex->SetEnabled(true);
	EXPECT_TRUE(static_cast<Collider*>(convex.Get())->IsInSimulation());

	Handle<SceneObject> staticObject = MakeObject("soup", Vec3{20.0f, 0.0f, 0.0f});
	Handle<Component> soup = staticObject->AddComponent(_meshType);
	static_cast<MeshCollider*>(soup.Get())->SetMesh(Handle<MeshAsset>(&mesh));
	soup->SetEnabled(false);
	soup->SetEnabled(true);
	EXPECT_TRUE(static_cast<Collider*>(soup.Get())->IsInSimulation());

	Handle<SceneObject> refused = MakeObject("soup on a body", Vec3{40.0f, 5.0f, 0.0f});
	refused->AddComponent(_rigidbodyType);
	Handle<Component> badSoup = refused->AddComponent(_meshType);
	static_cast<MeshCollider*>(badSoup.Get())->SetMesh(Handle<MeshAsset>(&mesh));
	badSoup->SetEnabled(false);
	badSoup->SetEnabled(true);
	EXPECT_FALSE(static_cast<Collider*>(badSoup.Get())->IsInSimulation());

	SceneSystem::Destroy(dynamicObject);
	SceneSystem::Destroy(staticObject);
	SceneSystem::Destroy(refused);
}

TEST_F(Physics3DColliderFixture, ShapeScaledByObject) {
	Handle<SceneObject> object = MakeObject("scaled", Vec3{});
	object->SetLocalScale(Vec3{3.0f, 1.0f, 1.0f});

	Handle<Component> sphere = object->AddComponent(_sphereType);
	Collider* collider = static_cast<Collider*>(sphere.Get());
	ASSERT_NE(collider, nullptr);

	PhysicsShapeRequest request;
	ASSERT_TRUE(static_cast<SphereCollider*>(collider)->DescribeShape(Vec3{3.0f, 1.0f, 1.0f}, request));
	EXPECT_FLOAT_EQ(request.radius, 1.5f);

	PhysicsShapeRequest boxRequest;
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(static_cast<BoxCollider*>(box.Get())->DescribeShape(Vec3{3.0f, 1.0f, 1.0f}, boxRequest));
	EXPECT_FLOAT_EQ(boxRequest.halfExtents.x, 1.5f);
	EXPECT_FLOAT_EQ(boxRequest.halfExtents.y, 0.5f);

	PhysicsShapeRequest capsuleRequest;
	Handle<Component> capsule = object->AddComponent(_capsuleType);
	ASSERT_TRUE(static_cast<CapsuleCollider*>(capsule.Get())->DescribeShape(Vec3{1.0f, 4.0f, 1.0f}, capsuleRequest));
	EXPECT_FLOAT_EQ(capsuleRequest.radius, 0.5f);
	EXPECT_GT(capsuleRequest.capsulePoint0.y - capsuleRequest.capsulePoint1.y, 2.0f);

	ASSERT_TRUE(WriteField(_capsuleType, capsule.Get(), "_height", 0.1f));
	PhysicsShapeRequest degenerate;
	ASSERT_TRUE(static_cast<CapsuleCollider*>(capsule.Get())->DescribeShape(Vec3{1.0f, 1.0f, 1.0f}, degenerate));
	EXPECT_FLOAT_EQ(degenerate.capsulePoint0.y, degenerate.capsulePoint1.y);
}

TEST_F(Physics3DColliderFixture, RefreshedMassStaysFinite) {
	Handle<SceneObject> object = MakeObject("crate", Vec3{0.0f, 50.0f, 0.0f});
	Handle<Component> bodyComponent = object->AddComponent(_rigidbodyType);
	ASSERT_TRUE(WriteField(_rigidbodyType, bodyComponent.Get(), "_mass", 12.0f));
	ASSERT_TRUE(WriteField(_rigidbodyType, bodyComponent.Get(), "_automaticInertiaTensor", false));
	ASSERT_TRUE(WriteField(_rigidbodyType, bodyComponent.Get(), "_inertiaTensor", Vec3{2.0f, 5.0f, 0.5f}));
	ASSERT_TRUE(WriteField(_rigidbodyType, bodyComponent.Get(), "_inertiaTensorRotation",
						   Quat::FromEuler(19.0f, 41.0f, 63.0f)));
	bodyComponent->SetEnabled(false);
	bodyComponent->SetEnabled(true);

	object->AddComponent(_boxType);
	Rigidbody* body = static_cast<Rigidbody*>(object->GetComponent<Rigidbody>().Get());
	ASSERT_NE(body, nullptr);
	body->SetAngularVelocity(Vec3{1.0f, -2.0f, 0.5f});

	RunFixedSteps(60);

	const Vec3 position = object->GetWorldPosition();
	const Quat rotation = object->GetWorldRotation();
	EXPECT_TRUE(Math::IsFinite(position.x) && Math::IsFinite(position.y) && Math::IsFinite(position.z));
	EXPECT_TRUE(Math::IsFinite(rotation.x) && Math::IsFinite(rotation.y) && Math::IsFinite(rotation.z) &&
				Math::IsFinite(rotation.w));
	EXPECT_LT(position.y, 50.0f);
}

TEST_F(Physics3DColliderFixture, CombineRuleFollowsPrecedenceAndIsSymmetric) {
	using Combine = PhysicsMaterialCombine;
	constexpr f32 A = 0.2f;
	constexpr f32 B = 0.8f;

	auto friction = [](Combine modeA, Combine modeB) {
		return CombinePhysicsMaterialValue(A, PackPhysicsMaterialCombine(modeA, Combine::Average),
										   B, PackPhysicsMaterialCombine(modeB, Combine::Average),
										   PHYSICS_COMBINE_FRICTION_SHIFT);
	};

	EXPECT_FLOAT_EQ(friction(Combine::Average, Combine::Average), 0.5f);
	EXPECT_FLOAT_EQ(friction(Combine::Minimum, Combine::Minimum), A);
	EXPECT_FLOAT_EQ(friction(Combine::Multiply, Combine::Multiply), A * B);
	EXPECT_FLOAT_EQ(friction(Combine::Maximum, Combine::Maximum), B);

	EXPECT_FLOAT_EQ(friction(Combine::Average, Combine::Maximum), B);
	EXPECT_FLOAT_EQ(friction(Combine::Average, Combine::Minimum), A);
	EXPECT_FLOAT_EQ(friction(Combine::Minimum, Combine::Multiply), A * B);
	EXPECT_FLOAT_EQ(friction(Combine::Multiply, Combine::Maximum), B);

	for (Combine modeA : {Combine::Average, Combine::Minimum, Combine::Multiply, Combine::Maximum}) {
		for (Combine modeB : {Combine::Average, Combine::Minimum, Combine::Multiply, Combine::Maximum}) {
			EXPECT_FLOAT_EQ(friction(modeA, modeB), friction(modeB, modeA))
				<< "modes " << static_cast<int>(modeA) << " and " << static_cast<int>(modeB);
		}
	}

	const u64 mixed = PackPhysicsMaterialCombine(Combine::Maximum, Combine::Minimum);
	EXPECT_FLOAT_EQ(CombinePhysicsMaterialValue(A, mixed, B, mixed, PHYSICS_COMBINE_FRICTION_SHIFT), B);
	EXPECT_FLOAT_EQ(CombinePhysicsMaterialValue(A, mixed, B, mixed, PHYSICS_COMBINE_BOUNCE_SHIFT), A);
}

TEST_F(Physics3DColliderFixture, AMaterialsFrictionObservablyChangesASlide) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	PhysicsMaterial slippery;
	slippery.friction = 0.0f;
	PhysicsMaterial grippy;
	grippy.friction = 1.0f;

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{400.0f, 1.0f, 40.0f}));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	auto slide = [&](const char* name, PhysicsMaterial& surface, f32 z) {
		Handle<SceneObject> object = MakeObject(name, Vec3{0.0f, 1.0f, z});
		object->AddComponent(_rigidbodyType);
		Handle<Component> box = object->AddComponent(_boxType);
		static_cast<Collider*>(box.Get())->SetMaterial(Handle<PhysicsMaterial>(&surface));
		box->SetEnabled(false);
		box->SetEnabled(true);

		Rigidbody* body = static_cast<Rigidbody*>(object->GetComponent<Rigidbody>().Get());
		body->SetLinearVelocity(Vec3{20.0f, 0.0f, 0.0f});
		RunFixedSteps(240);
		return object->GetWorldPosition().x;
	};

	const f32 slipperyX = slide("slippery", slippery, -8.0f);
	const f32 grippyX = slide("grippy", grippy, 8.0f);

	ASSERT_GT(slipperyX, 1.0f);

	EXPECT_GT(slipperyX, grippyX * 1.5f);
}

TEST_F(Physics3DColliderFixture, MaterialEditReachesShapes) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	PhysicsMaterial surface;
	surface.friction = 0.0f;

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{400.0f, 1.0f, 40.0f}));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	auto build = [&](const char* name, f32 z) {
		Handle<SceneObject> object = MakeObject(name, Vec3{0.0f, 1.0f, z});
		object->AddComponent(_rigidbodyType);
		Handle<Component> box = object->AddComponent(_boxType);
		static_cast<Collider*>(box.Get())->SetMaterial(Handle<PhysicsMaterial>(&surface));
		box->SetEnabled(false);
		box->SetEnabled(true);
		return object;
	};
	Handle<SceneObject> first = build("first", -8.0f);
	Handle<SceneObject> second = build("second", 8.0f);

	auto slide = [&](Handle<SceneObject> object) {
		auto* body = static_cast<Rigidbody*>(object->GetComponent<Rigidbody>().Get());
		body->SetLinearVelocity(Vec3{20.0f, 0.0f, 0.0f});
		RunFixedSteps(240);
		return object->GetWorldPosition().x;
	};

	const f32 slipperyX = slide(first);

	surface.friction = 1.0f;

	const f32 grippyX = slide(second);

	ASSERT_GT(slipperyX, 1.0f);
	EXPECT_GT(slipperyX, grippyX * 1.5f);
}

TEST_F(Physics3DColliderFixture, EditToMaterialsBouncinessReachesExistingShape) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	PhysicsMaterial surface;
	surface.bounciness = 0.0f;

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	static_cast<Collider*>(floorBox.Get())->SetMaterial(Handle<PhysicsMaterial>(&surface));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	Handle<SceneObject> ball = MakeObject("ball", Vec3{0.0f, 4.0f, 0.0f});
	ball->AddComponent(_rigidbodyType);
	Handle<Component> sphere = ball->AddComponent(_sphereType);
	static_cast<Collider*>(sphere.Get())->SetMaterial(Handle<PhysicsMaterial>(&surface));
	sphere->SetEnabled(false);
	sphere->SetEnabled(true);
	auto* body = static_cast<Rigidbody*>(ball->GetComponent<Rigidbody>().Get());

	auto dropAndMeasureRise = [&]() {
		ball->SetWorldPosition(Vec3{0.0f, 4.0f, 0.0f});
		body->SetLinearVelocity(Vec3{});
		body->WakeUp();
		f32 lowest = 4.0f;
		f32 rise = 0.0f;
		for (u32 step = 0; step < 120; ++step) {
			RunFixedSteps(1);
			const f32 y = ball->GetWorldPosition().y;
			if (y < lowest) {
				lowest = y;
			} else if (y - lowest > rise) {
				rise = y - lowest;
			}
		}
		return rise;
	};

	const f32 deadRise = dropAndMeasureRise();

	surface.bounciness = 0.9f;

	const f32 livelyRise = dropAndMeasureRise();

	ASSERT_LT(deadRise, 0.1f) << deadRise;
	EXPECT_GT(livelyRise, 0.5f);
}

TEST_F(Physics3DColliderFixture, CombineModeReachesContact) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	PhysicsMaterial floorSurface;
	floorSurface.friction = 1.0f;

	PhysicsMaterial averaged;
	averaged.friction = 0.0f;
	averaged.frictionCombine = PhysicsMaterialCombine::Average;

	PhysicsMaterial maximised;
	maximised.friction = 0.0f;
	maximised.frictionCombine = PhysicsMaterialCombine::Maximum;

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{400.0f, 1.0f, 40.0f}));
	static_cast<Collider*>(floorBox.Get())->SetMaterial(Handle<PhysicsMaterial>(&floorSurface));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	auto slide = [&](const char* name, PhysicsMaterial& surface, f32 z) {
		Handle<SceneObject> object = MakeObject(name, Vec3{0.0f, 1.0f, z});
		object->AddComponent(_rigidbodyType);
		Handle<Component> box = object->AddComponent(_boxType);
		static_cast<Collider*>(box.Get())->SetMaterial(Handle<PhysicsMaterial>(&surface));
		box->SetEnabled(false);
		box->SetEnabled(true);
		static_cast<Rigidbody*>(object->GetComponent<Rigidbody>().Get())->SetLinearVelocity(Vec3{20.0f, 0.0f, 0.0f});
		RunFixedSteps(240);
		return object->GetWorldPosition().x;
	};

	const f32 averagedX = slide("averaged", averaged, -8.0f);
	const f32 maximisedX = slide("maximised", maximised, 8.0f);

	ASSERT_GT(averagedX, 1.0f);

	EXPECT_GT(averagedX, maximisedX * 1.4f);
}

TEST_F(Physics3DColliderFixture, VocabularyResolvesNames) {
	PhysicsCategoryVocabulary vocabulary;
	ASSERT_TRUE(vocabulary.Rebuild("Default, Player, Enemy, Water"));
	EXPECT_EQ(vocabulary.Size(), 4u);

	const i32 player = vocabulary.IndexOf(StringHash("Player"));
	const i32 enemy = vocabulary.IndexOf(StringHash("Enemy"));
	ASSERT_GE(player, 0);
	ASSERT_GE(enemy, 0);
	EXPECT_NE(player, enemy);
	EXPECT_LT(vocabulary.IndexOf(StringHash("Ghost")), 0);

	EXPECT_FALSE(vocabulary.Rebuild("Water,Enemy,Player,Default"));
	EXPECT_EQ(vocabulary.IndexOf(StringHash("Player")), player);
	EXPECT_EQ(vocabulary.IndexOf(StringHash("Enemy")), enemy);

	EXPECT_FALSE(vocabulary.Rebuild("  Default ,\tPlayer,Enemy , Water, Player "));

	EXPECT_TRUE(vocabulary.Rebuild("Default,Player,Hostile,Water"));
	EXPECT_LT(vocabulary.IndexOf(StringHash("Enemy")), 0);
	EXPECT_GE(vocabulary.IndexOf(StringHash("Hostile")), 0);
}

TEST_F(Physics3DColliderFixture, DefaultFilterAcceptsAll) {
	PhysicsCategoryVocabulary vocabulary;
	ASSERT_TRUE(vocabulary.Rebuild("Default,Player,Enemy"));
	u32 unknown = 0;

	const PhysicsCategoryFilter both =
		ResolveCategoryFilter(vocabulary, "Player,Enemy", PhysicsCategoryMatch::AllExcept, "", unknown);
	EXPECT_EQ(unknown, 0u);
	const u64 playerBit = 1ull << vocabulary.IndexOf(StringHash("Player"));
	const u64 enemyBit = 1ull << vocabulary.IndexOf(StringHash("Enemy"));
	EXPECT_EQ(both.categoryBits, playerBit | enemyBit);

	EXPECT_EQ(both.maskBits, ~0ull);

	const PhysicsCategoryFilter nothing =
		ResolveCategoryFilter(vocabulary, "Default", PhysicsCategoryMatch::Only, "", unknown);
	EXPECT_EQ(nothing.maskBits, 0ull);

	const PhysicsCategoryFilter allButEnemy =
		ResolveCategoryFilter(vocabulary, "Player", PhysicsCategoryMatch::AllExcept, "Enemy", unknown);
	EXPECT_EQ(allButEnemy.maskBits & enemyBit, 0ull);
	EXPECT_NE(allButEnemy.maskBits & playerBit, 0ull);

	unknown = 0;
	const PhysicsCategoryFilter misspelled =
		ResolveCategoryFilter(vocabulary, "Playre", PhysicsCategoryMatch::AllExcept, "Enmey", unknown);
	EXPECT_EQ(unknown, 2u);
	EXPECT_EQ(misspelled.categoryBits, 0ull);
}

TEST_F(Physics3DColliderFixture, VocabularyOverflowReported) {
	str declaration;
	for (u32 index = 0; index < PHYSICS_CATEGORY_CAPACITY + 5; ++index) {
		if (index > 0) {
			declaration += ',';
		}
		declaration += "Category" + std::to_string(index);
	}

	PhysicsCategoryVocabulary vocabulary;
	ASSERT_TRUE(vocabulary.Rebuild(declaration));
	EXPECT_EQ(vocabulary.Size(), PHYSICS_CATEGORY_CAPACITY);
	EXPECT_EQ(vocabulary.OverflowCount(), 5u);

	for (u32 index = 0; index < PHYSICS_CATEGORY_CAPACITY + 5; ++index) {
		const i32 bit = vocabulary.IndexOf(StringHash(("Category" + std::to_string(index)).c_str()));
		EXPECT_LT(bit, static_cast<i32>(PHYSICS_CATEGORY_OVERFLOW_BIT));
	}
}

TEST_F(Physics3DColliderFixture, NewColliderUsesCurrentVocabulary) {
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<PhysicsCapturingSink>();
	LogManager::AddSink(sink);

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default,Ground")));

	Handle<SceneObject> object = MakeObject("ground", Vec3{});
	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Ground")));
	box->SetEnabled(false);
	box->SetEnabled(true);
	ASSERT_TRUE(static_cast<Collider*>(box.Get())->IsInSimulation());

	LogManager::Terminate();

	ASSERT_GT(sink->CountMatching(""), 0u);

	EXPECT_EQ(sink->CountMatching("doesn't declare"), 0u);
	EXPECT_EQ(sink->CountMatching("resolved to no categories"), 0u);
	sink.reset();
	JobSystem::Terminate();
}

TEST_F(Physics3DColliderFixture, ExcludedCategoryFallsThrough) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_NE(physics.type, nullptr);
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default,Ground")));

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_categories", str("Ground")));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	auto drop = [&](const char* name, const char* excludes, f32 z) -> f32 {
		Handle<SceneObject> object = MakeObject(name, Vec3{0.0f, 6.0f, z});
		object->AddComponent(_rigidbodyType);
		Handle<Component> box = object->AddComponent(_boxType);
		EXPECT_TRUE(WriteField(_boxType, box.Get(), "_collidesWith", str(excludes)));
		box->SetEnabled(false);
		box->SetEnabled(true);
		RunFixedSteps(180);
		return object->GetWorldPosition().y;
	};

	const f32 normalY = drop("lands", "", -8.0f);
	const f32 filteredY = drop("passes through", "Ground", 8.0f);

	EXPECT_GT(normalY, 0.0f);
	EXPECT_LT(filteredY, -5.0f);
}

TEST_F(Physics3DColliderFixture, RenamingCategoryReachesShapesThatAlreadyExist) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default,Ground")));

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_categories", str("Ground")));
	floorBox->SetEnabled(false);
	floorBox->SetEnabled(true);

	Handle<SceneObject> box = MakeObject("box", Vec3{0.0f, 6.0f, 0.0f});
	box->AddComponent(_rigidbodyType);
	Handle<Component> boxCollider = box->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, boxCollider.Get(), "_collidesWith", str("Ground")));
	ASSERT_TRUE(WriteField(_boxType, boxCollider.Get(), "_collidesWithMode", static_cast<u8>(1)));
	boxCollider->SetEnabled(false);
	boxCollider->SetEnabled(true);

	RunFixedSteps(180);
	const f32 resting = box->GetWorldPosition().y;
	ASSERT_GT(resting, 0.0f);

	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Ground,Default")));
	RunFixedSteps(60);
	EXPECT_GT(box->GetWorldPosition().y, 0.0f);

	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default,Terrain")));
	RunFixedSteps(180);
	EXPECT_LT(box->GetWorldPosition().y, resting - 1.0f);
}

TEST_F(Physics3DColliderFixture, RuntimeResizeChangesQuery) {
	Handle<SceneObject> object = MakeObject("ball", Vec3{});
	ASSERT_TRUE(object.IsValid());
	auto* sphere = static_cast<SphereCollider*>(object->AddComponent(_sphereType).Get());
	ASSERT_NE(sphere, nullptr);
	ASSERT_TRUE(sphere->IsInSimulation());

	const Ray ray{Vec3{2.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f}};
	RaycastHit hit;
	ASSERT_FALSE(Physics::Raycast(ray, hit, 1.2f));

	sphere->SetRadius(1.0f);
	EXPECT_TRUE(sphere->IsInSimulation());
	EXPECT_TRUE(Physics::Raycast(ray, hit, 1.2f));
	EXPECT_EQ(hit.collider.Get(), sphere);

	sphere->SetRadius(0.5f);
	EXPECT_FALSE(Physics::Raycast(ray, hit, 1.2f));
}

TEST_F(Physics3DColliderFixture, TriggerFlagWrittenAtRuntimeReachesShape) {
	Handle<SceneObject> object = MakeObject("door", Vec3{});
	auto* sphere = static_cast<SphereCollider*>(object->AddComponent(_sphereType).Get());
	ASSERT_NE(sphere, nullptr);

	const Ray ray{Vec3{2.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f}};
	PhysicsQueryFilter ignoreTriggers;
	ignoreTriggers.triggerInteraction = PhysicsQueryTriggers::Ignore;
	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(ray, hit, 4.0f, ignoreTriggers));

	sphere->SetTrigger(true);
	EXPECT_TRUE(sphere->IsInSimulation());
	EXPECT_FALSE(Physics::Raycast(ray, hit, 4.0f, ignoreTriggers));

	PhysicsQueryFilter acceptTriggers;
	acceptTriggers.triggerInteraction = PhysicsQueryTriggers::Collide;
	EXPECT_TRUE(Physics::Raycast(ray, hit, 4.0f, acceptTriggers));

	sphere->SetTrigger(false);
	EXPECT_TRUE(Physics::Raycast(ray, hit, 4.0f, ignoreTriggers));
}

TEST_F(Physics3DColliderFixture, RuntimeFilterIsPushed) {
	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindSettingsCategory("Physics", physics));
	ASSERT_TRUE(WriteField(physics.type, physics.live, "categories", str("Default,Ground")));

	Handle<SceneObject> object = MakeObject("ball", Vec3{});
	auto* sphere = static_cast<SphereCollider*>(object->AddComponent(_sphereType).Get());
	ASSERT_NE(sphere, nullptr);

	const Ray ray{Vec3{2.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f}};
	PhysicsQueryFilter refusesGround;
	refusesGround.collidesWith = str("Ground");
	RaycastHit hit;
	ASSERT_TRUE(Physics::Raycast(ray, hit, 4.0f, refusesGround));

	sphere->SetCategories(str("Ground"));
	EXPECT_FALSE(Physics::Raycast(ray, hit, 4.0f, refusesGround));

	sphere->SetCategories(str("Default"));
	EXPECT_TRUE(Physics::Raycast(ray, hit, 4.0f, refusesGround));

	PhysicsQueryFilter asDefault;
	asDefault.categories = str("Default");
	ASSERT_TRUE(Physics::Raycast(ray, hit, 4.0f, asDefault));
	sphere->SetCollidesWithMode(PhysicsCategoryMatch::Only);
	sphere->SetCollidesWith(str("Ground"));
	EXPECT_FALSE(Physics::Raycast(ray, hit, 4.0f, asDefault));
}

TEST_F(Physics3DColliderFixture, RuntimeMaterialSwapReachesShape) {
#ifdef SHALLTEAR_PHYSICS_BACKEND_STUB
	GTEST_SKIP();
#endif
	PhysicsMaterial dead;
	dead.bounciness = 0.0f;
	PhysicsMaterial lively;
	lively.bounciness = 0.9f;

	Handle<SceneObject> floor = MakeObject("floor", Vec3{0.0f, 0.0f, 0.0f});
	Handle<Component> floorBox = floor->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, floorBox.Get(), "_size", Vec3{40.0f, 1.0f, 40.0f}));
	auto* floorCollider = static_cast<Collider*>(floorBox.Get());
	floorCollider->SetMaterial(Handle<PhysicsMaterial>(&dead));

	Handle<SceneObject> ball = MakeObject("ball", Vec3{0.0f, 4.0f, 0.0f});
	ball->AddComponent(_rigidbodyType);
	auto* sphere = static_cast<Collider*>(ball->AddComponent(_sphereType).Get());
	sphere->SetMaterial(Handle<PhysicsMaterial>(&dead));
	auto* body = static_cast<Rigidbody*>(ball->GetComponent<Rigidbody>().Get());
	ASSERT_NE(body, nullptr);

	auto dropAndMeasureRise = [&]() {
		ball->SetWorldPosition(Vec3{0.0f, 4.0f, 0.0f});
		body->SetLinearVelocity(Vec3{});
		body->WakeUp();
		f32 lowest = 4.0f;
		f32 rise = 0.0f;
		for (u32 step = 0; step < 120; ++step) {
			RunFixedSteps(1);
			const f32 y = ball->GetWorldPosition().y;
			if (y < lowest) {
				lowest = y;
			} else if (y - lowest > rise) {
				rise = y - lowest;
			}
		}
		return rise;
	};

	const f32 deadRise = dropAndMeasureRise();

	floorCollider->SetMaterial(Handle<PhysicsMaterial>(&lively));
	sphere->SetMaterial(Handle<PhysicsMaterial>(&lively));

	const f32 livelyRise = dropAndMeasureRise();

	ASSERT_LT(deadRise, 0.1f) << deadRise;
	EXPECT_GT(livelyRise, 0.5f);
}

TEST_F(Physics3DColliderFixture, RebuildsLeaveOneShape) {
	PhysicsWorld& world = GetPhysicsWorld();
	const u32 bodiesBefore = world.GetBodyCount();

	Handle<SceneObject> object = MakeObject("static thing", Vec3{});
	auto* box = static_cast<BoxCollider*>(object->AddComponent(_boxType).Get());
	ASSERT_NE(box, nullptr);
	ASSERT_EQ(world.GetShapeCountOnBodyOf(*box), 1u);

	for (u32 index = 0; index < 8; ++index) {
		box->SetSize(Vec3{1.0f + static_cast<f32>(index), 1.0f, 1.0f});
		box->SetTrigger((index & 1u) != 0u);
	}

	EXPECT_TRUE(box->IsInSimulation());
	EXPECT_EQ(world.GetShapeCountOnBodyOf(*box), 1u);
	EXPECT_EQ(world.GetBodyCount(), bodiesBefore);

	const Ray ray{Vec3{6.0f, 0.0f, 0.0f}, Vec3{-1.0f, 0.0f, 0.0f}};
	RaycastHit hit;
	EXPECT_TRUE(Physics::Raycast(ray, hit, 2.5f));
}

#endif

TEST_F(Physics3DColliderFixture, ColliderSceneRoundTrips) {
	Handle<SceneObject> object = MakeObject("every shape", Vec3{1.0f, 2.0f, 3.0f});
	ASSERT_TRUE(object.IsValid());
	object->AddComponent(_rigidbodyType);

	Handle<Component> box = object->AddComponent(_boxType);
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_size", Vec3{2.0f, 0.5f, 4.0f}));
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_center", Vec3{0.25f, -0.5f, 0.0f}));
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_isTrigger", true));
	ASSERT_TRUE(WriteField(_boxType, box.Get(), "_categories", str("Solid,Ground")));

	Handle<Component> sphere = object->AddComponent(_sphereType);
	ASSERT_TRUE(WriteField(_sphereType, sphere.Get(), "_radius", 1.75f));

	Handle<Component> capsule = object->AddComponent(_capsuleType);
	ASSERT_TRUE(WriteField(_capsuleType, capsule.Get(), "_height", 3.5f));
	ASSERT_TRUE(WriteField(_capsuleType, capsule.Get(), "_direction", static_cast<u8>(2)));

	object->AddComponent(_meshType);

	const Path scenePath((_root / "colliders.scene").generic_string());
	ASSERT_TRUE(SceneSystem::SaveScene(_scene, scenePath));
	const str first = FileSystem::ReadTextFile(scenePath);
	ASSERT_FALSE(first.empty());

	Handle<Scene> reloaded = SceneSystem::LoadScene(scenePath);
	ASSERT_TRUE(reloaded.IsValid());

	std::filesystem::create_directories(_root / "again");
	const Path secondPath((_root / "again" / "colliders.scene").generic_string());
	ASSERT_TRUE(SceneSystem::SaveScene(reloaded, secondPath));

	EXPECT_EQ(FileSystem::ReadTextFile(secondPath), first);

	EXPECT_NE(first.find("BoxCollider"), str::npos);
	EXPECT_NE(first.find("MeshCollider"), str::npos);
	EXPECT_NE(first.find("ZAxis"), str::npos);

	EXPECT_NE(first.find("\"_categories\""), str::npos);
	EXPECT_NE(first.find("Solid,Ground"), str::npos);
	EXPECT_EQ(first.find("categoryBits"), str::npos);
	EXPECT_EQ(first.find("maskBits"), str::npos);
	EXPECT_NE(first.find("AllExcept"), str::npos);
}
