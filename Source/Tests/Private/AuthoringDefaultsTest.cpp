#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/AuthoringDefaults.h"
#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class AuthoringBaseComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(AuthoringBaseComponent, Component)
		SLT_REFLECT_FIELD(speed)
		SLT_END_REFLECT()

		AuthoringBaseComponent() = default;

		f32 speed = 1.0f;
	};

	class AuthoringDerivedComponent : public AuthoringBaseComponent {
	public:
		SLT_REFLECT_COMPONENT(AuthoringDerivedComponent, AuthoringBaseComponent)
		SLT_REFLECT_FIELD(range)
		SLT_END_REFLECT()

		AuthoringDerivedComponent() = default;

		f32 range = 10.0f;
	};

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class AuthoringDefaultsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_authoring_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			const std::filesystem::path engineAssets = RepoDir("EngineAssets");
			ASSERT_FALSE(engineAssets.empty());
			FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			TypeRegistry::InitializePrimitives();
			AuthoringBaseComponent::GetReflectType();
			AuthoringDerivedComponent::GetReflectType();
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			_scene = SceneSystem::CreateScene("Authoring");
		}

		void TearDown() override {
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			AuthoringDefaults::Shutdown();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static MaterialAsset* DefaultSky() {
			return static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("engine://Materials/DefaultSky.mat")));
		}

		static void ExpectDefaultSky(const Environment& environment, const char* what) {
			MaterialAsset* expected = DefaultSky();
			ASSERT_NE(expected, nullptr);
			EXPECT_EQ(environment.GetSkyMode(), SkyMode::Material) << what;
			EXPECT_EQ(environment.GetSkyMaterial().Get(), expected) << what << ": not DefaultSky";
		}

		static void ExpectGradient(const Environment& environment, const char* what) {
			EXPECT_EQ(environment.GetSkyMode(), SkyMode::Gradient) << what;
			EXPECT_FALSE(environment.GetSkyMaterial().IsValid()) << what;
		}

		template <typename T>
		static T* Find(Handle<SceneObject> object) {
			return object.IsValid() ? object->GetComponent<T>().Get() : nullptr;
		}
	};

	int _moduleA = 0;
	int _moduleB = 0;

} // anonymous namespace

TEST_F(AuthoringDefaultsFixture, EditorAddDrawsDefaultSky) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Sky", _scene);
	auto add = MakeUnique<AddComponentCommand>(object, Environment::GetReflectType()->name);
	AddComponentCommand* raw = add.get();
	UndoSystem::Push(std::move(add));
	Environment* added = static_cast<Environment*>(raw->GetCreated().Get());
	ASSERT_NE(added, nullptr);
	ExpectDefaultSky(*added, "the editor's add");
	EXPECT_EQ(added->GetAmbientMode(), AmbientMode::Sky);

	ASSERT_TRUE(UndoSystem::Undo());
	EXPECT_EQ(Find<Environment>(object), nullptr);
	ASSERT_TRUE(UndoSystem::Redo());
	ASSERT_NE(Find<Environment>(object), nullptr);
	ExpectDefaultSky(*Find<Environment>(object), "the redone add");

	Handle<SceneObject> scripted = SceneSystem::CreateObject("Scripted", _scene);
	Handle<Environment> byScript = scripted->AddComponent<Environment>();
	ASSERT_TRUE(byScript.IsValid());
	ExpectGradient(*byScript.Get(), "a script's AddComponent ran the editor's defaults");
}

TEST_F(AuthoringDefaultsFixture, ResetRestoresSkyInOneStep) {
	Handle<SceneObject> object = SceneSystem::CreateObject("Sky", _scene);
	Handle<Environment> environment = object->AddComponent<Environment>();
	ASSERT_TRUE(environment.IsValid());
	environment->SetSkyIntensity(3.0f);
	const EditTarget target(environment);

	EXPECT_TRUE(EditorSceneOps::DiffersFromDefault(target, StringHash("_skyMaterial"), {}));
	EXPECT_GT(EditorSceneOps::ResetComponentToDefaults(target), 0u);
	ExpectDefaultSky(*environment.Get(), "the component's Reset");
	EXPECT_FLOAT_EQ(environment->GetSkyIntensity(), 1.0f);
	EXPECT_FALSE(EditorSceneOps::DiffersFromDefault(target, StringHash("_skyMaterial"), {}));

	ASSERT_TRUE(UndoSystem::Undo());
	ExpectGradient(*environment.Get(), "the undone Reset");
	EXPECT_FLOAT_EQ(environment->GetSkyIntensity(), 3.0f);
	EXPECT_FALSE(UndoSystem::Undo());

	ASSERT_TRUE(EditorSceneOps::ResetToDefault(target, StringHash("_skyMaterial"), {}, "Reset Sky Material"));
	EXPECT_EQ(environment->GetSkyMaterial().Get(), DefaultSky());
}

