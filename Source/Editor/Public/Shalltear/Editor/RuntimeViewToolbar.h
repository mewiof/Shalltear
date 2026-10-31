#pragma once

#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorIcons.h"

#include <functional>

namespace Shalltear {

	struct RuntimeViewToggle {
		EditorIcons::Icon icon;
		str label;
		str tooltip;
		std::function<bool()> isOn;
		std::function<void(bool)> set;
	};

	namespace RuntimeViewToolbar {

		SHALLTEAR_EDITOR_API bool RegisterToggle(StringHash id, RuntimeViewToggle toggle, const void* owner = nullptr);

		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API void ForEachToggle(const std::function<void(StringHash id, const RuntimeViewToggle& toggle)>& visit);

		SHALLTEAR_EDITOR_API const RuntimeViewToggle* FindToggle(StringHash id);

		SHALLTEAR_EDITOR_API u64 GetVersion();

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace RuntimeViewToolbar

} // namespace Shalltear
