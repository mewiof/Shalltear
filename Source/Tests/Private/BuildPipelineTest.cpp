#include "TestsPCH.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/BuildTarget.h"
#include "Shalltear/Editor/ScriptProjectOps.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Jobs/JobSystem.h"

#include "ScopedSettingsStore.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	class FakeTarget : public IBuildTarget {
	public:
		FakeTarget(const char* id, bool available = true, bool monotonic = false)
			: _id(id), _available(available), _monotonic(monotonic) {}

		std::string_view GetId() const override { return _id; }
		std::string_view GetDisplayName() const override { return _id; }

		bool IsAvailable(str& outReason) const override {
			if (!_available) {
				outReason = "the FakeSDK is not installed on this machine";
			}
			return _available;
		}

		bool RequiresMonotonicBuildNumber() const override { return _monotonic; }

		void DescribeSettings(Vector<BuildSettingDesc>& out) const override {
			BuildSettingDesc desc;
			desc.key = "fakeKey";
			desc.type = BuildSettingType::String;
			desc.label = "Fake Key";
			desc.defaultValue = "fake-default";
			out.push_back(std::move(desc));
		}

		void Validate(const str&, const ResolvedProfile&, Vector<BuildIssue>&) const override {}

		void ConfigurePipeline(BuildContext& context) const override {
			context.entrySource = "FakeMain.cpp";
			context.defines.push_back("SLT_FAKE");
			context.sdkRoot = sdkRoot;
		}

		str GetLaunchCommand(const BuildContext& context) const override {
			return launchable ? "\"" + context.stagingDir + "/" + context.executableName + ".fake\"" : str{};
		}

		bool launchable = true;

		str sdkRoot;
	private:
		const char* _id;
		bool _available;
		bool _monotonic;
	};

	class BuildPipelineFixture : public ::testing::Test {
	protected:
		fs::path _root;
		fs::path _engine;
		fs::path _sdk;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			fs::path base = fs::temp_directory_path() /
							("shalltear_pipeline_" + std::to_string(stamp) + "_" + std::to_string(unique));
			_root = base / "MyGame";
			_engine = base / "EngineAssets";

			fs::create_directories(_root / "Assets" / "Scenes");
			WriteFile(_root / "Project.ini", "[Project]\nname = MyGame\n");
			WriteFile(_root / ".assets.db", "index\n");

			fs::create_directories(_engine / "Shaders");
			fs::create_directories(_engine / "Templates");
			WriteFile(_engine / "Shaders" / "Lit.hlsl", "lit\n");
			WriteFile(_engine / "Shaders" / "Lit.hlsl.meta", "meta\n");
			WriteFile(_engine / "Shaders" / "EditorGrid.hlsl", "grid\n");
			WriteFile(_engine / "Shaders" / "EditorGrid.hlsl.meta", "meta\n");
			WriteFile(_engine / "Templates" / "Component.h.template", "template\n");
			FileSystem::Mount("engine", Path(_engine.generic_string()));

			_sdk = base / "FakeSdk";
			WriteFile(_sdk / "bin" / "dxcompiler.dll", "not really a compiler\n");
		}

		void TearDown() override {
			FileSystem::Unmount("engine");
			BuildStepRegistry::Shutdown();
			BuildTargetRegistry::Shutdown();
			std::error_code ec;
			fs::remove_all(_root.parent_path(), ec);
		}

		str Root() const { return _root.generic_string(); }

		static void WriteFile(const fs::path& path, const str& text) {
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << text;
		}

		void WriteScene(const str& relative) const { WriteFile(_root / relative, "{}\n"); }

		void WriteProfile(const str& name, const str& target, const Vector<str>& scenes,
						  std::initializer_list<std::pair<const char*, const char*>> settings = {}) const {
			BuildProfile profile;
			profile.name = name;
			profile.target = target;
			if (!scenes.empty()) {
				profile.SetList("scenes", scenes);
			}
			for (const auto& [key, value] : settings) {
				profile.SetString(key, value);
			}
			BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), profile);
		}

		static void RunToCompletion(BuildPipeline& pipeline) {
			for (int i = 0; i < 5000 && pipeline.IsBusy(); ++i) {
				pipeline.PumpMainThread();
			}
			ASSERT_FALSE(pipeline.IsBusy());
		}

		static const BuildProfileResult* FindResult(const BuildResult& result, std::string_view profile) {
			for (const BuildProfileResult& entry : result.profiles) {
				if (entry.profile == profile) {
					return &entry;
				}
			}
			return nullptr;
		}
	};

	Vector<BuildStepDesc> NeuterEveryStep(Vector<str>& log) {
		Vector<BuildStepDesc> steps;
		BuildStepRegistry::GetSteps(steps);
		for (const BuildStepDesc& step : steps) {
			str name = step.name;
			BuildStepRegistry::Register(name, step.phase, [name, &log](BuildContext&) {
				log.push_back(name);
				return BuildStepStatus::Succeeded;
			});
		}
		return steps;
	}

	void NeuterStepsThatNeedARealProject() {
		BuildStepRegistry::Register("validate", BuildPhase::Validate,
									[](BuildContext&) { return BuildStepStatus::Succeeded; });
		for (const char* name : {"configure", "compile"}) {
			BuildStepRegistry::Register(name, BuildPhase::Compile,
										[](BuildContext&) { return BuildStepStatus::Succeeded; });
		}
		BuildStepRegistry::Register("boot-manifest", BuildPhase::PreBuild, [](BuildContext& context) {
			context.bootManifest = "{\n\t\"formatVersion\": 2,\n\t\"scenes\": []\n}\n";
			return BuildStepStatus::Succeeded;
		});
		BuildStepRegistry::Register("cook-content", BuildPhase::Stage, [](BuildContext& context) {
			return FileSystem::WriteTextFile(Path(context.stagingDir + "/cooked.marker"), str("stand-in"))
					   ? BuildStepStatus::Succeeded
					   : BuildStepStatus::Failed;
		});
	}

	struct ScopedProjectMount {
		explicit ScopedProjectMount(const str& root) { FileSystem::Mount("project", Path(root)); }
		~ScopedProjectMount() { FileSystem::Unmount("project"); }
	};

	str ReadFileText(const fs::path& path) {
		std::ifstream file(path, std::ios::binary);
		return str(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

	bool Contains(const Vector<str>& values, std::string_view value) {
		for (const str& entry : values) {
			if (entry == value) {
				return true;
			}
		}
		return false;
	}

} // anonymous namespace

TEST_F(BuildPipelineFixture, StepsOrderByPhaseThenRegistration) {
	BuildStepRegistry::Register("z-late-poststage", BuildPhase::PostBuild,
								[](BuildContext&) { return BuildStepStatus::Succeeded; });
	BuildStepRegistry::Register("a-late-validate", BuildPhase::Validate,
								[](BuildContext&) { return BuildStepStatus::Succeeded; });

	Vector<BuildStepDesc> steps;
	BuildStepRegistry::GetSteps(steps);

	size_t validateAt = 0;
	size_t postBuildAt = 0;
	size_t builtInValidateAt = 0;
	for (size_t i = 0; i < steps.size(); ++i) {
		if (steps[i].name == "a-late-validate") {
			validateAt = i;
		} else if (steps[i].name == "z-late-poststage") {
			postBuildAt = i;
		} else if (steps[i].name == "validate") {
			builtInValidateAt = i;
		}
		if (i > 0) {
			EXPECT_LE(static_cast<u8>(steps[i - 1].phase), static_cast<u8>(steps[i].phase)) << "step " << i << " (" << steps[i].name;
		}
	}
	EXPECT_LT(validateAt, postBuildAt);
	EXPECT_LT(builtInValidateAt, validateAt);
}

TEST_F(BuildPipelineFixture, StateReportsStepNOfM) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Default", "Fake", {"Assets/Scenes/Main.scene"});

	Vector<str> ran;
	Vector<BuildStepDesc> steps = NeuterEveryStep(ran);
	ASSERT_GE(steps.size(), 2u);

	int entries = 0;
	str held = steps[1].name;
	BuildStepRegistry::Register(held, steps[1].phase, [&entries](BuildContext&) {
		return ++entries < 2 ? BuildStepStatus::Running : BuildStepStatus::Succeeded;
	});

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Default");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;

	str label;
	for (int i = 0; i < 5000 && pipeline.IsBusy(); ++i) {
		pipeline.PumpMainThread();
		if (pipeline.GetStepName() == held && label.empty()) {
			label = pipeline.GetStatusLabel();
		}
	}

	EXPECT_EQ(label, std::format("step 2 of {}: {}", steps.size(), held));
	EXPECT_EQ(pipeline.GetState(), BuildState::Succeeded);
	EXPECT_EQ(ran.size() + 1, steps.size());
}

