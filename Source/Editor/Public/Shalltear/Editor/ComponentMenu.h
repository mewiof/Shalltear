#pragma once

#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear::ComponentMenu {

	SHALLTEAR_EDITOR_API void RegisterBuiltIns();

	SHALLTEAR_EDITOR_API void Register(StringHash componentTypeName, std::string_view category,
									   const void* owner = nullptr);

	SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

	SHALLTEAR_EDITOR_API std::string_view CategoryFor(const TypeInfo* componentType);

	SHALLTEAR_EDITOR_API void Shutdown();

} // namespace Shalltear::ComponentMenu
