#pragma once

#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear {

	class Object;

	namespace AuthoringDefaults {

		SHALLTEAR_EDITOR_API void RegisterBuiltIns();

		SHALLTEAR_EDITOR_API bool Register(StringHash componentType, StringHash field, std::string_view valueJSON,
										   const void* owner = nullptr);

		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API str StateFor(const TypeInfo* type);

		SHALLTEAR_EDITOR_API bool Write(Object& object, std::string_view state);

		SHALLTEAR_EDITOR_API void Shutdown();

	} // namespace AuthoringDefaults

} // namespace Shalltear