TEST_F(AuthoringDefaultsFixture, NewEnvironmentDrawsDefaultSky) {
	Vector<UI::Widgets::MenuItem> menu{GetMemoryResource(MemTag::Editor)};
	EditorSceneOps::BuildCreateMenu(_scene, {}, menu);
	const UI::Widgets::MenuItem* entry = nullptr;
	for (const UI::Widgets::MenuItem& item : menu) {
		entry = item.label == "Environment" ? &item : entry;
	}
	ASSERT_NE(entry, nullptr);
	entry->onSelect();
	Handle<SceneObject> created;
	for (u32 i = 0; i < _scene->GetRootCount(); ++i) {
		created = Find<Environment>(_scene->GetRoot(i)) ? _scene->GetRoot(i) : created;
	}
	ASSERT_NE(Find<Environment>(created), nullptr);
	ExpectDefaultSky(*Find<Environment>(created), "Create > Environment");

	Handle<Scene> fresh = SceneSystem::CreateScene("Fresh");
	EditorSceneOps::PopulateNewScene(fresh);
	u32 cameras = 0;
	u32 lights = 0;
	u32 environments = 0;
	for (u32 i = 0; i < fresh->GetRootCount(); ++i) {
		Handle<SceneObject> root = fresh->GetRoot(i);
		cameras += Find<Camera>(root) ? 1u : 0u;
		lights += Find<Light>(root) ? 1u : 0u;
		if (Environment* environment = Find<Environment>(root)) {
			++environments;
			EXPECT_TRUE(environment->GetIsGlobal());
			ExpectDefaultSky(*environment, "a new scene's Environment");
		}
	}
	EXPECT_EQ(cameras, 1u);
	EXPECT_EQ(lights, 1u);
	EXPECT_EQ(environments, 1u);
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Create Environment");
}

TEST_F(AuthoringDefaultsFixture, ModuleDefaultRestoresOverride) {
	const StringHash environmentType = Environment::GetReflectType()->name;
	ASSERT_TRUE(AuthoringDefaults::Register(environmentType, StringHash("_skyIntensity"), "2.5", &_moduleA));
	ASSERT_TRUE(AuthoringDefaults::Register(environmentType, StringHash("_skyMode"), R"("Color")", &_moduleB));

	auto addOne = [this]() {
		Handle<SceneObject> object = SceneSystem::CreateObject("Sky", _scene);
		auto add = MakeUnique<AddComponentCommand>(object, Environment::GetReflectType()->name);
		AddComponentCommand* raw = add.get();
		UndoSystem::Push(std::move(add));
		return static_cast<Environment*>(raw->GetCreated().Get());
	};
	Environment* both = addOne();
	ASSERT_NE(both, nullptr);
	EXPECT_FLOAT_EQ(both->GetSkyIntensity(), 2.5f);
	EXPECT_EQ(both->GetSkyMode(), SkyMode::Color);
	EXPECT_EQ(both->GetSkyMaterial().Get(), DefaultSky());

	EXPECT_EQ(AuthoringDefaults::UnregisterOwnedBy(&_moduleA), 1u);
	EXPECT_EQ(AuthoringDefaults::UnregisterOwnedBy(nullptr), 0u);
	Environment* withoutA = addOne();
	ASSERT_NE(withoutA, nullptr);
	EXPECT_FLOAT_EQ(withoutA->GetSkyIntensity(), 1.0f);
	EXPECT_EQ(withoutA->GetSkyMode(), SkyMode::Color);

	EXPECT_EQ(AuthoringDefaults::UnregisterOwnedBy(&_moduleB), 1u);
	ExpectDefaultSky(*addOne(), "the built-in, given back");
}

