#include "UI/FreeTypeFontSystem.h"

#include "Shalltear/Core/UI/FontAsset.h"

#include FT_TRUETYPE_TABLES_H

#include <algorithm>
#include <cmath>

namespace Shalltear::UI {

	namespace {

		bool IsBreakCodepoint(u32 codepoint) {
			return codepoint == ' ' || codepoint == '\t' || codepoint == '-' || codepoint == '/';
		}

		bool IsSpaceCodepoint(u32 codepoint) {
			return codepoint == ' ' || codepoint == '\t';
		}

		bool IsControlCodepoint(u32 codepoint) {
			return codepoint < 0x20 || (codepoint >= 0x7F && codepoint < 0xA0);
		}

		constexpr u32 ELLIPSIS_CODEPOINT = 0x2026;

	} // anonymous namespace

	FreeTypeFontSystem::FreeTypeFontSystem(const CreateInfo& createInfo)
		: _config(createInfo) {
		SLT_CORE_ASSERT_MSG(_config.backend != nullptr, "FreeTypeFontSystem requires an atlas backend");
		if (FT_Init_FreeType(&_library) != 0) {
			SLT_LOG_ERROR("UI", "FT_Init_FreeType failed. Text rendering unavailable");
			_library = nullptr;
			return;
		}
		if (FT_Stroker_New(_library, &_stroker) != 0) {
			_stroker = nullptr;
		}

		_atlasSize = {std::max(64, _config.atlasSize.x), std::max(64, _config.atlasSize.y)};
		_atlas = _config.backend->CreateAtlas(_atlasSize, AtlasFormat::R8);

		_scratchLine.reserve(256);
	}

	FreeTypeFontSystem::~FreeTypeFontSystem() {
		for (auto& [key, face] : _faces) {
			CloseFace(face);
		}
		_faces.clear();
		for (auto& [file, face] : _systemFaces) {
			CloseFace(face);
		}
		_systemFaces.clear();
		if (_stroker) {
			FT_Stroker_Done(_stroker);
		}
		if (_library) {
			FT_Done_FreeType(_library);
		}
	}

	bool FreeTypeFontSystem::OpenFace(FontFace& face, const u8* data, size_t size, u32 faceIndex) {
		FT_Face ftFace = nullptr;
		if (FT_New_Memory_Face(_library, data, static_cast<FT_Long>(size), static_cast<FT_Long>(faceIndex), &ftFace) != 0) {
			return false;
		}
		face.face = ftFace;
		face.slot = _nextSlot++;
		const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(ftFace, FT_SFNT_OS2));
		face.weight = os2 && os2->usWeightClass >= 100 && os2->usWeightClass <= 999
						  ? os2->usWeightClass
						  : static_cast<u16>((ftFace->style_flags & FT_STYLE_FLAG_BOLD) != 0 ? 700 : 400);
		face.italic = (ftFace->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
		_faceSlots[face.slot] = &face;
		return true;
	}

	void FreeTypeFontSystem::CloseFace(FontFace& face) {
		if (face.face) {
			FT_Done_Face(face.face);
			face.face = nullptr;
		}
		_faceSlots.erase(face.slot);
		face.slot = 0;
		face.charIndices.clear();
		face.kernings.clear();
	}

	bool FreeTypeFontSystem::IsCurrent(const FontFace& face) const {
		if (face.system) {
			return true;
		}
		const FontAsset* asset = face.asset.Get();
		return asset && asset->version == face.version;
	}

	FreeTypeFontSystem::FontFace* FreeTypeFontSystem::FaceForSlot(u32 slot) {
		auto it = _faceSlots.find(slot);
		return it != _faceSlots.end() && IsCurrent(*it->second) ? it->second : nullptr;
	}

