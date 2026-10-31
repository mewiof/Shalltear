#include "TestsPCH.h"

#include "CookHarness.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/BuildTarget.h"

#include "Asset/PakReader.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	class DocumentsTarget : public IBuildTarget {
	public:
		std::string_view GetId() const override { return "Docs"; }
		std::string_view GetDisplayName() const override { return "Docs"; }
		bool IsAvailable(str&) const override { return true; }
		void DescribeSettings(Vector<BuildSettingDesc>&) const override {}
		void Validate(const str&, const ResolvedProfile&, Vector<BuildIssue>&) const override {}
		void ConfigurePipeline(BuildContext& context) const override {
			context.entrySource = "PlayerMain.cpp";
			context.sdkRoot = sdkRoot;
		}
		str GetLaunchCommand(const BuildContext&) const override { return {}; }

		str sdkRoot;
	};

	class BuildDocumentsFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			static std::atomic<u64> counter{0};
			_base = fs::temp_directory_path() / ("shalltear_builddocs_" + std::to_string(counter.fetch_add(1)));
			_root = _base / "MyGame";
			std::error_code ec;
			fs::remove_all(_base, ec);
			fs::create_directories(_root / "Assets" / "Scenes", ec);
			fs::create_directories(_base / "engine", ec);
			fs::create_directories(_base / "Sdk", ec);
			WriteFile(_root / "Project.ini", "[Project]\nname = MyGame\n");
			CookHarness::WriteSyntheticEngineRoots(_base / "engine");

			FileSystem::Mount("project", Path(_root.generic_string()));
			FileSystem::Mount("engine", Path((_base / "engine").generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();

			Handle<Scene> scene = SceneSystem::CreateScene("Main");
			ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Assets/Scenes/Main.scene"), false));
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			AssetDatabase::ScanProjectDirectory();
			_sceneGuid = AssetDatabase::LookupGuid(Path("project://Assets/Scenes/Main.scene"));
			ASSERT_TRUE(_sceneGuid.IsValid());

			_target.sdkRoot = (_base / "Sdk").generic_string();
			BuildTargetRegistry::Register(&_target);
			BuildStepRegistry::Register("validate", BuildPhase::Validate,
										[](BuildContext&) { return BuildStepStatus::Succeeded; });
			for (const char* name : {"configure", "compile"}) {
				BuildStepRegistry::Register(name, BuildPhase::Compile, [](BuildContext&) { return BuildStepStatus::Succeeded; });
			}
		}

		void TearDown() override {
			BuildStepRegistry::Shutdown();
			BuildTargetRegistry::Shutdown();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			fs::remove_all(_base, ec);
		}

		static void WriteFile(const fs::path& path, const str& text) {
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << text;
		}

		static str ReadFile(const fs::path& path) {
			std::ifstream file(path, std::ios::binary);
			return str(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		}

		void WriteProfile(std::initializer_list<std::pair<const char*, const char*>> settings = {}) const {
			BuildProfile profile;
			profile.name = "Demo";
			profile.target = "Docs";
			Vector<str> scenes;
			scenes.push_back("Assets/Scenes/Main.scene");
			profile.SetList("scenes", scenes);
			for (const auto& [key, value] : settings) {
				profile.SetString(key, value);
			}
			ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(_root.generic_string(), "Demo"), profile));
		}

		void Build(BuildPipeline& pipeline) const {
			BuildRequest request;
			request.projectPath = _root.generic_string();
			request.profiles.push_back("Demo");
			str refusal;
			ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
			for (int i = 0; i < 5000 && pipeline.IsBusy(); ++i) {
				pipeline.PumpMainThread();
			}
			ASSERT_FALSE(pipeline.IsBusy());
		}

		static str Issues(const BuildPipeline& pipeline) {
			str out;
			for (const BuildProfileResult& profile : pipeline.GetResult().profiles) {
				for (const BuildIssue& issue : profile.issues) {
					out += issue.message + "\n";
				}
			}
			return out;
		}

		fs::path Staging() const { return _root / "Builds" / "Demo"; }
		fs::path Package() const { return Staging() / "Data" / "Content.slpak"; }

		fs::path _base;
		fs::path _root;
		Guid _sceneGuid;
		DocumentsTarget _target;
	};

} // anonymous namespace