TEST_F(BuildPipelineFixture, FailingStepAbortsProfileAndNotSiblings) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("First", "Fake", {"Assets/Scenes/Main.scene"});
	WriteProfile("Second", "Fake", {"Assets/Scenes/Main.scene"});

	Vector<str> ran;
	Vector<BuildStepDesc> steps = NeuterEveryStep(ran);

	const BuildStepDesc& middle = steps[steps.size() / 2];
	str failing = middle.name;
	str lastStep = steps.back().name;
	BuildStepRegistry::Register(failing, middle.phase, [](BuildContext& context) {
		return context.profile->name == "First" ? BuildStepStatus::Failed : BuildStepStatus::Succeeded;
	});

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("First");
	request.profiles.push_back("Second");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);

	const BuildResult& result = pipeline.GetResult();
	ASSERT_EQ(result.profiles.size(), 2u);
	EXPECT_EQ(result.profiles[0].outcome, BuildOutcome::Failed);
	EXPECT_NE(result.profiles[0].reason.find(failing), str::npos) << result.profiles[0].reason;
	EXPECT_EQ(result.profiles[1].outcome, BuildOutcome::Succeeded);
	EXPECT_FALSE(result.AllSucceeded());
	EXPECT_EQ(result.FailureCount(), 1u);

	u32 lastStepRuns = 0;
	for (const str& entry : ran) {
		lastStepRuns += entry == lastStep ? 1 : 0;
	}
	EXPECT_EQ(lastStepRuns, 1u);
	EXPECT_EQ(pipeline.GetState(), BuildState::Failed);
}

