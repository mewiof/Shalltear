#include "TestsPCH.h"

#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Editor/EditorInputOps.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Shalltear;
using namespace Shalltear::EditorInputOps;

TEST(EditorInputRuleTest, GlobalRouteClosedOnlyByPopup) {
	RouteFacts hostile;
	hostile.textFieldWantsKeyboard = true;
	hostile.pointerInside = false;
	hostile.gestureHeld = false;
	hostile.viewFocused = false;
	EXPECT_TRUE(ShouldOpen(Routes::GlobalShortcut, hostile));

	hostile.modalUp = true;
	EXPECT_FALSE(ShouldOpen(Routes::GlobalShortcut, hostile));
}

TEST(EditorInputRuleTest, DocumentRouteClosedByTextField) {
	RouteFacts facts;
	EXPECT_TRUE(ShouldOpen(Routes::DocumentShortcut, facts));

	facts.textFieldWantsKeyboard = true;
	EXPECT_FALSE(ShouldOpen(Routes::DocumentShortcut, facts));

	facts.textFieldWantsKeyboard = false;
	facts.pointerInside = false;
	EXPECT_TRUE(ShouldOpen(Routes::DocumentShortcut, facts));
}

TEST(EditorInputRuleTest, PointerRouteIgnoresKeyboard) {
	RouteFacts facts;
	facts.pointerInside = true;
	facts.textFieldWantsKeyboard = true;

	EXPECT_TRUE(ShouldOpen(Routes::ViewportPointer, facts));
	EXPECT_FALSE(ShouldOpen(Routes::ViewportKeyboard, facts));

	facts.textFieldWantsKeyboard = false;
	EXPECT_TRUE(ShouldOpen(Routes::ViewportPointer, facts));
	EXPECT_TRUE(ShouldOpen(Routes::ViewportKeyboard, facts));
}

TEST(EditorInputRuleTest, ViewportKeysNeedHoverAndFreeKeyboard) {
	RouteFacts neither;
	EXPECT_FALSE(ShouldOpen(Routes::ViewportKeyboard, neither));

	RouteFacts hoverOnly;
	hoverOnly.pointerInside = true;
	EXPECT_TRUE(ShouldOpen(Routes::ViewportKeyboard, hoverOnly));

	RouteFacts hoverButTyping;
	hoverButTyping.pointerInside = true;
	hoverButTyping.textFieldWantsKeyboard = true;
	EXPECT_FALSE(ShouldOpen(Routes::ViewportKeyboard, hoverButTyping));

	RouteFacts typingElsewhere;
	typingElsewhere.textFieldWantsKeyboard = true;
	EXPECT_FALSE(ShouldOpen(Routes::ViewportKeyboard, typingElsewhere));
}

TEST(EditorInputRuleTest, GameplayRouteNeedsAll3Facts) {
	RouteFacts nothing;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayInput, nothing));

	RouteFacts focused;
	focused.viewFocused = true;
	EXPECT_TRUE(ShouldOpen(Routes::GameplayInput, focused));

	RouteFacts focusedButTyping = focused;
	focusedButTyping.textFieldWantsKeyboard = true;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayInput, focusedButTyping));

	RouteFacts focusedUnderAModal = focused;
	focusedUnderAModal.modalUp = true;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayInput, focusedUnderAModal));
}

TEST(EditorInputRuleTest, AGameplayRouteIgnoresHoverEntirely) {
	RouteFacts hoveringOnly;
	hoveringOnly.pointerInside = true;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayInput, hoveringOnly));

	RouteFacts focusedButPointerAway;
	focusedButPointerAway.viewFocused = true;
	focusedButPointerAway.pointerInside = false;
	EXPECT_TRUE(ShouldOpen(Routes::GameplayInput, focusedButPointerAway));
}

TEST(EditorInputRuleTest, GameplayPointerNeedsAim) {
	RouteFacts focusedButAimedAway;
	focusedButAimedAway.viewFocused = true;
	focusedButAimedAway.pointerInside = false;
	EXPECT_TRUE(ShouldOpen(Routes::GameplayInput, focusedButAimedAway));
	EXPECT_FALSE(ShouldOpen(Routes::GameplayPointer, focusedButAimedAway));

	RouteFacts aimedAtTheView = focusedButAimedAway;
	aimedAtTheView.pointerInside = true;
	EXPECT_TRUE(ShouldOpen(Routes::GameplayInput, aimedAtTheView));
	EXPECT_TRUE(ShouldOpen(Routes::GameplayPointer, aimedAtTheView));
}

