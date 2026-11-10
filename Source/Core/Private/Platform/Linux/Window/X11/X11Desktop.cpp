#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window/WindowEvents.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <poll.h>

#include "Platform/Linux/Window/X11/X11Server.h"

namespace Shalltear::Linux {

	namespace {

		constexpr f64 CLIPBOARD_TIMEOUT_SECONDS = 1.0;

		RectI Intersect(const RectI& a, const RectI& b) {
			const i32 left = std::max(a.position.x, b.position.x);
			const i32 top = std::max(a.position.y, b.position.y);
			const i32 right = std::min(a.position.x + a.size.x, b.position.x + b.size.x);
			const i32 bottom = std::min(a.position.y + a.size.y, b.position.y + b.size.y);
			return right > left && bottom > top ? RectI{left, top, right - left, bottom - top} : RectI{};
		}

		f32 RefreshOf(const XRRModeInfo& mode) {
			const f64 total = static_cast<f64>(mode.hTotal) * static_cast<f64>(mode.vTotal);
			return total > 0.0 ? static_cast<f32>(static_cast<f64>(mode.dotClock) / total) : 0.0f;
		}

		int HexValue(char c) {
			return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
												: c >= 'A' && c <= 'F'	 ? c - 'A' + 10
																		 : -1;
		}

		// text/uri-list = 1 URI per CRLF line, # line = comment
		void ParseUriList(std::string_view text, std::vector<str>& outPaths) {
			size_t start = 0;
			while (start < text.size()) {
				size_t end = text.find_first_of("\r\n", start);
				if (end == std::string_view::npos) {
					end = text.size();
				}
				std::string_view line = text.substr(start, end - start);
				start = end + 1;
				constexpr std::string_view SCHEME = "file://";
				if (line.empty() || line[0] == '#' || line.substr(0, SCHEME.size()) != SCHEME) {
					continue;
				}
				line.remove_prefix(SCHEME.size());
				const size_t slash = line.find('/');
				if (slash == std::string_view::npos) {
					continue;
				}
				line.remove_prefix(slash);
				str path;
				for (size_t i = 0; i < line.size(); ++i) {
					if (line[i] == '%' && i + 2 < line.size() + 0 && HexValue(line[i + 1]) >= 0 && HexValue(line[i + 2]) >= 0) {
						path.push_back(static_cast<char>(HexValue(line[i + 1]) * 16 + HexValue(line[i + 2])));
						i += 2;
					} else {
						path.push_back(line[i]);
					}
				}
				outPaths.push_back(std::move(path));
			}
		}

	} // anonymous namespace

