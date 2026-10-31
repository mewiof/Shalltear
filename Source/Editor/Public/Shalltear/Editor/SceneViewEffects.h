#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {

	namespace SceneViewEffects {

		SHALLTEAR_EDITOR_API bool Register(std::string_view label, const void* owner, std::span<const StringHash> renderers = {});
		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API void GetLabels(Vector<str>& out);
		SHALLTEAR_EDITOR_API bool IsShown(std::string_view label);
		SHALLTEAR_EDITOR_API void GetHiddenRenderers(const std::function<bool(std::string_view)>& shows, Vector<StringHash>& out);

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace SceneViewEffects

} // namespace Shalltear
