#pragma once

#include "Shalltear/Core/Types.h"

#include <string>
#include <string_view>

namespace Shalltear::Platform {

	std::wstring UTF8ToWide(std::string_view utf8);
	str WideToUTF8(std::wstring_view wide);

} // namespace Shalltear::Platform
