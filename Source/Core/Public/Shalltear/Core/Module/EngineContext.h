#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	inline constexpr u32 SLT_ENGINE_CONTEXT_VERSION = 5;

	struct EngineContext {
		u32 version;
		u32 size;
		u64 engineHash;
		void* stateTable;
	};

	SHALLTEAR_CORE_API void CaptureEngineContext(EngineContext& out);

	SHALLTEAR_CORE_API bool BindEngineContext(const EngineContext* context);

} // namespace Shalltear
