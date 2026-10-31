#include "Shalltear/Core/BuildInfo.h"

#include "Shalltear/Core/StringHash.h"

#include "BuildInfo.generated.h"

#include "Shalltear/Core/BuildIdentity.generated.h"

namespace Shalltear {

	const char* BuildInfo::GetVersionString() noexcept {
		return SLT_BUILD_VERSION;
	}

	const char* BuildInfo::GetGitCommit() noexcept {
		return SLT_BUILD_GIT_COMMIT;
	}

	const char* BuildInfo::GetBuildConfig() noexcept {
		return SLT_BUILD_CONFIG;
	}

	const char* BuildInfo::GetBuildTimestamp() noexcept {
		return SLT_BUILD_TIMESTAMP;
	}

	u64 BuildInfo::GetEngineHash() noexcept {
		constexpr StringHash Hash(SLT_BUILD_VERSION "|" SLT_BUILD_GIT_COMMIT);
		return Hash.GetValue();
	}

	const char* BuildInfo::GetCompilerId() noexcept {
		return SLT_BUILD_COMPILER_ID;
	}

	const char* BuildInfo::GetCompilerVersion() noexcept {
		return SLT_BUILD_COMPILER_VERSION;
	}

	u64 BuildInfo::GetModuleAbiHash() noexcept {
		return SLT_MODULE_ABI_HASH;
	}

} // namespace Shalltear
