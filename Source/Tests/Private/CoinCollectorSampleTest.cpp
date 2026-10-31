#include "TestsPCH.h"

#include "Checkout.h"

#include <filesystem>
#include <fstream>
#include <string>

TEST(CoinCollectorSampleTest, EveryFileIncludesOnlyUmbrellaAndHeaders) {
	const std::filesystem::path root = Checkout::Root();
	if (root.empty()) {
		GTEST_SKIP() << "checkout not found";
	}
	const std::filesystem::path source = root / "Samples" / "CoinCollector" / "Source";
	std::error_code ec;

	size_t files = 0;
	size_t includes = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(source, ec)) {
		const std::filesystem::path& path = entry.path();
		if (path.extension() != ".h" && path.extension() != ".cpp") {
			continue;
		}
		++files;
		std::ifstream file(path, std::ios::binary);
		for (std::string line; std::getline(file, line);) {
			const size_t at = line.find_first_not_of(" \t");
			if (at == std::string::npos || line.compare(at, 8, "#include") != 0) {
				continue;
			}
			++includes;
			const size_t open = line.find_first_of("\"<", at);
			const size_t close = open == std::string::npos ? std::string::npos : line.find_first_of("\">", open + 1);
			const std::string target = close == std::string::npos ? line.substr(at) : line.substr(open + 1, close - open - 1);
			const bool own = close != std::string::npos && line[open] == '"' && std::filesystem::is_regular_file(path.parent_path() / target, ec);
			EXPECT_TRUE(target == "Shalltear.h" || own);
		}
	}
	EXPECT_GT(files, 0u) << source.generic_string();
	EXPECT_GE(includes, files) << source.generic_string();
}

#ifdef SHALLTEAR_SHARED

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window/FrameLoop.h"

#include "Shalltear/Editor/CompileService.h"
#include "Shalltear/Editor/CookOps.h"
#include "Shalltear/Editor/CppToolchain.h"
#include "Shalltear/Editor/ScriptProjectOps.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace Shalltear;
namespace fs = std::filesystem;

namespace {

	constexpr f64 FRAME_SECONDS = 1.0 / 60.0;

	fs::path FindSample() {
		std::error_code ec;
		for (fs::path probe = fs::current_path(ec); !probe.empty(); probe = probe.parent_path()) {
			if (fs::exists(probe / "Samples" / "CoinCollector" / "Project.ini", ec)) {
				return probe / "Samples" / "CoinCollector";
			}
			if (probe.parent_path() == probe) {
				break;
			}
		}
		return {};
	}

	class CoinCollectorSampleFixture : public ::testing::Test {
	protected:
		fs::path _root;
		str _sdkRoot;

		void SetUp() override {
			_sdkRoot = ScriptProjectOps::GetSDKRoot();
			if (_sdkRoot.empty()) {
				GTEST_SKIP() << "no Shalltear CMake package found";
			}
			CppToolchain::DiscoveryOptions discovery;
			discovery.sdkRoot = _sdkRoot;
			if (CppToolchain::ToolchainInfo toolchain = CppToolchain::Discover(discovery); !toolchain.IsComplete()) {
				GTEST_SKIP() << "toolchain incomplete:\n"
							 << CppToolchain::Describe(toolchain);
			}
			const fs::path sample = FindSample();
			ASSERT_FALSE(sample.empty());

			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() / ("shalltear_coins_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			for (const char* part : {"Assets", "BuildProfiles", "Source"}) {
				fs::copy(sample / part, _root / part, fs::copy_options::recursive);
			}
			for (const char* file : {"Project.ini", "ProjectSettings.ini", "CMakeLists.txt", "CMakePresets.json"}) {
				fs::copy_file(sample / file, _root / file);
			}
			str settings = FileSystem::ReadTextFile(Path((_root / "ProjectSettings.ini").generic_string()), MemTag::Editor);
			ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / "ProjectSettings.ini").generic_string()),
												  settings + "\n[Audio]\ndevice = None\n"));

			FileSystem::Mount("engine", Path((sample.parent_path().parent_path() / "EngineAssets").generic_string()));
			FileSystem::Mount("project", Path(_root.generic_string()));
			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			paths.user = Path((_root / "UserSettings.ini").generic_string());
			paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			Settings::Initialize(paths);
			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			for (const char* package : {"Physics3D.dll", "Audio.dll"}) {
				ASSERT_TRUE(ModuleSystem::IsLoaded(ModuleSystem::Load(Path(package), ModuleLoadPolicy::InPlace))) << package;
			}
			AssetDatabase::Initialize();
			AssetDatabase::ScanProjectDirectory();

			ScriptProjectOps::ScaffoldOptions options;
			options.moduleName = "CoinCollector";
			options.sdkRoot = _sdkRoot;
			options.editorExecutable = ScriptProjectOps::GetEditorExecutablePath();
			ScriptProjectOps::ScaffoldResult regenerated;
			ASSERT_TRUE(ScriptProjectOps::RegenerateProjectFiles(_root.generic_string(), options, regenerated));

			CompileService service;
			service.RefreshToolchain();
			CompileRequest request;
			request.projectPath = _root.generic_string();
			request.enabledPackages = {str("dev.shalltear.audio"), str("dev.shalltear.physics3d")};
			ASSERT_TRUE(service.RequestCompile(request));
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
			while (service.IsBusy() && std::chrono::steady_clock::now() < deadline) {
				service.PumpMainThread();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			service.PumpMainThread();
			if (service.GetState() != CompileState::Succeeded) {
				str diagnostics;
				for (const CompilerDiagnostic& diagnostic : service.GetDiagnostics()) {
					diagnostics += diagnostic.raw + "\n";
				}
				FAIL() << "tutorial code doesn't compile against this SDK:\n"
					   << diagnostics;
			}
			const str module = ScriptProjectOps::GetModuleBinaryPath(_root.generic_string(), str(ScriptProjectOps::GetActiveConfigName()));
			ASSERT_TRUE(ModuleSystem::IsLoaded(ModuleSystem::Load(Path(module))));
		}

		void TearDown() override {
			if (IsSkipped()) {
				return;
			}
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			AssetDatabase::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			FileSystem::Unmount("engine");
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		static Guid SceneGuid(const char* name) {
			return AssetDatabase::LookupGuid(Path(str("project://Assets/Scenes/") + name + ".scene"));
		}

		static str PrimaryName() {
			Scene* scene = SceneSystem::GetPrimaryScene().Get();
			return scene ? scene->name : str();
		}

		void RunFrame(FixedTimestep& fixed) {
			Time::BeginFrame(FRAME_SECONDS);
			SceneSystem::BeginFrame();
			const FrameTime& frame = Time::GetFrame();
			fixed.Configure(frame.fixedDeltaTime, frame.maximumDeltaTime);
			const u32 steps = fixed.Advance(frame.ComputeGameDelta());
			FrameStageArgs args;
			for (FrameStage stage : FRAME_STAGES) {
				if (stage == FrameStage::FixedUpdate) {
					for (u32 i = 0; i < steps; ++i) {
						FrameStages::Run(stage, args);
					}
				} else if (stage != FrameStage::Render) {
					FrameStages::Run(stage, args);
				}
			}
			AssetDatabase::CollectGarbage();
		}

		void RunSeconds(FixedTimestep& fixed, f64 seconds) {
			for (f64 elapsed = 0.0; elapsed < seconds; elapsed += FRAME_SECONDS) {
				RunFrame(fixed);
			}
		}
	};

} // anonymous namespace

