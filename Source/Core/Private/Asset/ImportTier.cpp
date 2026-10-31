#include "Shalltear/Core/Asset/ImportTier.h"

#include "ImportTierInternal.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"

namespace Shalltear {

	struct ImportTierBuilder {
		static bool ImportShader(ShaderAsset& shader, const Path& virtualOrPhysical) {
			return shader.ImportFromSource(virtualOrPhysical);
		}
		static u32 CompileVariant(ShaderAsset& shader, std::span<const str> wanted, u64 keywordHash) {
			return shader.CompileVariant(wanted, keywordHash);
		}
		static bool ImportTexture(TextureAsset& texture, const Path& sourcePath) {
			return texture.ImportFromSource(sourcePath);
		}
	};

	namespace ImportTier {

		void Install() {
			static const ImportTierTable table{&ImportTierBuilder::ImportShader, &ImportTierBuilder::CompileVariant,
											   &ImportTierBuilder::ImportTexture};
			ImportTierInternal::Set(&table);
		}

	} // namespace ImportTier

} // namespace Shalltear
