#include "EditorConfig.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/Window.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/EditorLayoutOps.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/PackageOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ScriptProjectOps.h"

#include <charconv>
#include <format>

namespace Shalltear {

	namespace {

		constexpr f32 AUTOSAVE_SECONDS = 5.0f;

		Path GetConfigDir() {
			Path projectDir = FileSystem::Resolve(Path("project://Library/Editor"));
			if (projectDir.ToString().find("://") == str::npos) {
				return projectDir;
			}
			return Path("Library/Editor");
		}

	} // anonymous namespace

	namespace {
		EditorConfig* _activeEditorConfig = nullptr;
	} // namespace

	EditorConfig* EditorConfig::GetActive() {
		return _activeEditorConfig;
	}

	EditorConfig::EditorConfig() {
		if (!_activeEditorConfig) {
			_activeEditorConfig = this;
		}
	}

	EditorConfig::~EditorConfig() {
		if (_activeEditorConfig == this) {
			_activeEditorConfig = nullptr;
		}
	}

	void EditorConfig::SaveSettings() {
		Path dir = GetConfigDir();
		FileSystem::CreateDirectory(dir);
		_settings.Save(dir / "Settings.ini");
	}

	void EditorConfig::LoadSettings() {
		_settings.Load(GetConfigDir() / "Settings.ini");

		Vector<str> recents{GetMemoryResource(MemTag::Editor)};
		for (u32 i = 0;; ++i) {
			str path = _settings.GetString("RecentScenes", std::format("path{}", i));
			if (path.empty()) {
				break;
			}
			recents.push_back(std::move(path));
		}
		for (size_t i = recents.size(); i > 0; --i) {
			EditorSceneOps::AddRecentScene(recents[i - 1]);
		}

		_restoredSceneSetup.clear();
		for (u32 i = 0;; ++i) {
			str path = _settings.GetString("SceneSetup", std::format("path{}", i));
			if (path.empty()) {
				break;
			}
			_restoredSceneSetup.push_back(std::move(path));
		}
	}

	str EditorConfig::TakePendingScene() {
		str pending = _settings.GetString("SceneSetup", "pending");
		if (!pending.empty()) {
			SetPendingScene({});
		}
		return pending;
	}

	void EditorConfig::SetPendingScene(std::string_view virtualPath) {
		_settings.SetString("SceneSetup", "pending", str(virtualPath));
		SaveSettings();
	}

	void EditorConfig::ApplyModuleSettings(str& modulePath) {
		ModulePreferences* live = ModuleCategory();
		if (!live) {
			return;
		}
		if (modulePath.empty()) {
			modulePath = live->binaryPath;
			return;
		}
		if (live->binaryPath == modulePath) {
			return;
		}
		live->binaryPath = modulePath;
		Settings::Save();
	}

	EditorConfig::ScriptPreferences* EditorConfig::ScriptCategory() {
		return Settings::DeclareCategory<ScriptPreferences>("Editor/Scripting", SettingScope::User);
	}

	EditorConfig::BuildPreferences* EditorConfig::BuildCategory() {
		return Settings::DeclareCategory<BuildPreferences>("Editor/Build", SettingScope::User);
	}

	EditorConfig::ModulePreferences* EditorConfig::ModuleCategory() {
		return Settings::DeclareCategory<ModulePreferences>("Editor/Module", SettingScope::UserProject);
	}

	EditorConfig::PlayPreferences* EditorConfig::PlayCategory() {
		return Settings::DeclareCategory<PlayPreferences>("Editor/Play", SettingScope::User);
	}

	EditorConfig::GeneralPreferences* EditorConfig::GeneralCategory() {
		return Settings::DeclareCategory<GeneralPreferences>("Editor/General", SettingScope::User);
	}

	EditorConfig::GraphPreferences* EditorConfig::GraphCategory() {
		return Settings::DeclareCategory<GraphPreferences>("Editor/Graph", SettingScope::User);
	}

	void EditorConfig::DeclareCategories() {
		(void)ScriptCategory();
		(void)BuildCategory();
		(void)ModuleCategory();
		(void)PlayCategory();
		(void)GeneralCategory();
		(void)GraphCategory();
		PackageOps::DeclareSettings();
	}

