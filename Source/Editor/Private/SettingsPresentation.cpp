#include "Shalltear/Editor/SettingsPresentation.h"

#include "Shalltear/Core/Containers.h"

#include "Shalltear/Core/Screen.h"
#include "Shalltear/Editor/EditorIdleOps.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct CategoryEntry {
			str path;
			SettingsCategoryPresentation presentation;
			const void* owner = nullptr;
		};

		struct FieldEntry {
			str path;
			str field;
			SettingsFieldPresentation presentation;
			const void* owner = nullptr;
		};

		struct RegistryState {
			Vector<CategoryEntry> categories{GetMemoryResource(MemTag::Editor)};
			Vector<FieldEntry> fields{GetMemoryResource(MemTag::Editor)};
		};

		RegistryState& State() {
			static RegistryState state;
			return state;
		}

	} // namespace

	void SettingsPresentation::Terminate() {
		RegistryState& state = State();
		state.categories.clear();
		state.categories.shrink_to_fit();
		state.fields.clear();
		state.fields.shrink_to_fit();
	}

	void SettingsPresentation::RegisterCategory(std::string_view categoryPath, SettingsCategoryPresentation presentation, const void* owner) {
		RegistryState& state = State();
		for (CategoryEntry& entry : state.categories) {
			if (entry.path == categoryPath) {
				entry.presentation = std::move(presentation);
				entry.owner = owner;
				return;
			}
		}
		state.categories.push_back({str(categoryPath), std::move(presentation), owner});
	}

	void SettingsPresentation::RegisterField(std::string_view categoryPath, std::string_view fieldName, SettingsFieldPresentation presentation, const void* owner) {
		RegistryState& state = State();
		for (FieldEntry& entry : state.fields) {
			if (entry.path == categoryPath && entry.field == fieldName) {
				entry.presentation = std::move(presentation);
				entry.owner = owner;
				return;
			}
		}
		state.fields.push_back({str(categoryPath), str(fieldName), std::move(presentation), owner});
	}

	u32 SettingsPresentation::UnregisterOwnedBy(const void* owner) {
		if (!owner) {
			return 0;
		}
		RegistryState& state = State();
		u32 removed = 0;
		removed += static_cast<u32>(std::erase_if(state.categories, [owner](const CategoryEntry& entry) {
			return entry.owner == owner;
		}));
		removed += static_cast<u32>(std::erase_if(state.fields, [owner](const FieldEntry& entry) {
			return entry.owner == owner;
		}));
		return removed;
	}

	const SettingsCategoryPresentation* SettingsPresentation::FindCategory(std::string_view categoryPath) {
		for (const CategoryEntry& entry : State().categories) {
			if (entry.path == categoryPath) {
				return &entry.presentation;
			}
		}
		return nullptr;
	}

	const SettingsFieldPresentation* SettingsPresentation::FindField(std::string_view categoryPath, std::string_view fieldName) {
		for (const FieldEntry& entry : State().fields) {
			if (entry.path == categoryPath && entry.field == fieldName) {
				return &entry.presentation;
			}
		}
		return nullptr;
	}

	void SettingsPresentation::RegisterBuiltIns() {
		{
			SettingsCategoryPresentation category;
			category.label = "Graphics";
			category.tooltip = "Rendering device and how frames are presented.";
			category.audience = SettingsAudience::EditorAndPlayer;
			category.keywords = "gpu device swapchain vulkan";
			category.order = 10;
			RegisterCategory("Graphics", std::move(category));

			SettingsFieldPresentation vsync;
			vsync.label = "V-Sync";
			vsync.tooltip = "Synchronizes frames with the display's refresh rate (for every window). \"Window Hint\" lets each window decide. "
							"Changing it causes a one-frame hitch.";
			vsync.keywords = "vertical sync tearing present framerate refresh";
			RegisterField("Graphics", "vsync", std::move(vsync));

			SettingsFieldPresentation adapter;
			adapter.label = "Adapter Index";
			adapter.tooltip = "GPU to render with. -1 picks one automatically.";
			adapter.keywords = "gpu graphics card device";
			RegisterField("Graphics", "adapter", std::move(adapter));

			SettingsFieldPresentation validation;
			validation.label = "Validation Layers";
			validation.tooltip = "Enables Vulkan validation. On by default in Debug builds. Requires the Vulkan validation layer.";
			validation.keywords = "vulkan debug layers khronos";
			RegisterField("Graphics", "validation", std::move(validation));

			SettingsFieldPresentation units;
			units.label = "Physical Light Units";
			units.tooltip = "Measures light intensity in physical units: lux for dir lights, lumens for point/spot lights, "
							"nits for the sky (all viewed through the Environment's exposure). When off, intensity uses an arbitrary scale.";
			units.keywords = "lumens lux candela photometric intensity exposure";
			RegisterField("Graphics", "physicalLightUnits", std::move(units));

			SettingsFieldPresentation profiles;
			profiles.label = "Quality Profiles";
			profiles.tooltip = "Graphics Profile asset ids separated by ';', in quality-level order. Empty uses the built-in defaults. "
							   "Create profiles with Assets > Create > Graphics Profile.";
			profiles.keywords = "quality level tier profile msaa shadows lights preset";
			RegisterField("Graphics", "qualityProfiles", std::move(profiles));

			SettingsFieldPresentation level;
			level.label = "Quality Level";
			level.tooltip = "Active profile, counting from 0. -1 uses the built-in defaults.";
			level.keywords = "quality level tier active current";
			RegisterField("Graphics", "qualityLevel", std::move(level));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Time";
			category.tooltip = "Simulation's step rate + how much a slow frame can catch up.";
			category.audience = SettingsAudience::EditorAndPlayer;
			category.keywords = "fixed timestep physics simulation rate delta hitch framerate";
			category.order = 12;
			RegisterCategory("Time", std::move(category));

			SettingsFieldPresentation fixedStep;
			fixedStep.label = "Fixed Timestep";
			fixedStep.tooltip = "Seconds of game time per FixedUpdate step. 0.0166667 is 60 steps a second.";
			fixedStep.keywords = "fixedupdate step simulation physics rate";
			RegisterField("Time", "fixedDeltaTime", std::move(fixedStep));

			SettingsFieldPresentation maximumDelta;
			maximumDelta.label = "Maximum Allowed Timestep";
			maximumDelta.tooltip = "Longest time step one frame can simulate after a hitch. "
								   "A frame runs at most Maximum Allowed Timestep / Fixed Timestep fixed steps.";
			maximumDelta.keywords = "hitch spike catch up spiral clamp maximum delta";
			RegisterField("Time", "maximumDeltaTime", std::move(maximumDelta));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Player";
			category.tooltip = "Built game's window: its mode, size and fullscreen switching.";
			category.audience = SettingsAudience::PlayerOnly;
			category.keywords = "resolution presentation fullscreen borderless windowed maximized window screen display monitor";
			category.order = 13;
			RegisterCategory("Player", std::move(category));

			const i64 windowed = static_cast<i64>(FullScreenMode::Windowed);

			SettingsFieldPresentation mode;
			mode.label = "Fullscreen Mode";
			mode.tooltip = "Window mode the game starts in. Fullscreen Window covers the monitor without a frame, Maximized Window "
						   "keeps the title bar, Windowed opens a centered window (of the size below). A mode players pick in "
						   "the game/with Alt+Enter is used at the next launch (until this setting changes).";
			mode.keywords = "fullscreen borderless windowed maximized startup";
			RegisterField("Player", "fullScreenMode", std::move(mode));

			SettingsFieldPresentation native;
			native.label = "Default Is Native Resolution";
			native.tooltip = "Runs fullscreen at the monitor's resolution. When off, the game renders at the width/height below + "
							 "scales to fit the screen (black bars if the aspect ratios differ).";
			native.keywords = "native resolution monitor";
			native.shownWhen.push_back({"fullScreenMode", windowed, false});
			RegisterField("Player", "defaultIsNativeResolution", std::move(native));

			SettingsFieldPresentation width;
			width.label = "Default Screen Width";
			width.tooltip = "Width in pxs of the window (or of the fullscreen image when not at native resolution).";
			width.keywords = "resolution size pixels";
			width.shownWhen.push_back({"fullScreenMode", windowed});
			width.shownWhen.push_back({"defaultIsNativeResolution", 0});
			SettingsFieldPresentation height = width;
			height.label = "Default Screen Height";
			height.tooltip = "Height in pxs of the window (or of the fullscreen image when not at native resolution).";
			RegisterField("Player", "defaultScreenWidth", std::move(width));
			RegisterField("Player", "defaultScreenHeight", std::move(height));

			SettingsFieldPresentation resizable;
			resizable.label = "Resizable Window";
			resizable.tooltip = "Lets the player resize the game's window. The last size is remembered.";
			resizable.keywords = "resize drag window frame";
			RegisterField("Player", "resizableWindow", std::move(resizable));

			SettingsFieldPresentation fullscreenSwitch;
			fullscreenSwitch.label = "Allow Fullscreen Switch";
			fullscreenSwitch.tooltip = "Alt+Enter switches between fullscreen and windowed.";
			fullscreenSwitch.keywords = "alt enter toggle shortcut";
			RegisterField("Player", "allowFullscreenSwitch", std::move(fullscreenSwitch));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "General";
			category.tooltip = "How the editor behaves on this computer.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "idle throttle throttling frame rate fps vsync gpu cpu power fan battery";
			category.order = 5;
			RegisterCategory("Editor/General", std::move(category));

			SettingsFieldPresentation mode;
			mode.label = "Interaction Mode";
			mode.tooltip = "How long the editor can idle between frames when nothing changes. Exists to balance input lag and GPU load. Input ends the idle at once -> "
						   "play mode never idles. \"Default\" idles up to 4 ms, \"No Throttling\" never idles.";
			mode.keywords = "idle throttle frame rate fps gpu power default monitor refresh custom";
			RegisterField("Editor/General", "interactionMode", std::move(mode));

			SettingsFieldPresentation throttling;
			throttling.label = "Frame Throttling (ms)";
			throttling.tooltip = "Number of milliseconds the editor can idle between frames when Interaction Mode is \"Custom\".";
			throttling.keywords = "idle custom milliseconds";
			throttling.shownWhen.push_back({"interactionMode", static_cast<i64>(InteractionMode::Custom)});
			RegisterField("Editor/General", "frameThrottlingMilliseconds", std::move(throttling));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Play";
			category.tooltip = "Which view is shown when play mode starts and stops.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "play stop game view runtime focus tab";
			category.order = 10;
			RegisterCategory("Editor/Play", std::move(category));

			SettingsFieldPresentation focus;
			focus.label = "Focus Runtime View";
			focus.tooltip = "Brings the Runtime View to the front when play starts. \"Only With A Camera\" does so only when a loaded scene has a camera.";
			focus.keywords = "game view focus play unfocused camera";
			RegisterField("Editor/Play", "focusRuntimeView", std::move(focus));

			SettingsFieldPresentation restore;
			restore.label = "Restore View On Stop";
			restore.tooltip = "Brings back the tab that was in front before play started.";
			restore.keywords = "stop restore tab scene view";
			RegisterField("Editor/Play", "restoreViewOnStop", std::move(restore));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Graph";
			category.tooltip = "How the graph editors' grid works.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "node shader visual effect animator node grid snap snapping";
			category.order = 15;
			RegisterCategory("Editor/Graph", std::move(category));

			SettingsFieldPresentation grid;
			grid.label = "Grid Size";
			grid.tooltip = "Spacing of the grid in graph editors. Hold Ctrl while dragging nodes to snap them to it.";
			grid.keywords = "snap snapping spacing step align";
			RegisterField("Editor/Graph", "gridSize", std::move(grid));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Scripting";
			category.tooltip = "How the editor compiles the project's C++ and opens source files.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "compile build cmake ninja clang external editor";
			category.order = 20;
			RegisterCategory("Editor/Scripting", std::move(category));

			SettingsFieldPresentation autoCompile;
			autoCompile.label = "Auto Compile";
			autoCompile.tooltip = "Compiles the project's C++ when a source file changes + when the project opens "
								  "with changes. When off, compile with Scripts > Compile (F7).";
			RegisterField("Editor/Scripting", "autoCompile", std::move(autoCompile));

			SettingsFieldPresentation externalEditor;
			externalEditor.label = "External Editor";
			externalEditor.tooltip = "Command run to open a source file. Accepts {file}, {line} and {column}.";
			externalEditor.keywords = "ide vscode visual studio open";
			RegisterField("Editor/Scripting", "externalEditorCommand", std::move(externalEditor));

			SettingsFieldPresentation cmake;
			cmake.label = "CMake Path";
			cmake.tooltip = "Override the discovered cmake executable. Empty uses PATH.";
			RegisterField("Editor/Scripting", "cmakePath", std::move(cmake));

			SettingsFieldPresentation ninja;
			ninja.label = "Ninja Path";
			ninja.tooltip = "Override the discovered ninja executable. Empty uses PATH.";
			RegisterField("Editor/Scripting", "ninjaPath", std::move(ninja));

			SettingsFieldPresentation compiler;
			compiler.label = "Compiler Path";
			compiler.tooltip = "Override the discovered clang++ executable. Empty uses PATH.";
			RegisterField("Editor/Scripting", "compilerPath", std::move(compiler));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Build";
			category.tooltip = "How players are built on this computer.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "player ship package gitignore sdk";
			category.order = 30;
			RegisterCategory("Editor/Build", std::move(category));

			SettingsFieldPresentation offerGitignore;
			offerGitignore.label = "Offer To Ignore Builds/";
			offerGitignore.tooltip = "Ask once whether to append Builds/ to an existing project's .gitignore.";
			offerGitignore.keywords = "git version control";
			RegisterField("Editor/Build", "offerGitignore", std::move(offerGitignore));

			SettingsFieldPresentation staticSDKRoot;
			staticSDKRoot.label = "Static SDK Root";
			staticSDKRoot.tooltip = "Static Shalltear SDK players link with. Empty searches the SDK, sibling build trees and SHALLTEAR_SDK_STATIC.";
			staticSDKRoot.keywords = "static sdk package link player";
			RegisterField("Editor/Build", "staticSDKRoot", std::move(staticSDKRoot));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Module";
			category.tooltip = "Game module binary this project loads on this computer.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "dll binary game code hot reload";
			category.order = 40;
			RegisterCategory("Editor/Module", std::move(category));

			SettingsFieldPresentation binaryPath;
			binaryPath.label = "Module Binary";
			binaryPath.tooltip = "Overrides the module binary the editor loads, like --module. Empty uses the project's.";
			binaryPath.keywords = "module dll override";
			RegisterField("Editor/Module", "binaryPath", std::move(binaryPath));
		}

		{
			SettingsCategoryPresentation category;
			category.label = "Packages";
			category.tooltip = "Optional engine features/packages this project uses.";
			category.audience = SettingsAudience::EditorOnly;
			category.keywords = "package manager feature module physics animation enable disable";
			category.order = 15;
			RegisterCategory("Packages", std::move(category));

			SettingsFieldPresentation enabled;
			enabled.label = "Enabled Packages";
			enabled.tooltip = "Package names separated by ';'. Their dependencies are enabled automatically.";
			enabled.keywords = "enabled built-in optional";
			RegisterField("Packages", "enabled", std::move(enabled));
		}
	}

} // namespace Shalltear
