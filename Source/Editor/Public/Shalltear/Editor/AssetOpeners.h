#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <string_view>

namespace Shalltear::AssetOpeners {

	using OpenFn = std::function<void(const Guid& asset)>;

	SHALLTEAR_EDITOR_API bool Register(std::string_view extension, OpenFn open, const void* owner);

	SHALLTEAR_EDITOR_API bool Has(std::string_view extension);

	SHALLTEAR_EDITOR_API bool Open(const Path& virtualPath);

	SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

	SHALLTEAR_EDITOR_API void Terminate();

} // namespace Shalltear::AssetOpeners
