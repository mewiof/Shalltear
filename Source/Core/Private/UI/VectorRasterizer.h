#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/VectorAsset.h"
#include "Shalltear/Core/UI/VectorIcon.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace Shalltear::UI {

	class VectorRasterizer {
	public:
		static constexpr f32 FLATNESS_PX = 0.1f;
		static constexpr i32 MAX_CUBIC_SEGMENTS = 64;

		// premultiplied RGBA8
		bool Rasterize(const VectorAsset& asset, f32 sizePx, const IconPalette& palette,
					   Vector<u8>& outPixels, Vec2I& outSize) {
			if (asset.IsEmpty() || sizePx < 1.0f) {
				return false;
			}
			const f32 scale = sizePx / std::max(asset.viewSize.x, asset.viewSize.y);
			const i32 width = std::max(1, static_cast<i32>(std::lround(asset.viewSize.x * scale)));
			const i32 height = std::max(1, static_cast<i32>(std::lround(asset.viewSize.y * scale)));
			Begin(width, height);

			_maskIndex = INVALID_VECTOR_INDEX;

			for (const VectorShape& shape : asset.shapes) {
				Color color = shape.slot < asset.slotDefaults.size() ? asset.slotDefaults[shape.slot] : Color::White();
				if (palette.Overrides(shape.slot)) {
					color = palette.slots[shape.slot];
				}
				color.a *= shape.opacity;
				if (color.a <= 0.0f) {
					continue;
				}

				const f32* mask = nullptr;
				if (shape.mask != INVALID_VECTOR_INDEX && shape.mask < asset.masks.size()) {
					mask = EnsureMask(asset, shape.mask, scale);
					if (!mask) {
						continue;
					}
				}

				AddPath({asset.verbs.data() + shape.firstVerb, shape.verbCount},
						{asset.points.data() + shape.firstPoint, shape.pointCount}, scale);
				SweepInto(_composite.data(), color, mask);
			}

			outSize = {width, height};
			ResolvePixels(outPixels);
			return true;
		}
	private:
		Vector<f32> _accum{GetMemoryResource(MemTag::UI)};
		Vector<f32> _composite{GetMemoryResource(MemTag::UI)};
		Vector<f32> _mask{GetMemoryResource(MemTag::UI)};
		i32 _width = 0;
		i32 _height = 0;
		i32 _stride = 0;
		u32 _maskIndex = INVALID_VECTOR_INDEX;

		i32 _minX = 0, _maxX = 0, _minY = 0, _maxY = 0;
		bool _dirty = false;

		Vec2 _pen{};
		Vec2 _start{};

		void Begin(i32 width, i32 height) {
			_width = width;
			_height = height;
			_stride = width + 2;
			const size_t accumSize = static_cast<size_t>(_stride) * height;
			if (_accum.size() < accumSize) {
				_accum.resize(accumSize, 0.0f);
			}
			std::fill_n(_accum.data(), accumSize, 0.0f);
			const size_t pixels = static_cast<size_t>(width) * height;
			_composite.assign(pixels * 4, 0.0f);
			ResetSpan();
		}

		void ResetSpan() {
			_minX = _width + 1;
			_maxX = 0;
			_minY = _height;
			_maxY = 0;
			_dirty = false;
		}

		void AddPath(std::span<const VectorVerb> verbs, std::span<const Vec2> points, f32 scale) {
			size_t at = 0;
			bool open = false;
			for (VectorVerb verb : verbs) {
				switch (verb) {
				case VectorVerb::Move:
					if (open) {
						AddLine(_pen, _start);
					}
					_pen = points[at++] * scale;
					_start = _pen;
					open = true;
					break;
				case VectorVerb::Line: {
					Vec2 to = points[at++] * scale;
					AddLine(_pen, to);
					_pen = to;
					break;
				}
				case VectorVerb::Cubic: {
					Vec2 c0 = points[at++] * scale;
					Vec2 c1 = points[at++] * scale;
					Vec2 to = points[at++] * scale;
					AddCubic(_pen, c0, c1, to);
					_pen = to;
					break;
				}
				case VectorVerb::Close:
					if (open) {
						AddLine(_pen, _start);
						_pen = _start;
						open = false;
					}
					break;
				}
			}
			if (open) {
				AddLine(_pen, _start);
			}
		}

		void AddCubic(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3) {
			const Vec2 d0 = p0 - p1 * 2.0f + p2;
			const Vec2 d1 = p1 - p2 * 2.0f + p3;
			const f32 deviation = std::max(d0.Length(), d1.Length());
			i32 segments = 1;
			if (deviation > 0.0f) {
				segments = static_cast<i32>(std::ceil(std::sqrt(3.0f * deviation / (4.0f * FLATNESS_PX))));
				segments = std::clamp(segments, 1, MAX_CUBIC_SEGMENTS);
			}
			Vec2 previous = p0;
			for (i32 i = 1; i <= segments; ++i) {
				const f32 t = static_cast<f32>(i) / static_cast<f32>(segments);
				const f32 u = 1.0f - t;
				const Vec2 point = p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
				AddLine(previous, point);
				previous = point;
			}
		}

		void AddLine(Vec2 from, Vec2 to) {
			if (from.y == to.y) {
				return;
			}
			f32 direction = 1.0f;
			if (from.y > to.y) {
				std::swap(from, to);
				direction = -1.0f;
			}
			const f32 heightF = static_cast<f32>(_height);
			if (to.y <= 0.0f || from.y >= heightF) {
				return;
			}

			const f32 dxdy = (to.x - from.x) / (to.y - from.y);
			f32 x = from.x;
			f32 yTop = from.y;
			if (yTop < 0.0f) {
				x -= yTop * dxdy;
				yTop = 0.0f;
			}
			const f32 yBottom = std::min(to.y, heightF);
			const i32 firstRow = static_cast<i32>(std::floor(yTop));
			const i32 lastRow = std::min(_height, static_cast<i32>(std::ceil(yBottom)));
			const f32 widthF = static_cast<f32>(_width);

			for (i32 y = firstRow; y < lastRow; ++y) {
				const f32 rowTop = std::max(static_cast<f32>(y), yTop);
				const f32 rowBottom = std::min(static_cast<f32>(y + 1), yBottom);
				const f32 dy = rowBottom - rowTop;
				if (dy <= 0.0f) {
					continue;
				}
				const f32 xNext = x + dxdy * dy;
				const f32 d = dy * direction;

				f32 x0 = std::min(x, xNext);
				f32 x1 = std::max(x, xNext);
				x0 = std::clamp(x0, 0.0f, widthF);
				x1 = std::clamp(x1, 0.0f, widthF);

				f32* row = _accum.data() + static_cast<size_t>(y) * _stride;
				const f32 x0floor = std::floor(x0);
				const i32 x0i = static_cast<i32>(x0floor);
				const f32 x1ceil = std::ceil(x1);
				const i32 x1i = static_cast<i32>(x1ceil);

				if (x1i <= x0i + 1) {
					const f32 midFraction = 0.5f * (x0 + x1) - x0floor;
					row[x0i] += d - d * midFraction;
					row[x0i + 1] += d * midFraction;
				} else {
					const f32 slope = 1.0f / (x1 - x0);
					const f32 x0f = x0 - x0floor;
					const f32 areaFirst = 0.5f * slope * (1.0f - x0f) * (1.0f - x0f);
					const f32 x1f = x1 - x1ceil + 1.0f;
					const f32 areaLast = 0.5f * slope * x1f * x1f;
					row[x0i] += d * areaFirst;
					if (x1i == x0i + 2) {
						row[x0i + 1] += d * (1.0f - areaFirst - areaLast);
					} else {
						const f32 areaSecond = slope * (1.5f - x0f);
						row[x0i + 1] += d * (areaSecond - areaFirst);
						for (i32 column = x0i + 2; column < x1i - 1; ++column) {
							row[column] += d * slope;
						}
						const f32 areaBeforeLast = areaSecond + static_cast<f32>(x1i - x0i - 3) * slope;
						row[x1i - 1] += d * (1.0f - areaBeforeLast - areaLast);
					}
					row[x1i] += d * areaLast;
				}

				_minX = std::min(_minX, x0i);
				_maxX = std::max(_maxX, std::min(std::max(x1i, x0i + 1), _stride - 1));
				_minY = std::min(_minY, y);
				_maxY = std::max(_maxY, y);
				_dirty = true;
				x = xNext;
			}
		}

		void SweepInto(f32* dst, Color color, const f32* mask) {
			if (!_dirty) {
				return;
			}
			const i32 minX = std::max(0, _minX);
			const i32 maxX = std::min(_maxX, _stride - 1);
			for (i32 y = _minY; y <= _maxY; ++y) {
				f32* row = _accum.data() + static_cast<size_t>(y) * _stride;
				f32 accumulated = 0.0f;
				for (i32 x = minX; x <= maxX; ++x) {
					accumulated += row[x];
					row[x] = 0.0f;
					if (x >= _width) {
						continue;
					}
					f32 coverage = std::min(std::abs(accumulated), 1.0f);
					if (coverage <= 0.0f) {
						continue;
					}
					const size_t pixel = static_cast<size_t>(y) * _width + x;
					if (mask) {
						coverage *= mask[pixel];
						if (coverage <= 0.0f) {
							continue;
						}
					}
					const f32 sourceAlpha = coverage * color.a;
					const f32 inverse = 1.0f - sourceAlpha;
					f32* texel = dst + pixel * 4;
					texel[0] = color.r * sourceAlpha + texel[0] * inverse;
					texel[1] = color.g * sourceAlpha + texel[1] * inverse;
					texel[2] = color.b * sourceAlpha + texel[2] * inverse;
					texel[3] = sourceAlpha + texel[3] * inverse;
				}
			}
			ResetSpan();
		}

		const f32* EnsureMask(const VectorAsset& asset, u32 maskIndex, f32 scale) {
			if (_maskIndex == maskIndex) {
				return _mask.data();
			}
			const VectorMask& mask = asset.masks[maskIndex];
			const size_t pixels = static_cast<size_t>(_width) * _height;
			_mask.assign(pixels, 0.0f);
			_maskIndex = maskIndex;

			for (u32 i = 0; i < mask.shapeCount; ++i) {
				const VectorMaskShape& shape = asset.maskShapes[mask.firstShape + i];
				AddPath({asset.maskVerbs.data() + shape.firstVerb, shape.verbCount},
						{asset.maskPoints.data() + shape.firstPoint, shape.pointCount}, scale);
				SweepMask(shape.color);
			}
			return _mask.data();
		}

		void SweepMask(Color color) {
			if (!_dirty) {
				return;
			}
			const f32 luminance = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
			const i32 minX = std::max(0, _minX);
			const i32 maxX = std::min(_maxX, _stride - 1);
			for (i32 y = _minY; y <= _maxY; ++y) {
				f32* row = _accum.data() + static_cast<size_t>(y) * _stride;
				f32 accumulated = 0.0f;
				for (i32 x = minX; x <= maxX; ++x) {
					accumulated += row[x];
					row[x] = 0.0f;
					if (x >= _width) {
						continue;
					}
					const f32 coverage = std::min(std::abs(accumulated), 1.0f);
					if (coverage <= 0.0f) {
						continue;
					}
					const f32 sourceAlpha = coverage * color.a;
					f32& value = _mask[static_cast<size_t>(y) * _width + x];
					value = luminance * sourceAlpha + value * (1.0f - sourceAlpha);
				}
			}
			ResetSpan();
		}

		void ResolvePixels(Vector<u8>& outPixels) const {
			const size_t count = static_cast<size_t>(_width) * _height * 4;
			outPixels.resize(count);
			for (size_t i = 0; i < count; ++i) {
				outPixels[i] = static_cast<u8>(std::clamp(_composite[i], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
		}
	};

} // namespace Shalltear::UI
