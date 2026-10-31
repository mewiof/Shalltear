#pragma once

#include "Shalltear/Core/Input/InputAction.h"
#include "Shalltear/Core/Input/InputEvent.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	namespace UI {
		class Context;
	}

	namespace EditorInputOps {

		struct RouteFacts {
			bool textFieldWantsKeyboard = false;
			bool pointerInside = false;
			bool gestureHeld = false;
			bool viewFocused = false;
			bool modalUp = false;
		};

		struct RouteNeeds {
			bool yieldToTextField = false;
			bool pointerInside = false;
			bool gestureHeld = false;
			bool viewFocused = false;
			bool yieldToModal = false;
		};

		namespace Routes {

			inline constexpr RouteNeeds GlobalShortcut{.yieldToModal = true};

			inline constexpr RouteNeeds DocumentShortcut{.yieldToTextField = true, .yieldToModal = true};

			inline constexpr RouteNeeds ViewportPointer{.pointerInside = true, .yieldToModal = true};

			inline constexpr RouteNeeds ViewportKeyboard{.yieldToTextField = true, .pointerInside = true, .yieldToModal = true};

			inline constexpr RouteNeeds HeldGesture{.gestureHeld = true};

			inline constexpr RouteNeeds GameplayInput{.yieldToTextField = true, .viewFocused = true, .yieldToModal = true};

			inline constexpr RouteNeeds GameplayPointer{.yieldToTextField = true, .pointerInside = true, .viewFocused = true, .yieldToModal = true};

			inline constexpr RouteNeeds PanelKeyboard{.yieldToTextField = true, .viewFocused = true, .yieldToModal = true};

			inline constexpr RouteNeeds PanelPointer{.pointerInside = true, .yieldToModal = true};

		} // namespace Routes

		SHALLTEAR_EDITOR_API bool ShouldOpen(const RouteNeeds& needs, const RouteFacts& facts);

		SHALLTEAR_EDITOR_API void ApplyRoute(InputActionMap& map, const RouteNeeds& needs, const RouteFacts& facts);

		SHALLTEAR_EDITOR_API RouteFacts ApplicationFacts();

		SHALLTEAR_EDITOR_API RouteFacts ApplicationFacts(const UI::Context& context);

		SHALLTEAR_EDITOR_API bool AdvanceGamePointerArm(bool armed, const InputEvent& event, bool aimedAtView);

		enum class Shortcut : u8 {
			ReloadModule,
			CompileModule,
			RecompileModule,
			OpenBuildProfiles,
			BuildAndRun,
			TogglePlay,
			TogglePause,
			Step,
			Undo,
			Redo,
			SaveScene,
			SaveSceneAs,
			DeleteSelection,
			RenameSelection,
			SelectAll,
			DuplicateSelection,

			Count
		};

		SHALLTEAR_EDITOR_API str ChordLabel(Shortcut shortcut);

		enum class PanelChord : u8 {
			AssetBrowserBack,
			AssetBrowserForward,
			AssetBrowserUp,

			Count
		};

		SHALLTEAR_EDITOR_API ActionId BindPanelChord(InputActionMap& map, PanelChord chord);
		SHALLTEAR_EDITOR_API str ChordLabel(PanelChord chord);

		class SHALLTEAR_EDITOR_API EditorShortcuts {
		public:
			void Build();

			void Route(const RouteFacts& facts);

			void Evaluate();

			bool WasPerformed(Shortcut shortcut) const;

			static RouteNeeds RouteFor(Shortcut shortcut);
		private:
			InputActionMap _global;
			InputActionMap _document;
			ActionId _ids[static_cast<u32>(Shortcut::Count)] = {};
		};

	} // namespace EditorInputOps

} // namespace Shalltear
