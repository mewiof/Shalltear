#pragma once

#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear::Platform {

	inline str ParseXDGUserDir(std::string_view config, std::string_view key, std::string_view home) {
		while (!config.empty()) {
			const size_t end = config.find('\n');
			std::string_view line = config.substr(0, end);
			config = end == std::string_view::npos ? std::string_view() : config.substr(end + 1);
			while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
				line.remove_prefix(1);
			}
			if (!line.starts_with(key) || line.size() <= key.size() || line[key.size()] != '=') {
				continue;
			}
			std::string_view value = line.substr(key.size() + 1);
			while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) {
				value.remove_suffix(1);
			}
			if (value.size() < 2 || value.front() != '"' || value.back() != '"') {
				return {};
			}
			value = value.substr(1, value.size() - 2);
			constexpr std::string_view Home = "$HOME";
			if (value.starts_with(Home)) {
				return home.empty() ? str() : str(home) + str(value.substr(Home.size()));
			}
			return value.starts_with('/') ? str(value) : str();
		}
		return {};
	}

} // namespace Shalltear::Platform