TEST_F(BuildPipelineFixture, UnavailableTargetSkipped) {
	FakeTarget available("Fake");
	available.sdkRoot = _sdk.generic_string();
	FakeTarget missing("FakeConsole", false);
	BuildTargetRegistry::Register(&available);
	BuildTargetRegistry::Register(&missing);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Console", "FakeConsole", {"Assets/Scenes/Main.scene"});
	WriteProfile("Desktop", "Fake", {"Assets/Scenes/Main.scene"});
	WriteProfile("Alien", "NoSuchTarget", {"Assets/Scenes/Main.scene"});

	Vector<str> ran;
	NeuterEveryStep(ran);

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Console");
	request.profiles.push_back("Desktop");
	request.profiles.push_back("Alien");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);

	const BuildResult& result = pipeline.GetResult();
	const BuildProfileResult* console = FindResult(result, "Console");
	ASSERT_NE(console, nullptr);
	EXPECT_EQ(console->outcome, BuildOutcome::Skipped);
	EXPECT_NE(console->reason.find("FakeSDK"), str::npos) << console->reason;

	const BuildProfileResult* alien = FindResult(result, "Alien");
	ASSERT_NE(alien, nullptr);
	EXPECT_EQ(alien->outcome, BuildOutcome::Skipped);
	EXPECT_NE(alien->reason.find("NoSuchTarget"), str::npos) << alien->reason;

	EXPECT_EQ(FindResult(result, "Desktop")->outcome, BuildOutcome::Succeeded);

	EXPECT_TRUE(result.AllSucceeded());
	EXPECT_EQ(pipeline.GetState(), BuildState::Succeeded);
}

TEST_F(BuildPipelineFixture, CancelMidStepStopsRun) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Default", "Fake", {"Assets/Scenes/Main.scene"});

	Vector<str> ran;
	Vector<BuildStepDesc> steps = NeuterEveryStep(ran);

	bool sawCancelRequest = false;
	BuildStepRegistry::Register(steps[1].name, steps[1].phase, [&sawCancelRequest](BuildContext& context) {
		sawCancelRequest = sawCancelRequest || context.IsCancelRequested();
		return BuildStepStatus::Running;
	});

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Default");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;

	for (int i = 0; i < 8; ++i) {
		pipeline.PumpMainThread();
	}
	ASSERT_TRUE(pipeline.IsBusy());
	pipeline.Cancel();
	RunToCompletion(pipeline);

	EXPECT_EQ(pipeline.GetState(), BuildState::Cancelled);
	ASSERT_EQ(pipeline.GetResult().profiles.size(), 1u);
	EXPECT_EQ(pipeline.GetResult().profiles[0].outcome, BuildOutcome::Cancelled);
	EXPECT_FALSE(Contains(ran, steps.back().name));
	EXPECT_TRUE(sawCancelRequest);
}

TEST_F(BuildPipelineFixture, SecondRequestRefusedWhileRunning) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Default", "Fake", {"Assets/Scenes/Main.scene"});

	Vector<str> ran;
	Vector<BuildStepDesc> steps = NeuterEveryStep(ran);
	BuildStepRegistry::Register(steps[1].name, steps[1].phase,
								[](BuildContext&) { return BuildStepStatus::Running; });

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Default");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	for (int i = 0; i < 4; ++i) {
		pipeline.PumpMainThread();
	}
	ASSERT_TRUE(pipeline.IsBusy());

	EXPECT_FALSE(pipeline.Request(request, refusal));
	EXPECT_NE(refusal.find("already running"), str::npos) << refusal;
	EXPECT_NE(refusal.find("Default"), str::npos) << refusal;

	pipeline.Cancel();
	RunToCompletion(pipeline);
}