TEST_F(CoinCollectorSampleFixture, MenuLevelsChainBackToMenu) {
	Time::FrameScope clock{FrameTime{}};
	FixedTimestep fixed(Time::GetFrame().fixedDeltaTime, Time::GetFrame().maximumDeltaTime);

	ASSERT_TRUE(SceneSystem::LoadSceneByGuid(SceneGuid("Main")).IsValid());
	Handle<SceneObject> menuObject = SceneSystem::GetPrimaryScene()->FindObjectByName("Menu");
	ASSERT_TRUE(menuObject.IsValid());
	const TypeInfo* mainMenuType = TypeRegistry::FindType(StringHash("MainMenu"));
	ASSERT_NE(mainMenuType, nullptr);
	Component* mainMenu = nullptr;
	for (u32 i = 0; i < menuObject->GetComponentCount(); ++i) {
		Component* candidate = menuObject->GetComponentAt(i).Get();
		if (candidate && TypeRegistry::IsDerivedFrom(candidate->GetType(), mainMenuType)) {
			mainMenu = candidate;
		}
	}
	ASSERT_NE(mainMenu, nullptr);
	const FieldInfo* firstLevel = FindField(mainMenuType, StringHash("firstLevel"));
	ASSERT_NE(firstLevel, nullptr);
	auto* level1Asset = static_cast<Handle<SceneAsset>*>(firstLevel->addrFn(mainMenu))->Get();
	ASSERT_NE(level1Asset, nullptr);
	EXPECT_EQ(level1Asset->assetId, SceneGuid("Level1"));

	SceneSystem::ReplaceScenes(level1Asset->assetId);
	RunFrame(fixed);
	ASSERT_EQ(PrimaryName(), "Level1");

	Scene* level = SceneSystem::GetPrimaryScene().Get();
	Handle<SceneObject> player = level->FindObjectByName("Player");
	ASSERT_TRUE(player.IsValid());
	Vector<Vec3> coins{GetMemoryResource(MemTag::Scene)};
	struct Collect {
		Vector<Vec3>* out;
		static void Visit(SceneObject& node, void* user) {
			if (node.name == "Coin") {
				static_cast<Collect*>(user)->out->push_back(node.GetLocalPosition());
			}
		}
	} collect{&coins};
	level->ForEachObject(&Collect::Visit, &collect);
	ASSERT_EQ(coins.size(), 5u);

	RunSeconds(fixed, 0.5);
	for (size_t i = 0; i < coins.size(); ++i) {
		ASSERT_EQ(PrimaryName(), "Level1") << i << " coins";
		player->SetLocalPosition(Vec3{coins[i].x, 1.0f, coins[i].z});
		RunSeconds(fixed, 0.25);
	}
	EXPECT_EQ(PrimaryName(), "Level2");

	RunSeconds(fixed, 20.0);
	EXPECT_EQ(PrimaryName(), "Level2");
	RunSeconds(fixed, 6.0);
	EXPECT_EQ(PrimaryName(), "Main");
}

TEST_F(CoinCollectorSampleFixture, BuildShipsEveryLevelMenuReaches) {
	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(SceneGuid("Main"));
	CookOps::CookClosure closure;
	str error;
	ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;

	for (const char* path : {"Assets/Scenes/Level1.scene", "Assets/Scenes/Level2.scene", "Assets/Levels/Level1.level",
							 "Assets/Levels/Level2.level", "Assets/Prefabs/Coin.prefab", "Assets/Audio/Pickup.wav",
							 "Assets/UI/Menu.slxml", "Assets/UI/Hud.slxml", "Assets/UI/Game.slpanel"}) {
		const Guid guid = AssetDatabase::LookupGuid(Path(str("project://") + path));
		ASSERT_TRUE(guid.IsValid()) << path;
		EXPECT_NE(std::find(closure.assets.begin(), closure.assets.end(), guid), closure.assets.end()) << path;
	}
}

#endif
