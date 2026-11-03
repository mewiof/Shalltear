#pragma once

#include "Shalltear/Core/Types.h"

#include <cstddef>

namespace Shalltear::UI {

	struct SystemFontFile {
		const u8* data = nullptr;
		size_t size = 0;
		u32 faceIndex = 0;
	};

	class SystemFontFallback {
	public:
		SystemFontFallback() = default;
		~SystemFontFallback();
		SystemFontFallback(const SystemFontFallback&) = delete;
		SystemFontFallback& operator=(const SystemFontFallback&) = delete;

		// owned by this object, null when no installed font has codepoint
		const SystemFontFile* Find(u32 codepoint, u16 weight, bool italic);
	private:
		struct Impl;
		Impl* _impl = nullptr;
		bool _attempted = false;
	};

} // namespace Shalltear::UI