TEST_F(BuildPipelineFixture, SweepLeavesBuiltIns) {
	int moduleImage = 0;
	const void* owner = &moduleImage;

	Vector<BuildStepDesc> before;
	BuildStepRegistry::GetSteps(before);
	size_t builtInSteps = before.size();
	Vector<str> builtInTargets;
	BuildTargetRegistry::GetTargetIds(builtInTargets);

	FakeTarget target("FakeFromAModule");
	target.sdkRoot = _sdk.generic_string();
	ASSERT_TRUE(BuildTargetRegistry::Register(&target, owner));
	ASSERT_TRUE(BuildStepRegistry::Register(
		"sign-the-executable", BuildPhase::PostBuild, [](BuildContext&) { return BuildStepStatus::Succeeded; },
		owner));

	Vector<BuildStepDesc> withModule;
	BuildStepRegistry::GetSteps(withModule);
	EXPECT_EQ(withModule.size(), builtInSteps + 1);
	EXPECT_NE(BuildTargetRegistry::Find("FakeFromAModule"), nullptr);

	EXPECT_EQ(BuildStepRegistry::UnregisterOwnedBy(owner), 1u);
	EXPECT_EQ(BuildTargetRegistry::UnregisterOwnedBy(owner), 1u);

	Vector<BuildStepDesc> after;
	BuildStepRegistry::GetSteps(after);
	EXPECT_EQ(after.size(), builtInSteps);
	EXPECT_EQ(BuildTargetRegistry::Find("FakeFromAModule"), nullptr);

	EXPECT_EQ(BuildStepRegistry::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(BuildTargetRegistry::UnregisterOwnedBy(nullptr), 0u);
	Vector<str> targetsAfter;
	BuildTargetRegistry::GetTargetIds(targetsAfter);
	EXPECT_EQ(targetsAfter.size(), builtInTargets.size());
}

TEST_F(BuildPipelineFixture, ModuleStepOverrideRestored) {
	int moduleImage = 0;
	const void* owner = &moduleImage;

	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Default", "Fake", {"Assets/Scenes/Main.scene"});
	NeuterStepsThatNeedARealProject();

	size_t builtInCount = 0;
	{
		Vector<BuildStepDesc> steps;
		BuildStepRegistry::GetSteps(steps);
		builtInCount = steps.size();
	}

	ASSERT_TRUE(BuildStepRegistry::Register(
		"cook-content", BuildPhase::Stage, [](BuildContext&) { return BuildStepStatus::Succeeded; }, owner));
	{
		Vector<BuildStepDesc> steps;
		BuildStepRegistry::GetSteps(steps);
		EXPECT_EQ(steps.size(), builtInCount);
	}

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Default");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	ASSERT_EQ(pipeline.GetState(), BuildState::Succeeded);
	ASSERT_FALSE(fs::exists(_root / "Builds" / "Default" / "cooked.marker"));

	EXPECT_EQ(BuildStepRegistry::UnregisterOwnedBy(owner), 1u);
	{
		Vector<BuildStepDesc> steps;
		BuildStepRegistry::GetSteps(steps);
		EXPECT_EQ(steps.size(), builtInCount);
		for (const BuildStepDesc& step : steps) {
			if (step.name == "cook-content") {
				EXPECT_EQ(step.owner, nullptr);
			}
		}
	}

	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	ASSERT_EQ(pipeline.GetState(), BuildState::Succeeded);
	EXPECT_TRUE(fs::exists(_root / "Builds" / "Default" / "cooked.marker"));
}

TEST_F(BuildPipelineFixture, ModuleTargetOverrideRestored) {
	int moduleImage = 0;
	const void* owner = &moduleImage;

	FakeTarget impostor("Windows");
	ASSERT_TRUE(BuildTargetRegistry::Register(&impostor, owner));

	auto has = [](std::string_view id, std::string_view key) {
		Vector<BuildSettingDesc> schema;
		BuildTargetRegistry::GetSchemaFor(id, schema);
		for (const BuildSettingDesc& desc : schema) {
			if (desc.key == key) {
				return true;
			}
		}
		return false;
	};

	EXPECT_TRUE(has("Windows", "fakeKey"));
	EXPECT_FALSE(has("Windows", "copyDebugSymbols"));

	EXPECT_EQ(BuildTargetRegistry::UnregisterOwnedBy(owner), 1u);

	EXPECT_TRUE(has("Windows", "copyDebugSymbols"));
	EXPECT_FALSE(has("Windows", "fakeKey"));
	Vector<str> ids;
	BuildTargetRegistry::GetTargetIds(ids);
	ASSERT_GE(ids.size(), 2u);
	EXPECT_EQ(ids[0], "Windows");
	EXPECT_EQ(BuildTargetRegistry::GetDefaultTargetId(), "Windows");
}

TEST_F(BuildPipelineFixture, TheSchemaResolvesDifferentlyForTwoTargets) {
	FakeTarget fake("Fake");
	fake.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&fake);

	Vector<BuildSettingDesc> core;
	BuildProfileOps::GetCoreSettings(core);
	Vector<BuildSettingDesc> windows;
	BuildTargetRegistry::GetSchemaFor("Windows", windows);
	Vector<BuildSettingDesc> fakeSchema;
	BuildTargetRegistry::GetSchemaFor("Fake", fakeSchema);
	Vector<BuildSettingDesc> unknown;
	BuildTargetRegistry::GetSchemaFor("NoSuchTarget", unknown);

	auto has = [](const Vector<BuildSettingDesc>& schema, std::string_view key) {
		for (const BuildSettingDesc& desc : schema) {
			if (desc.key == key) {
				return true;
			}
		}
		return false;
	};

	EXPECT_TRUE(has(core, "scenes"));
	EXPECT_FALSE(has(core, "copyDebugSymbols"));
	EXPECT_TRUE(has(windows, "copyDebugSymbols"));
	EXPECT_FALSE(has(windows, "fakeKey"));
	EXPECT_TRUE(has(fakeSchema, "fakeKey"));
	EXPECT_FALSE(has(fakeSchema, "copyDebugSymbols"));

	EXPECT_EQ(unknown.size(), core.size());

	WriteProfile("Default", "Fake", {});
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Default", fakeSchema, resolved);
	const ResolvedSetting* setting = resolved.Find("fakeKey");
	ASSERT_NE(setting, nullptr);
	EXPECT_EQ(setting->value, "fake-default");
}

