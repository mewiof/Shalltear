#pragma once

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorIdleOps.h"

namespace Shalltear {

	enum class PlayModeFocus : u8 {
		Always = 0,
		OnlyWithCamera,
		Never
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::PlayModeFocus)
SLT_ENUM_VALUE(Always)
SLT_ENUM_VALUE(OnlyWithCamera)
SLT_ENUM_VALUE(Never)
SLT_END_ENUM()

namespace Shalltear {

	class Window;

	class EditorConfig {
	public:
		EditorConfig();
		~EditorConfig();

		EditorConfig(const EditorConfig&) = delete;
		EditorConfig& operator=(const EditorConfig&) = delete;

		static EditorConfig* GetActive();

		struct ScriptPreferences {
			SLT_REFLECT_VALUE(ScriptPreferences)
			SLT_REFLECT_CONSTRUCTIBLE()
			SLT_REFLECT_FIELD(autoCompile)
			SLT_REFLECT_FIELD(externalEditorCommand)
			SLT_REFLECT_FIELD(cmakePath)
			SLT_REFLECT_FIELD(ninjaPath)
			SLT_REFLECT_FIELD(compilerPath)
			SLT_END_REFLECT()

			bool autoCompile = true;
			str externalEditorCommand;
			str cmakePath;
			str ninjaPath;
			str compilerPath;
		};

		struct ModulePreferences {
			SLT_REFLECT_VALUE(ModulePreferences)
			SLT_REFLECT_CONSTRUCTIBLE()
			SLT_REFLECT_FIELD(binaryPath)
			SLT_END_REFLECT()

			str binaryPath;
		};

		static void DeclareCategories();

		void LoadSettings();
		static void ApplyModuleSettings(str& modulePath);

		static ScriptPreferences ReadScriptPreferences();
		static void WriteScriptPreferences(const ScriptPreferences& preferences);

		struct BuildPreferences {
			SLT_REFLECT_VALUE(BuildPreferences)
			SLT_REFLECT_CONSTRUCTIBLE()
			SLT_REFLECT_FIELD(offerGitignore)
			SLT_REFLECT_FIELD(staticSDKRoot)
			SLT_END_REFLECT()

			bool offerGitignore = true;
			str staticSDKRoot;
		};

		static BuildPreferences ReadBuildPreferences();
		static void WriteBuildPreferences(const BuildPreferences& preferences);

		struct PlayPreferences {
			SLT_REFLECT_VALUE(PlayPreferences)
			SLT_REFLECT_CONSTRUCTIBLE()
			SLT_REFLECT_FIELD(focusRuntimeView)
			SLT_REFLECT_FIELD(restoreViewOnStop)
			SLT_END_REFLECT()

			PlayModeFocus focusRuntimeView = PlayModeFocus::OnlyWithCamera;
			bool restoreViewOnStop = true;
		};

		static PlayPreferences ReadPlayPreferences();

		struct GeneralPreferences {
			SLT_REFLECT_VALUE(GeneralPreferences)
			SLT_REFLECT_CONSTRUCTIBLE()
			SLT_REFLECT_FIELD(interactionMode)
			SLT_REFLECT_FIELD(frameThrottlingMilliseconds, SLT_RANGE(0, EditorIdleOps::CUSTOM_MAX_MILLISECONDS))
			SLT_REFLECT_FIELD(showSelectionOutline, SLT_HIDDEN)
			SLT_END_REFLECT()

			InteractionMode interactionMode = InteractionMode::Default;
			i32 frameThrottlingMilliseconds = EditorIdleOps::DEFAULT_IDLE_MILLISECONDS;
			bool showSelectionOutline = true;
		};

		static GeneralPreferences ReadGeneralPreferences();
		static void WriteGeneralPreferences(const GeneralPreferences& preferences);

		struct GraphPreferences {
			SLT_REFLECT_VALUE(GraphPreferences)
			SLT_REFLECT_CONSTRUCTIBLE()
			SLT_REFLECT_FIELD(gridSize, SLT_RANGE(GRAPH_GRID_MIN, GRAPH_GRID_MAX))
			SLT_END_REFLECT()

			static constexpr i32 GRAPH_GRID_MIN = 5;
			static constexpr i32 GRAPH_GRID_MAX = 200;
			i32 gridSize = 20;
		};

		static GraphPreferences ReadGraphPreferences();

		void MigrateLegacyScriptSettings(const str& projectPath);

		void MigrateSettingsIniToStore(bool haveProject);

		void SaveSettings();

		const Vector<str>& GetRestoredSceneSetup() const { return _restoredSceneSetup; }

		str TakePendingScene();
		void SetPendingScene(std::string_view virtualPath);

		bool RestoreLayout(Handle<Window> mainWindow);
		void SaveNow(Handle<Window> mainWindow);
		void TickAutosave(f32 deltaSeconds, Handle<Window> mainWindow);
	private:
		static ScriptPreferences* ScriptCategory();
		static BuildPreferences* BuildCategory();
		static ModulePreferences* ModuleCategory();
		static PlayPreferences* PlayCategory();
		static GeneralPreferences* GeneralCategory();
		static GraphPreferences* GraphCategory();

		Config _settings;
		Vector<str> _restoredSceneSetup{GetMemoryResource(MemTag::Editor)};
		str _lastSavedLayout;
		f32 _autosaveAccum = 0.0f;
	};

} // namespace Shalltear
