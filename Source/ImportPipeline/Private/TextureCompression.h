#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/ImportPipeline/ImportPipelineAPI.h"

#include "Shalltear/Core/Graphics/TextureCodecs.h"

#include <span>

namespace Shalltear {

	void RegisterTextureCodecs(const void* owner);
	void ShutdownTextureCodecs();

	namespace TextureCompressionOps {

		// out = tightly packed RGBA8 lvls, mip order
		SHALLTEAR_IMPORTPIPELINE_API void BuildChain(std::span<const u8> rgba8, Vec2I size, u32 mipLevels, TexelMeaning meaning,
													 Vector<u8>& out);

	} // namespace TextureCompressionOps

} // namespace Shalltear