	FreeTypeFontSystem::FontFace* FreeTypeFontSystem::GetOrCreateFace(Handle<FontAsset> font) {
		if (!_library) {
			return nullptr;
		}
		FontAsset* asset = font.Get();
		u64 key = font.GetInstanceId();
		auto it = _faces.find(key);
		if (it != _faces.end()) {
			FontFace& face = it->second;
			if (!asset) {
				CloseFace(face);
				_faces.erase(it);
				return nullptr;
			}
			if (face.version == asset->version) {
				return &face;
			}
			CloseFace(face);
			_faces.erase(it);
			_fontsChanged = true;
		}
		if (!asset || asset->data.empty()) {
			return nullptr;
		}
		auto [inserted, ok] = _faces.emplace(key, FontFace{});
		FontFace& face = inserted->second;
		if (!OpenFace(face, asset->data.data(), asset->data.size(), 0)) {
			SLT_LOG_ERROR("UI", "FontAsset '{}' rejected by FreeType", asset->virtualPath);
			_faces.erase(inserted);
			return nullptr;
		}
		face.asset = font;
		face.version = asset->version;
		return &face;
	}

	FreeTypeFontSystem::FontFace* FreeTypeFontSystem::GetOrCreateSystemFace(const SystemFontFile& file) {
		auto it = _systemFaces.find(&file);
		if (it != _systemFaces.end()) {
			return it->second.face ? &it->second : nullptr;
		}
		auto [inserted, ok] = _systemFaces.emplace(&file, FontFace{});
		FontFace& face = inserted->second;
		face.system = true;
		return OpenFace(face, file.data, file.size, file.faceIndex) ? &face : nullptr;
	}

	void FreeTypeFontSystem::SweepFonts() {
		bool changed = _fontsChanged;
		for (auto it = _faces.begin(); it != _faces.end();) {
			const FontAsset* asset = it->second.asset.Get();
			if (!asset) {
				CloseFace(it->second);
				it = _faces.erase(it);
				continue;
			}
			changed |= asset->version != it->second.version;
			++it;
		}
		_fontsChanged = false;
		if (changed) {
			ClearPage();
			++_metricsGeneration;
		}
	}

	u32 FreeTypeFontSystem::GlyphIndexOf(FontFace& face, u32 codepoint) {
		auto it = face.charIndices.find(codepoint);
		if (it != face.charIndices.end()) {
			return it->second;
		}
		const u32 glyphIndex = FT_Get_Char_Index(face.face, codepoint);
		face.charIndices.emplace(codepoint, glyphIndex);
		return glyphIndex;
	}

	FreeTypeFontSystem::FontFace* FreeTypeFontSystem::FindInFallbacks(const FontAsset& font, u32 codepoint) {
		for (size_t i = 0; i < font.fallbacks.size(); ++i) {
			const Guid& guid = font.fallbacks[i];
			if (std::find(_scratchVisited.begin(), _scratchVisited.end(), guid) != _scratchVisited.end()) {
				continue;
			}
			_scratchVisited.push_back(guid);
			FontAsset* next = font.GetFallback(i);
			if (!next) {
				continue;
			}
			FontFace* face = GetOrCreateFace(Handle<FontAsset>(next));
			if (face && GlyphIndexOf(*face, codepoint) != 0) {
				return face;
			}
			if (FontFace* nested = FindInFallbacks(*next, codepoint)) {
				return nested;
			}
		}
		return nullptr;
	}

	FreeTypeFontSystem::FontFace& FreeTypeFontSystem::ResolveOwner(FontFace& primary, u32 codepoint) {
		if (IsControlCodepoint(codepoint) || GlyphIndexOf(primary, codepoint) != 0) {
			return primary;
		}
		if (const FontAsset* font = primary.asset.Get(); font && !font->fallbacks.empty()) {
			_scratchVisited.clear();
			_scratchVisited.push_back(font->assetId);
			if (FontFace* found = FindInFallbacks(*font, codepoint)) {
				return *found;
			}
		}
		if (const SystemFontFile* file = _systemFallback.Find(codepoint, primary.weight, primary.italic)) {
			FontFace* system = GetOrCreateSystemFace(*file);
			if (system && GlyphIndexOf(*system, codepoint) != 0) {
				return *system;
			}
		}
		return primary;
	}

