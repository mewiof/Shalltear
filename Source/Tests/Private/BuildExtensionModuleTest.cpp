#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/BuildTarget.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ProjectOps.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/SettingsPresentation.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Shalltear;
using namespace UITest;

namespace {

	namespace fs = std::filesystem;

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";

	constexpr const char* PROBE_TARGET = "HotReloadProbe";
	constexpr const char* PROBE_TARGET_NAME = "Hot Reload Probe Console";
	constexpr const char* PROBE_STEP = "hot-reload-sign";

	class HostTarget : public IBuildTarget {
	public:
		std::string_view GetId() const override { return "FixtureTarget"; }
		std::string_view GetDisplayName() const override { return "Fixture Target"; }
		bool IsAvailable(str&) const override { return true; }
		void DescribeSettings(Vector<BuildSettingDesc>&) const override {}
		void Validate(const str&, const ResolvedProfile&, Vector<BuildIssue>&) const override {}
		void ConfigurePipeline(BuildContext& context) const override { context.entrySource = "PlayerMain.cpp"; }
		str GetLaunchCommand(const BuildContext&) const override { return {}; }
	};

	class BuildExtensionModuleFixture : public ::testing::Test {
	protected:
		fs::path _root;
		fs::path _engine;
		ModuleHandle _module;
		HostTarget _hostTarget;

		bool _haveExtensions = false;

		BuildPipeline _pipeline;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			fs::path base = fs::temp_directory_path() /
							("shalltear_buildext_" + std::to_string(stamp) + "_" + std::to_string(unique));
			_root = base / "MyGame";
			_engine = base / "EngineAssets";

			fs::create_directories(_root / "Assets" / "Scenes");
			WriteFile(_root / "Project.ini", "[Project]\nname = ExtensionProbe\n");
			WriteFile(_root / ".assets.db", "index\n");
			WriteFile(_root / "Assets" / "Scenes" / "Main.scene", "{}\n");

			fs::create_directories(_engine / "Shaders");
			WriteFile(_engine / "Shaders" / "Lit.hlsl", "lit\n");
			FileSystem::Mount("engine", Path(_engine.generic_string()));

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PropertyDrawers::Initialize();
			PanelManager::Initialize();
			ASSERT_TRUE(ProjectOps::MountProject(_root.generic_string()));

			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));

			_haveExtensions = EditorExtensions::Load(ModuleBase());
#ifdef SHALLTEAR_SHARED
			ASSERT_TRUE(_haveExtensions);
