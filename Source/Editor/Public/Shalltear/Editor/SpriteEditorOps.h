#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>
#include <string_view>

namespace Shalltear {
	class JSONValue;
	class Path;
} // namespace Shalltear

namespace Shalltear::SpriteEditorOps {

	enum class SliceMode : u8 {
		CellSize = 0,
		CellCount,
		Automatic,
	};

	struct SliceDesc {
		SliceMode mode = SliceMode::CellSize;
		// CellSize: texels per cell, CellCount: columns/rows
		Vec2I cell{32, 32};
		Vec2I offset{};
		Vec2I padding{};
	};

	// 4 byte rgba texel, top row first (null to keep empty cell)
	SHALLTEAR_EDITOR_API void Slice(const SliceDesc& desc, Vec2I imageSize, const u8* rgba, Vector<RectI>& out);

	// a new rect should take id/name of the sprite with most overlap
	SHALLTEAR_EDITOR_API void MergeSlices(Vector<TextureSprite>& sprites, std::span<const RectI> rects, std::string_view baseName);

	SHALLTEAR_EDITOR_API str MintId();
	SHALLTEAR_EDITOR_API str UniqueName(std::span<const TextureSprite> sprites, std::string_view base);

	SHALLTEAR_EDITOR_API bool DecodeTexels(const TextureAsset& texture, Vector<u8>& rgba);

	SHALLTEAR_EDITOR_API bool ApplySprites(const Path& texturePath, std::span<const TextureSprite> sprites);

	struct SHALLTEAR_EDITOR_API CanvasView {
		UI::Rect box{};
		Vec2I imageSize{};
		f32 zoom = 1.0f;
		// physical px from the centered fit
		Vec2 pan{};

		[[nodiscard]] f32 Scale() const;
		[[nodiscard]] Vec2 ToTexel(Vec2 point) const;
		[[nodiscard]] Vec2 ToPoint(Vec2 texel) const;
		[[nodiscard]] UI::Rect ToRect(const RectI& texels) const;
	};

	SHALLTEAR_EDITOR_API void ZoomAt(CanvasView& view, Vec2 point, f32 notches);

	enum class GrabEdge : u8 {
		None = 0,
		Left = 1 << 0,
		Top = 1 << 1,
		Right = 1 << 2,
		Bottom = 1 << 3,
	};

	enum class GrabKind : u8 {
		None = 0,
		Create,
		Move,
		Resize,
	};

	struct Grab {
		GrabKind kind = GrabKind::None;
		i32 sprite = -1;
		u8 edges = 0;
	};

	// handlePx = edge reach of the selected sprite. Later sprite picked first
	SHALLTEAR_EDITOR_API Grab Pick(const CanvasView& view, std::span<const TextureSprite> sprites, i32 selected, Vec2 point, f32 handlePx);

	// delta = texels since press. Result inside image, at least 1 texel
	SHALLTEAR_EDITOR_API RectI Drag(const RectI& start, const Grab& grab, Vec2I delta, Vec2I imageSize);

	SHALLTEAR_EDITOR_API RectI SpanRect(Vec2 fromTexel, Vec2 toTexel, Vec2I imageSize);

	SHALLTEAR_EDITOR_API SpriteBorder ClampBorder(const SpriteBorder& border, Vec2I rectSize);

} // namespace Shalltear::SpriteEditorOps
