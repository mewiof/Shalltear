#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	class SceneObject;

	namespace EditorTransformGesture {

		SHALLTEAR_EDITOR_API void Begin(std::span<const Handle<SceneObject>> objects);

		SHALLTEAR_EDITOR_API void End();

	} // namespace EditorTransformGesture

} // namespace Shalltear
