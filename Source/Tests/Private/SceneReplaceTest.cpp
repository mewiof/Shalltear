#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace Shalltear;

namespace {

	std::vector<std::string>& Events() {
		static std::vector<std::string> events;
		return events;
	}

	class ReplaceProbe : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(ReplaceProbe, Behavior)
		SLT_REFLECT_FIELD(tag)
		SLT_REFLECT_FIELD(replaceOnEnable)
		SLT_REFLECT_FIELD(replaceOnUpdate)
		SLT_REFLECT_FIELD(shared)
		SLT_END_REFLECT()

		str tag;
		Guid replaceOnEnable;
		Guid replaceOnUpdate;
		Handle<TextAsset> shared;
	protected:
		void OnEnable() override {
			Events().push_back("enable:" + std::string(tag));
			if (replaceOnEnable.IsValid()) {
				SceneSystem::ReplaceScenes(replaceOnEnable);
			}
		}
		void OnDisable() override { Events().push_back("disable:" + std::string(tag)); }
		void OnDestroy() override { Events().push_back("destroy:" + std::string(tag)); }
		void OnUpdate() override {
			if (replaceOnUpdate.IsValid()) {
				SceneSystem::ReplaceScenes(replaceOnUpdate);
				replaceOnUpdate = {};
			}
		}
	};

	class SceneReplaceFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_replace_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Scenes");
			std::filesystem::create_directories(_root / "Data");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			Events().clear();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::CollectGarbage();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid SaveScene(const char* name, Guid replaceOnEnable = {}, Handle<TextAsset> shared = {}) {
			const Path path(str("project://Scenes/") + name + ".scene");
			Handle<Scene> scene = SceneSystem::CreateScene(name);
			Handle<ReplaceProbe> probe = SceneSystem::CreateObject("Probe", scene)->AddComponent<ReplaceProbe>();
			probe->tag = name;
			probe->replaceOnEnable = replaceOnEnable;
			probe->shared = shared;
			EXPECT_TRUE(SceneSystem::SaveScene(scene, path));
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			AssetDatabase::ScanProjectDirectory();
			Events().clear();
			const Guid guid = AssetDatabase::LookupGuid(path);
			EXPECT_TRUE(guid.IsValid());
			return guid;
		}

		void Frame() {
			SceneSystem::UpdateAll();
			SceneSystem::EndFrame();
		}

		static std::vector<std::string> LoadedNames() {
			std::vector<std::string> names;
			for (const Handle<Scene>& scene : SceneSystem::GetLoadedScenes()) {
				names.emplace_back(scene->name);
			}
			return names;
		}

		static size_t IndexOf(const std::string& event) {
			const std::vector<std::string>& events = Events();
			for (size_t i = 0; i < events.size(); ++i) {
				if (events[i] == event) {
					return i;
				}
			}
			return events.size();
		}
	};

} // namespace

SLT_REGISTER_TYPE(ReplaceProbe)

TEST_F(SceneReplaceFixture, SwitchLandsAtEndFrame) {
	const Guid a = SaveScene("A");
	const Guid b = SaveScene("B");
	const Guid c = SaveScene("C");
	SceneSystem::LoadSceneByGuid(a);
	SceneSystem::LoadSceneByGuid(b);

	SceneSystem::ReplaceScenes(c);
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"A", "B"}));

	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"C"}));
	ASSERT_TRUE(SceneSystem::GetPrimaryScene().IsValid());
	EXPECT_EQ(SceneSystem::GetPrimaryScene()->name, "C");
}

TEST_F(SceneReplaceFixture, TheSceneToKeepOutlivesTheSwitch) {
	const Guid persistent = SaveScene("Persistent");
	const Guid level1 = SaveScene("Level1");
	const Guid level2 = SaveScene("Level2");
	Handle<Scene> kept = SceneSystem::LoadSceneByGuid(persistent);
	SceneSystem::LoadSceneByGuid(level1);

	SceneSystem::ReplaceScenes(level2, kept);
	Frame();

	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"Persistent", "Level2"}));
	EXPECT_TRUE(kept.IsValid());
	EXPECT_EQ(SceneSystem::GetPrimaryScene()->name, "Level2");
	EXPECT_EQ(IndexOf("disable:Persistent"), Events().size());
}

TEST_F(SceneReplaceFixture, OutgoingDiesBeforeIncoming) {
	const Guid a = SaveScene("A");
	const Guid b = SaveScene("B");
	SceneSystem::LoadSceneByGuid(a);
	Events().clear();

	SceneSystem::ReplaceScenes(b);
	Frame();

	const size_t destroyed = IndexOf("destroy:A");
	const size_t enabled = IndexOf("enable:B");
	ASSERT_LT(destroyed, Events().size());
	ASSERT_LT(enabled, Events().size());
	EXPECT_LT(IndexOf("disable:A"), destroyed);
	EXPECT_LT(destroyed, enabled);
}

TEST_F(SceneReplaceFixture, HookRequestLandsAtEndFrame) {
	const Guid menu = SaveScene("Menu");
	const Guid boot = SaveScene("Boot", menu);

	SceneSystem::LoadSceneByGuid(boot);
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"Boot"}));

	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"Menu"}));
}

TEST_F(SceneReplaceFixture, RequestFromTickLandsInSameFrame) {
	const Guid a = SaveScene("A");
	const Guid b = SaveScene("B");
	Handle<Scene> scene = SceneSystem::LoadSceneByGuid(a);
	scene->GetRoot(0)->GetComponent<ReplaceProbe>()->replaceOnUpdate = b;

	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"B"}));
}

