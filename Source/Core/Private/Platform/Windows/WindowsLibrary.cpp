#include "Platform/Library.h"

#include "Platform/Windows/WindowsHeaders.h"

#include "Shalltear/Core/StringUtil.h"

#include <format>

namespace Shalltear::Platform {

	void* OpenLibrary(const str& utf8Path, str* outError) {
		HMODULE image = ::LoadLibraryW(StringUtil::UTF8ToWide(utf8Path).c_str());
		if (!image && outError) {
			*outError = std::format("error {}", ::GetLastError());
		}
		return image;
	}

	void CloseLibrary(void* imageBase) {
		if (imageBase) {
			::FreeLibrary(static_cast<HMODULE>(imageBase));
		}
	}

	bool ClaimLibraryFile(const str&) {
		// writer already refused by loader's hold on a mapped image file
		return true;
	}

	void ReleaseLibraryFile(const str&) {
	}

	void* FindSymbol(const void* imageBase, const char* name) {
		if (!imageBase || !name) {
			return nullptr;
		}
		return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(const_cast<void*>(imageBase)), name));
	}

	void* GetExecutableImage() {
		return ::GetModuleHandleW(nullptr);
	}

	void* ImageBaseOf(const void* address) {
		if (!address) {
			return nullptr;
		}

		HMODULE owner = nullptr;
		if (!::GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(address),
				&owner)) {
			return nullptr;
		}
		return owner;
	}

	ImageSpan GetImageSpan(const void* imageBase) {
		if (!imageBase) {
			return {};
		}

		const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(imageBase);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return {};
		}
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
			static_cast<const u8*>(imageBase) + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) {
			return {};
		}
		const u8* begin = static_cast<const u8*>(imageBase);
		return {begin, begin + nt->OptionalHeader.SizeOfImage};
	}

	std::string_view GetSharedLibrarySuffix() {
		return ".dll";
	}

	str GetSystemLibraryFileName(std::string_view stem) {
		return str(stem) + ".dll";
	}

	std::string_view GetDebugSymbolsSuffix() {
		return ".pdb";
	}

} // namespace Shalltear::Platform
