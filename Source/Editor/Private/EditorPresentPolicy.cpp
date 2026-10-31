#include "Shalltear/Editor/EditorPresentPolicy.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Window/Window.h"

namespace Shalltear {

	void EditorPresentPolicy::Install() {
		Handle<EditorPresentPolicy> self(this);
		WindowEvents::Created.Subscribe(EventBus::GetGlobal(), self, &EditorPresentPolicy::OnWindowCreated);
	}

	void EditorPresentPolicy::OnWindowCreated(const WindowCreatedEvent& event) {
		if (Window* window = event.window.Get()) {
			window->SetVSyncHint(false);
		}
	}

} // namespace Shalltear
