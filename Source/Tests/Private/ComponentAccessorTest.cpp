#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Scene.h"

#ifndef SHALLTEAR_SHARED
#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Joints.h"
#include "Shalltear/Physics3D/Rigidbody.h"

#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Animation/AnimatorController.h"
#endif

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

#define CA_ACCESS_PROBE(Suffix, member)          \
	template <typename T, typename = void>       \
	struct CaNames##Suffix : std::false_type {}; \
	template <typename T>                        \
	struct CaNames##Suffix<T, std::void_t<decltype(std::declval<T&>().member)>> : std::true_type {}

	CA_ACCESS_PROBE(FieldOfView, _fieldOfViewDeg);
	CA_ACCESS_PROBE(LightType, _lightType);
	CA_ACCESS_PROBE(Mesh, _mesh);
	CA_ACCESS_PROBE(Materials, _materials);
	CA_ACCESS_PROBE(BlendShapeWeights, _blendShapeWeights);
	CA_ACCESS_PROBE(ExposedBones, _exposedBones);
#ifndef SHALLTEAR_SHARED
	CA_ACCESS_PROBE(Clip, _clip);
	CA_ACCESS_PROBE(ControllerField, _controller);
	CA_ACCESS_PROBE(Mass, _mass);
	CA_ACCESS_PROBE(IsTrigger, _isTrigger);
	CA_ACCESS_PROBE(Size, _size);
	CA_ACCESS_PROBE(Radius, _radius);
	CA_ACCESS_PROBE(Height, _height);
	CA_ACCESS_PROBE(Convex, _convex);
	CA_ACCESS_PROBE(BreakForce, _breakForce);
	CA_ACCESS_PROBE(MotorTargetVelocity, _motorTargetVelocity);
	CA_ACCESS_PROBE(MinDistance, _minDistance);
	CA_ACCESS_PROBE(Swing1Limit, _swing1Limit);
	CA_ACCESS_PROBE(LimitMax, _limitMax);