	f32 FreeTypeFontSystem::KerningBetween(FontFace& primary, const GlyphEntry* previous, const GlyphEntry* current) {
		if (!previous || !current || previous->faceSlot != current->faceSlot || previous->glyphIndex == 0 ||
			current->glyphIndex == 0) {
			return 0.0f;
		}
		FontFace* face = current->faceSlot == primary.slot ? &primary : FaceForSlot(current->faceSlot);
		if (!face || !FT_HAS_KERNING(face->face)) {
			return 0.0f;
		}
		const u64 pairKey = (static_cast<u64>(previous->glyphIndex) << 32) | current->glyphIndex;
		auto it = face->kernings.find(pairKey);
		if (it == face->kernings.end()) {
			FT_Vector kerning{};
			FT_Get_Kerning(face->face, previous->glyphIndex, current->glyphIndex, FT_KERNING_DEFAULT, &kerning);
			it = face->kernings.emplace(pairKey, static_cast<i32>(kerning.x)).first;
		}
		return static_cast<f32>(it->second) / 64.0f;
	}

	bool FreeTypeFontSystem::EnsureSize(FontFace& face, u16 sizePx) {
		if (!face.face) {
			return false;
		}
		if (face.face->size && face.face->size->metrics.x_ppem == sizePx && face.face->size->metrics.y_ppem == sizePx) {
			return true;
		}
		return FT_Set_Pixel_Sizes(face.face, 0, sizePx) == 0;
	}

	bool FreeTypeFontSystem::PackAlloc(i32 width, i32 height, i32& outX, i32& outY) {
		if (!_atlas.IsValid() || _pageFull) {
			return false;
		}
		i32 padded = _config.glyphPadding;
		i32 w = width + padded * 2;
		i32 h = height + padded * 2;
		if (w > _atlasSize.x || h > _atlasSize.y) {
			if (!_oversizeLogged) {
				SLT_LOG_WARN("UI", "a {}x{} glyph is larger than the glyph atlas ({}x{}). Won't render", width, height,
							 _atlasSize.x, _atlasSize.y);
				_oversizeLogged = true;
			}
			return false;
		}
		if (_shelfX + w > _atlasSize.x) {
			_shelfX = 1;
			_shelfY += _shelfHeight + 1;
			_shelfHeight = 0;
		}
		if (_shelfY + h > _atlasSize.y) {
			_pageFull = true;
			return false;
		}
		outX = _shelfX + padded;
		outY = _shelfY + padded;
		_shelfX += w;
		_shelfHeight = std::max(_shelfHeight, h);
		return true;
	}

	void FreeTypeFontSystem::ReclaimIfFull() {
		if (!_pageFull) {
			_reclaimedLast = false;
			return;
		}
		if (_reclaimedLast && !_overflowLogged) {
			SLT_LOG_ERROR("UI", "glyph atlas ({}x{}) can't hold one frame's text (extra glyphs won't render)", _atlasSize.x,
						  _atlasSize.y);
			_overflowLogged = true;
		}
		ClearPage();
		_reclaimedLast = true;
	}

	void FreeTypeFontSystem::ClearPage() {
		_glyphs.clear();
		_shelfX = 1;
		_shelfY = 1;
		_shelfHeight = 0;
		_pageFull = false;
		++_generation;
	}

	FreeTypeFontSystem::GlyphEntry* FreeTypeFontSystem::GetOrCreateGlyph(FontFace& primary, u16 sizePx, u32 codepoint, u16 strokePx) {
		GlyphKey key{primary.slot, codepoint, sizePx, strokePx};
		auto it = _glyphs.find(key);
		if (it != _glyphs.end()) {
			return &it->second;
		}
		FontFace& owner = ResolveOwner(primary, codepoint);
		u32 glyphIndex = GlyphIndexOf(owner, codepoint);
		if (glyphIndex == 0) {
			glyphIndex = GlyphIndexOf(owner, static_cast<u32>('?'));
		}
		GlyphEntry entry;
		entry.faceSlot = owner.slot;
		RasterizeAndPack(owner, sizePx, glyphIndex, strokePx, entry);
		auto [inserted, ok] = _glyphs.emplace(key, entry);
		return &inserted->second;
	}

