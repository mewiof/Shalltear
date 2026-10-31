#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UIAtlas.h"
#include "Shalltear/Core/UI/UITypes.h"

#include <span>
#include <string_view>

namespace Shalltear {
	class FontAsset;
}

namespace Shalltear::UI {

	struct ShapedGlyph {
		Rect rect;
		Rect uv;
	};

	struct CaretStop {
		u32 byteOffset = 0;
		f32 x = 0.0f;
		u32 line = 0;
	};

	struct ShapedRun {
		Vector<ShapedGlyph> glyphs{GetMemoryResource(MemTag::UI)};
		Vector<ShapedGlyph> outlineGlyphs{GetMemoryResource(MemTag::UI)};
		Vector<CaretStop> caretStops{GetMemoryResource(MemTag::UI)};

		Vec2 extent{};
		f32 lineHeight = 0.0f;
		bool multiline = false;
		bool truncated = false;
		RHI::TextureHandle atlas;

		SHALLTEAR_CORE_API CaretStop StopForByte(u32 byteOffset) const;
		SHALLTEAR_CORE_API f32 CaretXForByte(u32 byteOffset) const;
		SHALLTEAR_CORE_API u32 ByteForCaret(f32 x, u32 line) const;
		SHALLTEAR_CORE_API u32 LineCount() const;
	};

	// physical px
	struct TextQuery {
		Handle<FontAsset> font;
		f32 sizePx = 16.0f;
		std::string_view utf8;
		f32 wrapWidth = 0.0f;
		TextOverflow overflow = TextOverflow::Wrap;
		f32 outlineWidth = 0.0f;
		f32 letterSpacing = 0.0f;
	};

	struct TextStats {
		u64 walks = 0;
		u64 glyphsResolved = 0;
		u64 runsBuilt = 0;
	};

	class SHALLTEAR_CORE_API IFontSystem {
	public:
		virtual ~IFontSystem() = default;

		virtual bool ShapeText(const TextQuery& query, ShapedRun& out) = 0;

		// extent only, must not build a run
		virtual Vec2 MeasureText(const TextQuery& query) = 0;

		virtual f32 GetLineHeight(Handle<FontAsset> font, f32 sizePx) = 0;

		virtual bool GetAtlasPage(AtlasPage& out) const {
			(void)out;
			return false;
		}

		virtual u32 GetAtlasGeneration() const { return 0; }

		virtual TextStats GetTextStats() const { return {}; }
		virtual void ResetTextStats() {}
	};

} // namespace Shalltear::UI
