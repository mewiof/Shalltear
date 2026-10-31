#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::UI {

	enum class AtlasFormat : u8 {
		R8 = 0,
		RGBA8 // premultiplied, UI encoded space
	};

	[[nodiscard]] inline u32 AtlasBytesPerTexel(AtlasFormat format) {
		return format == AtlasFormat::RGBA8 ? 4u : 1u;
	}

	struct AtlasPage {
		RHI::TextureHandle texture;
		Vec2I size{};
		AtlasFormat format = AtlasFormat::R8;
		u32 entries = 0;
		bool full = false;
	};

	class SHALLTEAR_CORE_API IAtlasBackend {
	public:
		virtual ~IAtlasBackend() = default;

		virtual RHI::TextureHandle CreateAtlas(Vec2I size, AtlasFormat format) = 0;

		// tightly packed (no row padding)
		virtual void UpdateAtlas(RHI::TextureHandle atlas, RectI region, std::span<const u8> pixels) = 0;
	};

} // namespace Shalltear::UI