#endif

	struct CaPublicProbe {
		f32 _fieldOfViewDeg = 0.0f;
		LightType _lightType = LightType::Directional;
		Handle<MeshAsset> _mesh;
		Vector<Handle<MaterialAsset>> _materials;
		Vector<f32> _blendShapeWeights;
		Vector<i16> _exposedBones;
		u64 _clip = 0;
		u64 _controller = 0;
		f32 _mass = 0.0f;
		bool _isTrigger = false;
		Vec3 _size{};
		f32 _radius = 0.0f;
		f32 _height = 0.0f;
		bool _convex = false;
		f32 _breakForce = 0.0f;
		f32 _motorTargetVelocity = 0.0f;
		f32 _minDistance = 0.0f;
		f32 _swing1Limit = 0.0f;
		f32 _limitMax = 0.0f;
	};

	static_assert(CaNamesFieldOfView<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesLightType<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(!CaNamesFieldOfView<Camera>::value, "Camera::_fieldOfViewDeg is reachable");
	static_assert(!CaNamesLightType<Light>::value, "Light::_lightType is reachable");

	static_assert(CaNamesMesh<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesMaterials<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesBlendShapeWeights<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesExposedBones<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(!CaNamesMesh<MeshRenderer>::value, "MeshRenderer::_mesh is reachable");
	static_assert(!CaNamesMesh<SkinnedMeshRenderer>::value, "SkinnedMeshRenderer::_mesh is reachable");
	static_assert(!CaNamesMaterials<MeshRenderer>::value, "MeshRenderer::_materials is reachable");
	static_assert(!CaNamesMaterials<SkinnedMeshRenderer>::value, "SkinnedMeshRenderer::_materials is reachable");
	static_assert(!CaNamesBlendShapeWeights<SkinnedMeshRenderer>::value, "SkinnedMeshRenderer::_blendShapeWeights is reachable");
	static_assert(!CaNamesExposedBones<Skeleton>::value, "Skeleton::_exposedBones is reachable");

#ifndef SHALLTEAR_SHARED
	static_assert(CaNamesMass<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesIsTrigger<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesSize<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesRadius<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesHeight<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesConvex<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesBreakForce<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesMotorTargetVelocity<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesMinDistance<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesSwing1Limit<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesLimitMax<CaPublicProbe>::value, "probe can't see a public member");

	static_assert(!CaNamesMass<Rigidbody>::value, "Rigidbody::_mass is reachable");
	static_assert(!CaNamesIsTrigger<Collider>::value, "Collider::_isTrigger is reachable");
	static_assert(!CaNamesIsTrigger<BoxCollider>::value, "a Collider field is reachable through a derived shape");
	static_assert(!CaNamesSize<BoxCollider>::value, "BoxCollider::_size is reachable");
	static_assert(!CaNamesRadius<SphereCollider>::value, "SphereCollider::_radius is reachable");
	static_assert(!CaNamesRadius<CapsuleCollider>::value, "CapsuleCollider::_radius is reachable");
	static_assert(!CaNamesHeight<CapsuleCollider>::value, "CapsuleCollider::_height is reachable");
	static_assert(!CaNamesConvex<MeshCollider>::value, "MeshCollider::_convex is reachable");
	static_assert(!CaNamesBreakForce<Joint>::value, "Joint::_breakForce is reachable");
	static_assert(!CaNamesBreakForce<FixedJoint>::value, "a Joint field is reachable through a derived joint");
	static_assert(!CaNamesMotorTargetVelocity<HingeJoint>::value, "HingeJoint::_motorTargetVelocity is reachable");
	static_assert(!CaNamesMinDistance<SpringJoint>::value, "SpringJoint::_minDistance is reachable");
	static_assert(!CaNamesSwing1Limit<CharacterJoint>::value, "CharacterJoint::_swing1Limit is reachable");
	static_assert(!CaNamesLimitMax<PrismaticJoint>::value, "PrismaticJoint::_limitMax is reachable");

	static_assert(CaNamesClip<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(CaNamesControllerField<CaPublicProbe>::value, "probe can't see a public member");
	static_assert(!CaNamesClip<Animator>::value, "Animator::_clip is reachable");
	static_assert(!CaNamesControllerField<AnimatorStateMachine>::value,
				  "AnimatorStateMachine::_controller is reachable");
#endif

	struct CaMigratedType {
		const TypeInfo* type;
		const char* name;
		u32 ownFieldCount;
	};

	class ComponentAccessorFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_accessor_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			TypeRegistry::InitializePrimitives();
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
	};

} // anonymous namespace

TEST_F(ComponentAccessorFixture, ReflectedSetterReachesStorage) {
	Handle<Scene> scene = SceneSystem::CreateScene("Accessors");
	Handle<SceneObject> object = SceneSystem::CreateObject("Rig", scene);
	Camera* camera = object->AddComponent<Camera>().Get();
	Light* light = object->AddComponent<Light>().Get();
	ASSERT_NE(camera, nullptr);
	ASSERT_NE(light, nullptr);

	const TypeInfo* cameraType = Camera::GetReflectType();
	const FieldInfo* fov = FindField(cameraType, StringHash("_fieldOfViewDeg"));
	ASSERT_NE(fov, nullptr);
	ASSERT_NE(fov->setFn, nullptr);
	ASSERT_NE(fov->getFn, nullptr);

	Variant in = 33.0f;
	fov->setFn(camera, &in);
	EXPECT_FLOAT_EQ(camera->GetFieldOfViewDeg(), 33.0f);

	camera->SetFieldOfViewDeg(77.0f);
	Variant out;
	fov->getFn(camera, &out);
	ASSERT_TRUE(out.Is<f32>());
	EXPECT_FLOAT_EQ(out.As<f32>(), 77.0f);

	const FieldInfo* type = FindField(Light::GetReflectType(), StringHash("_lightType"));
	ASSERT_NE(type, nullptr);
	Variant spot = static_cast<i64>(LightType::Spot);
	type->setFn(light, &spot);
	EXPECT_EQ(light->GetLightType(), LightType::Spot);

	light->SetLightType(LightType::Point);
	Variant readBack;
	type->getFn(light, &readBack);
	ASSERT_TRUE(readBack.Is<i64>());
	EXPECT_EQ(static_cast<LightType>(readBack.As<i64>()), LightType::Point);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(ComponentAccessorFixture, ReflectedContainerWriteReachesStorage) {
	Handle<Scene> scene = SceneSystem::CreateScene("Containers");
	Handle<SceneObject> object = SceneSystem::CreateObject("Rig", scene);
	SkinnedMeshRenderer* renderer = object->AddComponent<SkinnedMeshRenderer>().Get();
	ASSERT_NE(renderer, nullptr);

	const FieldInfo* weights = FindField(SkinnedMeshRenderer::GetReflectType(), StringHash("_blendShapeWeights"));
	ASSERT_NE(weights, nullptr);
	ASSERT_NE(weights->container, nullptr);
	ASSERT_NE(weights->addrFn, nullptr);
	ASSERT_NE(weights->container->resizeFn, nullptr);
	ASSERT_NE(weights->container->atFn, nullptr);
	ASSERT_NE(weights->container->sizeFn, nullptr);

	void* list = weights->addrFn(renderer);
	ASSERT_NE(list, nullptr);
	weights->container->resizeFn(list, 3);
	ASSERT_EQ(renderer->GetBlendShapeWeightCount(), 3u);
	*static_cast<f32*>(weights->container->atFn(list, 1)) = 62.5f;
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(1), 62.5f);

	renderer->SetBlendShapeWeight(4, 17.0f);
	EXPECT_EQ(weights->container->sizeFn(weights->addrFn(renderer)), 5u);
	EXPECT_FLOAT_EQ(*static_cast<f32*>(weights->container->atFn(weights->addrFn(renderer), 4)), 17.0f);
	EXPECT_FLOAT_EQ(*static_cast<f32*>(weights->container->atFn(weights->addrFn(renderer), 3)), 0.0f);

	const FieldInfo* materials = FindField(MeshRenderer::GetReflectType(), StringHash("_materials"));
	ASSERT_NE(materials, nullptr);
	ASSERT_NE(materials->container, nullptr);
	MeshRenderer* plain = object->AddComponent<MeshRenderer>().Get();
	ASSERT_NE(plain, nullptr);
	MaterialAsset material;
	plain->SetMaterial(2, Handle<MaterialAsset>(&material));
	void* slots = materials->addrFn(plain);
	ASSERT_EQ(materials->container->sizeFn(slots), 3u);
	EXPECT_EQ(*static_cast<u64*>(materials->container->atFn(slots, 2)), material.GetInstanceId());

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(ComponentAccessorFixture, ExposedBoneListsCannotBeGrownApart) {
	Handle<Scene> scene = SceneSystem::CreateScene("Exposed");
	Handle<SceneObject> rig = SceneSystem::CreateObject("Rig", scene);
	Handle<SceneObject> boneA = SceneSystem::CreateObject("A", scene, rig);
	Handle<SceneObject> boneB = SceneSystem::CreateObject("B", scene, rig);
	Skeleton* skeleton = rig->AddComponent<Skeleton>().Get();
	ASSERT_NE(skeleton, nullptr);

	skeleton->AddExposedBone(7, boneA);
	skeleton->AddExposedBone(3, boneB);
	ASSERT_EQ(skeleton->GetExposedBoneCount(), 2u);

	EXPECT_EQ(skeleton->GetExposedBone(0), 7);
	EXPECT_EQ(skeleton->GetExposedObject(0).GetInstanceId(), boneA.GetInstanceId());
	EXPECT_EQ(skeleton->GetExposedBone(1), 3);
	EXPECT_EQ(skeleton->GetExposedObject(1).GetInstanceId(), boneB.GetInstanceId());

	const TypeInfo* type = Skeleton::GetReflectType();
	const FieldInfo* bones = FindField(type, StringHash("_exposedBones"));
	const FieldInfo* objects = FindField(type, StringHash("_exposedObjects"));
	ASSERT_NE(bones, nullptr);
	ASSERT_NE(objects, nullptr);
	ASSERT_NE(bones->container, nullptr);
	ASSERT_NE(objects->container, nullptr);
	EXPECT_EQ(bones->container->sizeFn(bones->addrFn(skeleton)), 2u);
	EXPECT_EQ(objects->container->sizeFn(objects->addrFn(skeleton)), 2u);

	skeleton->SetExposedBoneCount(1);
	EXPECT_EQ(skeleton->GetExposedBoneCount(), 1u);
	EXPECT_EQ(bones->container->sizeFn(bones->addrFn(skeleton)), 1u);
	EXPECT_EQ(objects->container->sizeFn(objects->addrFn(skeleton)), 1u);
	EXPECT_TRUE(boneB.IsValid());

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST(ComponentAccessorTest, MigratedMembersArePrivate) {
	EXPECT_TRUE((CaNamesFieldOfView<CaPublicProbe>::value));
	EXPECT_TRUE((CaNamesLightType<CaPublicProbe>::value));
	EXPECT_FALSE((CaNamesFieldOfView<Camera>::value));
	EXPECT_FALSE((CaNamesLightType<Light>::value));
	EXPECT_TRUE((CaNamesMaterials<CaPublicProbe>::value));
	EXPECT_FALSE((CaNamesMesh<MeshRenderer>::value));
	EXPECT_FALSE((CaNamesMesh<SkinnedMeshRenderer>::value));
	EXPECT_FALSE((CaNamesMaterials<MeshRenderer>::value));
	EXPECT_FALSE((CaNamesBlendShapeWeights<SkinnedMeshRenderer>::value));
	EXPECT_FALSE((CaNamesExposedBones<Skeleton>::value));
}

TEST_F(ComponentAccessorFixture, MigratedComponentRoundTripsUnderNewKeys) {
	Handle<Scene> scene = SceneSystem::CreateScene("Keys");
	Handle<SceneObject> object = SceneSystem::CreateObject("Rig", scene);
	Camera* camera = object->AddComponent<Camera>().Get();
	Light* light = object->AddComponent<Light>().Get();
	ASSERT_NE(camera, nullptr);
	ASSERT_NE(light, nullptr);

	Handle<SceneObject> drawn = SceneSystem::CreateObject("Drawn", scene, object);
	MeshRenderer* plain = drawn->AddComponent<MeshRenderer>().Get();
	SkinnedMeshRenderer* skinned = drawn->AddComponent<SkinnedMeshRenderer>().Get();
	Skeleton* skeletonRig = drawn->AddComponent<Skeleton>().Get();
	ASSERT_NE(plain, nullptr);
	ASSERT_NE(skinned, nullptr);
	ASSERT_NE(skeletonRig, nullptr);
	MaterialAsset slotMaterial;
	plain->SetMaterial(0, Handle<MaterialAsset>(&slotMaterial));
	skinned->SetMaterial(0, Handle<MaterialAsset>(&slotMaterial));
	skinned->SetBlendShapeWeight(1, 35.0f);
	skeletonRig->AddExposedBone(4, drawn);

	camera->SetFieldOfViewDeg(41.0f);
	camera->SetNearPlane(0.25f);
	camera->SetFarPlane(512.0f);
	camera->SetPriority(7);
	camera->SetClearColor(Color(0.25f, 0.5f, 0.75f, 1.0f));
	light->SetLightType(LightType::Spot);
	light->SetColor(Color(0.1f, 0.2f, 0.3f, 1.0f));
	light->SetIntensity(2.5f);
	light->SetRange(19.0f);
	light->SetSpotAngleDeg(66.0f);

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://keys.scene")));
	str text = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://keys.scene")), MemTag::Scene);

	for (const char* key : {"\"_fieldOfViewDeg\"", "\"_nearPlane\"", "\"_farPlane\"", "\"_priority\"",
							"\"_clearColor\"", "\"_lightType\"", "\"_color\"", "\"_intensity\"", "\"_range\"",
							"\"_spotAngleDeg\"",
							"\"_mesh\"", "\"_materials\"", "\"_skin\"", "\"_skeleton\"", "\"_blendShapeWeights\"",
							"\"_exposedBones\"", "\"_exposedObjects\""}) {
		EXPECT_NE(text.find(key), str::npos) << key << ' ' << text;
	}

	for (const char* old : {"\"fieldOfViewDeg\"", "\"nearPlane\"", "\"farPlane\"", "\"spotAngleDeg\"", "\"mesh\"",
							"\"materials\"", "\"skin\"", "\"skeleton\"", "\"blendShapeWeights\"", "\"exposedBones\"",
							"\"exposedObjects\""}) {
		EXPECT_EQ(text.find(old), str::npos) << old << ' ' << text;
	}

	EXPECT_EQ(text.find(std::format("{:016x}", StringHash("_lightType").GetValue())), str::npos) << text;
	EXPECT_NE(text.find("\"_type\": \"Light\""), str::npos) << text;

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://keys.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	ASSERT_EQ(reloaded->GetRootCount(), 1u);
	Handle<SceneObject> rig = reloaded->GetRoot(0);
	ASSERT_TRUE(rig.IsValid());
	Camera* loadedCamera = rig->GetComponent<Camera>().Get();
	Light* loadedLight = rig->GetComponent<Light>().Get();
	ASSERT_NE(loadedCamera, nullptr);
	ASSERT_NE(loadedLight, nullptr);

	EXPECT_FLOAT_EQ(loadedCamera->GetFieldOfViewDeg(), 41.0f);
	EXPECT_FLOAT_EQ(loadedCamera->GetNearPlane(), 0.25f);
	EXPECT_FLOAT_EQ(loadedCamera->GetFarPlane(), 512.0f);
	EXPECT_EQ(loadedCamera->GetPriority(), 7);
	EXPECT_FLOAT_EQ(loadedCamera->GetClearColor().b, 0.75f);
	EXPECT_EQ(loadedLight->GetLightType(), LightType::Spot);
	EXPECT_FLOAT_EQ(loadedLight->GetColor().g, 0.2f);
	EXPECT_FLOAT_EQ(loadedLight->GetIntensity(), 2.5f);
	EXPECT_FLOAT_EQ(loadedLight->GetRange(), 19.0f);
	EXPECT_FLOAT_EQ(loadedLight->GetSpotAngleDeg(), 66.0f);
}

TEST_F(ComponentAccessorFixture, EveryMigratedComponentSpellsFieldsForDisk) {
	Vector<CaMigratedType> migrated;
	migrated.push_back({Camera::GetReflectType(), "Camera", 11});
	migrated.push_back({Light::GetReflectType(), "Light", 10});
	migrated.push_back({MeshRenderer::GetReflectType(), "MeshRenderer", 3});
	migrated.push_back({Skeleton::GetReflectType(), "Skeleton", 3});
	migrated.push_back({SkinnedMeshRenderer::GetReflectType(), "SkinnedMeshRenderer", 6});
#ifndef SHALLTEAR_SHARED
	migrated.push_back({Rigidbody::GetReflectType(), "Rigidbody", 20});
	migrated.push_back({Collider::GetReflectType(), "Collider", 5});
	migrated.push_back({BoxCollider::GetReflectType(), "BoxCollider", 2});
	migrated.push_back({SphereCollider::GetReflectType(), "SphereCollider", 2});
	migrated.push_back({CapsuleCollider::GetReflectType(), "CapsuleCollider", 4});
	migrated.push_back({MeshCollider::GetReflectType(), "MeshCollider", 2});
	migrated.push_back({Joint::GetReflectType(), "Joint", 7});
	migrated.push_back({HingeJoint::GetReflectType(), "HingeJoint", 11});
	migrated.push_back({FixedJoint::GetReflectType(), "FixedJoint", 0});
	migrated.push_back({SpringJoint::GetReflectType(), "SpringJoint", 4});
	migrated.push_back({CharacterJoint::GetReflectType(), "CharacterJoint", 12});
	migrated.push_back({PrismaticJoint::GetReflectType(), "PrismaticJoint", 11});
	migrated.push_back({Animator::GetReflectType(), "Animator", 6});
	migrated.push_back({AnimatorStateMachine::GetReflectType(), "AnimatorStateMachine", 1});
#endif

	u32 total = 0;
	for (const CaMigratedType& row : migrated) {
		ASSERT_NE(row.type, nullptr) << row.name;
		EXPECT_EQ(row.type->fieldCount, row.ownFieldCount) << row.name << " owns " << row.type->fieldCount << ' ' << row.ownFieldCount;
		for (u32 index = 0; index < row.type->fieldCount; ++index) {
			const FieldInfo& field = row.type->fields[index];
			ASSERT_NE(field.displayName, nullptr) << row.name << " field " << index;
			EXPECT_EQ(field.displayName[0], '_');
		}
		total += row.type->fieldCount;
	}

#ifndef SHALLTEAR_SHARED
	EXPECT_EQ(total, 12u + 4u + 80u + 17u + 3u + 1u + 3u);
#else
	EXPECT_EQ(total, 12u + 4u + 10u + 3u + 1u + 3u);
#endif
}

#ifndef SHALLTEAR_SHARED

TEST_F(ComponentAccessorFixture, ReflectedWriteReachesPackageComponentsPrivateStorage) {
	Rigidbody body;
	const TypeInfo* type = Rigidbody::GetReflectType();
	ASSERT_NE(type, nullptr);

	const FieldInfo* mass = FindField(type, StringHash("_mass"));
	ASSERT_NE(mass, nullptr);
	ASSERT_NE(mass->setFn, nullptr);
	ASSERT_NE(mass->getFn, nullptr);

	Variant in = 42.5f;
	mass->setFn(&body, &in);
	EXPECT_FLOAT_EQ(body.GetMass(), 42.5f);

	body.SetMass(7.25f);
	Variant out;
	mass->getFn(&body, &out);
	ASSERT_TRUE(out.Is<f32>());
	EXPECT_FLOAT_EQ(out.As<f32>(), 7.25f);

	const FieldInfo* kinematic = FindField(type, StringHash("_isKinematic"));
	ASSERT_NE(kinematic, nullptr);
	Variant on = true;
	kinematic->setFn(&body, &on);
	EXPECT_TRUE(body.IsKinematic());
	body.SetKinematic(false);
	Variant readBack;
	kinematic->getFn(&body, &readBack);
	ASSERT_TRUE(readBack.Is<bool>());
	EXPECT_FALSE(readBack.As<bool>());

	CharacterJoint ragdoll;
	const FieldInfo* swing = FindField(CharacterJoint::GetReflectType(), StringHash("_swing1Limit"));
	ASSERT_NE(swing, nullptr);
	Variant degrees = 55.0f;
	swing->setFn(&ragdoll, &degrees);
	EXPECT_FLOAT_EQ(ragdoll.GetSwing1Limit(), 55.0f);

	const FieldInfo* breakForce = FindField(CharacterJoint::GetReflectType(), StringHash("_breakForce"));
	ASSERT_NE(breakForce, nullptr);
	ragdoll.SetBreakForce(250.0f);
	Variant force;
	breakForce->getFn(&ragdoll, &force);
	ASSERT_TRUE(force.Is<f32>());
	EXPECT_FLOAT_EQ(force.As<f32>(), 250.0f);
}

TEST(ComponentAccessorTest, PackageMigratedMembersArePrivate) {
	EXPECT_TRUE((CaNamesMass<CaPublicProbe>::value));
	EXPECT_FALSE((CaNamesMass<Rigidbody>::value));
	EXPECT_FALSE((CaNamesIsTrigger<BoxCollider>::value));
	EXPECT_FALSE((CaNamesBreakForce<FixedJoint>::value));
	EXPECT_FALSE((CaNamesSwing1Limit<CharacterJoint>::value));
	EXPECT_FALSE((CaNamesClip<Animator>::value));
	EXPECT_FALSE((CaNamesControllerField<AnimatorStateMachine>::value));
}

#endif
