#include "Platform/Process.h"

#include "Platform/Windows/WindowsHeaders.h"

#include <shlobj.h>

#include "Shalltear/Core/StringUtil.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>

namespace Shalltear::Platform {

	namespace {

		str ReadEnvironmentPath(const wchar_t* name) {
			wchar_t buffer[MAX_PATH];
			DWORD length = ::GetEnvironmentVariableW(name, buffer, MAX_PATH);
			if (length == 0 || length >= MAX_PATH) {
				return {};
			}
			return StringUtil::WideToUTF8(std::wstring_view(buffer, length));
		}

	} // anonymous namespace

	str GetExecutablePath() {
		wchar_t buffer[MAX_PATH * 2] = {};
		DWORD length = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
		if (length == 0 || length >= std::size(buffer)) {
			return {};
		}
		return std::filesystem::path(std::wstring(buffer, length)).generic_string();
	}

	str GetUserDataDirectory() {
		str local = ReadEnvironmentPath(L"LOCALAPPDATA");
		if (local.empty()) {
			str profile = ReadEnvironmentPath(L"USERPROFILE");
			if (profile.empty()) {
				return {};
			}
			local = profile + "/AppData/Local";
		}
		return local;
	}

	str GetUserHomeDirectory() {
		return ReadEnvironmentPath(L"USERPROFILE");
	}

	str GetUserPicturesDirectory() {
		// FOLDERID_Pictures (without linking uuid)
		constexpr GUID Pictures = {0x33E28130, 0x4E1E, 0x4676, {0x83, 0x5A, 0x98, 0x39, 0x5C, 0x3B, 0xC3, 0xBB}};
		PWSTR wide = nullptr;
		const HRESULT result = ::SHGetKnownFolderPath(Pictures, KF_FLAG_DEFAULT, nullptr, &wide);
		const std::u8string utf8 = SUCCEEDED(result) && wide ? std::filesystem::path(wide).generic_u8string() : std::u8string();
		::CoTaskMemFree(wide);
		return str(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	}

	bool AcquireStandardOutput(bool createConsole) {
		HANDLE existing = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (existing != nullptr && existing != INVALID_HANDLE_VALUE &&
			::GetFileType(existing) != FILE_TYPE_UNKNOWN) {
			return true;
		}

		auto adopt = []() {
			FILE* reopened = nullptr;
			::freopen_s(&reopened, "CONOUT$", "w", stdout);
			::freopen_s(&reopened, "CONOUT$", "w", stderr);
			std::cout.clear();
			std::cerr.clear();
		};

		if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
			adopt();
			return true;
		}
		if (createConsole && ::AllocConsole()) {
			adopt();
			return true;
		}
		return false;
	}

	void PrepareConsoleOutput() {
		HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
		if (hOut != INVALID_HANDLE_VALUE) {
			DWORD mode = 0;
			if (::GetConsoleMode(hOut, &mode)) {
				mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
				::SetConsoleMode(hOut, mode);
			}
		}
		::SetConsoleOutputCP(CP_UTF8);
	}

	void ToLocalTime(std::time_t time, std::tm& out) {
		::localtime_s(&out, &time);
	}

} // namespace Shalltear::Platform