#endif

			BuildTargetRegistry::Register(&_hostTarget);
		}

		void TearDown() override {
			if (ModuleSystem::IsLoaded(_module)) {
				EditorExtensions::Unload(ModuleBase());
			}
			BuildStepRegistry::Shutdown();
			BuildTargetRegistry::Shutdown();
			SettingsPresentation::Terminate();

			PanelManager::Terminate();
			PropertyDrawers::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			FileSystem::Unmount("engine");
			std::error_code ec;
			fs::remove_all(_root.parent_path(), ec);
		}

		const void* ModuleBase() const { return ModuleSystem::GetNativeHandle(_module); }

		str Root() const { return _root.generic_string(); }

		static void WriteFile(const fs::path& path, const str& text) {
			std::error_code ec;
			fs::create_directories(path.parent_path(), ec);
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << text;
		}

		void WriteProfile(const str& name, const str& target) const {
			BuildProfile profile;
			profile.name = name;
			profile.target = target;
			Vector<str> scenes{GetMemoryResource(MemTag::Editor)};
			scenes.push_back("Assets/Scenes/Main.scene");
			profile.SetList("scenes", scenes);
			ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), profile));
		}

		bool ReloadThrough(const char* binary) {
			EventBus& bus = EventBus::GetGlobal();
			EditorEvents::ModuleReloadBegan.Publish(bus, {ModuleBase()});
			Path path{binary};
			bool succeeded = ModuleSystem::Reload(_module, &path);
			EditorEvents::ModuleReloadEnded.Publish(bus, {succeeded, ModuleBase()});
			return succeeded;
		}

		static void NeuterStepsThatNeedARealProject() {
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
			BuildStepRegistry::Register("cook-content", BuildPhase::Stage,
										[](BuildContext&) { return BuildStepStatus::Succeeded; });
		}

		void BuildProfileNamed(const str& name) {
			BuildRequest request;
			request.projectPath = Root();
			request.profiles.push_back(name);
			str refusal;
			ASSERT_TRUE(_pipeline.Request(request, refusal)) << refusal;
			for (int i = 0; i < 5000 && _pipeline.IsBusy(); ++i) {
				_pipeline.PumpMainThread();
			}
			ASSERT_FALSE(_pipeline.IsBusy());
		}

		static const BuildStepDesc* FindStep(const Vector<BuildStepDesc>& steps, std::string_view name) {
			for (const BuildStepDesc& step : steps) {
				if (step.name == name) {
					return &step;
				}
			}
			return nullptr;
		}

		static str ReadText(const fs::path& path) {
			std::ifstream file(path, std::ios::binary);
			return str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		}
	};

	struct Harness {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		explicit Harness(Handle<EditorPanel> opened) : context(MakeDesc(), &fonts), panel(opened) {
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {900, 700};
			return desc;
		}
	};

	void CollectTexts(UI::Context& context, UI::ElementHandle from, Vector<str>& out) {
		str text = context.GetTextString(from);
		if (!text.empty()) {
			out.push_back(std::move(text));
		}
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectTexts(context, handle, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

	bool SaysExactly(UI::Context& context, UI::ElementHandle from, std::string_view wanted) {
		Vector<str> texts{GetMemoryResource(MemTag::Editor)};
		CollectTexts(context, from, texts);
		for (const str& text : texts) {
			if (text == wanted) {
				return true;
			}
		}
		return false;
	}

	bool SaysSomethingContaining(UI::Context& context, UI::ElementHandle from, std::string_view fragment) {
		Vector<str> texts{GetMemoryResource(MemTag::Editor)};
		CollectTexts(context, from, texts);
		for (const str& text : texts) {
			if (text.find(fragment) != str::npos) {
				return true;
			}
		}
		return false;
	}

} // anonymous namespace

TEST_F(BuildExtensionModuleFixture, ModuleRegistersStepAndTarget) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}

	const IBuildTarget* target = BuildTargetRegistry::Find(PROBE_TARGET);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(target->GetDisplayName(), PROBE_TARGET_NAME);

	Vector<BuildStepDesc> steps{GetMemoryResource(MemTag::Editor)};
	BuildStepRegistry::GetSteps(steps);
	const BuildStepDesc* probe = FindStep(steps, PROBE_STEP);
	ASSERT_NE(probe, nullptr);
	EXPECT_EQ(probe->owner, ModuleBase());
	EXPECT_EQ(probe->phase, BuildPhase::PostBuild);

	const BuildStepDesc* builtIn = FindStep(steps, "post-build-command");
	ASSERT_NE(builtIn, nullptr);
	EXPECT_LT(builtIn - steps.data(), probe - steps.data());

	EXPECT_EQ(FindStep(steps, "compile")->owner, nullptr);
	EXPECT_NE(BuildTargetRegistry::Find("Windows"), nullptr);
}

TEST_F(BuildExtensionModuleFixture, ModulesTargetContributesSchemaKey) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}

	Vector<BuildSettingDesc> schema{GetMemoryResource(MemTag::Editor)};
	BuildTargetRegistry::GetSchemaFor(PROBE_TARGET, schema);
	const BuildSettingDesc* signing = nullptr;
	for (const BuildSettingDesc& desc : schema) {
		if (desc.key == "signingIdentity") {
			signing = &desc;
		}
	}
	ASSERT_NE(signing, nullptr);
	EXPECT_EQ(signing->defaultValue, "probe-v1");

	Vector<BuildSettingDesc> windows{GetMemoryResource(MemTag::Editor)};
	BuildTargetRegistry::GetSchemaFor("Windows", windows);
	for (const BuildSettingDesc& desc : windows) {
		EXPECT_NE(desc.key, "signingIdentity");
	}

	WriteProfile("Console", PROBE_TARGET);
	ResolvedProfile resolved;
	BuildProfileOps::ResolveProfile(Root(), "Console", schema, resolved);
	const ResolvedSetting* setting = resolved.Find("signingIdentity");
	ASSERT_NE(setting, nullptr);
	EXPECT_EQ(setting->value, "probe-v1");
}

TEST_F(BuildExtensionModuleFixture, ModuleStepRunsForItsTarget) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	NeuterStepsThatNeedARealProject();
	WriteProfile("Console", PROBE_TARGET);
	WriteProfile("Elsewhere", "FixtureTarget");

	BuildProfileNamed("Console");
	ASSERT_EQ(_pipeline.GetState(), BuildState::Succeeded) << _pipeline.GetResult().profiles[0].reason;

	fs::path signature = _root / "Builds" / "Console" / "Signature.txt";
	ASSERT_TRUE(fs::exists(signature));
	EXPECT_EQ(ReadText(signature), "signed by v1 as probe-v1\n");

	BuildProfileNamed("Elsewhere");
	ASSERT_EQ(_pipeline.GetState(), BuildState::Succeeded);
	EXPECT_FALSE(fs::exists(_root / "Builds" / "Elsewhere" / "Signature.txt"));
}

