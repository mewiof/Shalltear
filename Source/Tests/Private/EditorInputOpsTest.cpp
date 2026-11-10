#include "TestsPCH.h"

#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Editor/EditorInputOps.h"

#include "TestPlatform.h"

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

		void PostChord(Key key, KeyMod mods) {
			auto modKey = [&](KeyMod flag, Key modifier, bool down) {
				if (HasFlag(mods, flag)) {
					TestPlatform::PostKey(_window, modifier, down);
				}
			};
			modKey(KeyMod::Ctrl, Key::LeftCtrl, true);
			modKey(KeyMod::Shift, Key::LeftShift, true);
			modKey(KeyMod::Alt, Key::LeftAlt, true);
			TestPlatform::PostKey(_window, key, true);
			TestPlatform::PostKey(_window, key, false);
			modKey(KeyMod::Alt, Key::LeftAlt, false);
			modKey(KeyMod::Shift, Key::LeftShift, false);
			modKey(KeyMod::Ctrl, Key::LeftCtrl, false);
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
		Key key;
		KeyMod mods;
		const char* name;
	};

	const ChordCase allChords[] = {
		{Shortcut::ReloadModule, Key::R, KeyMod::Ctrl, "Ctrl+R ReloadModule"},
		{Shortcut::CompileModule, Key::F7, KeyMod::None, "F7 CompileModule"},
		{Shortcut::RecompileModule, Key::F7, KeyMod::Shift, "Shift+F7 RecompileModule"},
		{Shortcut::OpenBuildProfiles, Key::B, KeyMod::Ctrl | KeyMod::Shift, "Ctrl+Shift+B OpenBuildProfiles"},
		{Shortcut::BuildAndRun, Key::B, KeyMod::Ctrl, "Ctrl+B BuildAndRun"},
		{Shortcut::TogglePlay, Key::P, KeyMod::Ctrl, "Ctrl+P TogglePlay"},
		{Shortcut::TogglePause, Key::P, KeyMod::Ctrl | KeyMod::Shift, "Ctrl+Shift+P TogglePause"},
		{Shortcut::Step, Key::P, KeyMod::Ctrl | KeyMod::Alt, "Ctrl+Alt+P Step"},
		{Shortcut::Undo, Key::Z, KeyMod::Ctrl, "Ctrl+Z Undo"},
		{Shortcut::Redo, Key::Y, KeyMod::Ctrl, "Ctrl+Y Redo"},
		{Shortcut::SaveScene, Key::S, KeyMod::Ctrl, "Ctrl+S SaveScene"},
		{Shortcut::SaveSceneAs, Key::S, KeyMod::Ctrl | KeyMod::Shift, "Ctrl+Shift+S SaveSceneAs"},
		{Shortcut::DeleteSelection, Key::Delete, KeyMod::None, "Del DeleteSelection"},
		{Shortcut::RenameSelection, Key::F2, KeyMod::None, "F2 RenameSelection"},
		{Shortcut::SelectAll, Key::A, KeyMod::Ctrl, "Ctrl+A SelectAll"},
		{Shortcut::DuplicateSelection, Key::D, KeyMod::Ctrl, "Ctrl+D DuplicateSelection"},
	};

} // anonymous namespace

TEST_F(EditorShortcutTest, EveryShortcutFiresOnItsRealChord) {
	for (const ChordCase& chord : allChords) {
		PostChord(chord.key, chord.mods);
		RouteAndEvaluate(false);
		EXPECT_TRUE(_shortcuts.WasPerformed(chord.shortcut)) << chord.name;
	}
}

TEST_F(EditorShortcutTest, TextFieldClosesDocumentMap) {
	for (const ChordCase& chord : allChords) {
		PostChord(chord.key, chord.mods);
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
	PostChord(Key::B, KeyMod::Ctrl | KeyMod::Shift);
	RouteAndEvaluate(false);
	EXPECT_TRUE(_shortcuts.WasPerformed(Shortcut::OpenBuildProfiles));
	EXPECT_FALSE(_shortcuts.WasPerformed(Shortcut::BuildAndRun));

	PostChord(Key::P, KeyMod::Ctrl | KeyMod::Alt);
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
	TestPlatform::PostKey(_window, Key::Space, true);
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
	PostChord(Key::Z, KeyMod::Ctrl);
	RouteAndEvaluate(true);
	ASSERT_FALSE(_shortcuts.WasPerformed(Shortcut::Undo));

	WindowSystem::PumpEvents();
	RouteAndEvaluate(false);
	EXPECT_FALSE(_shortcuts.WasPerformed(Shortcut::Undo));
}
