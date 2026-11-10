#include "TestsPCH.h"

#include "Checkout.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

	constexpr std::string_view PLATFORM_TOKENS[] = {
		"_WIN32",
		"_WIN64",
		"__linux__",
		"__APPLE__",
		"__ANDROID__",
		"__MINGW32__",
		"SLT_PLATFORM_",
	};

	constexpr std::string_view SYSTEM_HEADERS[] = {
		"<windows.h>",
		"<Windows.h>",
		"<windowsx.h>",
		"<shellapi.h>",
		"<shlobj.h>",
		"<dwmapi.h>",
		"<unistd.h>",
		"<dlfcn.h>",
		"<pthread.h>",
		"<poll.h>",
		"<spawn.h>",
		"<sys/",
		"<linux/",
		"<X11/",
		"<wayland-",
		"<xkbcommon/",
	};

	constexpr std::string_view DECLARING_HEADERS[] = {
		"Core/Public/Shalltear/Core/Platform.h",
		"Core/Public/Shalltear/Core/Export.h",
		"Core/Public/Shalltear/Core/Module/ModuleAPI.h",
	};

	bool IsIdentifierChar(char c) {
		return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
	}

	bool NamesPlatform(std::string_view text) {
		for (std::string_view token : PLATFORM_TOKENS) {
			for (size_t at = text.find(token); at != std::string_view::npos; at = text.find(token, at + 1)) {
				if (at == 0 || !IsIdentifierChar(text[at - 1])) {
					return true;
				}
			}
		}
		for (std::string_view header : SYSTEM_HEADERS) {
			if (text.find(header) != std::string_view::npos) {
				return true;
			}
		}
		return false;
	}

	bool IsPlatformFamilyFile(std::string_view relative) {
		constexpr std::string_view MARKER = "/Private/Platform/";
		const size_t at = relative.find(MARKER);
		return at != std::string_view::npos && relative.find('/', at + MARKER.size()) != std::string_view::npos;
	}

	bool IsExempt(std::string_view relative) {
		if (relative.starts_with("Tests/") || relative.find("/ThirdParty/") != std::string_view::npos ||
			relative.ends_with("/stb_image.h") || IsPlatformFamilyFile(relative)) {
			return true;
		}
		for (std::string_view header : DECLARING_HEADERS) {
			if (relative == header) {
				return true;
			}
		}
		return false;
	}

	bool IsSource(const std::filesystem::path& path) {
		const std::string extension = path.extension().string();
		return extension == ".cpp" || extension == ".h" || extension == ".c" || extension == ".inl";
	}

	std::string ReadAll(const std::filesystem::path& path) {
		std::ifstream file(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}

} // anonymous namespace

TEST(PlatformBoundaryTest, MatcherFindsMacrosAndHeaders) {
	EXPECT_TRUE(NamesPlatform("#if defined(_WIN32)"));
	EXPECT_TRUE(NamesPlatform("#ifdef SLT_PLATFORM_LINUX"));
	EXPECT_TRUE(NamesPlatform("#include <sys/stat.h>"));
	EXPECT_FALSE(NamesPlatform("MemTag::Platform + SHALLTEAR_WIN32X"));
	EXPECT_FALSE(NamesPlatform("#include <filesystem>"));

	const std::filesystem::path header = Checkout::Find("Source/Core/Public/Shalltear/Core/Platform.h");
	ASSERT_FALSE(header.empty());
	EXPECT_TRUE(NamesPlatform(ReadAll(header)));
}

TEST(PlatformBoundaryTest, FamilyDirectoryIsRecognized) {
	EXPECT_TRUE(IsPlatformFamilyFile("Core/Private/Platform/Windows/WindowsClock.cpp"));
	EXPECT_TRUE(IsPlatformFamilyFile("Editor/Private/Platform/Linux/Window/X11/X11Window.cpp"));
	EXPECT_FALSE(IsPlatformFamilyFile("Core/Private/Platform/Clock.h"));
	EXPECT_FALSE(IsPlatformFamilyFile("Core/Private/Time.cpp"));
}

TEST(PlatformBoundaryTest, SharedSourcesNameNoPlatform) {
	const std::filesystem::path source = Checkout::Find("Source");
	ASSERT_FALSE(source.empty());

	std::vector<std::string> offenders;
	size_t scanned = 0;
	std::error_code ec;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(source, ec)) {
		if (!entry.is_regular_file(ec) || !IsSource(entry.path())) {
			continue;
		}
		const std::string relative = std::filesystem::relative(entry.path(), source, ec).generic_string();
		if (IsExempt(relative)) {
			continue;
		}
		++scanned;
		if (NamesPlatform(ReadAll(entry.path()))) {
			offenders.push_back(relative);
		}
	}

	EXPECT_GT(scanned, size_t{900});
	EXPECT_TRUE(offenders.empty()) << offenders.size() << " file(s), 1st " << offenders.front();
}
