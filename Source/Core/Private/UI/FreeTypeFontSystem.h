#pragma once

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

		void ReclaimIfFull();
	private:
		struct FontFace {
			FT_Face face = nullptr;
			HashMap<u32, u32> charIndices{GetMemoryResource(MemTag::UI)};
			HashMap<u64, i32> kernings{GetMemoryResource(MemTag::UI)};
		};

		struct GlyphKey {
			u64 faceKey = 0;
			u32 codepoint = 0;
			u16 sizePx = 0;
			u16 strokePx = 0;
			bool operator==(const GlyphKey&) const = default;
		};
		struct GlyphKeyHash {
			size_t operator()(const GlyphKey& key) const {
				u64 hash = key.faceKey * 0x9E3779B97F4A7C15ull;
				hash ^= (static_cast<u64>(key.codepoint) << 32) | (static_cast<u64>(key.sizePx) << 16) | key.strokePx;
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
			bool inAtlas = false;
		};

		FontFace* GetOrCreateFace(Handle<FontAsset> font);
		bool EnsureSize(FontFace& face, u16 sizePx);
		GlyphEntry* GetOrCreateGlyph(FontFace& face, u64 faceKey, u16 sizePx, u32 codepoint, u16 strokePx);
		bool RasterizeAndPack(FontFace& face, u32 codepoint, u16 strokePx, GlyphEntry& out);
		bool PackAlloc(i32 width, i32 height, i32& outX, i32& outY);
	private:
		CreateInfo _config;
		FT_Library _library = nullptr;
		FT_Stroker _stroker = nullptr;

		HashMap<u64, FontFace> _faces{GetMemoryResource(MemTag::UI)};
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
		};
		Vector<Pending> _scratchLine{GetMemoryResource(MemTag::UI)};
		ShapedRun _measureScratch;
		TextStats _textStats;

		bool Shape(const TextQuery& query, ShapedRun& out, bool emit);
		Vector<u8> _scratchPixels{GetMemoryResource(MemTag::UI)};
	};

} // namespace Shalltear::UI
