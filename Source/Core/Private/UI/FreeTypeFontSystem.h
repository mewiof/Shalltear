#pragma once

#include "UI/SystemFontFallback.h"
#include "UI/UIInternal.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_STROKER_H

namespace Shalltear::UI {

	class FreeTypeFontSystem final : public IFontSystem {
	public:
		struct CreateInfo {
			IAtlasBackend* backend = nullptr;
			Vec2I atlasSize = {1024, 1024};
			i32 glyphPadding = 2;
		};

		explicit FreeTypeFontSystem(const CreateInfo& createInfo);
		~FreeTypeFontSystem() override;
		FreeTypeFontSystem(const FreeTypeFontSystem&) = delete;
		FreeTypeFontSystem& operator=(const FreeTypeFontSystem&) = delete;

		bool ShapeText(const TextQuery& query, ShapedRun& out) override;
		Vec2 MeasureText(const TextQuery& query) override;
		f32 GetLineHeight(Handle<FontAsset> font, f32 sizePx) override;
		bool DescribeFont(Handle<FontAsset> font, FontFaceInfo& out) override;

		TextStats GetTextStats() const override { return _textStats; }
		void ResetTextStats() override { _textStats = {}; }

		bool GetAtlasPage(AtlasPage& out) const override {
			if (!_atlas.IsValid()) {
				return false;
			}
			out.texture = _atlas;
			out.size = _atlasSize;
			out.format = AtlasFormat::R8;
			out.entries = static_cast<u32>(_glyphs.size());
			out.full = _pageFull;
			return true;
		}
		u32 GetAtlasGeneration() const override { return _generation; }
		u32 GetMetricsGeneration() const override { return _metricsGeneration; }

		// frame start only, may clear page
		void SweepFonts();
		void ReclaimIfFull();
	private:
		struct FontFace {
			FT_Face face = nullptr;
			Handle<FontAsset> asset;
			u32 version = 0;
			u32 slot = 0;
			u16 weight = 400;
			bool italic = false;
			bool system = false;
			struct Axis {
				i32 index = -1;
				f32 min = 0.0f;
				f32 max = 0.0f;
			};
			Axis wght, ital, slnt, opsz;
			Vector<FT_Fixed> defaultCoords{GetMemoryResource(MemTag::UI)};
			Vector<FT_Fixed> appliedCoords{GetMemoryResource(MemTag::UI)};
			HashMap<u32, u32> charIndices{GetMemoryResource(MemTag::UI)};
			HashMap<u64, i32> kernings{GetMemoryResource(MemTag::UI)};
		};

		struct StyledFace {
			FontFace* face = nullptr;
			f32 weight = 400.0f;
			f32 ital = 0.0f;
			f32 slnt = 0.0f;
			bool embolden = false;
			bool oblique = false;
		};

		struct GlyphKey {
			u32 faceSlot = 0;
			u32 codepoint = 0;
			u16 sizePx = 0;
			u16 strokePx = 0;
			u16 weight = 400;
			FontStyle style = FontStyle::Normal;
			bool operator==(const GlyphKey&) const = default;
		};
		struct GlyphKeyHash {
			size_t operator()(const GlyphKey& key) const {
				u64 hash = ((static_cast<u64>(key.faceSlot) << 32) | key.codepoint) * 0x9E3779B97F4A7C15ull;
				hash ^= (static_cast<u64>(key.weight) << 33) | (static_cast<u64>(key.style) << 32) |
						(static_cast<u64>(key.sizePx) << 16) | key.strokePx;
				return static_cast<size_t>(hash * 0x100000001B3ull);
			}
		};

		struct GlyphEntry {
			i32 advance = 0;
			i32 bearingX = 0;
			i32 bearingY = 0;
			i32 width = 0;
			i32 height = 0;
			Rect uv{};
			u32 glyphIndex = 0;
			// face drawing glyph (a fallback when key's face lacks it)
			u32 faceSlot = 0;
			bool inAtlas = false;
		};

