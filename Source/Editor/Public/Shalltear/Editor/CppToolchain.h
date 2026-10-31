#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <string_view>

namespace Shalltear::CppToolchain {

	enum class ToolStatus : u8 {
		Missing = 0,
		Found,
		Unusable,
	};

	struct ToolInfo {
		str name;
		str path;
		str version;
		ToolStatus status = ToolStatus::Missing;
	};

	struct ToolchainInfo {
		ToolInfo cmake;
		ToolInfo ninja;
		ToolInfo compiler;

		str sdkRoot;
		str sdkCompilerId;
		str sdkCompilerVersion;
		str sdkCompilerTarget;
		str sdkLinkMode;

		bool IsComplete() const {
			return cmake.status == ToolStatus::Found &&
				   ninja.status == ToolStatus::Found &&
				   compiler.status == ToolStatus::Found;
		}
	};

	struct DiscoveryOptions {
		str cmakePath;
		str ninjaPath;
		str compilerPath;

		str sdkRoot;

		Vector<str> extraSearchDirectories{GetMemoryResource(MemTag::Editor)};

		bool probeVersions = true;
	};

	SHALLTEAR_EDITOR_API ToolchainInfo Discover(const DiscoveryOptions& options);
	SHALLTEAR_EDITOR_API ToolchainInfo Discover();

	SHALLTEAR_EDITOR_API str Describe(const ToolchainInfo& info);

	SHALLTEAR_EDITOR_API bool ReadSDKIdentity(const str& sdkRoot, ToolchainInfo& out);

	// must match what BuildInfo::GetEngineHash() hashes
	SHALLTEAR_EDITOR_API str GetEngineIdentity();

} // namespace Shalltear::CppToolchain