TEST_F(BuildPipelineFixture, DefaultTargetIsWindows) {
	Vector<str> ids;
	BuildTargetRegistry::GetTargetIds(ids);
	ASSERT_GE(ids.size(), 2u);
	EXPECT_EQ(ids[0], "Windows");
	EXPECT_EQ(ids[1], "WindowsServer");
	EXPECT_EQ(BuildTargetRegistry::GetDefaultTargetId(), "Windows");
	EXPECT_NE(BuildTargetRegistry::Find("WindowsServer"), nullptr);
	EXPECT_EQ(BuildTargetRegistry::Find("Windows")->GetDisplayName(), "Windows");
	EXPECT_EQ(BuildTargetRegistry::Find("WindowsServer")->GetDisplayName(), "Windows Dedicated Server");
}

TEST_F(BuildPipelineFixture, StagingComputedWithoutBuild) {
	WriteScene("Assets/Scenes/Main.scene");
	WriteScene("Assets/Scenes/FinalBoss.scene");
	WriteProfile("Demo", "Windows", {"Assets/Scenes/Main.scene"});

	Vector<BuildSettingDesc> schema;
	BuildTargetRegistry::GetSchemaFor("Windows", schema);
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Demo", schema, resolved);

	EXPECT_EQ(BuildPipeline::GetStagingDirectory(Root(), resolved, {}), Root() + "/Builds/Demo");
	EXPECT_EQ(BuildPipeline::GetStagingDirectory(Root(), resolved, "C:/artifacts"), "C:/artifacts/Demo");
	EXPECT_EQ(BuildPipeline::GetIntermediateDirectory(Root(), "Demo"), Root() + "/Library/Player/Demo");

	Vector<str> staged;
	BuildPipeline::GetStagedScenes(resolved, staged);
	ASSERT_EQ(staged.size(), 1u);
	EXPECT_EQ(staged[0], "Assets/Scenes/Main.scene");

	EXPECT_TRUE(BuildPipeline::IsEditorOnlyEngineAsset("Templates/Component.h.template"));
	EXPECT_TRUE(BuildPipeline::IsEditorOnlyEngineAsset("Schemas/Layout.xsd"));
	EXPECT_TRUE(BuildPipeline::IsEditorOnlyEngineAsset("Schemas/slss.tmLanguage.json"));
	EXPECT_TRUE(BuildPipeline::IsEditorOnlyEngineAsset("Shaders/EditorGrid.hlsl"));
	EXPECT_TRUE(BuildPipeline::IsEditorOnlyEngineAsset("shaders/editorgrid.hlsl.meta"));
	EXPECT_FALSE(BuildPipeline::IsEditorOnlyEngineAsset("Shaders/Lit.hlsl"));
	EXPECT_FALSE(BuildPipeline::IsEditorOnlyEngineAsset("Fonts/Inter-Regular.ttf"));
}