TEST(EditorInputRuleTest, GameplayPointerKeepsKeyboardTerms) {
	RouteFacts aimedButUnfocused;
	aimedButUnfocused.pointerInside = true;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayPointer, aimedButUnfocused));

	RouteFacts open;
	open.viewFocused = true;
	open.pointerInside = true;
	ASSERT_TRUE(ShouldOpen(Routes::GameplayPointer, open));

	RouteFacts typingElsewhere = open;
	typingElsewhere.textFieldWantsKeyboard = true;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayPointer, typingElsewhere));

	RouteFacts underAModal = open;
	underAModal.modalUp = true;
	EXPECT_FALSE(ShouldOpen(Routes::GameplayPointer, underAModal));
}

TEST(EditorInputRuleTest, ViewFocusTermStaysLocal) {
	RouteFacts unfocused;
	unfocused.viewFocused = false;
	EXPECT_TRUE(ShouldOpen(Routes::GlobalShortcut, unfocused));

	unfocused.pointerInside = true;
	EXPECT_TRUE(ShouldOpen(Routes::ViewportKeyboard, unfocused));
	EXPECT_TRUE(ShouldOpen(Routes::ViewportPointer, unfocused));
}

TEST(EditorInputRuleTest, PopupClosesAllButInFlight) {
	RouteFacts facts;
	facts.pointerInside = true;
	facts.viewFocused = true;
	facts.modalUp = true;

	EXPECT_FALSE(ShouldOpen(Routes::GlobalShortcut, facts));
	EXPECT_FALSE(ShouldOpen(Routes::DocumentShortcut, facts));
	EXPECT_FALSE(ShouldOpen(Routes::ViewportPointer, facts));
	EXPECT_FALSE(ShouldOpen(Routes::ViewportKeyboard, facts));
	EXPECT_FALSE(ShouldOpen(Routes::GameplayInput, facts));
	EXPECT_FALSE(ShouldOpen(Routes::PanelKeyboard, facts));
	EXPECT_FALSE(ShouldOpen(Routes::PanelPointer, facts));

	facts.gestureHeld = true;
	EXPECT_TRUE(ShouldOpen(Routes::HeldGesture, facts));

	facts.modalUp = false;
	EXPECT_TRUE(ShouldOpen(Routes::GlobalShortcut, facts));
	EXPECT_TRUE(ShouldOpen(Routes::DocumentShortcut, facts));
	EXPECT_TRUE(ShouldOpen(Routes::ViewportPointer, facts));
	EXPECT_TRUE(ShouldOpen(Routes::ViewportKeyboard, facts));
	EXPECT_TRUE(ShouldOpen(Routes::GameplayInput, facts));
	EXPECT_TRUE(ShouldOpen(Routes::PanelKeyboard, facts));
	EXPECT_TRUE(ShouldOpen(Routes::PanelPointer, facts));
}

TEST(EditorInputRuleTest, PanelKeysNeedFocusPointerHover) {
	RouteFacts facts;
	facts.viewFocused = true;
	EXPECT_TRUE(ShouldOpen(Routes::PanelKeyboard, facts));
	facts.textFieldWantsKeyboard = true;
	EXPECT_FALSE(ShouldOpen(Routes::PanelKeyboard, facts));
	facts.textFieldWantsKeyboard = false;
	facts.viewFocused = false;
	facts.pointerInside = true;
	EXPECT_FALSE(ShouldOpen(Routes::PanelKeyboard, facts));

	EXPECT_TRUE(ShouldOpen(Routes::PanelPointer, facts));
	facts.textFieldWantsKeyboard = true;
	EXPECT_TRUE(ShouldOpen(Routes::PanelPointer, facts));
	facts.pointerInside = false;
	EXPECT_FALSE(ShouldOpen(Routes::PanelPointer, facts));
}

