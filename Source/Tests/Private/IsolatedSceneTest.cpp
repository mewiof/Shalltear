#include "TestsPCH.h"

#include "Checkout.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Shalltear;

namespace {

	std::vector<str> _isoLog;

	class IsoMarkComponent : public Component {
	public:
		i32 marker = 0;

		SLT_REFLECT_COMPONENT(IsoMarkComponent, Component)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()

		IsoMarkComponent() = default;
	protected:
		void OnAttach() override { _isoLog.push_back("attach:" + std::to_string(marker)); }
		void OnEnable() override { _isoLog.push_back("enable:" + std::to_string(marker)); }
		void OnDisable() override { _isoLog.push_back("disable:" + std::to_string(marker)); }
	};

	class IsoTickBehavior : public Behavior {
	public:
		i32 updates = 0;

		SLT_REFLECT_COMPONENT(IsoTickBehavior, Behavior)
		SLT_REFLECT_FIELD(updates)
		SLT_END_REFLECT()
	protected:
		void OnUpdate() override { ++updates; }
	};

	SLT_REGISTER_TYPE(IsoMarkComponent)
	SLT_REGISTER_TYPE(IsoTickBehavior)

	class IsolatedSceneFixture : public ::testing::Test {
	protected:
		size_t _sceneBytesBaseline = 0;

		void SetUp() override {
			_isoLog.clear();
			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
		}
	};

	class IsoSceneListener : public Object {
	public:
		u32 loaded = 0;
		u32 unloading = 0;
		u32 unloaded = 0;

		void OnLoaded(const SceneLoadedEvent&) { ++loaded; }
		void OnUnloading(const SceneUnloadingEvent&) { ++unloading; }
		void OnUnloaded(const SceneUnloadedEvent&) { ++unloaded; }
	};

} // anonymous namespace

TEST_F(IsolatedSceneFixture, IsolatedSceneNotListed) {
	Handle<Scene> world = SceneSystem::CreateScene("World");
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");

	ASSERT_TRUE(preview.IsValid());
	EXPECT_EQ(preview->GetKind(), SceneKind::Isolated);
	EXPECT_EQ(world->GetKind(), SceneKind::World);
	EXPECT_EQ(preview->GetState(), SceneState::Loaded);

	auto loaded = SceneSystem::GetLoadedScenes();
	ASSERT_EQ(loaded.size(), 1u);
	EXPECT_EQ(loaded[0].Get(), world.Get());
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
}

TEST_F(IsolatedSceneFixture, WorldListKeepsOrder) {
	Handle<Scene> first = SceneSystem::CreateScene("First");
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<Scene> second = SceneSystem::CreateScene("Second");

	auto loaded = SceneSystem::GetLoadedScenes();
	ASSERT_EQ(loaded.size(), 2u);
	EXPECT_EQ(loaded[0].Get(), first.Get());
	EXPECT_EQ(loaded[1].Get(), second.Get());

	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
	loaded = SceneSystem::GetLoadedScenes();
	ASSERT_EQ(loaded.size(), 2u);
	EXPECT_EQ(loaded[0].Get(), first.Get());
	EXPECT_EQ(loaded[1].Get(), second.Get());
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	SceneSystem::UnloadScene(second);
	SceneSystem::UnloadScene(first);
	SceneSystem::EndFrame();
}

TEST_F(IsolatedSceneFixture, IsolatedCountTracksLifetime) {
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	Handle<Scene> first = SceneSystem::CreateIsolatedScene("First");
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);
	Handle<Scene> second = SceneSystem::CreateIsolatedScene("Second");
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);

	Handle<Scene> world = SceneSystem::CreateScene("World");
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);
	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 1u);

	SceneSystem::UnloadScene(first);
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	SceneSystem::UnloadScene(second);
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);

	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
}

TEST_F(IsolatedSceneFixture, IsolatedSceneRunsOrdinaryObjectLifecycle) {
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<SceneObject> object = SceneSystem::CreateObject("Sphere", preview);
	ASSERT_TRUE(object.IsValid());
	EXPECT_EQ(object->GetScene().Get(), preview.Get());

	Handle<IsoMarkComponent> mark = object->AddComponent<IsoMarkComponent>();
	ASSERT_TRUE(mark.IsValid());
	mark->marker = 1;
	EXPECT_TRUE(mark->IsEffectivelyEnabled());
	EXPECT_EQ(preview->GetObjectCount(), 1u);
	EXPECT_EQ(preview->GetRootCount(), 1u);

	ASSERT_EQ(_isoLog.size(), 2u);
	EXPECT_EQ(_isoLog[0], "attach:0");
	EXPECT_EQ(_isoLog[1], "enable:0");

	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
	EXPECT_EQ(_isoLog.back(), "disable:1");
}