	EditorConfig::ScriptPreferences EditorConfig::ReadScriptPreferences() {
		const ScriptPreferences* live = ScriptCategory();
		return live ? *live : ScriptPreferences{};
	}

	void EditorConfig::WriteScriptPreferences(const ScriptPreferences& preferences) {
		ScriptPreferences* live = ScriptCategory();
		if (!live) {
			return;
		}
		*live = preferences;
		Settings::Save();
	}

	EditorConfig::BuildPreferences EditorConfig::ReadBuildPreferences() {
		const BuildPreferences* live = BuildCategory();
		return live ? *live : BuildPreferences{};
	}

	void EditorConfig::WriteBuildPreferences(const BuildPreferences& preferences) {
		BuildPreferences* live = BuildCategory();
		if (!live) {
			return;
		}
		*live = preferences;
		Settings::Save();
	}

	EditorConfig::PlayPreferences EditorConfig::ReadPlayPreferences() {
		const PlayPreferences* live = PlayCategory();
		return live ? *live : PlayPreferences{};
	}

	EditorConfig::GeneralPreferences EditorConfig::ReadGeneralPreferences() {
		const GeneralPreferences* live = GeneralCategory();
		return live ? *live : GeneralPreferences{};
	}

	void EditorConfig::WriteGeneralPreferences(const GeneralPreferences& preferences) {
		GeneralPreferences* live = GeneralCategory();
		if (!live) {
			return;
		}
		*live = preferences;
		Settings::Save();
	}

	EditorConfig::GraphPreferences EditorConfig::ReadGraphPreferences() {
		const GraphPreferences* live = GraphCategory();
		return live ? *live : GraphPreferences{};
	}

	void EditorConfig::MigrateSettingsIniToStore(bool haveProject) {
		const bool haveModuleKey = haveProject && _settings.Has("Module", "path");
		const bool hasAny = _settings.Has("Script", "autoCompile") || _settings.Has("Script", "autoBuild") ||
							_settings.Has("Script", "editorCommand") || _settings.Has("Script", "cmake") ||
							_settings.Has("Script", "ninja") || _settings.Has("Script", "compiler") ||
							_settings.Has("Build", "offerGitignore") || haveModuleKey;
		if (!hasAny) {
			return;
		}

		ScriptPreferences* script = ScriptCategory();
		BuildPreferences* build = BuildCategory();
		ModulePreferences* module = ModuleCategory();
		if (!script || !build || !module) {
			return;
		}

		if (_settings.Has("Script", "autoCompile")) {
			script->autoCompile = _settings.GetBool("Script", "autoCompile", script->autoCompile);
		} else if (_settings.Has("Script", "autoBuild")) {
			script->autoCompile = _settings.GetBool("Script", "autoBuild", script->autoCompile);
		}
		if (_settings.Has("Script", "editorCommand")) {
			script->externalEditorCommand = _settings.GetString("Script", "editorCommand");
		}
		if (_settings.Has("Script", "cmake")) {
			script->cmakePath = _settings.GetString("Script", "cmake");
		}
		if (_settings.Has("Script", "ninja")) {
			script->ninjaPath = _settings.GetString("Script", "ninja");
		}
		if (_settings.Has("Script", "compiler")) {
			script->compilerPath = _settings.GetString("Script", "compiler");
		}
		if (_settings.Has("Build", "offerGitignore")) {
			build->offerGitignore = _settings.GetBool("Build", "offerGitignore", build->offerGitignore);
		}
		if (haveModuleKey) {
			module->binaryPath = _settings.GetString("Module", "path");
		}

		const char* const scriptKeys[] = {"autoCompile", "autoBuild", "editorCommand", "cmake", "ninja", "compiler"};
		for (const char* key : scriptKeys) {
			_settings.Remove("Script", key);
		}
		_settings.Remove("Build", "offerGitignore");
		if (haveModuleKey) {
			_settings.Remove("Module", "path");
		}

		Settings::Save();
		SaveSettings();
		SLT_LOG_INFO("Editor", "moved the [Script], [Build] and [Module] preferences out of Settings.ini into the settings store");
	}

