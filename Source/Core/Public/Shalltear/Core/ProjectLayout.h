#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear::ProjectLayout {

	SHALLTEAR_CORE_API std::span<const std::string_view> GetContentRoots();

	SHALLTEAR_CORE_API bool IsReservedFileName(std::string_view fileName);

} // namespace Shalltear::ProjectLayout
