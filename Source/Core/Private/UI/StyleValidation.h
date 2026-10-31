#pragma once

#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear::UI {

	inline u32 CountShadows(std::string_view value) {
		u32 count = 0;
		size_t cursor = 0;
		while (cursor <= value.size()) {
			size_t next = value.find(',', cursor);
			if (next == std::string_view::npos) {
				next = value.size();
			}
			std::string_view token = value.substr(cursor, next - cursor);
			if (token.find_first_not_of(" \t\r\n") != std::string_view::npos) {
				++count;
			}
			if (next == value.size()) {
				break;
			}
			cursor = next + 1;
		}
		return count;
	}

} // namespace Shalltear::UI
