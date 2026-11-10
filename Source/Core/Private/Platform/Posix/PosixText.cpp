#include "Platform/Text.h"

namespace Shalltear::Platform {

	static_assert(sizeof(wchar_t) == 4, "wchar_t holds a whole code point here");

	namespace {

		constexpr char32_t REPLACEMENT = 0xFFFD;

		bool IsContinuation(char c) {
			return (static_cast<u8>(c) & 0xC0) == 0x80;
		}

		void AppendUTF8(str& out, char32_t cp) {
			if (cp < 0x80) {
				out.push_back(static_cast<char>(cp));
			} else if (cp < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			} else if (cp < 0x10000) {
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			} else {
				out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}

	} // anonymous namespace

	std::wstring UTF8ToWide(std::string_view utf8) {
		std::wstring wide;
		wide.reserve(utf8.size());
		size_t i = 0;
		while (i < utf8.size()) {
			const u8 lead = static_cast<u8>(utf8[i]);
			char32_t cp = 0;
			size_t extra = 0;
			char32_t smallest = 0;
			if (lead < 0x80) {
				cp = lead;
			} else if ((lead & 0xE0) == 0xC0) {
				cp = lead & 0x1F;
				extra = 1;
				smallest = 0x80;
			} else if ((lead & 0xF0) == 0xE0) {
				cp = lead & 0x0F;
				extra = 2;
				smallest = 0x800;
			} else if ((lead & 0xF8) == 0xF0) {
				cp = lead & 0x07;
				extra = 3;
				smallest = 0x10000;
			} else {
				wide.push_back(static_cast<wchar_t>(REPLACEMENT));
				++i;
				continue;
			}

			bool valid = i + extra < utf8.size() || (extra == 0);
			for (size_t k = 1; valid && k <= extra; ++k) {
				if (!IsContinuation(utf8[i + k])) {
					valid = false;
					break;
				}
				cp = (cp << 6) | (static_cast<u8>(utf8[i + k]) & 0x3F);
			}
			if (!valid || cp < smallest || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
				wide.push_back(static_cast<wchar_t>(REPLACEMENT));
				++i;
				continue;
			}
			wide.push_back(static_cast<wchar_t>(cp));
			i += extra + 1;
		}
		return wide;
	}

	str WideToUTF8(std::wstring_view wide) {
		str utf8;
		utf8.reserve(wide.size());
		for (wchar_t unit : wide) {
			char32_t cp = static_cast<char32_t>(unit);
			if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
				cp = REPLACEMENT;
			}
			AppendUTF8(utf8, cp);
		}
		return utf8;
	}

} // namespace Shalltear::Platform
