#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API BuildInfo {
	public:
		static const char* GetVersionString() noexcept;

		static const char* GetGitCommit() noexcept;

		static const char* GetBuildConfig() noexcept;

		static const char* GetBuildTimestamp() noexcept;

		static u64 GetEngineHash() noexcept;

		static const char* GetCompilerId() noexcept;
		static const char* GetCompilerVersion() noexcept;

		static u64 GetModuleAbiHash() noexcept;
	};

} // namespace Shalltear
