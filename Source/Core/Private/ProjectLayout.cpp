#include "Shalltear/Core/ProjectLayout.h"

#include <array>
#include <cctype>

namespace Shalltear::ProjectLayout {

	namespace {

		constexpr std::array<std::string_view, 1> ContentRoots{
			std::string_view("Assets")};

		constexpr std::string_view MetaExtension = ".meta";
		constexpr std::string_view AssetIndexFile = ".assets.db";

		bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
			if (a.size() != b.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
					return false;
				}
			}
			return true;
		}

	} // namespace

	bool IsReservedFileName(std::string_view fileName) {
		if (EqualsIgnoreCase(fileName, AssetIndexFile)) {
			return true;
		}
		return fileName.size() > MetaExtension.size() &&
			   EqualsIgnoreCase(fileName.substr(fileName.size() - MetaExtension.size()), MetaExtension);
	}

	std::span<const std::string_view> GetContentRoots() {
		return std::span<const std::string_view>(ContentRoots.data(), ContentRoots.size());
	}

} // namespace Shalltear::ProjectLayout
