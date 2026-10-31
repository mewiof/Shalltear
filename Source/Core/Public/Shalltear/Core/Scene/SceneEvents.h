#pragma once

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SceneLoadOperation;

	struct SceneLoadedEvent {
		Handle<Scene> scene;
	};

	struct SceneUnloadingEvent {
		Handle<Scene> scene;
	};

	struct SceneUnloadedEvent {
		u64 sceneId = 0;
	};

	struct SceneLoadCompletedEvent {
		Handle<SceneLoadOperation> operation;
		Handle<Scene> scene;
	};

	namespace SceneEvents {
		inline constexpr Event<SceneLoadedEvent> Loaded{"Scene.Loaded"_hash};
		inline constexpr Event<SceneUnloadingEvent> Unloading{"Scene.Unloading"_hash};
		inline constexpr Event<SceneUnloadedEvent> Unloaded{"Scene.Unloaded"_hash};
		inline constexpr Event<SceneLoadCompletedEvent> LoadCompleted{"Scene.LoadCompleted"_hash};
	} // namespace SceneEvents

} // namespace Shalltear
