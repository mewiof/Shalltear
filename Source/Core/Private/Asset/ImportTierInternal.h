#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class SHALLTEAR_CORE_API Path;
	class ShaderAsset;
	class TextureAsset;

	struct ImportTierTable {
		bool (*importShader)(ShaderAsset& shader, const Path& virtualOrPhysical) = nullptr;
		// keyword set must be sorted + unique
		u32 (*compileVariant)(ShaderAsset& shader, std::span<const str> wanted, u64 keywordHash) = nullptr;
		bool (*importTexture)(TextureAsset& texture, const Path& sourcePath) = nullptr;
	};

	namespace ImportTierInternal {

		const ImportTierTable* Get();
		void Set(const ImportTierTable* table);

	} // namespace ImportTierInternal

} // namespace Shalltear