	void X11Server::EnumerateMonitors(Vector<MonitorInfo>& out) {
		out.clear();
		monitorModes.clear();

		RectI workArea{};
		Vector<u8> raw{GetMemoryResource(MemTag::Platform)};
		if (ReadProperty(root, atoms.netWorkArea, XA_CARDINAL, raw) && raw.size() >= 4 * sizeof(long)) {
			const long* values = reinterpret_cast<const long*>(raw.data());
			workArea = {static_cast<i32>(values[0]), static_cast<i32>(values[1]), static_cast<i32>(values[2]), static_cast<i32>(values[3])};
		}
		auto finish = [&](MonitorInfo& monitor) {
			const RectI usable = Intersect(monitor.bounds, workArea);
			monitor.workArea = usable.size.x > 0 ? usable : monitor.bounds;
			monitor.scale = scale;
			monitor.id = StringHash(monitor.name).GetValue();
		};

		if (haveRandr) {
			int count = 0;
			XRRMonitorInfo* monitors = x.XRRGetMonitors(display, root, True, &count);
			XRRScreenResources* resources = count > 0 ? x.XRRGetScreenResourcesCurrent(display, root) : nullptr;
			for (int i = 0; monitors && i < count; ++i) {
				const XRRMonitorInfo& source = monitors[i];
				MonitorInfo monitor;
				if (char* name = x.XGetAtomName(display, source.name)) {
					monitor.name = name;
					x.XFree(name);
				}
				monitor.bounds = {source.x, source.y, source.width, source.height};
				monitor.primary = source.primary != 0;
				finish(monitor);

				MonitorModes modes;
				modes.monitorId = monitor.id;
				if (resources && source.noutput > 0) {
					if (XRROutputInfo* output = x.XRRGetOutputInfo(display, resources, source.outputs[0])) {
						RRMode current = 0;
						if (output->crtc) {
							if (XRRCrtcInfo* crtc = x.XRRGetCrtcInfo(display, resources, output->crtc)) {
								current = crtc->mode;
								x.XRRFreeCrtcInfo(crtc);
							}
						}
						for (int m = 0; m < output->nmode; ++m) {
							for (int r = 0; r < resources->nmode; ++r) {
								const XRRModeInfo& mode = resources->modes[r];
								if (mode.id != output->modes[m]) {
									continue;
								}
								if (mode.id == current) {
									monitor.refreshRate = RefreshOf(mode);
								}
								WindowInternal::DisplayMode entry;
								entry.sizePx = {static_cast<i32>(mode.width), static_cast<i32>(mode.height)};
								entry.refreshRate = RefreshOf(mode);
								const bool seen = std::any_of(modes.modes.begin(), modes.modes.end(), [&](const WindowInternal::DisplayMode& other) {
									return other.sizePx == entry.sizePx && other.refreshRate == entry.refreshRate;
								});
								if (!seen) {
									modes.modes.push_back(entry);
								}
							}
						}
						x.XRRFreeOutputInfo(output);
					}
				}
				std::sort(modes.modes.begin(), modes.modes.end(), [](const WindowInternal::DisplayMode& a, const WindowInternal::DisplayMode& b) {
					if (a.sizePx.x != b.sizePx.x) {
						return a.sizePx.x > b.sizePx.x;
					}
					if (a.sizePx.y != b.sizePx.y) {
						return a.sizePx.y > b.sizePx.y;
					}
					return a.refreshRate > b.refreshRate;
				});
				monitorModes.push_back(std::move(modes));
				out.push_back(std::move(monitor));
			}
			if (resources) {
				x.XRRFreeScreenResources(resources);
			}
			if (monitors) {
				x.XRRFreeMonitors(monitors);
			}
		}

		if (out.empty()) {
			MonitorInfo monitor;
			monitor.name = "screen";
			monitor.bounds = {0, 0, DisplayWidth(display, screen), DisplayHeight(display, screen)};
			monitor.primary = true;
			finish(monitor);
			out.push_back(std::move(monitor));
		}
		if (std::none_of(out.begin(), out.end(), [](const MonitorInfo& monitor) { return monitor.primary; })) {
			out.front().primary = true;
		}
	}

	void X11Server::EnumerateDisplayModes(u64 monitorId, Vector<WindowInternal::DisplayMode>& out) {
		for (const MonitorModes& entry : monitorModes) {
			if (entry.monitorId == monitorId) {
				out.insert(out.end(), entry.modes.begin(), entry.modes.end());
			}
		}
	}

	Handle<Window> X11Server::WindowAtPoint(Vec2I screenPx, Handle<Window> ignore) {
		auto hit = [&](Window::Impl* impl) {
			if (!impl || impl->self == ignore || !Native(*impl).shown || Native(*impl).iconic) {
				return false;
			}
			const RectI rect = GetRect(*impl);
			return screenPx.x >= rect.position.x && screenPx.x < rect.position.x + rect.size.x &&
				   screenPx.y >= rect.position.y && screenPx.y < rect.position.y + rect.size.y;
		};

		// bottom to top. No window manager, no list
		Vector<u8> raw{GetMemoryResource(MemTag::Platform)};
		if (ReadProperty(root, atoms.netClientListStacking, XA_WINDOW, raw)) {
			const ::Window* stacking = reinterpret_cast<const ::Window*>(raw.data());
			for (size_t i = raw.size() / sizeof(::Window); i > 0; --i) {
				Window::Impl* impl = FindImpl(stacking[i - 1]);
				if (hit(impl)) {
					return impl->self;
				}
			}
			return {};
		}
		const Vector<Window*>& windows = WindowInternal::GetState().windows;
		for (size_t i = windows.size(); i > 0; --i) {
			Window::Impl* impl = WindowInternalAccess::GetImpl(*windows[i - 1]);
			if (impl && impl->native && hit(impl)) {
				return impl->self;
			}
		}
		return {};
	}

