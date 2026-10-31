#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <functional>

namespace Shalltear {

	class Scene;
	class SceneObject;

	namespace EditorSceneBuild {

		Handle<SceneObject> Push(str name, Handle<Scene> scene, Handle<SceneObject> parent, StringHash component,
								 std::function<void(Handle<SceneObject>)> configure);

	} // namespace EditorSceneBuild

} // namespace Shalltear
