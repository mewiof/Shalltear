#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/Style.h"

#include <string_view>

namespace Shalltear::UI {

	struct TextMetrics;

	enum class RichUnit : u8 {
		None = 0,
		Pixels,
		Em,
		Percent,
		Delta
	};

	struct RichAmount {
		f32 value = 0.0f;
		RichUnit unit = RichUnit::None;

		bool operator==(const RichAmount&) const = default;
	};

	// colors in SRGB
	struct RichPaint {
		Color color{};
		f32 alpha = 1.0f;
		Color mark{};
		bool hasColor = false;
		bool hasAlpha = false;

		bool operator==(const RichPaint&) const = default;
	};

	struct RichSpan {
		u32 begin = 0;
		u32 end = 0;
		StyleValue<Handle<FontAsset>> font;
		RichAmount size;
		RichAmount letterSpacing;
		RichAmount lineHeight;
		RichAmount baselineShift;
		RichAmount advance;
		u16 fontWeight = 0;
		u16 paint = 0;
		bool italic = false;
		TextScript script = TextScript::Baseline;
		TextCase textCase = TextCase::Normal;
		TextDecoration decoration = TextDecoration::None;
		bool noBreak = false;
	};

	struct RichText {
		String display{GetMemoryResource(MemTag::UI)};
		Vector<RichSpan> spans{GetMemoryResource(MemTag::UI)};
		Vector<RichPaint> paints{GetMemoryResource(MemTag::UI)};
	};

	void ParseRichText(std::string_view source, const StyleVars& vars, RichText& out);

	[[nodiscard]] bool IsStyled(const RichText& rich);

	void ResolveRichRuns(const RichText& rich, const TextMetrics& base, f32 scale, Vector<TextRun>& out);

	[[nodiscard]] Color PaintColor(const RichPaint& paint, Color base);

	// names of <font> tags, as views into source
	void CollectRichFonts(std::string_view source, Vector<std::string_view>& out);

} // namespace Shalltear::UI