		FontFace* GetOrCreateFace(Handle<FontAsset> font);
		FontFace* GetOrCreateSystemFace(const SystemFontFile& file);
		bool OpenFace(FontFace& face, const u8* data, size_t size, u32 faceIndex);
		void CloseFace(FontFace& face);
		bool IsCurrent(const FontFace& face) const;
		FontFace* FaceForSlot(u32 slot);
		bool EnsureSize(FontFace& face, u16 sizePx);
		void ReadAxes(FontFace& face);
		StyledFace ResolveStyle(FontFace& family, u16 weight, FontStyle style);
		bool ApplyInstance(const StyledFace& styled, u16 sizePx);
		u32 GlyphIndexOf(FontFace& face, u32 codepoint);
		StyledFace ResolveOwner(FontFace& family, u16 weight, FontStyle style, u32 codepoint);
		bool FindInFallbacks(const FontAsset& font, u16 weight, FontStyle style, u32 codepoint, StyledFace& out);
		f32 KerningBetween(FontFace& primary, u16 sizePx, const GlyphEntry* previous, const GlyphEntry* current);
		GlyphEntry* GetOrCreateGlyph(FontFace& family, u16 weight, FontStyle style, u16 sizePx, u32 codepoint, u16 strokePx);
		bool RasterizeAndPack(const StyledFace& styled, u16 sizePx, u32 glyphIndex, u16 strokePx, GlyphEntry& out);
		bool PackAlloc(i32 width, i32 height, i32& outX, i32& outY);
		void ClearPage();

		struct RunState {
			FontFace* family = nullptr;
			FontFace* styled = nullptr;
			u16 sizePx = 0;
			u16 smallSizePx = 0;
			u16 weight = 400;
			FontStyle style = FontStyle::Normal;
			f32 ascender = 0.0f;
			f32 descender = 0.0f;
			f32 lineHeight = 0.0f;
			f32 halfLeading = 0.0f;
			f32 letterSpacing = 0.0f;
			f32 baselineShift = 0.0f;
			f32 advance = 0.0f;
			f32 underlineOffset = 0.0f;
			f32 underlineThickness = 0.0f;
			f32 strikeOffset = 0.0f;
			f32 strikeThickness = 0.0f;
			u16 paint = 0;
			TextCase textCase = TextCase::Normal;
			TextDecoration decoration = TextDecoration::None;
			bool noBreak = false;
		};
		bool PrepareRun(const TextQuery& query, const TextRun* desc, RunState& out);
	private:
		CreateInfo _config;
		FT_Library _library = nullptr;
		FT_Stroker _stroker = nullptr;

		SystemFontFallback _systemFallback;
		HashMap<u64, FontFace> _faces{GetMemoryResource(MemTag::UI)};
		HashMap<const SystemFontFile*, FontFace> _systemFaces{GetMemoryResource(MemTag::UI)};
		HashMap<u32, FontFace*> _faceSlots{GetMemoryResource(MemTag::UI)};
		u32 _nextSlot = 1;
		bool _fontsChanged = false;
		u32 _metricsGeneration = 0;
		Vector<Guid> _scratchVisited{GetMemoryResource(MemTag::UI)};
		Vector<FontFace*> _scratchCandidates{GetMemoryResource(MemTag::UI)};
		Vector<FT_Fixed> _scratchCoords{GetMemoryResource(MemTag::UI)};

		HashMap<GlyphKey, GlyphEntry, GlyphKeyHash> _glyphs{GetMemoryResource(MemTag::UI)};

		RHI::TextureHandle _atlas;
		Vec2I _atlasSize{};
		i32 _shelfX = 1;
		i32 _shelfY = 1;
		i32 _shelfHeight = 0;
		bool _pageFull = false;
		u32 _generation = 0;
		bool _reclaimedLast = false;
		bool _overflowLogged = false;
		bool _oversizeLogged = false;

		struct Pending {
			u32 codepoint = 0;
			u32 byteOffset = 0;
			GlyphEntry* entry = nullptr;
			u32 byteEnd = 0;
			u32 drawn = 0;
			u16 run = 0;
			u16 sizePx = 0;
		};
		Vector<Pending> _scratchLine{GetMemoryResource(MemTag::UI)};
		Vector<RunState> _scratchRuns{GetMemoryResource(MemTag::UI)};
		struct LineSpan {
			u32 glyphs = 0;
			u32 outlineGlyphs = 0;
			u32 caretStops = 0;
			u32 decorations = 0;
			f32 width = 0.0f;
		};
		Vector<LineSpan> _scratchSpans{GetMemoryResource(MemTag::UI)};
		ShapedRun _measureScratch;
		TextStats _textStats;

		bool Shape(const TextQuery& query, ShapedRun& out, bool emit);
		template <bool Styled>
		bool ShapeRuns(const TextQuery& query, ShapedRun& out, bool emit);
		Vector<u8> _scratchPixels{GetMemoryResource(MemTag::UI)};
	};

} // namespace Shalltear::UI
