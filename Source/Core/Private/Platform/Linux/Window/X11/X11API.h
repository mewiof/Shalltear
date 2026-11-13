#pragma once

// after engine headers (crappy Xlib.h macros None, Bool, Status, Always + Success)

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>

#undef None
#undef Bool
#undef Status
#undef Always
#undef Success

namespace Shalltear::Linux {

	inline constexpr unsigned long X11_NONE = 0;

#define SLT_X11_FUNCS(X)          \
	X(XInitThreads)               \
	X(XOpenDisplay)               \
	X(XCloseDisplay)              \
	X(XInternAtom)                \
	X(XGetAtomName)               \
	X(XCreateWindow)              \
	X(XDestroyWindow)             \
	X(XMapWindow)                 \
	X(XMapRaised)                 \
	X(XUnmapWindow)               \
	X(XWithdrawWindow)            \
	X(XIconifyWindow)             \
	X(XMoveWindow)                \
	X(XResizeWindow)              \
	X(XMoveResizeWindow)          \
	X(XRaiseWindow)               \
	X(XStoreName)                 \
	X(XSetWMProtocols)            \
	X(XSetWMNormalHints)          \
	X(XSetWMHints)                \
	X(XSetClassHint)              \
	X(XSetTransientForHint)       \
	X(XAllocSizeHints)            \
	X(XAllocWMHints)              \
	X(XAllocClassHint)            \
	X(XFree)                      \
	X(XChangeProperty)            \
	X(XDeleteProperty)            \
	X(XGetWindowProperty)         \
	X(XGetWindowAttributes)       \
	X(XTranslateCoordinates)      \
	X(XQueryPointer)              \
	X(XSelectInput)               \
	X(XSendEvent)                 \
	X(XPending)                   \
	X(XNextEvent)                 \
	X(XPeekEvent)                 \
	X(XFilterEvent)               \
	X(XFlush)                     \
	X(XSync)                      \
	X(XGetEventData)              \
	X(XFreeEventData)             \
	X(XQueryExtension)            \
	X(XSetInputFocus)             \
	X(XGetInputFocus)             \
	X(XGrabPointer)               \
	X(XUngrabPointer)             \
	X(XWarpPointer)               \
	X(XDefineCursor)              \
	X(XUndefineCursor)            \
	X(XCreateFontCursor)          \
	X(XCreatePixmapCursor)        \
	X(XCreateBitmapFromData)      \
	X(XFreePixmap)                \
	X(XFreeCursor)                \
	X(XSetSelectionOwner)         \
	X(XGetSelectionOwner)         \
	X(XConvertSelection)          \
	X(XSetErrorHandler)           \
	X(XkbSetDetectableAutoRepeat) \
	X(XkbKeycodeToKeysym)         \
	X(XkbLookupKeySym)            \
	X(XOpenIM)                    \
	X(XCloseIM)                   \
	X(XCreateIC)                  \
	X(XDestroyIC)                 \
	X(XSetICFocus)                \
	X(XUnsetICFocus)              \
	X(XSetLocaleModifiers)        \
	X(Xutf8LookupString)          \
	X(XLookupString)              \
	X(XRefreshKeyboardMapping)    \
	X(XCheckIfEvent)

#define SLT_XI_FUNCS(X) \
	X(XIQueryVersion)   \
	X(XISelectEvents)

#define SLT_XRANDR_FUNCS(X)         \
	X(XRRQueryExtension)            \
	X(XRRQueryVersion)              \
	X(XRRSelectInput)               \
	X(XRRGetMonitors)               \
	X(XRRFreeMonitors)              \
	X(XRRGetScreenResourcesCurrent) \
	X(XRRFreeScreenResources)       \
	X(XRRGetOutputInfo)             \
	X(XRRFreeOutputInfo)            \
	X(XRRGetCrtcInfo)               \
	X(XRRFreeCrtcInfo)              \
	X(XRRUpdateConfiguration)

#define SLT_XCURSOR_FUNCS(X) \
	X(XcursorLibraryLoadCursor)

	struct X11API {
#define SLT_X11_DECLARE(name) decltype(&::name) name = nullptr;
		SLT_X11_FUNCS(SLT_X11_DECLARE)
		SLT_XI_FUNCS(SLT_X11_DECLARE)
		SLT_XRANDR_FUNCS(SLT_X11_DECLARE)
		SLT_XCURSOR_FUNCS(SLT_X11_DECLARE)
#undef SLT_X11_DECLARE

		void* x11 = nullptr;
		void* xi = nullptr;
		void* xrandr = nullptr;
		void* xcursor = nullptr;

		// false when libX11 is absent. Input/RandR/cursor themes optional
		bool Load(str& outWhy);
		void Unload();
	};

} // namespace Shalltear::Linux