	bool FreeTypeFontSystem::RasterizeAndPack(FontFace& face, u16 sizePx, u32 glyphIndex, u16 strokePx, GlyphEntry& out) {
		FT_Face ftFace = face.face;
		out.glyphIndex = glyphIndex;
		if (glyphIndex == 0 || !EnsureSize(face, sizePx)) {
			return false;
		}

		if (FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_DEFAULT) != 0) {
			return false;
		}
		FT_Glyph ftGlyph = nullptr;
		if (FT_Get_Glyph(ftFace->glyph, &ftGlyph) != 0) {
			return false;
		}
		if (strokePx > 0 && _stroker) {
			FT_Stroker_Set(_stroker, static_cast<FT_Fixed>(strokePx) * 64, FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);
			FT_Glyph_StrokeBorder(&ftGlyph, _stroker, 0, 1);
		}
		if (FT_Glyph_To_Bitmap(&ftGlyph, FT_RENDER_MODE_NORMAL, nullptr, 1) != 0) {
			FT_Done_Glyph(ftGlyph);
			return false;
		}

		FT_BitmapGlyph bitmapGlyph = reinterpret_cast<FT_BitmapGlyph>(ftGlyph);
		const FT_Bitmap& bitmap = bitmapGlyph->bitmap;
		out.width = static_cast<i32>(bitmap.width);
		out.height = static_cast<i32>(bitmap.rows);
		out.bearingX = bitmapGlyph->left;
		out.bearingY = bitmapGlyph->top;
		out.advance = static_cast<i32>(bitmapGlyph->root.advance.x >> 16);

		if (out.width > 0 && out.height > 0) {
			i32 atlasX = 0;
			i32 atlasY = 0;
			if (!PackAlloc(out.width, out.height, atlasX, atlasY)) {
				FT_Done_Glyph(ftGlyph);
				return false;
			}

			const i32 padded = _config.glyphPadding;
			const i32 slotWidth = out.width + padded * 2;
			const i32 slotHeight = out.height + padded * 2;
			_scratchPixels.assign(static_cast<size_t>(slotWidth) * slotHeight, 0);
			for (i32 y = 0; y < out.height; ++y) {
				const u8* source = bitmap.buffer + static_cast<size_t>(y) * bitmap.pitch;
				std::copy(source, source + out.width, _scratchPixels.data() + static_cast<size_t>(y + padded) * slotWidth + padded);
			}
			_config.backend->UpdateAtlas(_atlas, RectI{atlasX - padded, atlasY - padded, slotWidth, slotHeight},
										 {_scratchPixels.data(), _scratchPixels.size()});

			f32 invW = 1.0f / static_cast<f32>(_atlasSize.x);
			f32 invH = 1.0f / static_cast<f32>(_atlasSize.y);
			out.uv = {atlasX * invW, atlasY * invH,
					  out.width * invW, out.height * invH};
			out.inAtlas = true;
		}

