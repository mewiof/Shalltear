#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/EnumFlags.h"
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

	enum class TextDecoration : u8 {
		None = 0,
		Underline = 1 << 0,
		Strikethrough = 1 << 1,
		Mark = 1 << 2,
	};
	SLT_ENUM_CLASS_FLAGS(TextDecoration)

	enum class TextCase : u8 {
		Normal = 0,
		Upper,
		Lower,
		SmallCaps
	};

	enum class TextScript : u8 {
		Baseline = 0,
		Sub,
		Super
	};

	// paint for glyphs from glyph/outlineGlyph to next span
	struct ShapedSpan {
		u32 glyph = 0;
		u32 outlineGlyph = 0;
		u16 paint = 0;
	};

	struct ShapedDecoration {
		Rect rect;
		u16 paint = 0;
		TextDecoration kind = TextDecoration::Underline;
	};

	struct ShapedRun {
		Vector<ShapedGlyph> glyphs{GetMemoryResource(MemTag::UI)};
		Vector<ShapedGlyph> outlineGlyphs{GetMemoryResource(MemTag::UI)};
		Vector<CaretStop> caretStops{GetMemoryResource(MemTag::UI)};
		Vector<ShapedSpan> spans{GetMemoryResource(MemTag::UI)};
		Vector<ShapedDecoration> decorations{GetMemoryResource(MemTag::UI)};

		Vec2 extent{};
		f32 lineHeight = 0.0f;
		// space above font's line box in each line (negative if tighter)
		f32 halfLeading = 0.0f;
		bool multiline = false;
		bool truncated = false;
		RHI::TextureHandle atlas;

		SHALLTEAR_CORE_API CaretStop StopForByte(u32 byteOffset) const;
		SHALLTEAR_CORE_API f32 CaretXForByte(u32 byteOffset) const;
		SHALLTEAR_CORE_API u32 ByteForCaret(f32 x, u32 line) const;
		SHALLTEAR_CORE_API u32 LineCount() const;
	};

	// look of TextQuery::utf8[begin, end). physical px, baselineShift +up
	struct TextRun {
		u32 begin = 0;
		u32 end = 0;
		Handle<FontAsset> font;
		f32 sizePx = 16.0f;
		f32 letterSpacing = 0.0f;
		f32 lineHeight = 0.0f;
		f32 lineHeightScale = 0.0f;
		f32 baselineShift = 0.0f;
		f32 advance = 0.0f;
		u16 fontWeight = 400;
		u16 paint = 0;
		FontStyle fontStyle = FontStyle::Normal;
		TextScript script = TextScript::Baseline;
		TextCase textCase = TextCase::Normal;
		TextDecoration decoration = TextDecoration::None;
		bool noBreak = false;
	};

	// physical px
	struct TextQuery {
		Handle<FontAsset> font;
		f32 sizePx = 16.0f;
		std::string_view utf8;
		f32 wrapWidth = 0.0f;
		TextOverflow overflow = TextOverflow::Wrap;
		// aligns each line to the widest one (ignore trailing spaces)
		Align align = Align::Start;
		// trailing spaces count before \n/end (a soft wrap's still hang)
		bool editable = false;
		f32 outlineWidth = 0.0f;
		f32 letterSpacing = 0.0f;
		f32 lineHeight = 0.0f;
		u16 fontWeight = 400;
		FontStyle fontStyle = FontStyle::Normal;
		// in order, should cover all of utf8 (fields above used if empty)
		std::span<const TextRun> runs;
	};

	struct FontFaceInfo {
		u16 weightMin = 400;
		u16 weightMax = 400;
		bool italic = false;
		bool variableItalic = false;
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

		virtual bool DescribeFont(Handle<FontAsset> font, FontFaceInfo& out) {
			(void)font;
			(void)out;
			return false;
		}

		virtual bool GetAtlasPage(AtlasPage& out) const {
			(void)out;
			return false;
		}

		virtual u32 GetAtlasGeneration() const { return 0; }

		// bumped when a font changes, re-measure cached text
		virtual u32 GetMetricsGeneration() const { return 0; }

		virtual TextStats GetTextStats() const { return {}; }
		virtual void ResetTextStats() {}
	};

} // namespace Shalltear::UI
