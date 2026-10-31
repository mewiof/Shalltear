#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Scene.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::vector<str> _gateLog;

	class GateScript : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(GateScript, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnAttach() override { _gateLog.push_back("script:attach"); }
		void OnEnable() override { _gateLog.push_back("script:enable"); }
		void OnDisable() override { _gateLog.push_back("script:disable"); }
		void OnDetach() override { _gateLog.push_back("script:detach"); }
		void OnDestroy() override { _gateLog.push_back("script:destroy"); }
	};

	class GateEngineComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(GateEngineComponent, Component)
		SLT_END_REFLECT()

		GateEngineComponent() = default;
	protected:
		void OnAttach() override { _gateLog.push_back("engine:attach"); }
		void OnEnable() override { _gateLog.push_back("engine:enable"); }
		void OnDisable() override { _gateLog.push_back("engine:disable"); }
		void OnDetach() override { _gateLog.push_back("engine:detach"); }
		void OnDestroy() override { _gateLog.push_back("engine:destroy"); }
	};

	SLT_REGISTER_TYPE(GateScript)
	SLT_REGISTER_TYPE(GateEngineComponent)

	std::vector<str> LinesFor(std::string_view kind) {
		std::vector<str> lines;
		for (const str& line : _gateLog) {
			if (line.starts_with(kind)) {
				lines.push_back(line.substr(kind.size() + 1));
			}
		}
		return lines;
	}

	using Lines = std::vector<str>;

	class ScriptHookGateFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		size_t _sceneBytesBaseline = 0;

		void SetUp() override {
			_gateLog.clear();
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_hookgate_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::CollectGarbage();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static void DriveEveryHook(Handle<SceneObject> node, Handle<GateScript> script) {
			script->SetEnabled(false);
			script->SetEnabled(true);
			node->SetActive(false);
			node->SetActive(true);
			SceneSystem::Destroy(node);
			SceneSystem::EndFrame();
		}
	};

} // anonymous namespace

TEST_F(ScriptHookGateFixture, StoppedScriptGetsNoHooks) {
	SceneSystem::SetPlaying(false);
	Handle<Scene> scene = SceneSystem::CreateScene("Stopped");
	Handle<SceneObject> node = SceneSystem::CreateObject("Node", scene);
	Handle<GateScript> script = node->AddComponent<GateScript>();
	node->AddComponent<GateEngineComponent>();

	DriveEveryHook(node, script);

	EXPECT_EQ(LinesFor("script"), Lines{});
	EXPECT_EQ(LinesFor("engine"), (Lines{"attach", "enable", "disable", "enable", "disable", "detach", "destroy"}));
}

TEST_F(ScriptHookGateFixture, WhilePlayingSameSequenceReachesScript) {
	Handle<Scene> scene = SceneSystem::CreateScene("Playing");
	Handle<SceneObject> node = SceneSystem::CreateObject("Node", scene);
	Handle<GateScript> script = node->AddComponent<GateScript>();

	DriveEveryHook(node, script);

	EXPECT_EQ(LinesFor("script"),
			  (Lines{"attach", "enable", "disable", "enable", "disable", "enable", "disable", "detach", "destroy"}));
}

TEST_F(ScriptHookGateFixture, LoadRunsHooksWhenPlaying) {
	Buffer snapshot;
	{
		Handle<Scene> authored = SceneSystem::CreateScene("Authored");
		Handle<SceneObject> node = SceneSystem::CreateObject("Node", authored);
		node->AddComponent<GateScript>();
		node->AddComponent<GateEngineComponent>();
		ASSERT_TRUE(SceneSystem::SaveSceneToMemory(authored, snapshot));
		SceneSystem::UnloadAllScenes();
		SceneSystem::EndFrame();
	}

	_gateLog.clear();
	SceneSystem::SetPlaying(false);
	ASSERT_TRUE(SceneSystem::LoadSceneFromMemory(snapshot).IsValid());
	SceneSystem::UnloadAllScenes();
	SceneSystem::EndFrame();
	EXPECT_EQ(LinesFor("script"), Lines{});
	EXPECT_EQ(LinesFor("engine"), (Lines{"attach", "enable", "disable", "detach", "destroy"}));

	_gateLog.clear();
	SceneSystem::SetPlaying(true);
	ASSERT_TRUE(SceneSystem::LoadSceneFromMemory(snapshot).IsValid());
	SceneSystem::UnloadAllScenes();
	SceneSystem::EndFrame();
	EXPECT_EQ(LinesFor("script"), (Lines{"attach", "enable", "disable", "detach", "destroy"}));
}

TEST_F(ScriptHookGateFixture, StoppedPrefabRunsNoHook) {
	Guid prefabId;
	{
		Handle<Scene> authoring = SceneSystem::CreateScene("Authoring");
		Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", authoring);
		unit->AddComponent<GateScript>();
		unit->AddComponent<GateEngineComponent>();
		prefabId = SceneSystem::SavePrefab(unit, Path("project://gate_unit.prefab"));
		ASSERT_TRUE(prefabId.IsValid());
		SceneSystem::UnloadAllScenes();
		SceneSystem::EndFrame();
	}

	_gateLog.clear();
	SceneSystem::SetPlaying(false);
	Handle<Scene> scene = SceneSystem::CreateScene("Stopped");
	ASSERT_TRUE(SceneSystem::Instantiate(prefabId, scene).IsValid());
	EXPECT_EQ(LinesFor("script"), Lines{});
	EXPECT_EQ(LinesFor("engine"), (Lines{"attach", "enable"}));
}

TEST_F(ScriptHookGateFixture, IsolatedSceneRunsNoHook) {
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<SceneObject> node = SceneSystem::CreateObject("Node", preview);
	Handle<GateScript> script = node->AddComponent<GateScript>();
	node->AddComponent<GateEngineComponent>();

	DriveEveryHook(node, script);

	EXPECT_EQ(LinesFor("script"), Lines{});
	EXPECT_EQ(LinesFor("engine"), (Lines{"attach", "enable", "disable", "enable", "disable", "detach", "destroy"}));
	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
}

TEST_F(ScriptHookGateFixture, TerminateTurnsPlayingBackOn) {
	SceneSystem::SetPlaying(false);
	SceneSystem::Terminate();
	SceneSystem::Initialize();

	Handle<Scene> scene = SceneSystem::CreateScene("Next");
	Handle<SceneObject> node = SceneSystem::CreateObject("Node", scene);
	node->AddComponent<GateScript>();
	EXPECT_EQ(LinesFor("script"), (Lines{"attach", "enable"}));
}