TEST_F(IsolatedSceneFixture, IsolatedSceneNeverPrimary) {
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	EXPECT_FALSE(SceneSystem::GetPrimaryScene().IsValid());

	Handle<Scene> world = SceneSystem::CreateScene("World");
	EXPECT_EQ(SceneSystem::GetPrimaryScene().Get(), world.Get());

	SceneSystem::SetPrimaryScene(preview);
	EXPECT_EQ(SceneSystem::GetPrimaryScene().Get(), world.Get());

	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
	EXPECT_FALSE(SceneSystem::GetPrimaryScene().IsValid());

	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
}

TEST_F(IsolatedSceneFixture, IsolatedScenesLifecycleIsAnnouncedToNobody) {
	EventBus& bus = EventBus::GetGlobal();
	IsoSceneListener listener;
	Handle<IsoSceneListener> self(&listener);
	SceneEvents::Loaded.Subscribe(bus, self, &IsoSceneListener::OnLoaded);
	SceneEvents::Unloading.Subscribe(bus, self, &IsoSceneListener::OnUnloading);
	SceneEvents::Unloaded.Subscribe(bus, self, &IsoSceneListener::OnUnloaded);

	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
	EXPECT_EQ(listener.loaded, 0u);
	EXPECT_EQ(listener.unloading, 0u);
	EXPECT_EQ(listener.unloaded, 0u);

	Handle<Scene> world = SceneSystem::CreateScene("World");
	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
	EXPECT_EQ(listener.loaded, 1u);
	EXPECT_EQ(listener.unloading, 1u);
	EXPECT_EQ(listener.unloaded, 1u);

	bus.UnsubscribeAll(listener.GetInstanceId());
}

TEST_F(IsolatedSceneFixture, TerminateCollectsAnIsolatedSceneNobodyDestroyed) {
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Leaked");
	Handle<SceneObject> object = SceneSystem::CreateObject("Sphere", preview);
	object->AddComponent<IsoMarkComponent>();
	ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 1u);

	SceneSystem::Terminate();
	EXPECT_FALSE(preview.IsValid());
	EXPECT_FALSE(object.IsValid());
	SceneSystem::Initialize();
	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 0u);
}

TEST_F(IsolatedSceneFixture, IsolatedSceneNeverTicks) {
	Handle<Scene> world = SceneSystem::CreateScene("World");
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");

	Handle<IsoTickBehavior> worldTicker =
		SceneSystem::CreateObject("W", world)->AddComponent<IsoTickBehavior>();
	Handle<IsoTickBehavior> previewTicker =
		SceneSystem::CreateObject("P", preview)->AddComponent<IsoTickBehavior>();
	ASSERT_TRUE(worldTicker.IsValid());
	ASSERT_TRUE(previewTicker.IsValid());

	for (int frame = 0; frame < 3; ++frame) {
		SceneSystem::UpdateAll();
		SceneSystem::EndFrame();
	}

	EXPECT_EQ(worldTicker->updates, 3);
	EXPECT_EQ(previewTicker->updates, 0);

	SceneSystem::UnloadScene(preview);
	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
}

TEST_F(IsolatedSceneFixture, IsolatedTickListCompacts) {
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<SceneObject> host = SceneSystem::CreateObject("Host", preview);

	auto cycle = [&] {
		Handle<IsoTickBehavior> behavior = host->AddComponent<IsoTickBehavior>();
		SceneSystem::Destroy(Handle<Component>(behavior.Get()));
		SceneSystem::EndFrame();
	};

	for (int i = 0; i < 16; ++i) {
		cycle();
	}
	size_t settled = MemoryManager::GetBytesForTag(MemTag::Scene);
	for (int i = 0; i < 200; ++i) {
		cycle();
	}
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), settled);

	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
}

namespace {

	constexpr const char* ISO_V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* ISO_V2_DLL = "ShalltearHotReloadModuleV2.dll";
	const StringHash IsoBehaviorType{"HotReloadBehavior"};

	class IsolatedSceneReloadFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		size_t _sceneBytesBaseline = 0;
		ModuleHandle _module;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_isoreload_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path(ISO_V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			AssetDatabase::CollectGarbage();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		const TypeInfo* BehaviorType() { return TypeRegistry::FindType(IsoBehaviorType); }

		Handle<Component> AddModuleBehavior(Handle<SceneObject> owner) {
			const TypeInfo* type = BehaviorType();
			EXPECT_NE(type, nullptr);
			return owner->AddComponent(type);
		}

		void SetCounter(Handle<Component> component, i32 value) {
			Variant boxed{value};
			ASSERT_TRUE(SetField(component.Get(), BehaviorType(), StringHash("counter"), &boxed));
		}

		i32 GetCounter(Handle<Component> component) {
			Variant out;
			EXPECT_TRUE(GetField(component.Get(), BehaviorType(), StringHash("counter"), &out));
			const i32* value = out.TryGet<i32>();
			return value ? *value : -1;
		}
	};

} // anonymous namespace