TEST_F(BuildExtensionModuleFixture, StatusCountsModuleStep) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	NeuterStepsThatNeedARealProject();
	WriteProfile("Console", PROBE_TARGET);

	Vector<BuildStepDesc> steps{GetMemoryResource(MemTag::Editor)};
	BuildStepRegistry::GetSteps(steps);

	str label;
	BuildRequest request;
	request.projectPath = Root();
	request.profiles.push_back("Console");
	str refusal;
	ASSERT_TRUE(_pipeline.Request(request, refusal)) << refusal;
	for (int i = 0; i < 5000 && _pipeline.IsBusy(); ++i) {
		_pipeline.PumpMainThread();
		if (_pipeline.GetStepName() == PROBE_STEP && label.empty()) {
			label = _pipeline.GetStatusLabel();
		}
	}
	EXPECT_EQ(label, std::format("step {} of {}: {}", steps.size(), steps.size(), PROBE_STEP));
}

TEST_F(BuildExtensionModuleFixture, ReloadingReplacesRegistrationsWithNewImages) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	NeuterStepsThatNeedARealProject();
	WriteProfile("Console", PROBE_TARGET);

	size_t stepsBefore = 0;
	{
		Vector<BuildStepDesc> before{GetMemoryResource(MemTag::Editor)};
		BuildStepRegistry::GetSteps(before);
		stepsBefore = before.size();
	}

	ASSERT_TRUE(ReloadThrough(V2_DLL));

	Vector<BuildStepDesc> after{GetMemoryResource(MemTag::Editor)};
	BuildStepRegistry::GetSteps(after);
	EXPECT_EQ(after.size(), stepsBefore);

	const BuildStepDesc* probe = FindStep(after, PROBE_STEP);
	ASSERT_NE(probe, nullptr);
	EXPECT_EQ(probe->owner, ModuleBase());

	Vector<BuildSettingDesc> schema{GetMemoryResource(MemTag::Editor)};
	BuildTargetRegistry::GetSchemaFor(PROBE_TARGET, schema);
	bool sawV2Default = false;
	for (const BuildSettingDesc& desc : schema) {
		sawV2Default |= desc.key == "signingIdentity" && desc.defaultValue == "probe-v2";
	}
	EXPECT_TRUE(sawV2Default);

	BuildProfileNamed("Console");
	ASSERT_EQ(_pipeline.GetState(), BuildState::Succeeded);
	EXPECT_EQ(ReadText(_root / "Builds" / "Console" / "Signature.txt"), "signed by v2 as probe-v2\n");
}

TEST_F(BuildExtensionModuleFixture, ReloadCyclesLeaveNoResidue) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	size_t stepsBefore = 0;
	{
		Vector<BuildStepDesc> before{GetMemoryResource(MemTag::Editor)};
		BuildStepRegistry::GetSteps(before);
		stepsBefore = before.size();
	}
	Vector<str> targetsBefore{GetMemoryResource(MemTag::Editor)};
	BuildTargetRegistry::GetTargetIds(targetsBefore);

	for (int cycle = 0; cycle < 3; ++cycle) {
		ASSERT_TRUE(ReloadThrough(V2_DLL)) << "cycle " << cycle;
		ASSERT_TRUE(ReloadThrough(V1_DLL)) << "cycle " << cycle;

		size_t steps = 0;
		{
			Vector<BuildStepDesc> live{GetMemoryResource(MemTag::Editor)};
			BuildStepRegistry::GetSteps(live);
			steps = live.size();
		}
		EXPECT_EQ(steps, stepsBefore) << "cycle " << cycle;
		Vector<str> targets{GetMemoryResource(MemTag::Editor)};
		BuildTargetRegistry::GetTargetIds(targets);
		EXPECT_EQ(targets.size(), targetsBefore.size()) << "cycle " << cycle;
	}
}

