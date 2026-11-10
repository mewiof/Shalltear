#include "Platform/Text.h"

#include "Platform/Windows/WindowsHeaders.h"

namespace Shalltear::Platform {

	std::wstring UTF8ToWide(std::string_view utf8) {
		if (utf8.empty()) {
			return std::wstring();
		}

		int count = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.length()), nullptr, 0);
		if (count == 0) {
			return std::wstring();
		}

		std::wstring result(count, 0);
		::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.length()), &result[0], count);
		return result;
	}

	str WideToUTF8(std::wstring_view wide) {
		if (wide.empty()) {
			return "";
		}

		int count = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.length()), nullptr, 0, nullptr, nullptr);
		if (count == 0) {
			return "";
		}

		str result(count, 0);
		::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.length()), &result[0], count, nullptr, nullptr);
		return result;
	}

} // namespace Shalltear::Platform