TEST_F(IsolatedSceneReloadFixture, IsolatedComponentsSurviveReloads) {
	Handle<Scene> world = SceneSystem::CreateScene("World");
	Handle<Scene> firstPreview = SceneSystem::CreateIsolatedScene("First");
	Handle<Scene> secondPreview = SceneSystem::CreateIsolatedScene("Second");
	ASSERT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);

	Handle<SceneObject> worldObject = SceneSystem::CreateObject("W", world);
	Handle<SceneObject> firstObject = SceneSystem::CreateObject("A", firstPreview);
	Handle<SceneObject> secondObject = SceneSystem::CreateObject("B", secondPreview);
	Handle<Component> worldComponent = AddModuleBehavior(worldObject);
	Handle<Component> firstComponent = AddModuleBehavior(firstObject);
	Handle<Component> secondComponent = AddModuleBehavior(secondObject);

	SetCounter(worldComponent, 100);
	SetCounter(firstComponent, 11);
	SetCounter(secondComponent, 22);

	Path v2{ISO_V2_DLL};
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v2));
	Path v1{ISO_V1_DLL};
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v1));

	ASSERT_TRUE(firstComponent.IsValid());
	ASSERT_TRUE(secondComponent.IsValid());
	ASSERT_TRUE(worldComponent.IsValid());
	EXPECT_EQ(GetCounter(firstComponent), 11);
	EXPECT_EQ(GetCounter(secondComponent), 22);
	EXPECT_EQ(GetCounter(worldComponent), 100);

	EXPECT_EQ(SceneSystem::GetIsolatedSceneCount(), 2u);
	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 1u);
	EXPECT_EQ(firstPreview->GetKind(), SceneKind::Isolated);

	SceneSystem::UpdateAll();
	SceneSystem::EndFrame();
	EXPECT_EQ(GetCounter(worldComponent), 101);
	EXPECT_EQ(GetCounter(firstComponent), 11);
	EXPECT_EQ(GetCounter(secondComponent), 22);

	SceneSystem::UnloadScene(secondPreview);
	SceneSystem::UnloadScene(firstPreview);
	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
}

namespace {

	struct AllScenesWalker {
		const char* path;
		u32 count;
		const char* why;
	};

	const AllScenesWalker allScenesWalkers[] = {
		{"Source/Core/Private/Scene/SceneInternal.h", 1, "declaration"},
		{"Source/Core/Private/Scene/SceneSystem.cpp", 6,
		 "owner"},
		{"Source/Core/Private/Scene/SceneMigration.cpp", 3,
		 "survives module reload"},
		{"Source/Core/Private/Scene/PrefabCompose.cpp", 3,
		 "re-merge on prefab change"},
	};

	u32 CountAllScenesUses(const std::filesystem::path& file) {
		std::ifstream stream(file);
		str line;
		u32 count = 0;
		while (std::getline(stream, line)) {
			size_t comment = line.find("//");
			if (comment != str::npos) {
				line.resize(comment);
			}
			size_t at = 0;
			while ((at = line.find("allScenes", at)) != str::npos) {
				++count;
				at += 9;
			}
		}
		return count;
	}

} // anonymous namespace

TEST(IsolatedSceneSourceGate, CoreInternalAllScenesWalkersAreEnumerated) {
	std::filesystem::path root = Checkout::Root();
	ASSERT_FALSE(root.empty()) << std::filesystem::current_path().string();
	std::filesystem::path coreDir = root / "Source/Core";
	ASSERT_TRUE(std::filesystem::exists(coreDir)) << root.string();

	Vector<str> foundPaths{GetMemoryResource(MemTag::Core)};
	Vector<u32> foundCounts{GetMemoryResource(MemTag::Core)};
	u32 scanned = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(coreDir, ec)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		str extension = entry.path().extension().string();
		if (extension != ".cpp" && extension != ".h") {
			continue;
		}
		++scanned;
		u32 uses = CountAllScenesUses(entry.path());
		if (uses > 0) {
			foundPaths.push_back(std::filesystem::relative(entry.path(), root, ec).generic_string());
			foundCounts.push_back(uses);
		}
	}
	EXPECT_GT(scanned, 100u);

	for (size_t i = 0; i < foundPaths.size(); ++i) {
		const AllScenesWalker* known = nullptr;
		for (const AllScenesWalker& walker : allScenesWalkers) {
			if (foundPaths[i] == walker.path) {
				known = &walker;
			}
		}
		ASSERT_NE(known, nullptr);
		EXPECT_EQ(foundCounts[i], known->count);
	}

	for (const AllScenesWalker& walker : allScenesWalkers) {
		bool present = false;
		for (const str& path : foundPaths) {
			present = present || path == walker.path;
		}
		EXPECT_TRUE(present);
	}
}