TEST_F(BuildDocumentsFixture, BuildPacksManifestAndSettings) {
	const str settings = "[Graphics]\nvsync = On\nadapter = 2\n";
	WriteFile(_root / "ProjectSettings.ini", settings);
	WriteProfile({{"version", "1.2.3"}});

	WriteFile(Staging() / "Boot.json", "{\"formatVersion\": 1}\n");
	WriteFile(Staging() / "ProjectSettings.ini", "[Graphics]\nadapter = 7\n");

	BuildPipeline pipeline;
	Build(pipeline);
	ASSERT_EQ(pipeline.GetState(), BuildState::Succeeded) << Issues(pipeline);

	EXPECT_FALSE(fs::exists(Staging() / "Boot.json"));
	EXPECT_FALSE(fs::exists(Staging() / "ProjectSettings.ini"));

	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(Path(Package().generic_string()), error)) << error;
	EXPECT_EQ(reader.GetDocument(PakDocumentKind::ProjectSettings), settings);

	const str manifest(reader.GetDocument(PakDocumentKind::BootManifest));
	ASSERT_FALSE(manifest.empty());
	EXPECT_NE(manifest.find("\"formatVersion\": 2"), str::npos) << manifest;
	EXPECT_NE(manifest.find("\"productName\": \"MyGame\""), str::npos) << manifest;
	EXPECT_NE(manifest.find(_sceneGuid.ToString()), str::npos) << manifest;

	for (std::string_view stamp : {"buildNumber", "buildTime", "\"version\"", "1.2.3"}) {
		EXPECT_EQ(manifest.find(stamp), str::npos) << stamp << "': " << manifest;
	}
}

TEST_F(BuildDocumentsFixture, StampOnlyBuildsCookSame) {
	WriteFile(_root / "ProjectSettings.ini", "[Graphics]\nvsync = Off\n");
	WriteProfile({{"buildNumber", "${AUTO}"}, {"version", "1.2.3"}});

	BuildPipeline first;
	Build(first);
	ASSERT_EQ(first.GetState(), BuildState::Succeeded) << Issues(first);
	const str firstPackage = ReadFile(Package());

	BuildPipeline second;
	Build(second);
	ASSERT_EQ(second.GetState(), BuildState::Succeeded) << Issues(second);
	const str secondPackage = ReadFile(Package());

	ASSERT_EQ(first.GetResult().profiles.size(), 1u);
	ASSERT_EQ(second.GetResult().profiles.size(), 1u);
	ASSERT_NE(first.GetResult().profiles[0].buildNumber, second.GetResult().profiles[0].buildNumber);

	ASSERT_FALSE(firstPackage.empty());
	EXPECT_TRUE(firstPackage == secondPackage);
}

TEST_F(BuildDocumentsFixture, DefaultSettingsCarryNoDocument) {
	WriteProfile();

	BuildPipeline pipeline;
	Build(pipeline);
	ASSERT_EQ(pipeline.GetState(), BuildState::Succeeded) << Issues(pipeline);

	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(Path(Package().generic_string()), error)) << error;
	EXPECT_TRUE(reader.GetDocument(PakDocumentKind::ProjectSettings).empty());
	EXPECT_FALSE(reader.GetDocument(PakDocumentKind::BootManifest).empty());

	ASSERT_EQ(pipeline.GetResult().profiles.size(), 1u);
	EXPECT_EQ(pipeline.GetResult().profiles[0].warningCount, 0u) << Issues(pipeline);
}

TEST_F(BuildDocumentsFixture, ManifestlessCookFailsBuild) {
	WriteProfile();
	BuildStepRegistry::Register("boot-manifest", BuildPhase::PreBuild,
								[](BuildContext&) { return BuildStepStatus::Succeeded; });

	BuildPipeline pipeline;
	Build(pipeline);
	EXPECT_EQ(pipeline.GetState(), BuildState::Failed);
	EXPECT_NE(Issues(pipeline).find("boot manifest"), str::npos) << Issues(pipeline);
	EXPECT_FALSE(fs::exists(Package()));
}