TEST_F(BuildPipelineFixture, CounterPerTargetSkipsFailed) {
	EXPECT_EQ(BuildProfileOps::NextBuildNumber(Root(), "Windows"), 1u);
	EXPECT_EQ(BuildProfileOps::NextBuildNumber(Root(), "Windows"), 2u);
	EXPECT_EQ(BuildProfileOps::NextBuildNumber(Root(), "WindowsServer"), 1u);

	Config counters;
	ASSERT_TRUE(counters.Load(Path(Root() + "/Library/BuildCounters.ini")));
	EXPECT_EQ(counters.GetI64("Counters", "Windows", 0), 2);
	EXPECT_EQ(counters.GetI64("Counters", "WindowsServer", 0), 1);

	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Auto", "Fake", {"Assets/Scenes/Main.scene"}, {{"buildNumber", "${AUTO}"}});
	NeuterStepsThatNeedARealProject();

	auto build = [this](bool failStaging) {
		BuildStepRegistry::Register("stage-content", BuildPhase::Stage, [failStaging](BuildContext&) {
			return failStaging ? BuildStepStatus::Failed : BuildStepStatus::Succeeded;
		});
		BuildPipeline pipeline;
		BuildRequest request;
		request.projectPath = Root();
		request.profiles.push_back("Auto");
		str refusal;
		EXPECT_TRUE(pipeline.Request(request, refusal)) << refusal;
		RunToCompletion(pipeline);
		return pipeline.GetResult().profiles.empty() ? 0u : pipeline.GetResult().profiles[0].buildNumber;
	};

	EXPECT_EQ(build(true), 1u);
	EXPECT_EQ(build(false), 2u);
}

TEST_F(BuildPipelineFixture, ABuildRecordsItsResult) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);

	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Demo", "Fake", {"Assets/Scenes/Main.scene"}, {{"version", "1.2.3"}});
	NeuterStepsThatNeedARealProject();

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Demo");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	ASSERT_EQ(pipeline.GetState(), BuildState::Succeeded);

	Config results;
	ASSERT_TRUE(results.Load(Path(Root() + "/Library/BuildResults.ini")));
	EXPECT_EQ(results.GetString("Demo", "result"), "Succeeded");
	EXPECT_EQ(results.GetString("Demo", "target"), "Fake");
	EXPECT_GT(results.GetI64("Demo", "artifactBytes", 0), 0);
}

TEST_F(BuildPipelineFixture, GameCarriesRuntimeNotices) {
	ScopedProjectMount mount(Root());
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);

	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Demo", "Fake", {"Assets/Scenes/Main.scene"});
	NeuterStepsThatNeedARealProject();

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Demo");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	ASSERT_EQ(pipeline.GetState(), BuildState::Succeeded);

	fs::path staged = _root / "Builds" / "Demo" / "THIRD-PARTY-NOTICES.txt";
	ASSERT_TRUE(fs::exists(staged));
	const str notices = ReadFileText(staged);

	EXPECT_NE(notices.find("FreeType"), str::npos);
	EXPECT_NE(notices.find("Inter"), str::npos);
	EXPECT_NE(notices.find("stb_image"), str::npos);
	EXPECT_NE(notices.find("zlib"), str::npos);

	EXPECT_EQ(notices.find("ufbx"), str::npos);
	EXPECT_EQ(notices.find("dxcompiler"), str::npos);
	EXPECT_EQ(notices.find("Source Code Pro"), str::npos);
}

TEST_F(BuildPipelineFixture, EditorNoticesSupersetOfGames) {
	fs::path beside = fs::path(ScriptProjectOps::GetEditorExecutablePath()).parent_path();
	fs::path full = beside / "THIRD-PARTY-NOTICES.txt";
	fs::path runtime = beside / "THIRD-PARTY-NOTICES-Runtime.txt";
	ASSERT_TRUE(fs::exists(full));
	ASSERT_TRUE(fs::exists(runtime));

	const str fullText = ReadFileText(full);
	const str runtimeText = ReadFileText(runtime);

	for (std::string_view component : {"FreeType", "zlib", "stb_image", "Vulkan Memory Allocator", "Vulkan-Headers", "Inter"}) {
		EXPECT_NE(runtimeText.find(component), str::npos);
		EXPECT_NE(fullText.find(component), str::npos) << component;
	}
	for (std::string_view component : {"ufbx", "dxcompiler", "Source Code Pro"}) {
		EXPECT_NE(fullText.find(component), str::npos) << component;
	}
	EXPECT_GT(fullText.size(), runtimeText.size());
}