		FT_Done_Glyph(ftGlyph);
		return true;
	}

	bool FreeTypeFontSystem::ShapeText(const TextQuery& query, ShapedRun& out) {
		return Shape(query, out, true);
	}

	bool FreeTypeFontSystem::Shape(const TextQuery& query, ShapedRun& out, bool emit) {
		++_textStats.walks;
		if (emit) {
			++_textStats.runsBuilt;
		}
		out.glyphs.clear();
		out.outlineGlyphs.clear();
		out.caretStops.clear();
		out.extent = {};
		out.lineHeight = 0.0f;
		out.multiline = false;
		out.atlas = _atlas;

		FontFace* face = GetOrCreateFace(query.font);
		if (!face) {
			return false;
		}
		u16 sizePx = static_cast<u16>(std::max(1, static_cast<i32>(std::lround(query.sizePx))));
		if (!EnsureSize(*face, sizePx)) {
			return false;
		}
		u16 strokePx = query.outlineWidth > 0.0f
						   ? static_cast<u16>(std::max(1, static_cast<i32>(std::lround(query.outlineWidth))))
						   : 0;

		FT_Face ftFace = face->face;
		f32 ascender = static_cast<f32>(ftFace->size->metrics.ascender) / 64.0f;
		f32 lineHeight = static_cast<f32>(ftFace->size->metrics.height) / 64.0f;
		if (lineHeight <= 0.0f) {
			lineHeight = ascender - static_cast<f32>(ftFace->size->metrics.descender) / 64.0f;
		}
		out.lineHeight = lineHeight;

		const f32 letterSpacing = query.letterSpacing;
		auto stepBefore = [&](const GlyphEntry* previous, const GlyphEntry* current, bool first) -> f32 {
			return (first ? 0.0f : letterSpacing) + KerningBetween(*face, previous, current);
		};

		f32 penY = 0.0f;
		f32 maxLineWidth = 0.0f;
		u32 lineCount = 0;
		const bool ellipsize = query.overflow == TextOverflow::Ellipsis;
		const bool wrap = query.overflow == TextOverflow::Wrap && query.wrapWidth > 0.0f;
		const f32 share = query.align == Align::Center ? 0.5f : (query.align == Align::End ? 1.0f : 0.0f);
		const bool align = emit && share > 0.0f;
		bool truncated = false;

		_scratchLine.clear();
		_scratchSpans.clear();

		auto pushCaretStop = [&](u32 byteOffset, f32 x) {
			if (emit) {
				out.caretStops.push_back({byteOffset, x, lineCount});
			}
		};
		u32 lineStartByte = 0;

		auto flushLine = [&](bool startNewLine, bool softWrap) {
			f32 baseline = penY + ascender;
			f32 x = 0.0f;
			f32 inked = 0.0f;
			const GlyphEntry* previous = nullptr;
			bool first = true;
			if (align) {
				_scratchSpans.push_back({static_cast<u32>(out.glyphs.size()), static_cast<u32>(out.outlineGlyphs.size()),
										 static_cast<u32>(out.caretStops.size()), 0.0f});
			}
			pushCaretStop(lineStartByte, 0.0f);
			for (const Pending& pending : _scratchLine) {
				GlyphEntry* entry = pending.entry;
				if (!entry) {
					continue;
				}
				x += stepBefore(previous, entry, first);
				first = false;

				if (emit && strokePx > 0) {
					GlyphEntry* stroke = GetOrCreateGlyph(*face, sizePx, pending.codepoint, strokePx);
					if (stroke && stroke->inAtlas) {
						ShapedGlyph glyph;
						glyph.rect = {Snap(x + stroke->bearingX), Snap(baseline - stroke->bearingY),
									  static_cast<f32>(stroke->width), static_cast<f32>(stroke->height)};
						glyph.uv = stroke->uv;
						out.outlineGlyphs.push_back(glyph);
					}
				}
				if (emit && entry->inAtlas) {
					ShapedGlyph glyph;
					glyph.rect = {Snap(x + entry->bearingX), Snap(baseline - entry->bearingY),
								  static_cast<f32>(entry->width), static_cast<f32>(entry->height)};
					glyph.uv = entry->uv;
					out.glyphs.push_back(glyph);
				}

				x += static_cast<f32>(entry->advance);
				if (!IsSpaceCodepoint(pending.codepoint)) {
					inked = x;
				}
				pushCaretStop(pending.byteEnd, x);
				previous = entry;
			}
			if (align) {
				_scratchSpans.back().width = query.editable && !softWrap ? x : inked;
			}
			maxLineWidth = std::max(maxLineWidth, x);
			_scratchLine.clear();
			++lineCount;
			if (startNewLine) {
				penY += lineHeight;
			}
			return x;
		};

		const char* text = query.utf8.data();
		const u32 length = static_cast<u32>(query.utf8.size());
		u32 i = 0;
		f32 lineAdvance = 0.0f;
		u32 lastBreakCount = 0;
		bool pendingOverflow = false;
		const f32 budget = query.wrapWidth > 0.0f ? query.wrapWidth : INFINITY;
		u32 overflowByte = length;

		auto appendEllipsis = [&]() {
			GlyphEntry* mark = GetOrCreateGlyph(*face, sizePx, ELLIPSIS_CODEPOINT, 0);
			if (!mark) {
				return;
			}
			const f32 markAdvance = static_cast<f32>(mark->advance);
			f32 x = 0.0f;
			const GlyphEntry* previous = nullptr;
			bool first = true;
			size_t keep = 0;
			u32 cutByte = overflowByte;
			for (size_t n = 0; n < _scratchLine.size(); ++n) {
				GlyphEntry* entry = _scratchLine[n].entry;
				if (!entry) {
					keep = n + 1;
					continue;
				}
				f32 pen = x + stepBefore(previous, entry, first);
				f32 withMark = pen + static_cast<f32>(entry->advance) + stepBefore(entry, mark, false) + markAdvance;
				if (withMark > budget) {
					cutByte = _scratchLine[n].byteOffset;
					break;
				}
				x = pen + static_cast<f32>(entry->advance);
				previous = entry;
				first = false;
				keep = n + 1;
			}
			_scratchLine.resize(keep);
			_scratchLine.push_back({ELLIPSIS_CODEPOINT, cutByte, mark, cutByte});
			truncated = true;
		};

		while (i < length) {
			u32 byteOffset = i;
			unsigned char c = static_cast<unsigned char>(text[i]);
			char32_t codepoint = 0;
			u32 extra = 0;
			if (c < 0x80) {
				codepoint = c;
			} else if ((c >> 5) == 0x6) {
				codepoint = c & 0x1F;
				extra = 1;
			} else if ((c >> 4) == 0xE) {
				codepoint = c & 0x0F;
				extra = 2;
			} else if ((c >> 3) == 0x1E) {
				codepoint = c & 0x07;
				extra = 3;
			} else {
				++i;
				continue;
			}
			++i;
			for (u32 k = 0; k < extra && i < length; ++k, ++i) {
				unsigned char cc = static_cast<unsigned char>(text[i]);
				if ((cc & 0xC0) != 0x80) {
					codepoint = '?';
					break;
				}
				codepoint = (codepoint << 6) | (cc & 0x3F);
			}

			if (codepoint == '\n') {
				if (ellipsize) {
					pendingOverflow = true;
					break;
				}
				flushLine(true, false);
				out.multiline = true;
				lineAdvance = 0.0f;
				lastBreakCount = 0;
				lineStartByte = i;
				continue;
			}

			++_textStats.glyphsResolved;
			GlyphEntry* entry = GetOrCreateGlyph(*face, sizePx, codepoint, 0);
			f32 step = 0.0f;
			if (!_scratchLine.empty() && _scratchLine.back().entry && entry) {
				step = stepBefore(_scratchLine.back().entry, entry, false);
			}
			f32 advance = entry ? static_cast<f32>(entry->advance) : 0.0f;
			f32 nextAdvance = lineAdvance + step + advance;

			if (wrap && nextAdvance > query.wrapWidth && !_scratchLine.empty()) {
				u32 emitCount = (lastBreakCount > 0 && lastBreakCount < _scratchLine.size())
									? lastBreakCount
									: static_cast<u32>(_scratchLine.size());
				Vector<Pending> tail{GetMemoryResource(MemTag::UI)};
				tail.assign(_scratchLine.begin() + emitCount, _scratchLine.end());
				_scratchLine.resize(emitCount);
				flushLine(true, true);
				out.multiline = true;
				_scratchLine = std::move(tail);
				lastBreakCount = 0;
				lineStartByte = _scratchLine.empty() ? byteOffset : _scratchLine.front().byteOffset;

				lineAdvance = 0.0f;
				const GlyphEntry* previous = nullptr;
				bool firstOnLine = true;
				for (const Pending& pending : _scratchLine) {
					if (!pending.entry) {
						continue;
					}
					lineAdvance += stepBefore(previous, pending.entry, firstOnLine);
					lineAdvance += static_cast<f32>(pending.entry->advance);
					firstOnLine = false;
					previous = pending.entry;
				}
				step = 0.0f;
				if (!_scratchLine.empty() && _scratchLine.back().entry && entry) {
					step = stepBefore(_scratchLine.back().entry, entry, false);
				}
				nextAdvance = lineAdvance + step + advance;
			}

			if (ellipsize && nextAdvance > budget) {
				overflowByte = byteOffset;
				pendingOverflow = true;
				break;
			}
			_scratchLine.push_back({static_cast<u32>(codepoint), byteOffset, entry, i});
			lineAdvance = nextAdvance;
			if (IsBreakCodepoint(codepoint)) {
				lastBreakCount = static_cast<u32>(_scratchLine.size());
			}
		}

		if (ellipsize && (lineAdvance > budget || pendingOverflow)) {
			appendEllipsis();
		}

		const bool endsInNewline = !ellipsize && length > 0 && text[length - 1] == '\n';
		if (!_scratchLine.empty() || lineCount == 0 || endsInNewline) {
			flushLine(false, false);
			penY += lineHeight;
		}

		out.extent = {maxLineWidth, std::max(penY, lineHeight)};
		out.truncated = truncated;

		if (align) {
			_scratchSpans.push_back({static_cast<u32>(out.glyphs.size()), static_cast<u32>(out.outlineGlyphs.size()),
									 static_cast<u32>(out.caretStops.size()), maxLineWidth});
			for (size_t n = 0; n + 1 < _scratchSpans.size(); ++n) {
				const LineSpan& line = _scratchSpans[n];
				const LineSpan& next = _scratchSpans[n + 1];
				const f32 shift = Snap((maxLineWidth - line.width) * share);
				for (u32 k = line.glyphs; k < next.glyphs; ++k) {
					out.glyphs[k].rect.x += shift;
				}
				for (u32 k = line.outlineGlyphs; k < next.outlineGlyphs; ++k) {
					out.outlineGlyphs[k].rect.x += shift;
				}
				for (u32 k = line.caretStops; k < next.caretStops; ++k) {
					out.caretStops[k].x += shift;
				}
			}
		}

		if (emit && truncated) {
			out.caretStops.clear();
			out.caretStops.push_back({0, 0.0f, 0});
		}

		return true;
	}

	Vec2 FreeTypeFontSystem::MeasureText(const TextQuery& query) {
		if (!Shape(query, _measureScratch, false)) {
			return {};
		}
		return _measureScratch.extent;
	}

	f32 FreeTypeFontSystem::GetLineHeight(Handle<FontAsset> font, f32 sizePx) {
		FontFace* face = GetOrCreateFace(font);
		if (!face) {
			return 0.0f;
		}
		u16 px = static_cast<u16>(std::max(1, static_cast<i32>(std::lround(sizePx))));
		if (!EnsureSize(*face, px)) {
			return 0.0f;
		}
		f32 lineHeight = static_cast<f32>(face->face->size->metrics.height) / 64.0f;
		if (lineHeight <= 0.0f) {
			lineHeight = static_cast<f32>(face->face->size->metrics.ascender - face->face->size->metrics.descender) / 64.0f;
		}
		return lineHeight;
	}

} // namespace Shalltear::UI