TEST_F(BuildExtensionModuleFixture, UnloadSweepsModuleOnly) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	SettingsPresentation::RegisterCategory("FixturePage", SettingsCategoryPresentation{});

	size_t stepsWithModule = 0;
	{
		Vector<BuildStepDesc> withModule{GetMemoryResource(MemTag::Editor)};
		BuildStepRegistry::GetSteps(withModule);
		ASSERT_NE(FindStep(withModule, PROBE_STEP), nullptr);
		stepsWithModule = withModule.size();
	}

	EventBus& bus = EventBus::GetGlobal();
	EditorEvents::ModuleReloadBegan.Publish(bus, {ModuleBase()});

	Vector<BuildStepDesc> swept{GetMemoryResource(MemTag::Editor)};
	BuildStepRegistry::GetSteps(swept);
	EXPECT_EQ(FindStep(swept, PROBE_STEP), nullptr);
	EXPECT_EQ(swept.size(), stepsWithModule - 1);
	EXPECT_EQ(BuildTargetRegistry::Find(PROBE_TARGET), nullptr);
	EXPECT_EQ(SettingsPresentation::FindCategory(PROBE_TARGET), nullptr);
	EXPECT_EQ(SettingsPresentation::FindField(PROBE_TARGET, "threshold"), nullptr);

	EXPECT_NE(FindStep(swept, "compile"), nullptr);
	EXPECT_NE(FindStep(swept, "post-build-command"), nullptr);
	EXPECT_NE(BuildTargetRegistry::Find("Windows"), nullptr);
	EXPECT_NE(BuildTargetRegistry::Find("WindowsServer"), nullptr);
	EXPECT_NE(BuildTargetRegistry::Find("FixtureTarget"), nullptr);
	EXPECT_NE(SettingsPresentation::FindCategory("FixturePage"), nullptr);

	ASSERT_TRUE(ModuleSystem::Unload(_module));
}

TEST_F(BuildExtensionModuleFixture, SettingsPageFollowsImage) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	const SettingsCategoryPresentation* page = SettingsPresentation::FindCategory(PROBE_TARGET);
	ASSERT_NE(page, nullptr);
	EXPECT_EQ(page->label, "Hot Reload Probe");

	const SettingsFieldPresentation* row = SettingsPresentation::FindField(PROBE_TARGET, "threshold");
	ASSERT_NE(row, nullptr);
	EXPECT_EQ(row->keywords, "probe-v1");

	ASSERT_TRUE(ReloadThrough(V2_DLL));
	row = SettingsPresentation::FindField(PROBE_TARGET, "threshold");
	ASSERT_NE(row, nullptr);
	EXPECT_EQ(row->keywords, "probe-v2");
}

TEST_F(BuildExtensionModuleFixture, ProfileWithGoneTargetSkipped) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	NeuterStepsThatNeedARealProject();
	WriteProfile("Console", PROBE_TARGET);

	EventBus& bus = EventBus::GetGlobal();
	EditorEvents::ModuleReloadBegan.Publish(bus, {ModuleBase()});
	ASSERT_TRUE(ModuleSystem::Unload(_module));

	BuildProfileNamed("Console");
	const BuildResult& result = _pipeline.GetResult();
	ASSERT_EQ(result.profiles.size(), 1u);
	EXPECT_EQ(result.profiles[0].outcome, BuildOutcome::Skipped);
	EXPECT_NE(result.profiles[0].reason.find(PROBE_TARGET), str::npos) << result.profiles[0].reason;
	EXPECT_FALSE(fs::exists(_root / "Builds" / "Console" / "Signature.txt"));

	BuildProfile onDisk;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Console"), onDisk));
	EXPECT_EQ(onDisk.target, PROBE_TARGET);
}

TEST_F(BuildExtensionModuleFixture, PanelMarksModulesStepAndOffersTarget) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	WriteProfile("Console", PROBE_TARGET);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("BuildProfilesPanel")));
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, PROBE_STEP));
	EXPECT_TRUE(SaysExactly(harness.context, harness.root, "from a module"));

	EXPECT_TRUE(SaysExactly(harness.context, harness.root, PROBE_TARGET_NAME));

	EXPECT_TRUE(SaysExactly(harness.context, harness.root, "Signing Identity"));
}

TEST_F(BuildExtensionModuleFixture, PanelReportsGoneTarget) {
	if (!_haveExtensions) {
		GTEST_SKIP() << "module editor extensions are shared link only";
	}
	WriteProfile("Console", PROBE_TARGET);

	EventBus& bus = EventBus::GetGlobal();
	EditorEvents::ModuleReloadBegan.Publish(bus, {ModuleBase()});
	ASSERT_TRUE(ModuleSystem::Unload(_module));

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("BuildProfilesPanel")));
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysExactly(harness.context, harness.root, str(PROBE_TARGET) + " (not on this machine)"));
	EXPECT_FALSE(SaysExactly(harness.context, harness.root, PROBE_TARGET_NAME));

	BuildProfile onDisk;
	ASSERT_TRUE(BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Console"), onDisk));
	EXPECT_EQ(onDisk.target, PROBE_TARGET);
}
