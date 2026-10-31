#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <cmath>

namespace Shalltear::UI {

	inline constexpr u32 INVALID_UI_INDEX = 0xFFFFFFFFu;

	struct ElementHandle {
		u32 index = INVALID_UI_INDEX;
		u32 generation = 0;
		u32 context = 0;

		[[nodiscard]] bool IsValid() const { return index != INVALID_UI_INDEX; }

		bool operator==(const ElementHandle& other) const {
			return index == other.index && generation == other.generation && context == other.context;
		}
	};

	struct Rect {
		f32 x = 0.0f;
		f32 y = 0.0f;
		f32 w = 0.0f;
		f32 h = 0.0f;

		[[nodiscard]] bool Contains(Vec2 p) const {
			return p.x >= x && p.y >= y && p.x <= x + w && p.y <= y + h;
		}
		bool operator==(const Rect&) const = default;
	};

	inline constexpr Rect InfiniteClip = {-1.0e5f, -1.0e5f, 2.0e5f, 2.0e5f};

	[[nodiscard]] inline Rect RectIntersect(const Rect& a, const Rect& b) {
		f32 x0 = a.x > b.x ? a.x : b.x;
		f32 y0 = a.y > b.y ? a.y : b.y;
		f32 x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
		f32 y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
		if (x1 <= x0 || y1 <= y0) {
			return {};
		}
		return {x0, y0, x1 - x0, y1 - y0};
	}

	[[nodiscard]] inline f32 SnapToPixel(f32 v) {
		return std::floor(v + 0.5f);
	}

	[[nodiscard]] inline Rect SnapRectToPixels(const Rect& r) {
		f32 x = SnapToPixel(r.x);
		f32 y = SnapToPixel(r.y);
		return {x, y, SnapToPixel(r.x + r.w) - x, SnapToPixel(r.y + r.h) - y};
	}

	[[nodiscard]] inline f32 SnapLengthToPixels(f32 logicalLength, f32 scale) {
		if (logicalLength == 0.0f) {
			return 0.0f;
		}
		f32 magnitude = SnapToPixel(std::abs(logicalLength) * scale);
		if (magnitude < 1.0f) {
			magnitude = 1.0f;
		}
		return logicalLength > 0.0f ? magnitude : -magnitude;
	}

	enum class Display : u8 {
		Flex = 0,
		None
	};

	enum class Position : u8 {
		Relative = 0,
		Absolute
	};

	enum class FlexDirection : u8 {
		Row = 0,
		Column
	};

	enum class Align : u8 {
		Auto = 0,
		Start,
		Center,
		End,
		Stretch,
		SpaceBetween,
		SpaceAround
	};

	enum class Overflow : u8 {
		Visible = 0,
		Hidden,
		Scroll
	};

	enum class TextOverflow : u8 {
		Wrap = 0,
		Clip,
		Ellipsis
	};

	struct Length {
		enum class Unit : u8 {
			Auto = 0,
			Pixel,
			Percent
		};

		Unit unit = Unit::Auto;
		f32 value = 0.0f;

		static constexpr Length Auto() { return {}; }
		static constexpr Length Pixel(f32 px) { return {Unit::Pixel, px}; }
		static constexpr Length Percent(f32 fraction) { return {Unit::Percent, fraction}; }

		[[nodiscard]] constexpr bool IsAuto() const { return unit == Unit::Auto; }
		bool operator==(const Length&) const = default;
	};

	struct Edges {
		f32 left = 0.0f;
		f32 top = 0.0f;
		f32 right = 0.0f;
		f32 bottom = 0.0f;

		static constexpr Edges All(f32 v) { return {v, v, v, v}; }
		bool operator==(const Edges&) const = default;
	};

	[[nodiscard]] inline Edges SnapEdgesToPixels(const Edges& edges, f32 scale) {
		return {
			SnapLengthToPixels(edges.left, scale),
			SnapLengthToPixels(edges.top, scale),
			SnapLengthToPixels(edges.right, scale),
			SnapLengthToPixels(edges.bottom, scale)};
	}

	struct Radii {
		f32 topLeft = 0.0f;
		f32 topRight = 0.0f;
		f32 bottomRight = 0.0f;
		f32 bottomLeft = 0.0f;

		static constexpr Radii All(f32 v) { return {v, v, v, v}; }
		[[nodiscard]] constexpr bool IsZero() const {
			return topLeft <= 0.0f && topRight <= 0.0f && bottomRight <= 0.0f && bottomLeft <= 0.0f;
		}
		bool operator==(const Radii&) const = default;
	};

	[[nodiscard]] inline constexpr Rect DeflateRect(const Rect& r, const Edges& e) {
		f32 w = r.w - e.left - e.right;
		f32 h = r.h - e.top - e.bottom;
		if (w <= 0.0f || h <= 0.0f) {
			return {};
		}
		return {r.x + e.left, r.y + e.top, w, h};
	}

	[[nodiscard]] inline constexpr Radii DeflateRadii(const Radii& r, const Edges& e) {
		auto pull = [](f32 radius, f32 a, f32 b) {
			f32 v = radius - (a + b) * 0.5f;
			return v > 0.0f ? v : 0.0f;
		};
		return {
			pull(r.topLeft, e.left, e.top),
			pull(r.topRight, e.right, e.top),
			pull(r.bottomRight, e.right, e.bottom),
			pull(r.bottomLeft, e.left, e.bottom)};
	}

	// SRGB, composited in that encoded space

	struct BoxShadow {
		f32 x = 0.0f;
		f32 y = 0.0f;
		f32 blur = 0.0f;
		f32 spread = 0.0f;
		Color color = Color(0.0f, 0.0f, 0.0f, 0.0f);
		bool inset = false;

		bool operator==(const BoxShadow&) const = default;
	};

	struct DropShadow {
		f32 x = 0.0f;
		f32 y = 0.0f;
		f32 blur = 0.0f;
		Color color = Color(0.0f, 0.0f, 0.0f, 0.0f);

		bool operator==(const DropShadow&) const = default;
	};

	enum class BrushType : u8 {
		None = 0,
		Solid,
		Image,
		LinearGradient
	};

	enum class SamplerKind : u8 {
		LinearClamp = 0,
		LinearWrap,
		NearestClamp,
		NearestWrap,
		Count
	};

	struct Brush {
		BrushType type = BrushType::None;
		Color solid = Color::White();
		RHI::TextureHandle image;
		SamplerKind sampler = SamplerKind::LinearClamp;
		Color gradient0 = Color::White();
		Color gradient1 = Color::White();
		Vec2 gradientDirection = {1.0f, 0.0f};

		static Brush SolidColor(Color color) {
			Brush brush;
			brush.type = BrushType::Solid;
			brush.solid = color;
			return brush;
		}
		static Brush ImageBrush(RHI::TextureHandle texture, SamplerKind sampler = SamplerKind::LinearClamp) {
			Brush brush;
			brush.type = BrushType::Image;
			brush.image = texture;
			brush.sampler = sampler;
			return brush;
		}
		static Brush Gradient(Color from, Color to, Vec2 direction = {1.0f, 0.0f}) {
			Brush brush;
			brush.type = BrushType::LinearGradient;
			brush.gradient0 = from;
			brush.gradient1 = to;
			brush.gradientDirection = direction;
			return brush;
		}

		bool operator==(const Brush&) const = default;
	};

} // namespace Shalltear::UI
