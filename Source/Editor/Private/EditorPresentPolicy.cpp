#include "Shalltear/Editor/EditorPresentPolicy.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Window/Window.h"

#include <algorithm>

namespace Shalltear {

	void EditorPresentPolicy::Install() {
		Handle<EditorPresentPolicy> self(this);
		WindowEvents::Created.Subscribe(EventBus::GetGlobal(), self, &EditorPresentPolicy::OnWindowCreated);
	}

	void EditorPresentPolicy::SetMode(InteractionMode mode) {
		if (mode == _mode) {
			return;
		}
		_mode = mode;
		std::erase_if(_windows, [](const Handle<Window>& window) { return !window.IsValid(); });
		for (const Handle<Window>& window : _windows) {
			window.Get()->SetVSyncHint(_mode == InteractionMode::Default);
		}
	}

	void EditorPresentPolicy::OnWindowCreated(const WindowCreatedEvent& event) {
		if (Window* window = event.window.Get()) {
			_windows.push_back(event.window);
			window->SetVSyncHint(_mode == InteractionMode::Default);
		}
	}

} // namespace Shalltear
