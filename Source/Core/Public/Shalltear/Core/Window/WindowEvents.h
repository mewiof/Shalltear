#pragma once

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Window.h"

#include <vector>

namespace Shalltear {

	struct WindowCreatedEvent {
		Handle<Window> window;
	};

	struct WindowCloseRequestedEvent {
		Handle<Window> window;
	};

	struct WindowDestroyingEvent {
		Handle<Window> window;
	};

	struct WindowDestroyedEvent {
		u64 windowId = 0;
	};

	struct WindowResizedEvent {
		Handle<Window> window;
		Vec2I clientSizePx;
		bool live = false;
	};

	struct WindowMovedEvent {
		Handle<Window> window;
		Vec2I positionPx;
	};

	struct WindowDPIChangedEvent {
		Handle<Window> window;
		f32 scale = 1.0f;
		RectI suggestedRect;
	};

	struct WindowFocusEvent {
		Handle<Window> window;
		bool focused = false;
	};

	struct WindowStateEvent {
		Handle<Window> window;
		WindowState state = WindowState::Normal;
	};

	struct WindowDragBeganEvent {
		Handle<Window> window;
		Vec2I screenCursorPx;
	};

	struct WindowDragMovedEvent {
		Handle<Window> window;
		Vec2I screenCursorPx;
		RectI windowRect;
	};

	struct WindowDragEndedEvent {
		Handle<Window> window;
		Vec2I screenCursorPx;
	};

	struct FilesDroppedEvent {
		Handle<Window> window;
		Vec2I clientPosPx;
		std::vector<str> paths;
	};

	struct MonitorsChangedEvent {};

	namespace WindowEvents {
		inline constexpr Event<WindowCreatedEvent> Created{"Window.Created"_hash};
		inline constexpr Event<WindowCloseRequestedEvent> CloseRequested{"Window.CloseRequested"_hash};
		inline constexpr Event<WindowDestroyingEvent> Destroying{"Window.Destroying"_hash};
		inline constexpr Event<WindowDestroyedEvent> Destroyed{"Window.Destroyed"_hash};
		inline constexpr Event<WindowResizedEvent> Resized{"Window.Resized"_hash};
		inline constexpr Event<WindowMovedEvent> Moved{"Window.Moved"_hash};
		inline constexpr Event<WindowDPIChangedEvent> DPIChanged{"Window.DpiChanged"_hash};
		inline constexpr Event<WindowFocusEvent> Focus{"Window.Focus"_hash};
		inline constexpr Event<WindowStateEvent> StateChanged{"Window.StateChanged"_hash};
		inline constexpr Event<WindowDragBeganEvent> DragBegan{"Window.DragBegan"_hash};
		inline constexpr Event<WindowDragMovedEvent> DragMoved{"Window.DragMoved"_hash};
		inline constexpr Event<WindowDragEndedEvent> DragEnded{"Window.DragEnded"_hash};
		inline constexpr Event<FilesDroppedEvent> FilesDropped{"Window.FilesDropped"_hash};
		inline constexpr Event<MonitorsChangedEvent> MonitorsChanged{"Window.MonitorsChanged"_hash};
	} // namespace WindowEvents

} // namespace Shalltear
