#include "UI/FreeTypeFontSystem.h"

#include "Shalltear/Core/UI/FontAsset.h"

#include FT_TRUETYPE_TABLES_H
#include FT_MULTIPLE_MASTERS_H
#include FT_SYNTHESIS_H

#include <algorithm>
#include <cmath>

namespace Shalltear::UI {

	namespace {

		constexpr f32 BOLD_WEIGHT = 600.0f;
		// css's preferred oblique angle for italic
		constexpr f32 ITALIC_OBLIQUE_DEGREES = 11.0f;
		// tan(14 deg) in 16.16, css's default oblique angle
		constexpr FT_Fixed SYNTHETIC_OBLIQUE_SHEAR = 0x3FD3;

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

		constexpr f32 SCRIPT_SCALE = 0.65f;
		constexpr f32 SUB_OFFSET = 0.15f;
		constexpr f32 SUPER_OFFSET = 0.35f;
		constexpr f32 SMALL_CAPS_SCALE = 0.8f;

		// Latin-1, Latin Extended-A, Greek, Cyrillic
		u32 UpperOf(u32 c) {
			if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) ||
				(c >= 0x430 && c <= 0x44F)) {
				return c - 32;
			}
			if (c == 0xFF) {
				return 0x178;
			}
			if (c == 0x3C2) {
				return 0x3A3;
			}
			if (c >= 0x450 && c <= 0x45F) {
				return c - 80;
			}
			if (c == 0x131) {
				return 'I';
			}
			if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {
				return c % 2 == 0 ? c - 1 : c;
			}
			if (c >= 0x100 && c <= 0x177 && c != 0x138 && c != 0x149 && c % 2 == 1) {
				return c - 1;
			}
			return c;
		}

		u32 LowerOf(u32 c) {
			if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) ||
				(c >= 0x410 && c <= 0x42F)) {
				return c + 32;
			}
			if (c == 0x178) {
				return 0xFF;
			}
			if (c >= 0x400 && c <= 0x40F) {
				return c + 80;
			}
			if (c == 0x130) {
				return 'i';
			}
			if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {
				return c % 2 == 1 ? c + 1 : c;
			}
			if (c >= 0x100 && c <= 0x177 && c != 0x138 && c % 2 == 0) {
				return c + 1;
			}
			return c;
		}

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
		ReadAxes(face);
		_faceSlots[face.slot] = &face;
		return true;
	}

	void FreeTypeFontSystem::ReadAxes(FontFace& face) {
		face.wght = face.ital = face.slnt = face.opsz = {};
		face.defaultCoords.clear();
		face.appliedCoords.clear();
		FT_MM_Var* variation = nullptr;
		if (!FT_HAS_MULTIPLE_MASTERS(face.face) || FT_Get_MM_Var(face.face, &variation) != 0 || !variation) {
			return;
		}
		for (FT_UInt i = 0; i < variation->num_axis; ++i) {
			const FT_Var_Axis& axis = variation->axis[i];
			face.defaultCoords.push_back(axis.def);
			FontFace::Axis* known = nullptr;
			switch (axis.tag) {
			case FT_MAKE_TAG('w', 'g', 'h', 't'):
				known = &face.wght;
				break;
			case FT_MAKE_TAG('i', 't', 'a', 'l'):
				known = &face.ital;
				break;
			case FT_MAKE_TAG('s', 'l', 'n', 't'):
				known = &face.slnt;
				break;
			case FT_MAKE_TAG('o', 'p', 's', 'z'):
				known = &face.opsz;
				break;
			default:
				break;
			}
			if (known) {
				*known = {static_cast<i32>(i), static_cast<f32>(axis.minimum) / 65536.0f, static_cast<f32>(axis.maximum) / 65536.0f};
			}
		}
		FT_Done_MM_Var(_library, variation);
		face.appliedCoords = face.defaultCoords;
	}

	bool FreeTypeFontSystem::DescribeFont(Handle<FontAsset> font, FontFaceInfo& out) {
		FontFace* face = GetOrCreateFace(font);
		if (!face) {
			return false;
		}
		const bool variable = face->wght.index >= 0;
		out.weightMin = variable ? static_cast<u16>(std::lround(face->wght.min)) : face->weight;
		out.weightMax = variable ? static_cast<u16>(std::lround(face->wght.max)) : face->weight;
		out.italic = face->italic;
		out.variableItalic = face->ital.index >= 0 || face->slnt.index >= 0;
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

	FreeTypeFontSystem::StyledFace FreeTypeFontSystem::ResolveStyle(FontFace& family, u16 weight, FontStyle style) {
		const bool italic = style == FontStyle::Italic;
		const f32 wanted = static_cast<f32>(weight);
		auto canItalic = [](const FontFace& face) { return face.italic || (face.ital.index >= 0 && face.ital.max >= 1.0f); };
		auto canOblique = [](const FontFace& face) { return face.slnt.index >= 0 && face.slnt.min < 0.0f; };
		auto canUpright = [](const FontFace& face) {
			return !face.italic || (face.ital.index >= 0 && face.ital.min <= 0.0f) || (face.slnt.index >= 0 && face.slnt.max >= 0.0f);
		};
		auto range = [](const FontFace& face, f32& lo, f32& hi) {
			lo = face.wght.index >= 0 ? face.wght.min : static_cast<f32>(face.weight);
			hi = face.wght.index >= 0 ? face.wght.max : static_cast<f32>(face.weight);
		};
		enum class Lean : u8 {
			Upright,
			Italic,
			Oblique,
			Synthetic
		};
		auto finish = [&](FontFace& face, Lean lean) {
			StyledFace styled;
			styled.face = &face;
			f32 lo, hi;
			range(face, lo, hi);
			styled.weight = std::clamp(wanted, lo, hi);
			if (face.ital.index >= 0) {
				styled.ital = std::clamp(lean == Lean::Italic ? 1.0f : 0.0f, face.ital.min, face.ital.max);
			}
			if (face.slnt.index >= 0) {
				styled.slnt = std::clamp(lean == Lean::Oblique ? -ITALIC_OBLIQUE_DEGREES : 0.0f, face.slnt.min, face.slnt.max);
			}
			styled.embolden = wanted >= BOLD_WEIGHT && styled.weight < BOLD_WEIGHT;
			const FontAsset* asset = face.system ? nullptr : face.asset.Get();
			styled.regularAdvance = styled.embolden && asset && asset->syntheticBoldSpacing == SyntheticBoldSpacing::Regular;
			styled.oblique = lean == Lean::Synthetic;
			return styled;
		};

		f32 familyLo, familyHi;
		range(family, familyLo, familyHi);
		if ((italic ? canItalic(family) : canUpright(family)) && familyLo <= wanted && wanted <= familyHi) {
			return finish(family, italic ? Lean::Italic : Lean::Upright);
		}

		_scratchCandidates.clear();
		_scratchCandidates.push_back(&family);
		const FontAsset* asset = family.system ? nullptr : family.asset.Get();
		for (size_t i = 0; asset && i < asset->faces.size(); ++i) {
			if (asset->faces[i] == asset->assetId) {
				continue;
			}
			FontAsset* member = asset->GetFace(i);
			FontFace* face = member ? GetOrCreateFace(Handle<FontAsset>(member)) : nullptr;
			if (face && std::find(_scratchCandidates.begin(), _scratchCandidates.end(), face) == _scratchCandidates.end()) {
				_scratchCandidates.push_back(face);
			}
		}

		Lean lean = Lean::Upright;
		bool (*tier)(const FontFace&) = nullptr;
		auto any = [this](bool (*test)(const FontFace&)) {
			return std::any_of(_scratchCandidates.begin(), _scratchCandidates.end(), [test](FontFace* face) { return test(*face); });
		};
		if (italic) {
			if (any(canItalic)) {
				lean = Lean::Italic;
				tier = canItalic;
			} else if (any(canOblique)) {
				lean = Lean::Oblique;
				tier = canOblique;
			} else {
				lean = Lean::Synthetic;
			}
		} else if (any(canUpright)) {
			tier = canUpright;
		}

		auto above = [&](f32 limit) -> FontFace* {
			FontFace* best = nullptr;
			f32 bestLo = INFINITY;
			for (FontFace* face : _scratchCandidates) {
				f32 lo, hi;
				range(*face, lo, hi);
				if ((!tier || tier(*face)) && lo > wanted && lo <= limit && lo < bestLo) {
					best = face;
					bestLo = lo;
				}
			}
			return best;
		};
		auto below = [&]() -> FontFace* {
			FontFace* best = nullptr;
			f32 bestHi = -INFINITY;
			for (FontFace* face : _scratchCandidates) {
				f32 lo, hi;
				range(*face, lo, hi);
				if ((!tier || tier(*face)) && hi < wanted && hi > bestHi) {
					best = face;
					bestHi = hi;
				}
			}
			return best;
		};
		FontFace* chosen = nullptr;
		for (FontFace* face : _scratchCandidates) {
			f32 lo, hi;
			range(*face, lo, hi);
			if ((!tier || tier(*face)) && lo <= wanted && wanted <= hi) {
				chosen = face;
				break;
			}
		}
		if (!chosen && wanted >= 400.0f && wanted <= 500.0f) {
			chosen = above(500.0f);
		}
		if (!chosen && wanted <= 500.0f) {
			chosen = below();
		}
		if (!chosen) {
			chosen = above(INFINITY);
		}
		if (!chosen) {
			chosen = below();
		}
		return finish(chosen ? *chosen : family, lean);
	}

	bool FreeTypeFontSystem::ApplyInstance(const StyledFace& styled, u16 sizePx) {
		FontFace& face = *styled.face;
		if (face.defaultCoords.empty()) {
			return true;
		}
		_scratchCoords.assign(face.defaultCoords.begin(), face.defaultCoords.end());
		auto set = [this](const FontFace::Axis& axis, f32 value) {
			if (axis.index >= 0) {
				_scratchCoords[static_cast<size_t>(axis.index)] = static_cast<FT_Fixed>(std::lround(value * 65536.0f));
			}
		};
		set(face.wght, styled.weight);
		set(face.ital, styled.ital);
		set(face.slnt, styled.slnt);
		set(face.opsz, std::clamp(static_cast<f32>(sizePx), face.opsz.min, face.opsz.max));
		if (_scratchCoords == face.appliedCoords) {
			return true;
		}
		if (FT_Set_Var_Design_Coordinates(face.face, static_cast<FT_UInt>(_scratchCoords.size()), _scratchCoords.data()) != 0) {
			return false;
		}
		face.appliedCoords.assign(_scratchCoords.begin(), _scratchCoords.end());
		return true;
	}

	bool FreeTypeFontSystem::FindInFallbacks(const FontAsset& font, u16 weight, FontStyle style, u32 codepoint, StyledFace& out) {
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
			if (FontFace* face = GetOrCreateFace(Handle<FontAsset>(next))) {
				const StyledFace styled = ResolveStyle(*face, weight, style);
				if (GlyphIndexOf(*styled.face, codepoint) != 0) {
					out = styled;
					return true;
				}
			}
			if (FindInFallbacks(*next, weight, style, codepoint, out)) {
				return true;
			}
		}
		return false;
	}

	FreeTypeFontSystem::StyledFace FreeTypeFontSystem::ResolveOwner(FontFace& family, u16 weight, FontStyle style, u32 codepoint) {
		const StyledFace styled = ResolveStyle(family, weight, style);
		if (IsControlCodepoint(codepoint) || GlyphIndexOf(*styled.face, codepoint) != 0) {
			return styled;
		}
		const u16 drawnWeight = styled.embolden ? weight : static_cast<u16>(std::lround(styled.weight));
		const bool drawnItalic =
			(styled.face->ital.index >= 0 ? styled.ital > 0.0f : styled.face->italic) || styled.slnt < 0.0f || styled.oblique;
		const FontStyle drawnStyle = drawnItalic ? FontStyle::Italic : FontStyle::Normal;
		if (const FontAsset* font = family.system ? nullptr : family.asset.Get(); font && !font->fallbacks.empty()) {
			_scratchVisited.clear();
			_scratchVisited.push_back(font->assetId);
			StyledFace found;
			if (FindInFallbacks(*font, drawnWeight, drawnStyle, codepoint, found)) {
				return found;
			}
		}
		if (const SystemFontFile* file = _systemFallback.Find(codepoint, drawnWeight, drawnItalic)) {
			FontFace* system = GetOrCreateSystemFace(*file);
			if (system && GlyphIndexOf(*system, codepoint) != 0) {
				return ResolveStyle(*system, drawnWeight, drawnStyle);
			}
		}
		return styled;
	}

	f32 FreeTypeFontSystem::KerningBetween(FontFace& primary, u16 sizePx, const GlyphEntry* previous, const GlyphEntry* current) {
		if (!previous || !current || previous->faceSlot != current->faceSlot || previous->glyphIndex == 0 ||
			current->glyphIndex == 0) {
			return 0.0f;
		}
		FontFace* face = current->faceSlot == primary.slot ? &primary : FaceForSlot(current->faceSlot);
		if (!face || !FT_HAS_KERNING(face->face)) {
			return 0.0f;
		}
		// TTF/OTF glyph indices are 16 bit
		const u64 pairKey = (static_cast<u64>(sizePx) << 32) | (static_cast<u64>(previous->glyphIndex & 0xFFFFu) << 16) |
							(current->glyphIndex & 0xFFFFu);
		auto it = face->kernings.find(pairKey);
		if (it == face->kernings.end()) {
			if (!EnsureSize(*face, sizePx)) {
				return 0.0f;
			}
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

	FreeTypeFontSystem::GlyphEntry* FreeTypeFontSystem::GetOrCreateGlyph(FontFace& family, u16 weight, FontStyle style, u16 sizePx,
																		 u32 codepoint, u16 strokePx) {
		GlyphKey key{family.slot, codepoint, sizePx, strokePx, weight, style};
		auto it = _glyphs.find(key);
		if (it != _glyphs.end()) {
			return &it->second;
		}
		const StyledFace owner = ResolveOwner(family, weight, style, codepoint);
		u32 glyphIndex = GlyphIndexOf(*owner.face, codepoint);
		if (glyphIndex == 0) {
			glyphIndex = GlyphIndexOf(*owner.face, static_cast<u32>('?'));
		}
		GlyphEntry entry;
		entry.faceSlot = owner.face->slot;
		RasterizeAndPack(owner, sizePx, glyphIndex, strokePx, entry);
		auto [inserted, ok] = _glyphs.emplace(key, entry);
		return &inserted->second;
	}

	bool FreeTypeFontSystem::RasterizeAndPack(const StyledFace& styled, u16 sizePx, u32 glyphIndex, u16 strokePx, GlyphEntry& out) {
		FontFace& face = *styled.face;
		FT_Face ftFace = face.face;
		out.glyphIndex = glyphIndex;
		if (glyphIndex == 0 || !ApplyInstance(styled, sizePx) || !EnsureSize(face, sizePx)) {
			return false;
		}

		if (FT_Load_Glyph(ftFace, glyphIndex, FT_LOAD_DEFAULT) != 0) {
			return false;
		}
		if (styled.embolden) {
			const FT_Pos regular = ftFace->glyph->advance.x;
			FT_GlyphSlot_Embolden(ftFace->glyph);
			if (styled.regularAdvance) {
				ftFace->glyph->advance.x = regular;
			}
		}
		if (styled.oblique) {
			FT_GlyphSlot_Slant(ftFace->glyph, SYNTHETIC_OBLIQUE_SHEAR, 0);
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
		const bool widened = styled.embolden && !styled.regularAdvance;
		out.advance = static_cast<i32>((bitmapGlyph->root.advance.x + (widened ? 0x8000 : 0)) >> 16);

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

	bool FreeTypeFontSystem::PrepareRun(const TextQuery& query, const TextRun* desc, RunState& out) {
		FontFace* family = GetOrCreateFace(desc ? desc->font : query.font);
		if (!family && desc) {
			family = GetOrCreateFace(query.font);
		}
		if (!family) {
			return false;
		}
		f32 size = desc ? desc->sizePx : query.sizePx;
		f32 shift = desc ? desc->baselineShift : 0.0f;
		const TextScript script = desc ? desc->script : TextScript::Baseline;
		if (script != TextScript::Baseline) {
			const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(family->face, FT_SFNT_OS2));
			const f32 upem = static_cast<f32>(family->face->units_per_EM);
			const bool sub = script == TextScript::Sub;
			const FT_Short scriptSize = os2 ? (sub ? os2->ySubscriptYSize : os2->ySuperscriptYSize) : 0;
			const FT_Short scriptOffset = os2 ? (sub ? os2->ySubscriptYOffset : os2->ySuperscriptYOffset) : 0;
			const bool declared = upem > 0.0f && scriptSize > 0;
			shift += (sub ? -1.0f : 1.0f) * size * (declared ? static_cast<f32>(scriptOffset) / upem : (sub ? SUB_OFFSET : SUPER_OFFSET));
			size *= declared ? static_cast<f32>(scriptSize) / upem : SCRIPT_SCALE;
		}
		out.family = family;
		out.sizePx = static_cast<u16>(std::max(1, static_cast<i32>(std::lround(size))));
		out.smallSizePx = static_cast<u16>(std::max(1, static_cast<i32>(std::lround(size * SMALL_CAPS_SCALE))));
		out.weight = std::clamp<u16>(desc ? desc->fontWeight : query.fontWeight, 1, 1000);
		out.style = desc ? desc->fontStyle : query.fontStyle;
		const StyledFace styled = ResolveStyle(*family, out.weight, out.style);
		if (!ApplyInstance(styled, out.sizePx) || !EnsureSize(*styled.face, out.sizePx)) {
			return false;
		}
		out.styled = styled.face;

		const FT_Face ftFace = styled.face->face;
		out.ascender = static_cast<f32>(ftFace->size->metrics.ascender) / 64.0f;
		out.descender = static_cast<f32>(ftFace->size->metrics.descender) / 64.0f;
		f32 fontLineHeight = static_cast<f32>(ftFace->size->metrics.height) / 64.0f;
		if (fontLineHeight <= 0.0f) {
			fontLineHeight = out.ascender - out.descender;
		}
		const f32 requested = desc ? (desc->lineHeightScale > 0.0f ? fontLineHeight * desc->lineHeightScale : desc->lineHeight) : query.lineHeight;
		out.lineHeight = requested > 0.0f ? std::max(1.0f, std::round(requested)) : fontLineHeight;
		out.halfLeading = std::floor((out.lineHeight - fontLineHeight) * 0.5f);
		out.letterSpacing = desc ? desc->letterSpacing : query.letterSpacing;
		out.baselineShift = shift;
		out.advance = desc ? desc->advance : 0.0f;
		out.paint = desc ? desc->paint : u16{0};
		out.textCase = desc ? desc->textCase : TextCase::Normal;
		out.decoration = desc ? desc->decoration : TextDecoration::None;
		out.noBreak = desc && desc->noBreak;

		if (out.decoration != TextDecoration::None) {
			const FT_Fixed scale = ftFace->size->metrics.y_scale;
			const f32 em = static_cast<f32>(out.sizePx);
			const auto* os2 = static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(ftFace, FT_SFNT_OS2));
			if (ftFace->underline_thickness > 0) {
				out.underlineOffset = -static_cast<f32>(FT_MulFix(ftFace->underline_position, scale)) / 64.0f;
				out.underlineThickness = static_cast<f32>(FT_MulFix(ftFace->underline_thickness, scale)) / 64.0f;
			} else {
				out.underlineOffset = em * 0.1f;
				out.underlineThickness = em / 14.0f;
			}
			out.underlineThickness = std::max(1.0f, std::round(out.underlineThickness));
			if (os2 && os2->yStrikeoutSize > 0) {
				// yStrikeoutPosition is stroke top, offsets here are centers
				out.strikeOffset = static_cast<f32>(FT_MulFix(os2->yStrikeoutPosition * 2 - os2->yStrikeoutSize, scale)) / 128.0f;
				out.strikeThickness = std::max(1.0f, std::round(static_cast<f32>(FT_MulFix(os2->yStrikeoutSize, scale)) / 64.0f));
			} else {
				out.strikeOffset = em * 0.3f;
				out.strikeThickness = out.underlineThickness;
			}
		}
		return true;
	}

	template <bool Styled>
	bool FreeTypeFontSystem::ShapeRuns(const TextQuery& query, ShapedRun& out, bool emit) {
		++_textStats.walks;
		if (emit) {
			++_textStats.runsBuilt;
		}
		out.glyphs.clear();
		out.outlineGlyphs.clear();
		out.caretStops.clear();
		out.spans.clear();
		out.decorations.clear();
		out.extent = {};
		out.lineHeight = 0.0f;
		out.halfLeading = 0.0f;
		out.multiline = false;
		out.atlas = _atlas;

		const u32 runCount = Styled ? static_cast<u32>(query.runs.size()) : 1u;
		_scratchRuns.resize(runCount);
		for (u32 r = 0; r < runCount; ++r) {
			if (!PrepareRun(query, Styled ? &query.runs[r] : nullptr, _scratchRuns[r])) {
				return false;
			}
		}
		const RunState& first = _scratchRuns[0];
		auto runOf = [&](u16 run) -> const RunState& {
			if constexpr (Styled) {
				return _scratchRuns[run];
			} else {
				return first;
			}
		};
		const u16 strokePx = query.outlineWidth > 0.0f
								 ? static_cast<u16>(std::max(1, static_cast<i32>(std::lround(query.outlineWidth))))
								 : 0;
		out.lineHeight = first.lineHeight;
		out.halfLeading = first.halfLeading;

		auto stepBefore = [&](const Pending* previous, const Pending& current, bool firstOnLine) -> f32 {
			if (!previous) {
				return 0.0f;
			}
			const RunState& before = runOf(previous->run);
			const RunState& run = runOf(current.run);
			const f32 spacing = firstOnLine ? 0.0f : before.letterSpacing;
			if constexpr (Styled) {
				if (previous->sizePx != current.sizePx || before.advance > 0.0f || run.advance > 0.0f ||
					(previous->run != current.run && (before.styled != run.styled || before.weight != run.weight || before.style != run.style))) {
					return spacing;
				}
			}
			return spacing + KerningBetween(*run.styled, current.sizePx, previous->entry, current.entry);
		};
		auto advanceOf = [&](const Pending& pending) -> f32 {
			if constexpr (Styled) {
				const f32 forced = _scratchRuns[pending.run].advance;
				if (forced > 0.0f) {
					return forced;
				}
			}
			return static_cast<f32>(pending.entry->advance);
		};

		f32 penY = 0.0f;
		f32 maxLineWidth = 0.0f;
		u32 lineCount = 0;
		const bool ellipsize = query.overflow == TextOverflow::Ellipsis;
		const bool wrap = query.overflow == TextOverflow::Wrap && query.wrapWidth > 0.0f;
		const f32 share = query.align == Align::Center ? 0.5f : (query.align == Align::End ? 1.0f : 0.0f);
		const bool align = emit && share > 0.0f;
		bool truncated = false;
		u16 currentRun = 0;

		_scratchLine.clear();
		_scratchSpans.clear();

		auto pushCaretStop = [&](u32 byteOffset, f32 x) {
			if (emit) {
				out.caretStops.push_back({byteOffset, x, lineCount});
			}
		};
		u32 lineStartByte = 0;
		f32 lastAdvanceY = first.lineHeight;

		auto flushLine = [&](bool startNewLine, bool softWrap) {
			u16 lineRun = currentRun;
			bool single = true;
			f32 top = 0.0f;
			f32 bottom = 0.0f;
			if constexpr (Styled) {
				bool any = false;
				for (const Pending& pending : _scratchLine) {
					const RunState& run = _scratchRuns[pending.run];
					const f32 runTop = run.halfLeading + run.ascender;
					const f32 runBottom = run.lineHeight - runTop;
					if (!any) {
						lineRun = pending.run;
						top = runTop;
						bottom = runBottom;
						any = true;
					} else if (pending.run != lineRun) {
						single = false;
						top = std::max(top, runTop);
						bottom = std::max(bottom, runBottom);
					}
				}
			}
			const RunState& lead = runOf(lineRun);
			const f32 baseline = single ? penY + lead.halfLeading + lead.ascender : penY + top;
			const f32 advanceY = single ? lead.lineHeight : top + bottom;

			f32 x = 0.0f;
			f32 inked = 0.0f;
			const Pending* previous = nullptr;
			bool firstOnLine = true;
			if (align) {
				_scratchSpans.push_back({static_cast<u32>(out.glyphs.size()), static_cast<u32>(out.outlineGlyphs.size()),
										 static_cast<u32>(out.caretStops.size()), static_cast<u32>(out.decorations.size()), 0.0f});
			}
			pushCaretStop(lineStartByte, 0.0f);

			u32 segmentRun = INVALID_UI_INDEX;
			f32 segmentStart = 0.0f;
			f32 segmentEnd = 0.0f;
			constexpr TextDecoration KINDS[] = {TextDecoration::Mark, TextDecoration::Underline, TextDecoration::Strikethrough};
			u32 joined[std::size(KINDS)] = {INVALID_UI_INDEX, INVALID_UI_INDEX, INVALID_UI_INDEX};
			auto closeSegment = [&](bool adjacent) {
				if (!emit || segmentRun == INVALID_UI_INDEX) {
					return;
				}
				const RunState& run = _scratchRuns[segmentRun];
				const f32 runBaseline = baseline - run.baselineShift;
				const f32 width = segmentEnd - segmentStart;
				for (size_t k = 0; k < std::size(KINDS); ++k) {
					const TextDecoration kind = KINDS[k];
					if (!HasFlag(run.decoration, kind)) {
						joined[k] = INVALID_UI_INDEX;
						continue;
					}
					if (width > 0.0f) {
						Rect rect{segmentStart, 0.0f, width, 0.0f};
						if (kind == TextDecoration::Mark) {
							rect.y = runBaseline - run.ascender;
							rect.h = run.ascender - run.descender;
						} else if (kind == TextDecoration::Underline) {
							rect.y = Snap(runBaseline + run.underlineOffset - run.underlineThickness * 0.5f);
							rect.h = run.underlineThickness;
						} else {
							rect.y = Snap(runBaseline - run.strikeOffset - run.strikeThickness * 0.5f);
							rect.h = run.strikeThickness;
						}
						ShapedDecoration* prior = joined[k] != INVALID_UI_INDEX ? &out.decorations[joined[k]] : nullptr;
						if (prior && prior->paint == run.paint && prior->rect.y == rect.y && prior->rect.h == rect.h) {
							prior->rect.w = segmentEnd - prior->rect.x;
						} else {
							joined[k] = static_cast<u32>(out.decorations.size());
							out.decorations.push_back({rect, run.paint, kind});
						}
					}
					if (!adjacent) {
						joined[k] = INVALID_UI_INDEX;
					}
				}
			};

			for (const Pending& pending : _scratchLine) {
				GlyphEntry* entry = pending.entry;
				if (!entry) {
					continue;
				}
				const RunState& run = runOf(pending.run);
				x += stepBefore(previous, pending, firstOnLine);
				firstOnLine = false;

				if (Styled && emit && (out.spans.empty() || out.spans.back().paint != run.paint)) {
					out.spans.push_back({static_cast<u32>(out.glyphs.size()), static_cast<u32>(out.outlineGlyphs.size()), run.paint});
				}
				const f32 left = Styled && run.advance > 0.0f ? x + (run.advance - static_cast<f32>(entry->advance)) * 0.5f : x;
				const f32 glyphBaseline = Styled && run.baselineShift != 0.0f ? baseline - run.baselineShift : baseline;
				if (emit && strokePx > 0) {
					GlyphEntry* stroke = GetOrCreateGlyph(*run.family, run.weight, run.style, pending.sizePx, pending.drawn, strokePx);
					if (stroke && stroke->inAtlas) {
						ShapedGlyph glyph;
						glyph.rect = {Snap(left + stroke->bearingX), Snap(glyphBaseline - stroke->bearingY),
									  static_cast<f32>(stroke->width), static_cast<f32>(stroke->height)};
						glyph.uv = stroke->uv;
						out.outlineGlyphs.push_back(glyph);
					}
				}
				if (emit && entry->inAtlas) {
					ShapedGlyph glyph;
					glyph.rect = {Snap(left + entry->bearingX), Snap(glyphBaseline - entry->bearingY),
								  static_cast<f32>(entry->width), static_cast<f32>(entry->height)};
					glyph.uv = entry->uv;
					out.glyphs.push_back(glyph);
				}
				if (Styled && run.decoration != TextDecoration::None && pending.run != segmentRun) {
					closeSegment(true);
					segmentRun = pending.run;
					segmentStart = x;
					segmentEnd = x;
				} else if (Styled && run.decoration == TextDecoration::None && segmentRun != INVALID_UI_INDEX) {
					closeSegment(false);
					segmentRun = INVALID_UI_INDEX;
				}

				x += advanceOf(pending);
				if (!IsSpaceCodepoint(pending.codepoint)) {
					inked = x;
					segmentEnd = x;
				}
				pushCaretStop(pending.byteEnd, x);
				previous = &pending;
			}
			if constexpr (Styled) {
				closeSegment(false);
			}
			if (align) {
				_scratchSpans.back().width = query.editable && !softWrap ? x : inked;
			}
			maxLineWidth = std::max(maxLineWidth, x);
			_scratchLine.clear();
			++lineCount;
			if (startNewLine) {
				penY += advanceY;
			}
			lastAdvanceY = advanceY;
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
			const u16 markRun = _scratchLine.empty() ? currentRun : _scratchLine.back().run;
			const RunState& run = runOf(markRun);
			GlyphEntry* glyph = GetOrCreateGlyph(*run.family, run.weight, run.style, run.sizePx, ELLIPSIS_CODEPOINT, 0);
			if (!glyph) {
				return;
			}
			Pending mark{ELLIPSIS_CODEPOINT, overflowByte, glyph, overflowByte, ELLIPSIS_CODEPOINT, markRun, run.sizePx};
			const f32 markAdvance = static_cast<f32>(glyph->advance);
			f32 x = 0.0f;
			const Pending* previous = nullptr;
			bool firstOnLine = true;
			size_t keep = 0;
			u32 cutByte = overflowByte;
			for (size_t n = 0; n < _scratchLine.size(); ++n) {
				const Pending& pending = _scratchLine[n];
				if (!pending.entry) {
					keep = n + 1;
					continue;
				}
				const f32 pen = x + stepBefore(previous, pending, firstOnLine);
				const f32 withMark = pen + advanceOf(pending) + stepBefore(&pending, mark, false) + markAdvance;
				if (withMark > budget) {
					cutByte = pending.byteOffset;
					break;
				}
				x = pen + advanceOf(pending);
				previous = &pending;
				firstOnLine = false;
				keep = n + 1;
			}
			_scratchLine.resize(keep);
			mark.byteOffset = cutByte;
			mark.byteEnd = cutByte;
			_scratchLine.push_back(mark);
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
			while (Styled && currentRun + 1u < runCount && byteOffset >= query.runs[currentRun].end) {
				++currentRun;
			}
			const RunState& run = runOf(currentRun);

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
			u32 drawn = codepoint;
			u16 glyphSize = run.sizePx;
			if (Styled && run.textCase != TextCase::Normal) {
				if (run.textCase == TextCase::Upper) {
					drawn = UpperOf(codepoint);
				} else if (run.textCase == TextCase::Lower) {
					drawn = LowerOf(codepoint);
				} else if (UpperOf(codepoint) != codepoint) {
					drawn = UpperOf(codepoint);
					glyphSize = run.smallSizePx;
				}
			}
			const Pending current{static_cast<u32>(codepoint), byteOffset, GetOrCreateGlyph(*run.family, run.weight, run.style, glyphSize, drawn, 0),
								  i, drawn, currentRun, glyphSize};
			f32 step = 0.0f;
			if (!_scratchLine.empty() && _scratchLine.back().entry && current.entry) {
				step = stepBefore(&_scratchLine.back(), current, false);
			}
			f32 advance = current.entry ? advanceOf(current) : 0.0f;
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
				const Pending* previous = nullptr;
				bool firstOnLine = true;
				for (const Pending& pending : _scratchLine) {
					if (!pending.entry) {
						continue;
					}
					lineAdvance += stepBefore(previous, pending, firstOnLine);
					lineAdvance += advanceOf(pending);
					firstOnLine = false;
					previous = &pending;
				}
				step = 0.0f;
				if (!_scratchLine.empty() && _scratchLine.back().entry && current.entry) {
					step = stepBefore(&_scratchLine.back(), current, false);
				}
				nextAdvance = lineAdvance + step + advance;
			}

			if (ellipsize && nextAdvance > budget) {
				overflowByte = byteOffset;
				pendingOverflow = true;
				break;
			}
			_scratchLine.push_back(current);
			lineAdvance = nextAdvance;
			if (IsBreakCodepoint(codepoint) && !(Styled && run.noBreak)) {
				lastBreakCount = static_cast<u32>(_scratchLine.size());
			}
		}

		if (ellipsize && (lineAdvance > budget || pendingOverflow)) {
			appendEllipsis();
		}

		const bool endsInNewline = !ellipsize && length > 0 && text[length - 1] == '\n';
		if (!_scratchLine.empty() || lineCount == 0 || endsInNewline) {
			flushLine(false, false);
			penY += lastAdvanceY;
		}

		out.extent = {maxLineWidth, std::max(penY, first.lineHeight)};
		out.truncated = truncated;

		if (align) {
			_scratchSpans.push_back({static_cast<u32>(out.glyphs.size()), static_cast<u32>(out.outlineGlyphs.size()),
									 static_cast<u32>(out.caretStops.size()), static_cast<u32>(out.decorations.size()), maxLineWidth});
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
				for (u32 k = line.decorations; k < next.decorations; ++k) {
					out.decorations[k].rect.x += shift;
				}
			}
		}

		if (emit && truncated) {
			out.caretStops.clear();
			out.caretStops.push_back({0, 0.0f, 0});
		}

		return true;
	}

	bool FreeTypeFontSystem::Shape(const TextQuery& query, ShapedRun& out, bool emit) {
		return query.runs.empty() ? ShapeRuns<false>(query, out, emit) : ShapeRuns<true>(query, out, emit);
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
		const StyledFace styled = ResolveStyle(*face, TextQuery{}.fontWeight, TextQuery{}.fontStyle);
		if (!ApplyInstance(styled, px) || !EnsureSize(*styled.face, px)) {
			return 0.0f;
		}
		const FT_Size_Metrics& metrics = styled.face->face->size->metrics;
		f32 lineHeight = static_cast<f32>(metrics.height) / 64.0f;
		if (lineHeight <= 0.0f) {
			lineHeight = static_cast<f32>(metrics.ascender - metrics.descender) / 64.0f;
		}
		return lineHeight;
	}

} // namespace Shalltear::UI
