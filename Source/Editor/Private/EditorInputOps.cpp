#include "Shalltear/Editor/EditorInputOps.h"

#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/WindowSystem.h"

namespace Shalltear {

	namespace EditorInputOps {

		namespace {

			struct ShortcutDef {
				StringHash name;
				Key key;
				KeyMod mods;
				bool document;
			};

			constexpr ShortcutDef Shortcuts[static_cast<u32>(Shortcut::Count)] = {
				{"Editor.ReloadModule"_hash, Key::R, KeyMod::Ctrl, false},
				{"Editor.CompileModule"_hash, Key::F7, KeyMod::None, false},
				{"Editor.RecompileModule"_hash, Key::F7, KeyMod::Shift, false},
				{"Editor.OpenBuildProfiles"_hash, Key::B, KeyMod::Ctrl | KeyMod::Shift, false},
				{"Editor.BuildAndRun"_hash, Key::B, KeyMod::Ctrl, false},
				{"Editor.TogglePlay"_hash, Key::P, KeyMod::Ctrl, false},
				{"Editor.TogglePause"_hash, Key::P, KeyMod::Ctrl | KeyMod::Shift, false},
				{"Editor.Step"_hash, Key::P, KeyMod::Ctrl | KeyMod::Alt, false},
				{"Editor.Undo"_hash, Key::Z, KeyMod::Ctrl, true},
				{"Editor.Redo"_hash, Key::Y, KeyMod::Ctrl, true},
				{"Editor.SaveScene"_hash, Key::S, KeyMod::Ctrl, true},
				{"Editor.SaveSceneAs"_hash, Key::S, KeyMod::Ctrl | KeyMod::Shift, true},
				{"Editor.DeleteSelection"_hash, Key::Delete, KeyMod::None, true},
				{"Editor.RenameSelection"_hash, Key::F2, KeyMod::None, true},
				{"Editor.SelectAll"_hash, Key::A, KeyMod::Ctrl, true},
				{"Editor.DuplicateSelection"_hash, Key::D, KeyMod::Ctrl, true},
			};

			constexpr u32 SHORTCUT_COUNT = static_cast<u32>(Shortcut::Count);

			bool InRange(Shortcut shortcut) {
				return static_cast<u32>(shortcut) < SHORTCUT_COUNT;
			}

			struct PanelChordDef {
				StringHash name;
				Key key;
				KeyMod mods;
			};

			constexpr PanelChordDef PanelChords[static_cast<u32>(PanelChord::Count)] = {
				{"AssetBrowser.Back"_hash, Key::Left, KeyMod::Alt},
				{"AssetBrowser.Forward"_hash, Key::Right, KeyMod::Alt},
				{"AssetBrowser.Up"_hash, Key::Backspace, KeyMod::None},
			};

			constexpr u32 PANEL_CHORD_COUNT = static_cast<u32>(PanelChord::Count);

			bool InRange(PanelChord chord) {
				return static_cast<u32>(chord) < PANEL_CHORD_COUNT;
			}

			ActionId BindExact(InputActionMap& map, StringHash name, Key key, KeyMod mods) {
				InputBinding binding;
				binding.source = InputBinding::Source::Key;
				binding.code = static_cast<u16>(key);
				binding.requiredMods = mods;
				binding.exactMods = true;

				const ActionId id = map.RegisterAction(name, ActionType::Button);
				map.ClearBindings(id);
				map.Bind(id, binding);
				return id;
			}

			const char* KeyLabel(Key key) {
				switch (key) {
				case Key::Delete:
					return "Del";
				default:
					return ToString(key);
				}
			}

			str Label(Key key, KeyMod mods) {
				str label;
				if ((mods & KeyMod::Ctrl) != KeyMod::None) {
					label += "Ctrl+";
				}
				if ((mods & KeyMod::Shift) != KeyMod::None) {
					label += "Shift+";
				}
				if ((mods & KeyMod::Alt) != KeyMod::None) {
					label += "Alt+";
				}
				if ((mods & KeyMod::Super) != KeyMod::None) {
					label += "Win+";
				}
				label += KeyLabel(key);
				return label;
			}

		} // anonymous namespace