	bool X11Server::SetClipboardText(std::string_view utf8) {
		clipboardText = str(utf8);
		x.XSetSelectionOwner(display, atoms.clipboard, helper, CurrentTime);
		ownsClipboard = x.XGetSelectionOwner(display, atoms.clipboard) == helper;
		x.XFlush(display);
		return ownsClipboard;
	}

	void X11Server::HandleSelectionRequest(const XSelectionRequestEvent& request) {
		XEvent reply{};
		reply.xselection.type = SelectionNotify;
		reply.xselection.requestor = request.requestor;
		reply.xselection.selection = request.selection;
		reply.xselection.target = request.target;
		reply.xselection.time = request.time;
		reply.xselection.property = X11_NONE;

		const Atom property = request.property ? request.property : request.target;
		if (request.selection == atoms.clipboard && ownsClipboard) {
			if (request.target == atoms.targets) {
				const Atom offered[] = {atoms.targets, atoms.utf8String, atoms.textPlainUtf8, XA_STRING};
				x.XChangeProperty(display, request.requestor, property, XA_ATOM, 32, PropModeReplace,
								  reinterpret_cast<const unsigned char*>(offered), 4);
				reply.xselection.property = property;
			} else if (request.target == atoms.utf8String || request.target == atoms.textPlainUtf8 || request.target == XA_STRING) {
				x.XChangeProperty(display, request.requestor, property, request.target, 8, PropModeReplace,
								  reinterpret_cast<const unsigned char*>(clipboardText.data()), static_cast<int>(clipboardText.size()));
				reply.xselection.property = property;
			}
		}
		x.XSendEvent(display, request.requestor, False, 0, &reply);
	}

	str X11Server::GetClipboardText() {
		if (ownsClipboard) {
			return clipboardText;
		}
		if (x.XGetSelectionOwner(display, atoms.clipboard) == X11_NONE) {
			return {};
		}
		x.XDeleteProperty(display, helper, atoms.selectionProperty);
		x.XConvertSelection(display, atoms.clipboard, atoms.utf8String, atoms.selectionProperty, helper, CurrentTime);
		x.XFlush(display);

		str result;
		bool incremental = false;
		const f64 deadline = Time::GetRealtimeSinceStartup() + CLIPBOARD_TIMEOUT_SECONDS;
		Vector<u8> chunk{GetMemoryResource(MemTag::Platform)};
		for (;;) {
			XEvent event;
			bool have = false;
			while (x.XPending(display) > 0) {
				x.XPeekEvent(display, &event);
				const bool ours = (event.type == SelectionNotify && event.xselection.requestor == helper) ||
								  (event.type == PropertyNotify && event.xproperty.window == helper);
				if (!ours) {
					// everything else left for Pump (in order)
					break;
				}
				x.XNextEvent(display, &event);
				have = true;
				break;
			}
			if (!have) {
				const f64 left = deadline - Time::GetRealtimeSinceStartup();
				if (left <= 0.0 || x.XPending(display) > 0) {
					break;
				}
				pollfd waits{ConnectionNumber(display), POLLIN, 0};
				::poll(&waits, 1, static_cast<int>(left * 1000.0) + 1);
				continue;
			}

			if (event.type == SelectionNotify) {
				if (event.xselection.property == X11_NONE) {
					break;
				}
				Atom type = 0;
				ReadProperty(helper, atoms.selectionProperty, AnyPropertyType, chunk, &type);
				x.XDeleteProperty(display, helper, atoms.selectionProperty);
				if (type != atoms.incr) {
					result.assign(reinterpret_cast<const char*>(chunk.data()), chunk.size());
					break;
				}
				incremental = true;
			} else if (incremental && event.xproperty.atom == atoms.selectionProperty && event.xproperty.state == PropertyNewValue) {
				ReadProperty(helper, atoms.selectionProperty, AnyPropertyType, chunk);
				x.XDeleteProperty(display, helper, atoms.selectionProperty);
				if (chunk.empty()) {
					break;
				}
				result.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
			}
		}
		return result;
	}