TEST_F(BuildPipelineFixture, CookRefusesProjectNobodyHasIndexed) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);

	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Demo", "Fake", {"Assets/Scenes/Main.scene"});
	BuildStepRegistry::Register("validate", BuildPhase::Validate,
								[](BuildContext&) { return BuildStepStatus::Succeeded; });
	for (const char* name : {"configure", "compile"}) {
		BuildStepRegistry::Register(name, BuildPhase::Compile,
									[](BuildContext&) { return BuildStepStatus::Succeeded; });
	}

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Demo");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	EXPECT_EQ(pipeline.GetState(), BuildState::Failed);

	Config results;
	ASSERT_TRUE(results.Load(Path(Root() + "/Library/BuildResults.ini")));
	EXPECT_EQ(results.GetString("Demo", "result"), "Failed");
}

namespace {

	class BuildLogCapture : public ILogSink {
	public:
		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			_events.push_back({event.level, event.category, event.message});
		}
		void Flush() override {}

		u32 CountMatching(LogLevel level, std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const Captured& event : _events) {
				if (event.level == level && event.category == "Build" && event.message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}

		bool SawBarrier() const {
			std::lock_guard<std::mutex> lock(_mutex);
			for (const Captured& event : _events) {
				if (event.category == "BuildLogCapture") {
					return true;
				}
			}
			return false;
		}

		void Clear() {
			std::lock_guard<std::mutex> lock(_mutex);
			_events.clear();
		}
	private:
		struct Captured {
			LogLevel level;
			str category;
			str message;
		};
		mutable std::mutex _mutex;
		Vector<Captured> _events;
	};

	std::shared_ptr<BuildLogCapture> StartCapture() {
		JobSystem::Initialize();
		LogManager::Initialize({false, false});
		LogManager::SetGlobalLevel(LogLevel::Trace);
		auto sink = std::make_shared<BuildLogCapture>();
		LogManager::AddSink(sink);
		SLT_LOG_INFO("BuildLogCapture", "barrier");
		for (int spin = 0; spin < 2000 && !sink->SawBarrier(); ++spin) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		EXPECT_TRUE(sink->SawBarrier());
		sink->Clear();
		return sink;
	}

	void StopCapture() {
		LogManager::Terminate();
		JobSystem::Terminate();
	}

} // anonymous namespace

TEST_F(BuildPipelineFixture, StepsRefusalReachesConsoleAndErrorCount) {
	FakeTarget target("Fake");
	target.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&target);

	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Demo", "Fake", {"Assets/Scenes/Main.scene"});
	BuildStepRegistry::Register("validate", BuildPhase::Validate,
								[](BuildContext&) { return BuildStepStatus::Succeeded; });
	for (const char* name : {"configure", "compile"}) {
		BuildStepRegistry::Register(name, BuildPhase::Compile,
									[](BuildContext&) { return BuildStepStatus::Succeeded; });
	}

	std::shared_ptr<BuildLogCapture> sink = StartCapture();
	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Demo");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	StopCapture();
	ASSERT_EQ(pipeline.GetState(), BuildState::Failed);

	const BuildProfileResult* result = FindResult(pipeline.GetResult(), "Demo");
	ASSERT_NE(result, nullptr);
	ASSERT_EQ(result->issues.size(), 1u);
	ASSERT_EQ(result->issues[0].severity, BuildIssueSeverity::Error);
	const str& message = result->issues[0].message;

	EXPECT_EQ(sink->CountMatching(LogLevel::Error, message), 1u) << message;
	EXPECT_EQ(result->errorCount, 1u);
}

TEST_F(BuildPipelineFixture, AValidationIssueIsPrintedOnce) {
	FakeTarget monotonic("FakeStore", true, true);
	monotonic.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&monotonic);
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Store", "FakeStore", {"Assets/Scenes/Main.scene"}, {{"buildNumber", "${AUTO}"}});
	for (const char* name : {"configure", "compile"}) {
		BuildStepRegistry::Register(name, BuildPhase::Compile,
									[](BuildContext&) { return BuildStepStatus::Succeeded; });
	}

	std::shared_ptr<BuildLogCapture> sink = StartCapture();
	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Store");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);
	StopCapture();

	EXPECT_EQ(sink->CountMatching(LogLevel::Warn, "requires a monotonic build number"), 1u);
}

