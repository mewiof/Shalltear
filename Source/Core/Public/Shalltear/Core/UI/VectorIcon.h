#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UIAtlas.h"
#include "Shalltear/Core/UI/UITypes.h"

namespace Shalltear {
	class VectorAsset;
}

namespace Shalltear::UI {

	inline constexpr u32 MAX_ICON_SLOTS = 4;

	inline constexpr f32 MAX_ICON_PX = 512.0f;

	struct IconPalette {
		Color slots[MAX_ICON_SLOTS]{};
		u8 overrideMask = 0;

		IconPalette& Set(u32 slot, Color color) {
			if (slot < MAX_ICON_SLOTS) {
				slots[slot] = color;
				overrideMask |= static_cast<u8>(1u << slot);
			}
			return *this;
		}

		[[nodiscard]] bool Overrides(u32 slot) const {
			return slot < MAX_ICON_SLOTS && (overrideMask & (1u << slot)) != 0;
		}

		[[nodiscard]] static IconPalette Tint(Color color) {
			IconPalette palette;
			palette.Set(0, color);
			return palette;
		}

		bool operator==(const IconPalette& other) const {
			if (overrideMask != other.overrideMask) {
				return false;
			}
			for (u32 slot = 0; slot < MAX_ICON_SLOTS; ++slot) {
				if (Overrides(slot) && slots[slot] != other.slots[slot]) {
					return false;
				}
			}
			return true;
		}
		bool operator!=(const IconPalette& other) const { return !(*this == other); }
	};

	// physical px
	struct IconQuery {
		Handle<VectorAsset> asset;
		f32 sizePx = 16.0f;
		IconPalette palette;
	};

	struct RasterizedIcon {
		RHI::TextureHandle atlas;
		Rect uv{};
		Vec2 sizePx{};
	};

	class SHALLTEAR_CORE_API IVectorIconSystem {
	public:
		virtual ~IVectorIconSystem() = default;

		virtual bool GetIcon(const IconQuery& query, RasterizedIcon& out) = 0;

		// never rasterizes
		virtual Vec2 MeasureIcon(Handle<VectorAsset> asset, f32 sizePx) = 0;

		// frame owner only
		virtual void EndFrame() = 0;

		struct Stats {
			u32 entries = 0;
			u32 rasterizations = 0;
			u32 compactions = 0;
			u32 atlasBytes = 0;
		};
		[[nodiscard]] virtual Stats GetStats() const = 0;

		virtual bool GetAtlasPage(AtlasPage& out) const {
			(void)out;
			return false;
		}
	};

} // namespace Shalltear::UI
