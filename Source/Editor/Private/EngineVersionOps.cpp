#include "Shalltear/Editor/EngineVersionOps.h"

#include "Shalltear/Core/BuildInfo.h"

#include <format>

namespace Shalltear::EngineVersionOps {

	str ReleaseLabel() {
		return "dev";
	}

	str DisplayLabel() {
		return std::format("{} {}", ReleaseLabel(), BuildInfo::GetGitCommit());
	}

} // namespace Shalltear::EngineVersionOps
