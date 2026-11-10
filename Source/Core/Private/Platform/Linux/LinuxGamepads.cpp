#include "Input/InputInternal.h"

#include "Shalltear/Core/Time.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace Shalltear::InputInternal {

	namespace {

		constexpr const char* DEVICE_DIRECTORY = "/dev/input";

		// must match WindowsGamepads.cpp
		constexpr f32 LEFT_STICK_DEADZONE = 7849.0f / 32767.0f;
		constexpr f32 RIGHT_STICK_DEADZONE = 8689.0f / 32767.0f;
		constexpr f32 TRIGGER_THRESHOLD = 30.0f / 255.0f;

		constexpr u16 VENDOR_SONY = 0x054c;
		constexpr u16 VENDOR_NINTENDO = 0x057e;

		constexpr size_t LONG_BITS = sizeof(unsigned long) * 8;

		template <size_t TCount>
		bool HasBit(const unsigned long (&bits)[TCount], size_t index) {
			return index / LONG_BITS < TCount && ((bits[index / LONG_BITS] >> (index % LONG_BITS)) & 1ul) != 0;
		}

		struct AxisRange {
			i32 minimum = 0;
			i32 maximum = 0;
			bool present = false;
		};

		struct Pad {
			int descriptor = -1;
			char node[32] = {};
			// west button on BTN_NORTH's code (stupid xpad quirk)
			bool northIsWest = false;
			AxisRange leftX, leftY, rightX, rightY, leftTrigger, rightTrigger;
			f32 raw[static_cast<size_t>(GamepadAxis::Count)] = {};
			bool buttons[static_cast<size_t>(GamepadButton::Count)] = {};
			bool keyDpad[4] = {};
			i32 hatX = 0;
			i32 hatY = 0;
		};

		struct Backend {
			Pad pads[Input::MAX_GAMEPADS];
			int watch = -1;
			bool started = false;
			bool rescan = true;
			f64 nextRetry = 0.0;
		};

		Backend& GetBackend() {
			static Backend backend;
			return backend;
		}

		AxisRange ReadRange(int descriptor, unsigned int axis, const unsigned long (&absBits)[ABS_CNT / LONG_BITS + 1]) {
			AxisRange range;
			input_absinfo info{};
			if (HasBit(absBits, axis) && ::ioctl(descriptor, EVIOCGABS(axis), &info) == 0 && info.maximum > info.minimum) {
				range.minimum = info.minimum;
				range.maximum = info.maximum;
				range.present = true;
			}
			return range;
		}

		f32 NormalizeStick(const AxisRange& range, i32 value) {
			if (!range.present) {
				return 0.0f;
			}
			const f32 span = static_cast<f32>(range.maximum - range.minimum);
			return std::fmin(1.0f, std::fmax(-1.0f, (static_cast<f32>(value - range.minimum) / span) * 2.0f - 1.0f));
		}

		f32 NormalizeTrigger(const AxisRange& range, i32 value) {
			if (!range.present) {
				return 0.0f;
			}
			const f32 span = static_cast<f32>(range.maximum - range.minimum);
			return std::fmin(1.0f, std::fmax(0.0f, static_cast<f32>(value - range.minimum) / span));
		}

		void ApplyStickDeadzone(f32 rawX, f32 rawY, f32 deadzone, f32& outX, f32& outY) {
			const f32 magnitude = std::sqrt(rawX * rawX + rawY * rawY);
			if (magnitude <= deadzone) {
				outX = 0.0f;
				outY = 0.0f;
				return;
			}
			const f32 clamped = magnitude > 1.0f ? 1.0f : magnitude;
			const f32 normalized = (clamped - deadzone) / (1.0f - deadzone);
			outX = (rawX / magnitude) * normalized;
			outY = (rawY / magnitude) * normalized;
		}

		f32 ApplyTriggerThreshold(f32 raw) {
			return raw <= TRIGGER_THRESHOLD ? 0.0f : (raw - TRIGGER_THRESHOLD) / (1.0f - TRIGGER_THRESHOLD);
		}

		void PushConnectionEvent(u8 index, bool connected) {
			InputEvent event;
			event.type = connected ? InputEventType::GamepadConnected : InputEventType::GamepadDisconnected;
			event.gamepadIndex = index;
			PushEvent(event);
		}

		bool IsOpen(const Backend& backend, const char* node) {
			for (const Pad& pad : backend.pads) {
				if (pad.descriptor >= 0 && std::strcmp(pad.node, node) == 0) {
					return true;
				}
			}
			return false;
		}

		void TryOpen(Backend& backend, const char* node) {
			if (std::strncmp(node, "event", 5) != 0 || IsOpen(backend, node)) {
				return;
			}
			u8 slot = 0;
			while (slot < Input::MAX_GAMEPADS && backend.pads[slot].descriptor >= 0) {
				++slot;
			}
			if (slot == Input::MAX_GAMEPADS) {
				return;
			}

			char path[64];
			std::snprintf(path, sizeof(path), "%s/%s", DEVICE_DIRECTORY, node);
			const int descriptor = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (descriptor < 0) {
				return;
			}

			unsigned long keyBits[KEY_CNT / LONG_BITS + 1] = {};
			unsigned long absBits[ABS_CNT / LONG_BITS + 1] = {};
			const bool isGamepad = ::ioctl(descriptor, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0 &&
								   ::ioctl(descriptor, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) >= 0 &&
								   HasBit(keyBits, BTN_SOUTH) && HasBit(absBits, ABS_X) && HasBit(absBits, ABS_Y);
			if (!isGamepad) {
				::close(descriptor);
				return;
			}

			Pad& pad = backend.pads[slot];
			pad = {};
			pad.descriptor = descriptor;
			std::snprintf(pad.node, sizeof(pad.node), "%s", node);
			input_id identity{};
			if (::ioctl(descriptor, EVIOCGID, &identity) == 0) {
				pad.northIsWest = identity.vendor != VENDOR_SONY && identity.vendor != VENDOR_NINTENDO;
			}
			pad.leftX = ReadRange(descriptor, ABS_X, absBits);
			pad.leftY = ReadRange(descriptor, ABS_Y, absBits);
			pad.rightX = ReadRange(descriptor, ABS_RX, absBits);
			pad.rightY = ReadRange(descriptor, ABS_RY, absBits);
			pad.leftTrigger = ReadRange(descriptor, ABS_Z, absBits);
			pad.rightTrigger = ReadRange(descriptor, ABS_RZ, absBits);
		}

		void Close(Backend& backend, u8 slot) {
			Pad& pad = backend.pads[slot];
			if (pad.descriptor < 0) {
				return;
			}
			::close(pad.descriptor);
			pad = {};
			if (GetState().gamepads[slot].connected) {
				GetState().gamepads[slot] = {};
				PushConnectionEvent(slot, false);
			}
		}

		void Scan(Backend& backend) {
			DIR* directory = ::opendir(DEVICE_DIRECTORY);
			if (!directory) {
				return;
			}
			while (const dirent* entry = ::readdir(directory)) {
				TryOpen(backend, entry->d_name);
			}
			::closedir(directory);
		}

		void DrainHotplug(Backend& backend) {
			if (backend.watch < 0) {
				return;
			}
			alignas(inotify_event) char buffer[4096];
			while (::read(backend.watch, buffer, sizeof(buffer)) > 0) {
				backend.rescan = true;
			}
		}

		void ApplyKey(Pad& pad, u16 code, bool down) {
			auto set = [&pad, down](GamepadButton button) {
				pad.buttons[static_cast<size_t>(button)] = down;
			};
			switch (code) {
			case BTN_SOUTH:
				set(GamepadButton::South);
				break;
			case BTN_EAST:
				set(GamepadButton::East);
				break;
			case BTN_NORTH:
				set(pad.northIsWest ? GamepadButton::West : GamepadButton::North);
				break;
			case BTN_WEST:
				set(pad.northIsWest ? GamepadButton::North : GamepadButton::West);
				break;
			case BTN_TL:
				set(GamepadButton::LeftBumper);
				break;
			case BTN_TR:
				set(GamepadButton::RightBumper);
				break;
			case BTN_SELECT:
				set(GamepadButton::Back);
				break;
			case BTN_START:
				set(GamepadButton::Start);
				break;
			case BTN_MODE:
				set(GamepadButton::Guide);
				break;
			case BTN_THUMBL:
				set(GamepadButton::LeftThumb);
				break;
			case BTN_THUMBR:
				set(GamepadButton::RightThumb);
				break;
			case BTN_DPAD_UP:
				pad.keyDpad[0] = down;
				break;
			case BTN_DPAD_DOWN:
				pad.keyDpad[1] = down;
				break;
			case BTN_DPAD_LEFT:
				pad.keyDpad[2] = down;
				break;
			case BTN_DPAD_RIGHT:
				pad.keyDpad[3] = down;
				break;
			default:
				break;
			}
		}

		void ApplyAxis(Pad& pad, u16 code, i32 value) {
			f32* raw = pad.raw;
			switch (code) {
			case ABS_X:
				raw[static_cast<size_t>(GamepadAxis::LeftX)] = NormalizeStick(pad.leftX, value);
				break;
			case ABS_Y:
				// evdev +y is down
				raw[static_cast<size_t>(GamepadAxis::LeftY)] = -NormalizeStick(pad.leftY, value);
				break;
			case ABS_RX:
				raw[static_cast<size_t>(GamepadAxis::RightX)] = NormalizeStick(pad.rightX, value);
				break;
			case ABS_RY:
				raw[static_cast<size_t>(GamepadAxis::RightY)] = -NormalizeStick(pad.rightY, value);
				break;
			case ABS_Z:
				raw[static_cast<size_t>(GamepadAxis::LeftTrigger)] = NormalizeTrigger(pad.leftTrigger, value);
				break;
			case ABS_RZ:
				raw[static_cast<size_t>(GamepadAxis::RightTrigger)] = NormalizeTrigger(pad.rightTrigger, value);
				break;
			case ABS_HAT0X:
				pad.hatX = value;
				break;
			case ABS_HAT0Y:
				pad.hatY = value;
				break;
			default:
				break;
			}
		}

		// false once device is gone
		bool ReadEvents(Pad& pad) {
			input_event events[32];
			for (;;) {
				const ssize_t length = ::read(pad.descriptor, events, sizeof(events));
				if (length < 0) {
					return errno == EAGAIN || errno == EINTR;
				}
				if (length == 0) {
					return true;
				}
				const size_t count = static_cast<size_t>(length) / sizeof(input_event);
				for (size_t i = 0; i < count; ++i) {
					const input_event& event = events[i];
					if (event.type == EV_KEY) {
						ApplyKey(pad, event.code, event.value != 0);
					} else if (event.type == EV_ABS) {
						ApplyAxis(pad, event.code, event.value);
					}
				}
			}
		}

		void Publish(const Pad& pad, GamepadState& out) {
			out.connected = true;
			std::memcpy(out.buttons, pad.buttons, sizeof(out.buttons));
			out.buttons[static_cast<size_t>(GamepadButton::DpadUp)] = pad.keyDpad[0] || pad.hatY < 0;
			out.buttons[static_cast<size_t>(GamepadButton::DpadDown)] = pad.keyDpad[1] || pad.hatY > 0;
			out.buttons[static_cast<size_t>(GamepadButton::DpadLeft)] = pad.keyDpad[2] || pad.hatX < 0;
			out.buttons[static_cast<size_t>(GamepadButton::DpadRight)] = pad.keyDpad[3] || pad.hatX > 0;

			f32* axes = out.axes;
			const f32* raw = pad.raw;
			ApplyStickDeadzone(raw[static_cast<size_t>(GamepadAxis::LeftX)], raw[static_cast<size_t>(GamepadAxis::LeftY)],
							   LEFT_STICK_DEADZONE,
							   axes[static_cast<size_t>(GamepadAxis::LeftX)], axes[static_cast<size_t>(GamepadAxis::LeftY)]);
			ApplyStickDeadzone(raw[static_cast<size_t>(GamepadAxis::RightX)], raw[static_cast<size_t>(GamepadAxis::RightY)],
							   RIGHT_STICK_DEADZONE,
							   axes[static_cast<size_t>(GamepadAxis::RightX)], axes[static_cast<size_t>(GamepadAxis::RightY)]);
			axes[static_cast<size_t>(GamepadAxis::LeftTrigger)] = ApplyTriggerThreshold(raw[static_cast<size_t>(GamepadAxis::LeftTrigger)]);
			axes[static_cast<size_t>(GamepadAxis::RightTrigger)] = ApplyTriggerThreshold(raw[static_cast<size_t>(GamepadAxis::RightTrigger)]);
		}

	} // anonymous namespace

	void PollGamepads(bool applicationFocused) {
		Backend& backend = GetBackend();
		State& state = GetState();

		if (!backend.started) {
			backend.started = true;
			backend.watch = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
			if (backend.watch >= 0) {
				// node visible before udev access granted (IN_ATTRIB after)
				::inotify_add_watch(backend.watch, DEVICE_DIRECTORY, IN_CREATE | IN_ATTRIB | IN_DELETE);
			}
		}

		DrainHotplug(backend);
		const f64 now = Time::GetRealtimeSinceStartup();
		if (backend.rescan || (backend.watch < 0 && now >= backend.nextRetry)) {
			backend.rescan = false;
			backend.nextRetry = now + 1.0;
			Scan(backend);
		}

		for (u8 i = 0; i < Input::MAX_GAMEPADS; ++i) {
			Pad& pad = backend.pads[i];
			if (pad.descriptor < 0) {
				continue;
			}
			if (!ReadEvents(pad)) {
				Close(backend, i);
				continue;
			}
			GamepadState& out = state.gamepads[i];
			if (!out.connected) {
				out.connected = true;
				PushConnectionEvent(i, true);
			}
			if (applicationFocused) {
				Publish(pad, out);
			} else {
				std::memset(out.buttons, 0, sizeof(out.buttons));
				std::memset(out.axes, 0, sizeof(out.axes));
			}
		}
	}

} // namespace Shalltear::InputInternal
