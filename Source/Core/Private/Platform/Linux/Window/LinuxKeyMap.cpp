#include "Platform/Linux/Window/LinuxWindow.h"

namespace Shalltear::Linux {

	namespace {

		struct KeysymEntry {
			u32 keysym;
			Key key;
		};

		// X11 keysym values, shared by xkbcommon
		constexpr KeysymEntry KEYSYMS[] = {
			{0xff1b, Key::Escape},
			{0xff0d, Key::Enter},
			{0xff09, Key::Tab},
			{0xfe20, Key::Tab},
			{0xff08, Key::Backspace},
			{0xff63, Key::Insert},
			{0xffff, Key::Delete},
			{0xff50, Key::Home},
			{0xff57, Key::End},
			{0xff55, Key::PageUp},
			{0xff56, Key::PageDown},
			{0xff51, Key::Left},
			{0xff53, Key::Right},
			{0xff52, Key::Up},
			{0xff54, Key::Down},
			{0x0020, Key::Space},
			{0xffe5, Key::CapsLock},
			{0xff14, Key::ScrollLock},
			{0xff7f, Key::NumLock},
			{0xff61, Key::PrintScreen},
			{0xff13, Key::Pause},
			{0xff67, Key::Menu},
			{0xffe1, Key::LeftShift},
			{0xffe2, Key::RightShift},
			{0xffe3, Key::LeftCtrl},
			{0xffe4, Key::RightCtrl},
			{0xffe9, Key::LeftAlt},
			{0xffea, Key::RightAlt},
			{0xfe03, Key::RightAlt},
			{0xffe7, Key::LeftAlt},
			{0xffe8, Key::RightAlt},
			{0xffeb, Key::LeftSuper},
			{0xffec, Key::RightSuper},
			{0x0027, Key::Apostrophe},
			{0x002c, Key::Comma},
			{0x002d, Key::Minus},
			{0x002e, Key::Period},
			{0x002f, Key::Slash},
			{0x003b, Key::Semicolon},
			{0x003d, Key::Equal},
			{0x005b, Key::LeftBracket},
			{0x005c, Key::Backslash},
			{0x005d, Key::RightBracket},
			{0x0060, Key::GraveAccent},
			{0xffae, Key::KpDecimal},
			{0xffac, Key::KpDecimal},
			{0xffaf, Key::KpDivide},
			{0xffaa, Key::KpMultiply},
			{0xffad, Key::KpSubtract},
			{0xffab, Key::KpAdd},
			{0xff8d, Key::KpEnter},
			{0xffbd, Key::KpEqual},
			// keypad without NumLock = navigation key printed on it
			{0xff9e, Key::Insert},
			{0xff9f, Key::Delete},
			{0xff95, Key::Home},
			{0xff9c, Key::End},
			{0xff9a, Key::PageUp},
			{0xff9b, Key::PageDown},
			{0xff96, Key::Left},
			{0xff98, Key::Right},
			{0xff97, Key::Up},
			{0xff99, Key::Down},
		};

		struct ScancodeEntry {
			u16 scancode;
			Key key;
		};

		// evdev codes of US layout's symbol keys
		constexpr ScancodeEntry SCANCODES[] = {
			{16, Key::Q},
			{17, Key::W},
			{18, Key::E},
			{19, Key::R},
			{20, Key::T},
			{21, Key::Y},
			{22, Key::U},
			{23, Key::I},
			{24, Key::O},
			{25, Key::P},
			{30, Key::A},
			{31, Key::S},
			{32, Key::D},
			{33, Key::F},
			{34, Key::G},
			{35, Key::H},
			{36, Key::J},
			{37, Key::K},
			{38, Key::L},
			{44, Key::Z},
			{45, Key::X},
			{46, Key::C},
			{47, Key::V},
			{48, Key::B},
			{49, Key::N},
			{50, Key::M},
			{2, Key::D1},
			{3, Key::D2},
			{4, Key::D3},
			{5, Key::D4},
			{6, Key::D5},
			{7, Key::D6},
			{8, Key::D7},
			{9, Key::D8},
			{10, Key::D9},
			{11, Key::D0},
			{12, Key::Minus},
			{13, Key::Equal},
			{26, Key::LeftBracket},
			{27, Key::RightBracket},
			{39, Key::Semicolon},
			{40, Key::Apostrophe},
			{41, Key::GraveAccent},
			{43, Key::Backslash},
			{51, Key::Comma},
			{52, Key::Period},
			{53, Key::Slash},
		};

		Key Offset(Key first, u32 offset) {
			return static_cast<Key>(static_cast<u16>(first) + static_cast<u16>(offset));
		}

	} // anonymous namespace

	Key KeyFromKeysym(u32 keysym) {
		if (keysym >= 'a' && keysym <= 'z') {
			return Offset(Key::A, keysym - 'a');
		}
		if (keysym >= 'A' && keysym <= 'Z') {
			return Offset(Key::A, keysym - 'A');
		}
		if (keysym >= '0' && keysym <= '9') {
			return Offset(Key::D0, keysym - '0');
		}
		if (keysym >= 0xffbe && keysym <= 0xffd5) {
			return Offset(Key::F1, keysym - 0xffbe);
		}
		if (keysym >= 0xffb0 && keysym <= 0xffb9) {
			return Offset(Key::Kp0, keysym - 0xffb0);
		}
		for (const KeysymEntry& entry : KEYSYMS) {
			if (entry.keysym == keysym) {
				return entry.key;
			}
		}
		return Key::Unknown;
	}

	Key KeyFromScancode(u32 evdevScancode) {
		// F13 to F24 given media keysyms by stock layouts
		constexpr u32 EVDEV_F13 = 183;
		constexpr u32 EVDEV_F24 = 194;
		if (evdevScancode >= EVDEV_F13 && evdevScancode <= EVDEV_F24) {
			return Offset(Key::F13, evdevScancode - EVDEV_F13);
		}
		for (const ScancodeEntry& entry : SCANCODES) {
			if (entry.scancode == evdevScancode) {
				return entry.key;
			}
		}
		return Key::Unknown;
	}

} // namespace Shalltear::Linux
