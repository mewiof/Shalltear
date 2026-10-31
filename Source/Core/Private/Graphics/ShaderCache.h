#pragma once

#include "Graphics/ShaderCompiler.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class ShaderCache {
	public:
		static u64 ComputeEntryKey(
			u64 bufferHash,
			u64 assetHash,
			std::string_view assetDirectory,
			std::span<const str> defines,
			ShaderStageKind stage,
			ShaderCompileMode mode);

		static bool TryLoad(u64 entryKey, Vector<u8>& outSpirv, Vector<ShaderIncludeDependency>& outIncludes);

		static void Store(u64 entryKey, std::span<const ShaderIncludeDependency> includes, std::span<const u8> spirv);

		static Path GetCacheDirectory();
	};

} // namespace Shalltear