TEST_F(SceneReplaceFixture, IdentityThatIsNotSceneChangesNothing) {
	const Guid a = SaveScene("A");
	SceneSystem::LoadSceneByGuid(a);
	ASSERT_TRUE(FileSystem::WriteTextFile(Path("project://Data/notes.txt"), str("not a scene")));
	Asset* notes = AssetDatabase::LoadAsset(Path("project://Data/notes.txt"));
	ASSERT_NE(notes, nullptr);
	const Guid text = notes->assetId;
	AssetDatabase::UnloadAsset(notes);
	ASSERT_TRUE(text.IsValid());

	SceneSystem::ReplaceScenes(text);
	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"A"}));

	SceneSystem::ReplaceScenes(Guid{0x1234u, 0x5678u});
	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"A"}));
}

TEST_F(SceneReplaceFixture, TheLastRequestInAFrameWins) {
	const Guid a = SaveScene("A");
	const Guid b = SaveScene("B");
	const Guid c = SaveScene("C");
	SceneSystem::LoadSceneByGuid(a);

	SceneSystem::ReplaceScenes(b);
	SceneSystem::ReplaceScenes(c);
	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"C"}));
	EXPECT_EQ(IndexOf("enable:B"), Events().size());
}

TEST_F(SceneReplaceFixture, CancellingLoadsDropsARequestedSwitch) {
	const Guid a = SaveScene("A");
	const Guid b = SaveScene("B");
	SceneSystem::LoadSceneByGuid(a);

	SceneSystem::ReplaceScenes(b);
	SceneSystem::CancelAsyncLoads();
	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"A"}));
}

TEST_F(SceneReplaceFixture, SharedAssetRevived) {
	ASSERT_TRUE(FileSystem::WriteTextFile(Path("project://Data/shared.txt"), str("both levels")));
	AssetDatabase::ScanProjectDirectory();
	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Data/shared.txt"));
	ASSERT_NE(loaded, nullptr);
	Handle<TextAsset> shared(static_cast<TextAsset*>(loaded));
	const Guid a = SaveScene("A", {}, shared);
	const Guid b = SaveScene("B", {}, shared);
	AssetDatabase::UnloadAsset(loaded);
	AssetDatabase::CollectGarbage();

	Handle<Scene> first = SceneSystem::LoadSceneByGuid(a);
	TextAsset* before = first->GetRoot(0)->GetComponent<ReplaceProbe>()->shared.Get();
	ASSERT_NE(before, nullptr);
	const u64 beforeId = before->GetInstanceId();

	SceneSystem::ReplaceScenes(b);
	Frame();

	ASSERT_EQ(LoadedNames(), (std::vector<std::string>{"B"}));
	TextAsset* after = SceneSystem::GetLoadedScenes()[0]->GetRoot(0)->GetComponent<ReplaceProbe>()->shared.Get();
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->GetInstanceId(), beforeId);
}

TEST_F(SceneReplaceFixture, SwitchBuildsSavedScene) {
	const Guid menu = SaveScene("Menu");
	const Guid level = SaveScene("Level");
	Asset* held = AssetDatabase::LoadAssetByGuid(menu);
	ASSERT_NE(held, nullptr);

	Handle<Scene> editing = SceneSystem::LoadScene(Path("project://Scenes/Menu.scene"));
	ASSERT_TRUE(editing.IsValid());
	SceneSystem::CreateObject("Added", editing);
	ASSERT_TRUE(SceneSystem::SaveScene(editing, Path("project://Scenes/Menu.scene")));
	SceneSystem::UnloadScene(editing);
	SceneSystem::EndFrame();

	SceneSystem::LoadSceneByGuid(level);
	SceneSystem::ReplaceScenes(menu);
	Frame();

	ASSERT_EQ(LoadedNames(), (std::vector<std::string>{"Menu"}));
	EXPECT_TRUE(SceneSystem::GetLoadedScenes()[0]->FindObjectByName("Added").IsValid());
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(menu), held);
	AssetDatabase::UnloadAsset(held);
}

TEST_F(SceneReplaceFixture, SaveAsGivesCopyIdentityOfOwn) {
	const Guid level1 = SaveScene("Level1");
	Handle<Scene> editing = SceneSystem::LoadScene(Path("project://Scenes/Level1.scene"));
	ASSERT_TRUE(SceneSystem::SaveScene(editing, Path("project://Scenes/Level2.scene")));
	SceneSystem::UnloadScene(editing);
	SceneSystem::EndFrame();

	const Guid level2 = AssetDatabase::LookupGuid(Path("project://Scenes/Level2.scene"));
	ASSERT_TRUE(level2.IsValid());
	EXPECT_NE(level2, level1);
	EXPECT_EQ(AssetDatabase::LookupPath(level1).ToString(), "project://Scenes/Level1.scene");
	EXPECT_EQ(AssetDatabase::LookupPath(level2).ToString(), "project://Scenes/Level2.scene");

	SceneSystem::ReplaceScenes(level1);
	Frame();
	EXPECT_EQ(LoadedNames(), (std::vector<std::string>{"Level1"}));
}

TEST_F(SceneReplaceFixture, DeletedFileSavesOldIdentity) {
	const Guid level = SaveScene("Level");
	Handle<Scene> editing = SceneSystem::LoadScene(Path("project://Scenes/Level.scene"));
	std::error_code ec;
	std::filesystem::remove(_root / "Scenes" / "Level.scene", ec);
	std::filesystem::remove(_root / "Scenes" / "Level.scene.meta", ec);
	AssetDatabase::RebuildDatabase();
	ASSERT_FALSE(AssetDatabase::LookupGuid(Path("project://Scenes/Level.scene")).IsValid());

	ASSERT_TRUE(SceneSystem::SaveScene(editing, Path("project://Scenes/Level.scene")));
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Scenes/Level.scene")), level);
}
