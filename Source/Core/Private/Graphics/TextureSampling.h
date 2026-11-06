#pragma once

#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Math/MathUtil.h"

namespace Shalltear::GraphicsInternal {

	inline u8 ResolveTextureAnisotropy(const TextureAsset::Settings& settings, bool volume, TextureAnisotropy mode, i32 maximum) {
		if (settings.filter == TextureFilter::Point || volume || mode == TextureAnisotropy::Disabled) {
			return 1;
		}
		const u8 cap = static_cast<u8>(Math::Clamp(maximum, 1, 16));
		if (mode == TextureAnisotropy::ForcedOn) {
			return settings.anisoLevel == 0 ? u8{1} : cap;
		}
		return Math::Clamp<u8>(settings.anisoLevel, 1, cap);
	}

	inline RHI::SamplerDesc TextureSamplerDesc(const TextureAsset::Settings& settings, bool volume, TextureAnisotropy mode, i32 maximum) {
		RHI::SamplerDesc desc;
		desc.filter = settings.filter == TextureFilter::Point ? RHI::FilterMode::Nearest : RHI::FilterMode::Linear;
		desc.mipFilter = settings.filter == TextureFilter::Trilinear ? RHI::FilterMode::Linear : RHI::FilterMode::Nearest;
		desc.addressMode = settings.addressMode;
		desc.maxAnisotropy = ResolveTextureAnisotropy(settings, volume, mode, maximum);
		desc.debugName = "TextureSampler";
		return desc;
	}

} // namespace Shalltear::GraphicsInternal
