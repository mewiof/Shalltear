#pragma once

#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear::Platform {

	struct ImageSpan {
		const u8* begin = nullptr;
		const u8* end = nullptr;

		bool Contains(const void* address) const {
			const u8* p = static_cast<const u8*>(address);
			return p && p >= begin && p < end;
		}
	};

	// image base, null on failure
	void* OpenLibrary(const str& utf8Path, str* outError = nullptr);
	void CloseLibrary(void* imageBase);

	// false while held elsewhere, claimed until ReleaseLibraryFile
	bool ClaimLibraryFile(const str& utf8Path);
	void ReleaseLibraryFile(const str& utf8Path);

	// null unless image itself defines name
	void* FindSymbol(const void* imageBase, const char* name);

	void* GetExecutableImage();

	void* ImageBaseOf(const void* address);
	ImageSpan GetImageSpan(const void* imageBase);

	std::string_view GetSharedLibrarySuffix();
	str GetSystemLibraryFileName(std::string_view stem);

	// empty when symbols stay inside image
	std::string_view GetDebugSymbolsSuffix();

} // namespace Shalltear::Platform
