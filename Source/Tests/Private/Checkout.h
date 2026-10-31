#pragma once

#include <filesystem>
#include <system_error>

namespace Checkout {

	inline std::filesystem::path Root() {
		std::error_code ec;
		for (std::filesystem::path probe = std::filesystem::current_path(ec); !probe.empty(); probe = probe.parent_path()) {
			if (std::filesystem::exists(probe / "LICENSE.txt", ec) && std::filesystem::exists(probe / "CMakePresets.json", ec)) {
				return probe;
			}
			if (probe.parent_path() == probe) {
				break;
			}
		}
		return {};
	}

	inline std::filesystem::path Find(const char* relative) {
		const std::filesystem::path root = Root();
		if (root.empty()) {
			return {};
		}
		std::error_code ec;
		std::filesystem::path candidate = root / relative;
		return std::filesystem::exists(candidate, ec) ? candidate : std::filesystem::path{};
	}

} // namespace Checkout
