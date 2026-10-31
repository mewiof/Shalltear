#include "TestsPCH.h"

#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>

#ifdef SHALLTEAR_SHARED

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateDirectory

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_DLL = "Physics3D.dll";
	constexpr const char* SCRIPT_MODULE_DLL = "ShalltearPhysicsScriptModule.dll";
	constexpr const char* SPHERE_COLLIDER_TYPE_NAME = "SphereCollider";

	using OverlapCountFn = u32 (*)(f32, f32, f32, f32);
	using RaycastHitsFn = bool (*)(f32, f32, f32, f32, f32, f32);

	const FieldInfo* FindTypeField(const TypeInfo* type, std::string_view fieldName) {
		StringHash wanted(fieldName);
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

	class Physics3DScriptAccessFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		const TypeInfo* _sphereType = nullptr;
		ModuleHandle _package;
		ModuleHandle _scriptModule;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 unique = counter.fetch_add(1);
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_script_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			_package = ModuleSystem::Load(Path(PACKAGE_DLL), ModuleLoadPolicy::InPlace);
			ASSERT_TRUE(ModuleSystem::IsLoaded(_package));

			_scriptModule = ModuleSystem::Load(Path(SCRIPT_MODULE_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_scriptModule));

			_sphereType = TypeRegistry::FindType(StringHash(SPHERE_COLLIDER_TYPE_NAME));
			ASSERT_NE(_sphereType, nullptr);

			_scene = SceneSystem::CreateScene("physics-script-access");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		template <typename Fn>
		Fn ResolveInModule(const char* name) {
			HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(_scriptModule));
			EXPECT_NE(image, nullptr);
			Fn fn = image ? reinterpret_cast<Fn>(GetProcAddress(image, name)) : nullptr;
			EXPECT_NE(fn, nullptr) << "missing export: " << name;
			return fn;
		}

		Handle<SceneObject> MakeSphere(const char* name, const Vec3& position, f32 radius) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			if (!object.IsValid()) {
				return {};
			}
			object->SetActive(false);
			object->SetWorldPosition(position);
			Handle<Component> collider = object->AddComponent(_sphereType);
			if (!collider.Get()) {
				return {};
			}
			WriteField(_sphereType, collider.Get(), "_radius", radius);
			object->SetActive(true);
			return object;
		}
	};

} // anonymous namespace

TEST_F(Physics3DScriptAccessFixture, ModuleThatLinksPackageQueriesHostsWorld) {
	ASSERT_TRUE(MakeSphere("near-a", Vec3{0.0f, 0.0f, 0.0f}, 0.5f).IsValid());
	ASSERT_TRUE(MakeSphere("near-b", Vec3{2.0f, 0.0f, 0.0f}, 0.5f).IsValid());
	ASSERT_TRUE(MakeSphere("far", Vec3{50.0f, 0.0f, 0.0f}, 0.5f).IsValid());

	auto overlapCount = ResolveInModule<OverlapCountFn>("PhysicsScriptModuleOverlapSphereCount");
	ASSERT_NE(overlapCount, nullptr);

	EXPECT_EQ(overlapCount(1.0f, 0.0f, 0.0f, 1.0f), 2u);
	EXPECT_EQ(overlapCount(50.0f, 0.0f, 0.0f, 1.0f), 1u);
}

TEST_F(Physics3DScriptAccessFixture, ModuleSeesHostsGeometryThroughCastToo) {
	ASSERT_TRUE(MakeSphere("target", Vec3{0.0f, 0.0f, 5.0f}, 1.0f).IsValid());

	auto raycastHits = ResolveInModule<RaycastHitsFn>("PhysicsScriptModuleRaycastHits");
	ASSERT_NE(raycastHits, nullptr);

	EXPECT_TRUE(raycastHits(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_FALSE(raycastHits(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f));
}

#endif