TEST(EditorInputRuleTest, HeldGestureRouteSurvivesPointerLeaving) {
	RouteFacts facts;
	facts.gestureHeld = true;
	facts.pointerInside = false;
	EXPECT_TRUE(ShouldOpen(Routes::HeldGesture, facts));

	facts.gestureHeld = false;
	EXPECT_FALSE(ShouldOpen(Routes::HeldGesture, facts));
}

namespace {

	InputEvent Press(bool down = true) {
		InputEvent event;
		event.type = down ? InputEventType::MouseDown : InputEventType::MouseUp;
		event.button = MouseButton::Left;
		return event;
	}

	InputEvent KeyPress(Key key) {
		InputEvent event;
		event.type = InputEventType::KeyDown;
		event.key = key;
		return event;
	}

} // anonymous namespace

TEST(EditorInputRuleTest, ClickInViewArmsPointer) {
	EXPECT_FALSE(AdvanceGamePointerArm(false, Press(), false));
	EXPECT_TRUE(AdvanceGamePointerArm(false, Press(), true));

	EXPECT_TRUE(AdvanceGamePointerArm(true, Press(), false));
	EXPECT_TRUE(AdvanceGamePointerArm(true, Press(false), true));
	EXPECT_FALSE(AdvanceGamePointerArm(false, Press(false), true));
}

TEST(EditorInputRuleTest, EscapeAloneDisarmsPointer) {
	EXPECT_FALSE(AdvanceGamePointerArm(true, KeyPress(Key::Escape), false));
	EXPECT_FALSE(AdvanceGamePointerArm(true, KeyPress(Key::Escape), true));

	EXPECT_TRUE(AdvanceGamePointerArm(true, KeyPress(Key::W), true));
	EXPECT_TRUE(AdvanceGamePointerArm(true, KeyPress(Key::Tab), true));
	EXPECT_FALSE(AdvanceGamePointerArm(false, KeyPress(Key::W), true));

	InputEvent escape = KeyPress(Key::Escape);
	escape.type = InputEventType::KeyUp;
	EXPECT_TRUE(AdvanceGamePointerArm(true, escape, true));
}

#ifdef _WIN32

namespace {

	class EditorShortcutTest : public ::testing::Test {
	protected:
		void SetUp() override {
			WindowSystem::Initialize();
			WindowDesc desc;
			desc.title = "EditorShortcutTest";
			desc.clientSize = {320, 240};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			ASSERT_TRUE(_window.IsValid());
			_shortcuts.Build();
		}
		void TearDown() override {
			WindowSystem::Terminate();
		}

		HWND Hwnd() const {
			return static_cast<HWND>(_window->GetNativeHandle().hwnd);
		}

		static LPARAM KeyLParam(u16 scancode, bool up = false) {
			LPARAM lParam = 1;
			lParam |= static_cast<LPARAM>(scancode & 0xFF) << 16;
			if (up) {
				lParam |= (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
			}
			return lParam;
		}

		void PostChord(int vk, u16 scancode, KeyMod mods) {
			auto modKey = [&](KeyMod flag, int modVk, u16 modScan, bool up) {
				if (HasFlag(mods, flag)) {
					::PostMessageW(Hwnd(), up ? WM_KEYUP : WM_KEYDOWN, modVk, KeyLParam(modScan, up));
				}
			};
			modKey(KeyMod::Ctrl, VK_CONTROL, 0x1D, false);
			modKey(KeyMod::Shift, VK_SHIFT, 0x2A, false);
			modKey(KeyMod::Alt, VK_MENU, 0x38, false);
			::PostMessageW(Hwnd(), WM_KEYDOWN, vk, KeyLParam(scancode));
			::PostMessageW(Hwnd(), WM_KEYUP, vk, KeyLParam(scancode, true));
			modKey(KeyMod::Alt, VK_MENU, 0x38, true);
			modKey(KeyMod::Shift, VK_SHIFT, 0x2A, true);
			modKey(KeyMod::Ctrl, VK_CONTROL, 0x1D, true);
			WindowSystem::PumpEvents();
		}

		void RouteAndEvaluate(bool textFieldWantsKeyboard) {
			RouteFacts facts;
			facts.textFieldWantsKeyboard = textFieldWantsKeyboard;
			_shortcuts.Route(facts);
			_shortcuts.Evaluate();
		}

		Handle<Window> _window;
		EditorShortcuts _shortcuts;
	};