	void EditorConfig::MigrateLegacyScriptSettings(const str& projectPath) {
		MigrateSettingsIniToStore(!projectPath.empty());

		if (projectPath.empty()) {
			return;
		}

		BuildProfileOps::MigrateStaticSDKRootToUserScope(projectPath);

		ScriptProjectOps::ScriptSettings settings = ScriptProjectOps::ReadScriptSettings(projectPath);
		if (settings.compileCommandIsLegacy) {
			if (ScriptProjectOps::WriteScriptSettings(projectPath, settings)) {
				SLT_LOG_INFO("Editor", "renamed Project.ini [Script] buildCommand to compileCommand (value kept: {})",
							 settings.compileCommand);
				settings.compileCommandIsLegacy = false;
			}
		}

		str legacy = _settings.GetString("Module", "build");
		if (legacy.empty()) {
			return;
		}
		if (settings.compileCommand.empty()) {
			settings.compileCommand = legacy;
			if (!ScriptProjectOps::WriteScriptSettings(projectPath, settings)) {
				return;
			}
			SLT_LOG_INFO("Editor", "moved the legacy --module-build command into Project.ini [Script] compileCommand: {}", legacy);
		} else {
			SLT_LOG_INFO("Editor", "dropped the legacy --module-build command (Project.ini [Script] compileCommand already sets one)");
		}
		_settings.Remove("Module", "build");
		SaveSettings();
	}

	bool EditorConfig::RestoreLayout(Handle<Window> mainWindow) {
		Path file = GetConfigDir() / "Layout.ini";
		if (!FileSystem::Exists(file)) {
			return false;
		}
		return EditorLayoutOps::ApplyLayout(mainWindow, FileSystem::ReadTextFile(file, MemTag::Editor));
	}

	void EditorConfig::SaveNow(Handle<Window> mainWindow) {
		if (!mainWindow.IsValid()) {
			return;
		}
		Path dir = GetConfigDir();
		FileSystem::CreateDirectory(dir);
		str layout = EditorLayoutOps::CaptureLayout(mainWindow);
		if (FileSystem::WriteTextFile(dir / "Layout.ini", layout)) {
			_lastSavedLayout = layout;
		}
		EditorLayoutOps::SaveActiveLayout(mainWindow);

		_settings.RemoveSection("RecentScenes");
		Vector<str> recents{GetMemoryResource(MemTag::Editor)};
		EditorSceneOps::GetRecentScenes(recents);
		for (size_t i = 0; i < recents.size(); ++i) {
			_settings.SetString("RecentScenes", std::format("path{}", i), recents[i]);
		}

		str pending = _settings.GetString("SceneSetup", "pending");
		_settings.RemoveSection("SceneSetup");
		Vector<str> setup{GetMemoryResource(MemTag::Editor)};
		EditorSceneOps::GetSceneSetup(setup);
		for (size_t i = 0; i < setup.size(); ++i) {
			_settings.SetString("SceneSetup", std::format("path{}", i), setup[i]);
		}
		if (!pending.empty()) {
			_settings.SetString("SceneSetup", "pending", pending);
		}
		_settings.Save(dir / "Settings.ini");
	}

	void EditorConfig::TickAutosave(f32 deltaSeconds, Handle<Window> mainWindow) {
		_autosaveAccum += deltaSeconds;
		if (_autosaveAccum < AUTOSAVE_SECONDS) {
			return;
		}
		_autosaveAccum = 0.0f;
		if (!mainWindow.IsValid()) {
			return;
		}
		str layout = EditorLayoutOps::CaptureLayout(mainWindow);
		if (layout == _lastSavedLayout) {
			return;
		}
		Path dir = GetConfigDir();
		FileSystem::CreateDirectory(dir);
		if (FileSystem::WriteTextFile(dir / "Layout.ini", layout)) {
			_lastSavedLayout = layout;
		}
	}

} // namespace Shalltear