	void X11Server::HandleClientMessage(Window::Impl& impl, const XClientMessageEvent& message) {
		X11Window& w = Native(impl);
		const long* data = message.data.l;

		if (message.message_type == atoms.wmProtocols) {
			const Atom protocol = static_cast<Atom>(data[0]);
			if (protocol == atoms.wmDeleteWindow) {
				if (CanPublish(impl)) {
					WindowEvents::CloseRequested.Publish(EventBus::GetGlobal(), WindowCloseRequestedEvent{impl.self});
				}
			} else if (protocol == atoms.netWmPing) {
				XEvent reply{};
				reply.xclient = message;
				reply.xclient.window = root;
				x.XSendEvent(display, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &reply);
			}
			return;
		}

		auto answer = [&](Atom type, long l1, long l2) {
			XEvent reply{};
			reply.xclient.type = ClientMessage;
			reply.xclient.window = w.dropSource;
			reply.xclient.message_type = type;
			reply.xclient.format = 32;
			reply.xclient.data.l[0] = static_cast<long>(w.handle);
			reply.xclient.data.l[1] = l1;
			reply.xclient.data.l[4] = l2;
			x.XSendEvent(display, w.dropSource, False, 0, &reply);
		};

		if (message.message_type == atoms.xdndEnter) {
			w.dropSource = static_cast<::Window>(data[0]);
			w.dropAccepts = false;
			if (data[1] & 1) {
				Vector<u8> raw{GetMemoryResource(MemTag::Platform)};
				if (ReadProperty(w.dropSource, atoms.xdndTypeList, XA_ATOM, raw)) {
					const Atom* types = reinterpret_cast<const Atom*>(raw.data());
					for (size_t i = 0; i < raw.size() / sizeof(Atom); ++i) {
						w.dropAccepts = w.dropAccepts || types[i] == atoms.textUriList;
					}
				}
			} else {
				for (int i = 2; i <= 4; ++i) {
					w.dropAccepts = w.dropAccepts || static_cast<Atom>(data[i]) == atoms.textUriList;
				}
			}
		} else if (message.message_type == atoms.xdndPosition) {
			const Vec2I screenPos{static_cast<i32>((data[2] >> 16) & 0xFFFF), static_cast<i32>(data[2] & 0xFFFF)};
			w.dropClientPos = screenPos - w.clientOrigin;
			answer(atoms.xdndStatus, w.dropAccepts ? 1 : 0, w.dropAccepts ? static_cast<long>(atoms.xdndActionCopy) : 0);
		} else if (message.message_type == atoms.xdndLeave) {
			w.dropSource = 0;
			w.dropAccepts = false;
		} else if (message.message_type == atoms.xdndDrop) {
			if (w.dropAccepts) {
				x.XConvertSelection(display, atoms.xdndSelection, atoms.textUriList, atoms.xdndSelection, w.handle, static_cast<::Time>(data[2]));
			} else {
				answer(atoms.xdndFinished, 0, 0);
				w.dropSource = 0;
			}
		}
	}

	void X11Server::HandleDropSelection(Window::Impl& impl, const XSelectionEvent& selection) {
		X11Window& w = Native(impl);
		std::vector<str> paths;
		if (selection.property != X11_NONE) {
			Vector<u8> raw{GetMemoryResource(MemTag::Platform)};
			if (ReadProperty(w.handle, selection.property, AnyPropertyType, raw)) {
				ParseUriList(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()), paths);
			}
			x.XDeleteProperty(display, w.handle, selection.property);
		}
		if (w.dropSource) {
			XEvent reply{};
			reply.xclient.type = ClientMessage;
			reply.xclient.window = w.dropSource;
			reply.xclient.message_type = atoms.xdndFinished;
			reply.xclient.format = 32;
			reply.xclient.data.l[0] = static_cast<long>(w.handle);
			reply.xclient.data.l[1] = paths.empty() ? 0 : 1;
			reply.xclient.data.l[2] = paths.empty() ? 0 : static_cast<long>(atoms.xdndActionCopy);
			x.XSendEvent(display, w.dropSource, False, 0, &reply);
		}
		w.dropSource = 0;
		w.dropAccepts = false;
		if (CanPublish(impl) && !paths.empty()) {
			WindowEvents::FilesDropped.Publish(EventBus::GetGlobal(), FilesDroppedEvent{impl.self, w.dropClientPos, std::move(paths)});
		}
	}

} // namespace Shalltear::Linux
