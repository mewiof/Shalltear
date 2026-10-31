#include "Shalltear/Editor/EditorTransformGesture.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/Editor/EditorEvents.h"

namespace Shalltear {

	namespace {

		bool _active = false;

	} // anonymous namespace

	void EditorTransformGesture::Begin(std::span<const Handle<SceneObject>> objects) {
		if (objects.empty()) {
			End();
			return;
		}
		_active = true;
		EditorEvents::TransformGesture.Publish(EventBus::GetGlobal(), {objects});
	}

	void EditorTransformGesture::End() {
		if (!_active) {
			return;
		}
		_active = false;
		EditorEvents::TransformGesture.Publish(EventBus::GetGlobal(), {{}});
	}

} // namespace Shalltear
