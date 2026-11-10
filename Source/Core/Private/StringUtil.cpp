#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Core/Platform.h"

#include <algorithm>
#include <cctype>

#include "Platform/Text.h"

namespace Shalltear {

	std::vector<str> StringUtil::Split(std::string_view s, std::string_view delimiter) {
		std::vector<str> result;
		if (delimiter.empty()) {
			result.emplace_back(s);
			return result;
		}

		size_t start = 0;
		size_t pos = s.find(delimiter);

		while (pos != std::string_view::npos) {
			if (pos > start) {
				result.emplace_back(s.substr(start, pos - start));
			}
			start = pos + delimiter.length();
			pos = s.find(delimiter, start);
		}

		if (start < s.length()) {
			result.emplace_back(s.substr(start));
		}

		return result;
	}

	str StringUtil::Join(const std::vector<str>& parts, std::string_view delimiter) {
		if (parts.empty()) {
			return "";
		}

		size_t totalLen = delimiter.length() * (parts.size() - 1);
		for (const auto& part : parts) {
			totalLen += part.length();
		}

		str result;
		result.reserve(totalLen);

		for (size_t i = 0; i < parts.size(); ++i) {
			result += parts[i];
			if (i < parts.size() - 1) {
				result += delimiter;
			}
		}
		return result;
	}

	str StringUtil::TrimLeft(std::string_view s) {
		size_t start = s.find_first_not_of(" \t\n\r\f\v");
		return (start == std::string_view::npos) ? "" : str(s.substr(start));
	}

	str StringUtil::TrimRight(std::string_view s) {
		size_t end = s.find_last_not_of(" \t\n\r\f\v");
		return (end == std::string_view::npos) ? "" : str(s.substr(0, end + 1));
	}

	str StringUtil::Trim(std::string_view s) {
		return TrimRight(TrimLeft(s));
	}

	bool StringUtil::StartsWith(std::string_view s, std::string_view prefix) {
		return s.starts_with(prefix);
	}

	bool StringUtil::EndsWith(std::string_view s, std::string_view suffix) {
		return s.ends_with(suffix);
	}

	str StringUtil::ToLower(std::string_view s) {
		str result(s);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return static_cast<char>(std::tolower(c));
		});
		return result;
	}

	str StringUtil::ToUpper(std::string_view s) {
		str result(s);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
			return static_cast<char>(std::toupper(c));
		});
		return result;
	}

	str StringUtil::Replace(std::string_view s, std::string_view search, std::string_view replace) {
		if (search.empty()) {
			return str(s);
		}

		str result(s);
		size_t pos = 0;
		while ((pos = result.find(search, pos)) != str::npos) {
			result.replace(pos, search.length(), replace);
			pos += replace.length();
		}
		return result;
	}

	std::wstring StringUtil::UTF8ToWide(std::string_view utf8Str) {
		return Platform::UTF8ToWide(utf8Str);
	}

	str StringUtil::WideToUTF8(std::wstring_view wideStr) {
		return Platform::WideToUTF8(wideStr);
	}

} // namespace Shalltear