	struct ChordCase {
		Shortcut shortcut;
		int vk;
		u16 scancode;
		KeyMod mods;
		const char* name;
	};

	const ChordCase allChords[] = {
		{Shortcut::ReloadModule, 'R', 0x13, KeyMod::Ctrl, "Ctrl+R ReloadModule"},
		{Shortcut::CompileModule, VK_F7, 0x41, KeyMod::None, "F7 CompileModule"},
		{Shortcut::RecompileModule, VK_F7, 0x41, KeyMod::Shift, "Shift+F7 RecompileModule"},
		{Shortcut::OpenBuildProfiles, 'B', 0x30, KeyMod::Ctrl | KeyMod::Shift, "Ctrl+Shift+B OpenBuildProfiles"},
		{Shortcut::BuildAndRun, 'B', 0x30, KeyMod::Ctrl, "Ctrl+B BuildAndRun"},
		{Shortcut::TogglePlay, 'P', 0x19, KeyMod::Ctrl, "Ctrl+P TogglePlay"},
		{Shortcut::TogglePause, 'P', 0x19, KeyMod::Ctrl | KeyMod::Shift, "Ctrl+Shift+P TogglePause"},
		{Shortcut::Step, 'P', 0x19, KeyMod::Ctrl | KeyMod::Alt, "Ctrl+Alt+P Step"},
		{Shortcut::Undo, 'Z', 0x2C, KeyMod::Ctrl, "Ctrl+Z Undo"},
		{Shortcut::Redo, 'Y', 0x15, KeyMod::Ctrl, "Ctrl+Y Redo"},
		{Shortcut::SaveScene, 'S', 0x1F, KeyMod::Ctrl, "Ctrl+S SaveScene"},
		{Shortcut::SaveSceneAs, 'S', 0x1F, KeyMod::Ctrl | KeyMod::Shift, "Ctrl+Shift+S SaveSceneAs"},
		{Shortcut::DeleteSelection, VK_DELETE, 0x53, KeyMod::None, "Del DeleteSelection"},
		{Shortcut::RenameSelection, VK_F2, 0x3C, KeyMod::None, "F2 RenameSelection"},
		{Shortcut::SelectAll, 'A', 0x1E, KeyMod::Ctrl, "Ctrl+A SelectAll"},
		{Shortcut::DuplicateSelection, 'D', 0x20, KeyMod::Ctrl, "Ctrl+D DuplicateSelection"},
	};

} // anonymous namespace

TEST_F(EditorShortcutTest, EveryShortcutFiresOnItsRealChord) {
	for (const ChordCase& chord : allChords) {
		PostChord(chord.vk, chord.scancode, chord.mods);
		RouteAndEvaluate(false);
		EXPECT_TRUE(_shortcuts.WasPerformed(chord.shortcut)) << chord.name;
	}
}

TEST_F(EditorShortcutTest, TextFieldClosesDocumentMap) {
	for (const ChordCase& chord : allChords) {
		PostChord(chord.vk, chord.scancode, chord.mods);
		RouteAndEvaluate(true);

		bool isGlobal = !EditorShortcuts::RouteFor(chord.shortcut).yieldToTextField;
		EXPECT_EQ(_shortcuts.WasPerformed(chord.shortcut), isGlobal);
	}
}

TEST_F(EditorShortcutTest, RouteSplitIs8And8) {
	ASSERT_EQ(std::size(allChords), static_cast<size_t>(Shortcut::Count));

	u32 global = 0;
	u32 document = 0;
	for (const ChordCase& chord : allChords) {
		if (EditorShortcuts::RouteFor(chord.shortcut).yieldToTextField) {
			++document;
		} else {
			++global;
		}
	}
	EXPECT_EQ(global, 8u);
	EXPECT_EQ(document, 8u);
}

TEST_F(EditorShortcutTest, EveryLabelIsTheChordThatFires) {
	for (const ChordCase& chord : allChords) {
		str label = ChordLabel(chord.shortcut);
		EXPECT_FALSE(label.empty()) << chord.name;
		str expected(chord.name, std::string_view(chord.name).find(' '));
		EXPECT_EQ(label, expected) << chord.name << " is labeled " << label;
	}
}

TEST(EditorInputLabelTest, PanelChordLabeledNotBound) {
	EXPECT_EQ(ChordLabel(PanelChord::AssetBrowserBack), str("Alt+") + ToString(Key::Left));

	InputActionMap map;
	for (u32 i = 0; i < static_cast<u32>(PanelChord::Count); ++i) {
		const PanelChord chord = static_cast<PanelChord>(i);
		const str label = ChordLabel(chord);
		EXPECT_FALSE(label.empty()) << "panel chord " << i;
		const ActionId id = BindPanelChord(map, chord);
		EXPECT_NE(id, INVALID_ACTION_ID) << label << " bound nothing";
		EXPECT_EQ(BindPanelChord(map, chord), id) << label;
		for (u32 s = 0; s < static_cast<u32>(Shortcut::Count); ++s) {
			EXPECT_NE(label, ChordLabel(static_cast<Shortcut>(s))) << label << ' ' << s;
		}
	}

	EXPECT_TRUE(ChordLabel(PanelChord::Count).empty());
	EXPECT_EQ(BindPanelChord(map, PanelChord::Count), INVALID_ACTION_ID);
}

TEST_F(EditorShortcutTest, DeleteChordLabelMatchesMenu) {
	EXPECT_EQ(ChordLabel(Shortcut::DeleteSelection), "Del");
	EXPECT_STREQ(ToString(Key::Delete), "Delete");
}

TEST_F(EditorShortcutTest, ShortcutOutsideTableHasNoLabel) {
	EXPECT_TRUE(ChordLabel(Shortcut::Count).empty());
	EXPECT_TRUE(ChordLabel(static_cast<Shortcut>(200)).empty());
}

TEST_F(EditorShortcutTest, OverlappingChordFiresOnlyShortcut) {
	PostChord('B', 0x30, KeyMod::Ctrl | KeyMod::Shift);
	RouteAndEvaluate(false);
	EXPECT_TRUE(_shortcuts.WasPerformed(Shortcut::OpenBuildProfiles));
	EXPECT_FALSE(_shortcuts.WasPerformed(Shortcut::BuildAndRun));

	PostChord('P', 0x19, KeyMod::Ctrl | KeyMod::Alt);
	RouteAndEvaluate(false);
	EXPECT_TRUE(_shortcuts.WasPerformed(Shortcut::Step));
	EXPECT_FALSE(_shortcuts.WasPerformed(Shortcut::TogglePlay));
	EXPECT_FALSE(_shortcuts.WasPerformed(Shortcut::TogglePause));
}

TEST_F(EditorShortcutTest, RouteReapplyKeepsMapOpen) {
	InputActionMap map;
	ActionId hold = map.RegisterAction("Hold"_hash, ActionType::Button);
	map.Bind(hold, InputBinding{InputBinding::Source::Key, static_cast<u16>(Key::Space)});

	RouteFacts open;
	ApplyRoute(map, Routes::GlobalShortcut, open);
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SPACE, KeyLParam(0x39));
	WindowSystem::PumpEvents();
	map.Evaluate();
	ASSERT_TRUE(map.IsPressed(hold));

	for (int frame = 0; frame < 3; ++frame) {
		ApplyRoute(map, Routes::GlobalShortcut, open);
		WindowSystem::PumpEvents();
		map.Evaluate();
		EXPECT_TRUE(map.IsPressed(hold)) << "frame " << frame;
	}
}

TEST_F(EditorShortcutTest, MissedShortcutDoesNotFireLater) {
	PostChord('Z', 0x2C, KeyMod::Ctrl);
	RouteAndEvaluate(true);
	ASSERT_FALSE(_shortcuts.WasPerformed(Shortcut::Undo));

	WindowSystem::PumpEvents();
	RouteAndEvaluate(false);
	EXPECT_FALSE(_shortcuts.WasPerformed(Shortcut::Undo));
}

#endif