		bool ShouldOpen(const RouteNeeds& needs, const RouteFacts& facts) {
			if (needs.yieldToTextField && facts.textFieldWantsKeyboard) {
				return false;
			}
			if (needs.pointerInside && !facts.pointerInside) {
				return false;
			}
			if (needs.gestureHeld && !facts.gestureHeld) {
				return false;
			}
			if (needs.viewFocused && !facts.viewFocused) {
				return false;
			}
			if (needs.yieldToModal && facts.modalUp) {
				return false;
			}
			return true;
		}

		void ApplyRoute(InputActionMap& map, const RouteNeeds& needs, const RouteFacts& facts) {
			if (ShouldOpen(needs, facts)) {
				map.Enable();
			} else {
				map.Disable();
			}
		}

		RouteFacts ApplicationFacts() {
			UI::Context* context = UISystem::GetContext(WindowSystem::GetFocusedWindow());
			RouteFacts facts;
			facts.textFieldWantsKeyboard = UISystem::WantsKeyboard(WindowSystem::GetFocusedWindow());
			facts.modalUp = context && context->GetOpenPopupCount() > 0;
			return facts;
		}

		RouteFacts ApplicationFacts(const UI::Context& context) {
			RouteFacts facts = ApplicationFacts();
			facts.modalUp = context.GetOpenPopupCount() > 0;
			return facts;
		}

		bool AdvanceGamePointerArm(bool armed, const InputEvent& event, bool aimedAtView) {
			if (event.type == InputEventType::MouseDown) {
				return armed || aimedAtView;
			}
			if (event.type == InputEventType::KeyDown && event.key == Key::Escape) {
				return false;
			}
			return armed;
		}

		str ChordLabel(Shortcut shortcut) {
			if (!InRange(shortcut)) {
				return {};
			}
			const ShortcutDef& def = Shortcuts[static_cast<u32>(shortcut)];
			return Label(def.key, def.mods);
		}

		str ChordLabel(PanelChord chord) {
			if (!InRange(chord)) {
				return {};
			}
			const PanelChordDef& def = PanelChords[static_cast<u32>(chord)];
			return Label(def.key, def.mods);
		}

		ActionId BindPanelChord(InputActionMap& map, PanelChord chord) {
			if (!InRange(chord)) {
				return INVALID_ACTION_ID;
			}
			const PanelChordDef& def = PanelChords[static_cast<u32>(chord)];
			return BindExact(map, def.name, def.key, def.mods);
		}

		RouteNeeds EditorShortcuts::RouteFor(Shortcut shortcut) {
			if (!InRange(shortcut)) {
				return Routes::GlobalShortcut;
			}
			return Shortcuts[static_cast<u32>(shortcut)].document ? Routes::DocumentShortcut
																  : Routes::GlobalShortcut;
		}

		void EditorShortcuts::Build() {
			for (u32 i = 0; i < SHORTCUT_COUNT; ++i) {
				const ShortcutDef& def = Shortcuts[i];
				_ids[i] = BindExact(def.document ? _document : _global, def.name, def.key, def.mods);
			}
		}

		void EditorShortcuts::Route(const RouteFacts& facts) {
			ApplyRoute(_global, Routes::GlobalShortcut, facts);
			ApplyRoute(_document, Routes::DocumentShortcut, facts);
		}

		void EditorShortcuts::Evaluate() {
			_global.Evaluate();
			_document.Evaluate();
		}

		bool EditorShortcuts::WasPerformed(Shortcut shortcut) const {
			if (!InRange(shortcut)) {
				return false;
			}
			u32 index = static_cast<u32>(shortcut);
			const InputActionMap& map = Shortcuts[index].document ? _document : _global;
			return map.WasPerformed(_ids[index]);
		}

	} // namespace EditorInputOps

} // namespace Shalltear
