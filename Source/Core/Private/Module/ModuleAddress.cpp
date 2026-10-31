#include "Module/ModuleAddress.h"

#include "Shalltear/Core/Platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Shalltear::ModuleInternal {

	void* ImageBaseOf(const void* address) {
		if (!address) {
			return nullptr;
		}

		HMODULE owner = nullptr;
		if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(address),
				&owner)) {
			return nullptr;
		}
		return owner;
	}

	size_t ImageSizeOf(const void* imageBase) {
		if (!imageBase) {
			return 0;
		}

		const auto* dos = static_cast<const IMAGE_DOS_HEADER*>(imageBase);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return 0;
		}
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
			static_cast<const u8*>(imageBase) + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) {
			return 0;
		}
		return nt->OptionalHeader.SizeOfImage;
	}

	bool IsAddressInImage(const void* address, const void* imageBase) {
		if (!address || !imageBase) {
			return false;
		}
		const u8* base = static_cast<const u8*>(imageBase);
		const u8* p = static_cast<const u8*>(address);
		return p >= base && p < base + ImageSizeOf(imageBase);
	}

} // namespace Shalltear::ModuleInternal