TEST_F(AuthoringDefaultsFixture, RedoAfterModuleLeftRebuildsSameComponent) {
	ASSERT_TRUE(AuthoringDefaults::Register(Environment::GetReflectType()->name, StringHash("_skyIntensity"), "4", &_moduleA));
	Handle<SceneObject> object = SceneSystem::CreateObject("Sky", _scene);
	UndoSystem::Push(MakeUnique<AddComponentCommand>(object, Environment::GetReflectType()->name));
	ASSERT_NE(Find<Environment>(object), nullptr);
	EXPECT_FLOAT_EQ(Find<Environment>(object)->GetSkyIntensity(), 4.0f);

	ASSERT_TRUE(UndoSystem::Undo());
	AuthoringDefaults::UnregisterOwnedBy(&_moduleA);
	ASSERT_TRUE(UndoSystem::Redo());
	ASSERT_NE(Find<Environment>(object), nullptr);
	EXPECT_FLOAT_EQ(Find<Environment>(object)->GetSkyIntensity(), 4.0f);
}

TEST_F(AuthoringDefaultsFixture, NearestBaseDefaultWins) {
	const StringHash base = AuthoringBaseComponent::GetReflectType()->name;
	const StringHash derived = AuthoringDerivedComponent::GetReflectType()->name;
	ASSERT_TRUE(AuthoringDefaults::Register(base, StringHash("speed"), "3", &_moduleA));
	ASSERT_TRUE(AuthoringDefaults::Register(derived, StringHash("range"), "25", &_moduleA));

	Handle<SceneObject> object = SceneSystem::CreateObject("Derived", _scene);
	UndoSystem::Push(MakeUnique<AddComponentCommand>(object, derived));
	AuthoringDerivedComponent* component = Find<AuthoringDerivedComponent>(object);
	ASSERT_NE(component, nullptr);
	EXPECT_FLOAT_EQ(component->speed, 3.0f);
	EXPECT_FLOAT_EQ(component->range, 25.0f);

	ASSERT_TRUE(AuthoringDefaults::Register(derived, StringHash("speed"), "7", &_moduleB));
	Handle<SceneObject> second = SceneSystem::CreateObject("Derived", _scene);
	UndoSystem::Push(MakeUnique<AddComponentCommand>(second, derived));
	ASSERT_NE(Find<AuthoringDerivedComponent>(second), nullptr);
	EXPECT_FLOAT_EQ(Find<AuthoringDerivedComponent>(second)->speed, 7.0f);

	Handle<SceneObject> plain = SceneSystem::CreateObject("Base", _scene);
	UndoSystem::Push(MakeUnique<AddComponentCommand>(plain, base));
	ASSERT_NE(Find<AuthoringBaseComponent>(plain), nullptr);
	EXPECT_FLOAT_EQ(Find<AuthoringBaseComponent>(plain)->speed, 3.0f);

	AuthoringDefaults::UnregisterOwnedBy(&_moduleA);
	AuthoringDefaults::UnregisterOwnedBy(&_moduleB);
}

TEST_F(AuthoringDefaultsFixture, UnwritableDefaultRefused) {
	const StringHash environmentType = Environment::GetReflectType()->name;
	EXPECT_FALSE(AuthoringDefaults::Register(environmentType, StringHash("_noSuchField"), "1", &_moduleA));
	EXPECT_FALSE(AuthoringDefaults::Register(environmentType, StringHash("_skyIntensity"), "{not json", &_moduleA));
	EXPECT_FALSE(AuthoringDefaults::Register(environmentType, StringHash("_skyIntensity"), "", &_moduleA));
	EXPECT_FALSE(AuthoringDefaults::Register(StringHash("NoSuchComponent"), StringHash("_skyIntensity"), "1", &_moduleA));
	EXPECT_EQ(AuthoringDefaults::UnregisterOwnedBy(&_moduleA), 0u);
	EXPECT_TRUE(AuthoringDefaults::StateFor(Camera::GetReflectType()).empty());
}