TEST_F(BuildPipelineFixture, ServerTargetPicksEntryAndName) {
	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Client", "Windows", {"Assets/Scenes/Main.scene"});
	WriteProfile("Server", "WindowsServer", {"Assets/Scenes/Main.scene"});
	WriteProfile("NamedServer", "WindowsServer", {"Assets/Scenes/Main.scene"},
				 {{"executableName", "Northwind"}});

	struct Seen {
		str entrySource;
		str executableName;
		Vector<str> defines;
		bool windowedSubsystem = true;
	};
	Vector<Seen> seen;
	BuildStepRegistry::Register("validate", BuildPhase::Validate, [&seen](BuildContext& context) {
		seen.push_back(Seen{context.entrySource, context.executableName, context.defines, context.windowedSubsystem});
		return BuildStepStatus::Succeeded;
	});
	for (const char* name : {"configure", "compile"}) {
		BuildStepRegistry::Register(name, BuildPhase::Compile,
									[](BuildContext&) { return BuildStepStatus::Succeeded; });
	}

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Client");
	request.profiles.push_back("Server");
	request.profiles.push_back("NamedServer");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);

	ASSERT_EQ(seen.size(), 3u);
	EXPECT_EQ(seen[0].entrySource, "PlayerMain.cpp");
	EXPECT_EQ(seen[0].executableName, "MyGame");
	EXPECT_TRUE(Contains(seen[0].defines, "SLT_PLAYER"));

	EXPECT_EQ(seen[1].entrySource, "ServerMain.cpp");
	EXPECT_EQ(seen[1].executableName, "MyGameServer");
	EXPECT_TRUE(Contains(seen[1].defines, "SLT_SERVER"));
	EXPECT_FALSE(Contains(seen[1].defines, "SLT_PLAYER"));

	EXPECT_TRUE(seen[0].windowedSubsystem);
	EXPECT_FALSE(seen[1].windowedSubsystem);
	EXPECT_FALSE(seen[2].windowedSubsystem);

	EXPECT_EQ(seen[2].executableName, "Northwind");
}

TEST_F(BuildPipelineFixture, ValidationNeedsStaticSdk) {
	ScopedSettingsStore settings(_root / "SettingsStore",
								 "[Editor/Build]\nstaticSDKRoot = " + Root() + "/NoSuchSdk\n");

	WriteScene("Assets/Scenes/Main.scene");
	WriteProfile("Default", "Windows", {"Assets/Scenes/Main.scene"});

	Vector<BuildIssue> issues;
	ResolvedProfile resolved;
	Vector<BuildSettingDesc> schema;
	BuildTargetRegistry::GetSchemaFor("Windows", schema);
	BuildProfileOps::ResolveProfile(Root(), "Default", schema, resolved);
	BuildTargetRegistry::Find("Windows")->Validate(Root(), resolved, issues);

	ASSERT_EQ(issues.size(), 1u);
	EXPECT_EQ(issues[0].severity, BuildIssueSeverity::Error);
	EXPECT_NE(issues[0].message.find("STATIC SDK"), str::npos) << issues[0].message;
	EXPECT_NE(issues[0].message.find("Static SDK Root"), str::npos) << issues[0].message;

	FakeTarget monotonic("FakeStore", true, true);
	monotonic.sdkRoot = _sdk.generic_string();
	BuildTargetRegistry::Register(&monotonic);
	WriteProfile("Store", "FakeStore", {"Assets/Scenes/Main.scene"}, {{"buildNumber", "${AUTO}"}});
	for (const char* name : {"configure", "compile"}) {
		BuildStepRegistry::Register(name, BuildPhase::Compile,
									[](BuildContext&) { return BuildStepStatus::Succeeded; });
	}

	BuildPipeline pipeline;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Store");
	str refusal;
	ASSERT_TRUE(pipeline.Request(request, refusal)) << refusal;
	RunToCompletion(pipeline);

	ASSERT_EQ(pipeline.GetResult().profiles.size(), 1u);
	const BuildProfileResult& entry = pipeline.GetResult().profiles[0];
	const BuildIssue* warning = nullptr;
	for (const BuildIssue& issue : entry.issues) {
		if (issue.key == "buildNumber" && issue.severity == BuildIssueSeverity::Warning) {
			warning = &issue;
		}
	}
	ASSERT_NE(warning, nullptr);
	EXPECT_NE(warning->message.find("monotonic"), str::npos) << warning->message;
}
