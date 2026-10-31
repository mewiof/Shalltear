#include "UI/UIInternal.h"

#include "Shalltear/Core/Time.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::UI {

	namespace {

		constexpr Radii ZeroRadii{};

		constexpr f32 ROTATED_COVERAGE_MARGIN_PX = 1.0f;

		Radii ScaleRadii(const Radii& radii, f32 scale) {
			return {radii.topLeft * scale, radii.topRight * scale, radii.bottomRight * scale, radii.bottomLeft * scale};
		}

		Radii RadiiReachedBy(const Rect& region, const Radii& radii, const Rect& narrowed) {
			auto reaches = [&](f32 radius, f32 x, f32 y) {
				if (radius <= 0.0f) {
					return 0.0f;
				}
				Rect overlap = RectIntersect(Rect{x, y, radius, radius}, narrowed);
				return overlap.w > 0.0f && overlap.h > 0.0f ? radius : 0.0f;
			};
			f32 right = region.x + region.w;
			f32 bottom = region.y + region.h;
			return {
				reaches(radii.topLeft, region.x, region.y),
				reaches(radii.topRight, right - radii.topRight, region.y),
				reaches(radii.bottomRight, right - radii.bottomRight, bottom - radii.bottomRight),
				reaches(radii.bottomLeft, region.x, bottom - radii.bottomLeft)};
		}

		f32 SnapEdge(f32 logical, f32 scale) {
			return logical <= 0.0f ? 0.0f : SnapLengthToPixels(logical, scale);
		}

		Edges SnapEdges(const Edges& edges, f32 scale) {
			return {
				SnapEdge(edges.left, scale),
				SnapEdge(edges.top, scale),
				SnapEdge(edges.right, scale),
				SnapEdge(edges.bottom, scale)};
		}

		f32 SnapOffset(f32 logical, f32 scale) {
			return SnapLengthToPixels(logical, scale);
		}

		Rect ContextRect(const ContextImpl& ctx) {
			return {0.0f, 0.0f, static_cast<f32>(ctx.desc.sizePx.x), static_cast<f32>(ctx.desc.sizePx.y)};
		}

		constexpr i32 SHADOW_ATLAS_MAX_PX = 2048;
		constexpr i32 SHADOW_ATLAS_STEP_PX = 128;
		constexpr f32 MAX_SHADOW_BLUR_PX = 64.0f;

		i32 ShadowMarginPx(f32 blurPx) {
			return static_cast<i32>(std::ceil(0.5f * (1.0f + blurPx))) + 1;
		}

		i32 RoundUpToStep(i32 value) {
			return std::max(SHADOW_ATLAS_STEP_PX, (value + SHADOW_ATLAS_STEP_PX - 1) / SHADOW_ATLAS_STEP_PX * SHADOW_ATLAS_STEP_PX);
		}

		struct PaintState {
			ContextImpl* ctx = nullptr;
			RenderList* list = nullptr;
			f32 scale = 1.0f;
			Rect ink{};
			Rect maskRect{};
			Radii maskRadii{};
			bool hasMask = false;
			i32 shelfX = 0;
			i32 shelfY = 0;
			i32 shelfHeight = 0;
			Vec2I atlasUsed{};
		};

		RectI PackShadowSlot(PaintState& state, Vec2I size) {
			if (size.x > SHADOW_ATLAS_MAX_PX || size.y > SHADOW_ATLAS_MAX_PX) {
				return {};
			}
			if (state.shelfX + size.x > SHADOW_ATLAS_MAX_PX) {
				state.shelfY += state.shelfHeight;
				state.shelfX = 0;
				state.shelfHeight = 0;
			}
			if (state.shelfY + size.y > SHADOW_ATLAS_MAX_PX) {
				return {};
			}
			RectI slot{state.shelfX, state.shelfY, size.x, size.y};
			state.shelfX += size.x;
			state.shelfHeight = std::max(state.shelfHeight, size.y);
			state.atlasUsed.x = std::max(state.atlasUsed.x, slot.Right());
			state.atlasUsed.y = std::max(state.atlasUsed.y, slot.Bottom());
			return slot;
		}

		Radii MaskSuppliedArcs(const PaintState& state, const Rect& box, const Radii& radii) {
			if (!state.hasMask) {
				return ZeroRadii;
			}
			auto same = [](f32 a, f32 b) { return std::abs(a - b) < 0.01f; };
			auto corner = [&](f32 radius, f32 maskRadius, f32 x, f32 maskX, f32 y, f32 maskY) {
				return radius > 0.0f && same(radius, maskRadius) && same(x, maskX) && same(y, maskY) ? radius : 0.0f;
			};
			const Rect& mask = state.maskRect;
			const f32 right = box.x + box.w;
			const f32 bottom = box.y + box.h;
			const f32 maskRight = mask.x + mask.w;
			const f32 maskBottom = mask.y + mask.h;
			return {corner(radii.topLeft, state.maskRadii.topLeft, box.x, mask.x, box.y, mask.y),
					corner(radii.topRight, state.maskRadii.topRight, right, maskRight, box.y, mask.y),
					corner(radii.bottomRight, state.maskRadii.bottomRight, right, maskRight, bottom, maskBottom),
					corner(radii.bottomLeft, state.maskRadii.bottomLeft, box.x, mask.x, bottom, maskBottom)};
		}

		Radii RadiiMinus(const Radii& radii, const Radii& supplied) {
			return {supplied.topLeft > 0.0f ? 0.0f : radii.topLeft,
					supplied.topRight > 0.0f ? 0.0f : radii.topRight,
					supplied.bottomRight > 0.0f ? 0.0f : radii.bottomRight,
					supplied.bottomLeft > 0.0f ? 0.0f : radii.bottomLeft};
		}

		void PushQuad(RenderList& list, const UIQuad& quad, RHI::TextureHandle texture, SamplerKind sampler, i32 z) {
			u32 quadIndex = static_cast<u32>(list.quads.size());
			list.quads.push_back(quad);

			if (!list.commands.empty()) {
				UICommand& last = list.commands.back();
				if (last.kind == UICommandKind::Quads &&
					last.texture == texture && last.sampler == sampler && last.z == z &&
					last.firstQuad + last.quadCount == quadIndex) {
					++last.quadCount;
					return;
				}
			}
			UICommand command;
			command.kind = UICommandKind::Quads;
			command.sampler = sampler;
			command.z = z;
			command.texture = texture;
			command.firstQuad = quadIndex;
			command.quadCount = 1;
			list.commands.push_back(command);
		}

		struct EmitParams {
			Rect rect{};
			Color colorSRGB = Color::White();
			Color color1SRGB = Color::White();
			bool gradient = false;
			Vec2 gradientDirection{1.0f, 0.0f};
			RHI::TextureHandle texture{};
			Rect uv{};
			SamplerKind sampler = SamplerKind::LinearClamp;
			Radii radii{};
			f32 softness = 0.0f;
			// rads clockwise about center
			f32 rotation = 0.0f;
			bool dither = false;
			bool glyph = false;
			bool premultiplied = false;
			bool snap = true;
			const Rect* visualRect = nullptr;
			const Rect* innerRect = nullptr;
			Radii innerRadii{};
		};

		Rect RotatedBounds(const Rect& rect, f32 rotation) {
			if (rotation == 0.0f) {
				return rect;
			}
			const f32 absCos = std::abs(std::cos(rotation));
			const f32 absSin = std::abs(std::sin(rotation));
			const f32 width = rect.w * absCos + rect.h * absSin;
			const f32 height = rect.w * absSin + rect.h * absCos;
			return {rect.x + (rect.w - width) * 0.5f, rect.y + (rect.h - height) * 0.5f, width, height};
		}

		void EmitRect(RenderList& list, Rect& ink, const EmitParams& params, f32 opacity, i32 z,
					  const Rect& clipRect, const Radii& clipRadii) {
			UIQuad quad;
			quad.rect = params.snap ? SnapRect(params.rect) : params.rect;
			quad.uv = params.uv;

			if (params.rotation != 0.0f) {
				SLT_CORE_ASSERT_MSG(!params.visualRect && !params.innerRect,
									"a turned quad already spends uv on its shape box (can't also blur or ring)");
				quad.uv = quad.rect;
				quad.rect = {quad.rect.x - ROTATED_COVERAGE_MARGIN_PX, quad.rect.y - ROTATED_COVERAGE_MARGIN_PX,
							 quad.rect.w + ROTATED_COVERAGE_MARGIN_PX * 2.0f,
							 quad.rect.h + ROTATED_COVERAGE_MARGIN_PX * 2.0f};
			}

			ink = RectUnion(ink, RotatedBounds(quad.rect, params.rotation));

			Color color0 = params.colorSRGB;
			color0.a *= opacity;
			quad.color0 = color0;
			if (params.gradient) {
				Color color1 = params.color1SRGB;
				color1.a *= opacity;
				quad.color1 = color1;
			} else {
				quad.color1 = color0;
			}

			quad.radii = params.radii;
			quad.clipRect = clipRect;
			quad.clipRadii = clipRadii;
			quad.softness = params.softness;
			quad.rotation = params.rotation;
			quad.flags = 0;
			if (params.rotation != 0.0f) {
				quad.flags |= QUAD_FLAG_ROTATED | QUAD_FLAG_SDF_FROM_UV;
			}
			if (params.glyph) {
				quad.flags |= QUAD_FLAG_GLYPH;
			}
			if (params.premultiplied) {
				quad.flags |= QUAD_FLAG_PREMULTIPLIED;
			}
			if (params.dither) {
				quad.flags |= QUAD_FLAG_DITHER;
			}
			if (params.gradient && std::abs(params.gradientDirection.y) > std::abs(params.gradientDirection.x)) {
				quad.flags |= QUAD_FLAG_GRADIENT_VERTICAL;
			}
			if (params.visualRect) {
				quad.uv = {params.visualRect->x, params.visualRect->y, params.visualRect->w, params.visualRect->h};
				quad.flags |= QUAD_FLAG_SDF_FROM_UV;
			} else if (params.innerRect && params.innerRect->w > 0.0f && params.innerRect->h > 0.0f) {
				quad.uv = *params.innerRect;
				quad.flags |= QUAD_FLAG_RING;
				quad.color1 = Color(params.innerRadii.topLeft, params.innerRadii.topRight,
									params.innerRadii.bottomRight, params.innerRadii.bottomLeft);
			}
			SLT_CORE_ASSERT_MSG(!(params.visualRect && params.innerRect), "a quad can't be both a blur and a ring (uv holds 1 box)");
			SLT_CORE_ASSERT_MSG(!(params.gradient && params.innerRect), "a quad can't be both a gradient and a ring (color1 holds 1 of them)");

			SamplerKind sampler = params.glyph ? SamplerKind::NearestClamp : params.sampler;
			PushQuad(list, quad, params.texture, sampler, z);
		}

		void EmitRingQuad(RenderList& list, Rect& ink, const Rect& bounds, const Radii& radii, const Edges& widthPx,
						  const Radii& innerRadii, Color colorSRGB, f32 opacity, i32 z,
						  const Rect& clipRect, const Radii& clipRadii) {
			if (widthPx == Edges{} || bounds.w <= 0.0f || bounds.h <= 0.0f) {
				return;
			}
			const Rect inner = DeflateRect(bounds, widthPx);

			EmitParams params;
			params.rect = bounds;
			params.snap = false;
			params.colorSRGB = colorSRGB;
			params.radii = radii;
			params.innerRect = &inner;
			params.innerRadii = innerRadii;
			EmitRect(list, ink, params, opacity, z, clipRect, clipRadii);
		}

	} // anonymous namespace

	namespace {
		void BuildElement(PaintState& state, u32 index, Rect clipRect, Radii clipRadii, f32 opacity, i32 inheritedZ);
	}

	struct PaintScopeAccess {
		static PaintScope Make(RenderList* list, Rect* ink, const Rect& clipRect, const Radii& clipRadii, f32 opacity, i32 z) {
			PaintScope scope;
			scope._list = list;
			scope._ink = ink;
			scope._clipRect = clipRect;
			scope._clipRadii = clipRadii;
			scope._opacity = opacity;
			scope._z = z;
			return scope;
		}
	};

	void PaintScope::EmitQuad(const Rect& rect, Color colorSRGB, const Radii& radii, f32 softness) {
		EmitParams params;
		params.rect = rect;
		params.colorSRGB = colorSRGB;
		params.radii = radii;
		params.softness = softness;
		EmitRect(*_list, *_ink, params, _opacity, _z, _clipRect, _clipRadii);
	}

	void PaintScope::EmitTexturedQuad(const Rect& rect, RHI::TextureHandle texture, const Rect& uv, Color tintSRGB,
									  SamplerKind sampler, const Radii& radii, TexelEncoding encoding) {
		EmitParams params;
		params.rect = rect;
		params.colorSRGB = tintSRGB;
		params.texture = texture;
		params.uv = uv;
		params.sampler = sampler;
		params.radii = radii;
		params.glyph = encoding == TexelEncoding::Coverage;
		params.premultiplied = encoding == TexelEncoding::Premultiplied;
		EmitRect(*_list, *_ink, params, _opacity, _z, _clipRect, _clipRadii);
	}

	void PaintScope::EmitLine(Vec2 a, Vec2 b, f32 widthPx, Color colorSRGB, LineCap cap) {
		const f32 dx = b.x - a.x;
		const f32 dy = b.y - a.y;
		const f32 length = std::sqrt(dx * dx + dy * dy);
		if (!(length > 0.0f) || !(widthPx > 0.0f)) {
			return;
		}

		const bool round = cap == LineCap::Round;
		const f32 boxLength = round ? length + widthPx : length;
		const f32 halfWidth = widthPx * 0.5f;

		EmitParams params;
		params.rect = {(a.x + b.x) * 0.5f - boxLength * 0.5f, (a.y + b.y) * 0.5f - halfWidth, boxLength, widthPx};
		params.snap = false;
		params.colorSRGB = colorSRGB;
		params.radii = round ? Radii::All(halfWidth) : ZeroRadii;
		params.rotation = std::atan2(dy, dx);
		EmitRect(*_list, *_ink, params, _opacity, _z, _clipRect, _clipRadii);
	}

	void PaintScope::EmitRing(const Rect& rect, const Edges& widthsPx, Color colorSRGB, const Radii& radii) {
		EmitRingQuad(*_list, *_ink, SnapRect(rect), radii, widthsPx, DeflateRadii(radii, widthsPx),
					 colorSRGB, _opacity, _z, _clipRect, _clipRadii);
	}

	namespace {

		void EmitOutsetShadows(PaintState& state, u32 index, const Rect& bounds, f32 opacity, i32 z,
							   const Rect& clipRect, const Radii& clipRadii) {
			const Style& style = state.ctx->ComputedS(index);
			const Element& element = state.ctx->E(index);
			const f32 scale = state.scale;
			Radii physicalRadii = ScaleRadii(style.borderRadius, scale);

			for (u8 i = 0; i < style.boxShadowCount; ++i) {
				const BoxShadow& shadow = style.boxShadows[i];
				if (shadow.inset || shadow.color.a <= 0.0f) {
					continue;
				}
				f32 blur = std::max(0.0f, shadow.blur * scale);
				f32 spread = shadow.spread * scale;
				Rect shadowRect = {
					bounds.x + shadow.x * scale - spread,
					bounds.y + shadow.y * scale - spread,
					bounds.w + 2.0f * spread,
					bounds.h + 2.0f * spread};
				Rect snapped = SnapRect(shadowRect);
				Radii shadowRadii = {
					physicalRadii.topLeft > 0.0f ? physicalRadii.topLeft + spread : 0.0f,
					physicalRadii.topRight > 0.0f ? physicalRadii.topRight + spread : 0.0f,
					physicalRadii.bottomRight > 0.0f ? physicalRadii.bottomRight + spread : 0.0f,
					physicalRadii.bottomLeft > 0.0f ? physicalRadii.bottomLeft + spread : 0.0f};

				EmitParams params;
				params.rect = {snapped.x - blur, snapped.y - blur, snapped.w + 2.0f * blur, snapped.h + 2.0f * blur};
				params.visualRect = &snapped;
				params.colorSRGB = shadow.color * Color(element.tint.r, element.tint.g, element.tint.b, element.tint.a);
				params.radii = shadowRadii;
				params.softness = blur;
				params.dither = blur > 0.0f;
				params.snap = false;
				EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
			}
		}

		void EmitBackgroundAndInsetShadows(PaintState& state, u32 index, const Rect& bounds, f32 opacity, i32 z,
										   const Rect& clipRect, const Radii& clipRadii, const Radii& outerRadii) {
			const Style& style = state.ctx->ComputedS(index);
			const Element& element = state.ctx->E(index);
			const f32 scale = state.scale;
			const Radii physicalRadii = ScaleRadii(style.borderRadius, scale);

			switch (style.background.type) {
			case BrushType::Solid: {
				EmitParams params;
				params.rect = bounds;
				params.snap = false;
				params.colorSRGB = style.background.solid * element.tint;
				params.radii = outerRadii;
				EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
				break;
			}
			case BrushType::Image: {
				EmitParams params;
				params.rect = bounds;
				params.snap = false;
				params.colorSRGB = element.tint;
				params.texture = style.background.image;
				params.uv = {0.0f, 0.0f, 1.0f, 1.0f};
				params.sampler = style.background.sampler;
				params.radii = outerRadii;
				EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
				break;
			}
			case BrushType::LinearGradient: {
				EmitParams params;
				params.rect = bounds;
				params.snap = false;
				params.colorSRGB = style.background.gradient0;
				params.color1SRGB = style.background.gradient1;
				params.gradient = true;
				params.gradientDirection = style.background.gradientDirection;
				params.radii = outerRadii;
				params.dither = true;
				EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
				break;
			}
			default:
				break;
			}

			for (u8 i = 0; i < style.boxShadowCount; ++i) {
				const BoxShadow& shadow = style.boxShadows[i];
				if (!shadow.inset || shadow.color.a <= 0.0f) {
					continue;
				}
				const f32 spread = SnapEdge(shadow.spread, scale);
				const f32 offX = SnapOffset(shadow.x, scale);
				const f32 offY = SnapOffset(shadow.y, scale);
				const Edges insets{offX + spread, offY + spread, spread - offX, spread - offY};
				Color color = shadow.color * Color(element.tint.r, element.tint.g, element.tint.b, element.tint.a);
				EmitRingQuad(*state.list, state.ink, bounds, outerRadii, insets,
							 DeflateRadii(physicalRadii, Edges::All(spread)),
							 color, opacity, z, clipRect, clipRadii);
			}
		}

		void EmitBorder(PaintState& state, u32 index, const Rect& bounds, f32 opacity, i32 z,
						const Rect& clipRect, const Radii& clipRadii, const Radii& outerRadii) {
			const Style& style = state.ctx->ComputedS(index);
			const Edges width = SnapEdges(style.borderWidth, state.scale);
			if (width == Edges{}) {
				return;
			}
			const Radii radii = ScaleRadii(style.borderRadius, state.scale);
			EmitRingQuad(*state.list, state.ink, bounds, outerRadii, width,
						 DeflateRadii(radii, width),
						 style.borderColor, opacity, z, clipRect, clipRadii);
		}

		void EmitText(PaintState& state, u32 index, f32 opacity, i32 z,
					  const Rect& clipRect, const Radii& clipRadii) {
			ContextImpl& ctx = *state.ctx;
			TextComponent* text = ctx.GetText(index);
			if (!text || text->utf8.empty() || !ctx.fontSystem) {
				return;
			}
			ctx.EnsureShaped(index);
			const ShapedRun& run = text->shaped;
			if (run.glyphs.empty() && run.outlineGlyphs.empty() &&
				(index != ctx.focused || ctx.E(index).textInputIndex == INVALID_UI_INDEX)) {
				return;
			}

			Element& element = ctx.E(index);
			const Style& style = ctx.ComputedS(index);
			bool isInput = element.textInputIndex != INVALID_UI_INDEX;

			const Vec2 origin = ctx.TextOriginPx(index);
			const f32 dx = origin.x;
			const f32 dy = origin.y;

			if (isInput && index == ctx.focused) {
				TextInputState* input = ctx.GetTextInput(index);
				if (input) {
					const f32 lineHeight = run.lineHeight > 0.0f ? run.lineHeight : run.extent.y;
					const f32 band = run.multiline ? lineHeight : std::max(run.extent.y, run.lineHeight);
					const CaretStop at = run.StopForByte(static_cast<u32>(input->cursor));
					const f32 caretX = at.x;
					if (input->cursor != input->anchor) {
						const CaretStop a = run.StopForByte(static_cast<u32>(std::min(input->cursor, input->anchor)));
						const CaretStop b = run.StopForByte(static_cast<u32>(std::max(input->cursor, input->anchor)));
						for (u32 line = a.line; line <= b.line; ++line) {
							f32 lineEnd = 0.0f;
							for (const CaretStop& stop : run.caretStops) {
								if (stop.line == line) {
									lineEnd = std::max(lineEnd, stop.x);
								}
							}
							const f32 x0 = line == a.line ? a.x : 0.0f;
							const f32 x1 = line == b.line ? b.x : lineEnd;
							if (x1 <= x0) {
								continue;
							}
							EmitParams selection;
							selection.rect = {dx + x0, dy + static_cast<f32>(line) * lineHeight, x1 - x0, band};
							selection.colorSRGB = Color(0.2f, 0.4f, 0.8f, 0.5f);
							EmitRect(*state.list, state.ink, selection, opacity, z, clipRect, clipRadii);
						}
					}
					const f64 sinceMove = Time::GetRealtimeSinceStartup() - ctx.caretBlinkStart;
					if (input->cursor == input->anchor && std::fmod(sinceMove, 1.0) < 0.5) {
						EmitParams caret;
						caret.rect = {Snap(dx + caretX), dy + static_cast<f32>(at.line) * lineHeight, std::max(1.0f, Snap(state.scale)),
									  band};
						caret.colorSRGB = text->color;
						caret.snap = false;
						EmitRect(*state.list, state.ink, caret, opacity, z, clipRect, clipRadii);
					}
				}
			}

			auto emitGlyphs = [&](const Vector<ShapedGlyph>& glyphs, Color colorSRGB) {
				if (glyphs.empty()) {
					return;
				}
				Color color = colorSRGB * element.tint;
				for (const ShapedGlyph& glyph : glyphs) {
					EmitParams params;
					params.rect = {glyph.rect.x + dx, glyph.rect.y + dy, glyph.rect.w, glyph.rect.h};
					params.snap = false;
					params.uv = glyph.uv;
					params.texture = run.atlas;
					params.colorSRGB = color;
					params.glyph = true;
					EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
				}
			};

			if (!run.outlineGlyphs.empty()) {
				emitGlyphs(run.outlineGlyphs, style.textOutlineColor);
			}
			emitGlyphs(run.glyphs, text->color);

			if (isInput && index == ctx.focused) {
				const TextInputState* input = ctx.GetTextInput(index);
				const i32 end = static_cast<i32>(text->utf8.size());
				if (input && !input->completion.empty() && input->cursor == end && input->anchor == end) {
					const Color completionColor = input->completionColor;
					const ShapedRun* tail = ctx.ShapeCompletion(index);
					if (tail) {
						const CaretStop at = run.StopForByte(static_cast<u32>(end));
						const f32 lineHeight = run.lineHeight > 0.0f ? run.lineHeight : run.extent.y;
						const f32 x = Snap(dx + at.x);
						const f32 y = dy + static_cast<f32>(at.line) * lineHeight;
						for (const ShapedGlyph& glyph : tail->glyphs) {
							EmitParams params;
							params.rect = {glyph.rect.x + x, glyph.rect.y + y, glyph.rect.w, glyph.rect.h};
							params.snap = false;
							params.uv = glyph.uv;
							params.texture = tail->atlas;
							params.colorSRGB = completionColor * element.tint;
							params.glyph = true;
							EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
						}
					}
				}
			}
		}

		void EmitIcon(PaintState& state, u32 index, f32 opacity, i32 z,
					  const Rect& clipRect, const Radii& clipRadii) {
			ContextImpl& ctx = *state.ctx;
			IconComponent* icon = ctx.GetIcon(index);
			if (!icon || !ctx.iconSystem) {
				return;
			}
			IconQuery query;
			query.asset = icon->asset;
			query.sizePx = icon->sizePx * ctx.desc.contentScale;
			query.palette = icon->palette;
			RasterizedIcon rasterized;
			if (!ctx.iconSystem->GetIcon(query, rasterized) || !rasterized.atlas.IsValid()) {
				return;
			}

			const Style& style = ctx.ComputedS(index);
			const Rect& content = ctx.L(index).content;
			f32 dx = content.x;
			f32 dy = content.y;
			const f32 freeX = std::max(0.0f, content.w - rasterized.sizePx.x);
			if (style.textAlign == Align::Center) {
				dx += freeX * 0.5f;
			} else if (style.textAlign == Align::End) {
				dx += freeX;
			}
			const f32 freeY = std::max(0.0f, content.h - rasterized.sizePx.y);
			if (style.textAlignVertical == Align::Center) {
				dy += freeY * 0.5f;
			} else if (style.textAlignVertical == Align::End) {
				dy += freeY;
			}

			EmitParams params;
			params.rect = {dx, dy, rasterized.sizePx.x, rasterized.sizePx.y};
			params.uv = rasterized.uv;
			params.texture = rasterized.atlas;
			params.sampler = SamplerKind::NearestClamp;
			params.colorSRGB = ctx.E(index).tint;
			params.premultiplied = true;
			EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
		}

		void EmitImage(PaintState& state, u32 index, f32 opacity, i32 z,
					   const Rect& clipRect, const Radii& clipRadii) {
			ContextImpl& ctx = *state.ctx;
			IconComponent* picture = ctx.GetPicture(index);
			if (!picture) {
				return;
			}
			const TextureAsset* image = picture->texture.Get();
			if (!image || !image->texture.IsValid()) {
				return;
			}

			const Vec2 extent = ctx.MeasureIconExtent(index);
			if (extent.x <= 0.0f || extent.y <= 0.0f) {
				return;
			}

			const Style& style = ctx.ComputedS(index);
			const Rect& content = ctx.L(index).content;
			f32 dx = content.x;
			f32 dy = content.y;
			const f32 freeX = std::max(0.0f, content.w - extent.x);
			if (style.textAlign == Align::Center) {
				dx += freeX * 0.5f;
			} else if (style.textAlign == Align::End) {
				dx += freeX;
			}
			const f32 freeY = std::max(0.0f, content.h - extent.y);
			if (style.textAlignVertical == Align::Center) {
				dy += freeY * 0.5f;
			} else if (style.textAlignVertical == Align::End) {
				dy += freeY;
			}

			EmitParams params;
			params.rect = {dx, dy, extent.x, extent.y};
			params.uv = {0.0f, 0.0f, 1.0f, 1.0f};
			params.texture = image->texture;
			params.sampler = image->settings.filter == RHI::FilterMode::Nearest ? SamplerKind::NearestClamp
																				: SamplerKind::LinearClamp;
			params.colorSRGB = ctx.E(index).tint;
			EmitRect(*state.list, state.ink, params, opacity, z, clipRect, clipRadii);
		}

		u32 OpenShadowScope(PaintState& state, i32 z) {
			RenderList& list = *state.list;
			const u32 pushIndex = static_cast<u32>(list.commands.size());
			UICommand push;
			push.kind = UICommandKind::PushShadow;
			push.z = z;
			push.shadowCaster = static_cast<u32>(list.shadowCasters.size());
			list.commands.push_back(push);
			list.shadowCasters.push_back(UIShadowCaster{});

			UIQuad quad;
			quad.color0 = Color(0.0f, 0.0f, 0.0f, 0.0f);
			quad.color1 = quad.color0;
			quad.clipRect = InfiniteClip;
			quad.flags = QUAD_FLAG_GLYPH;
			PushQuad(list, quad, ShadowMaskTexture, SamplerKind::NearestClamp, z);
			return pushIndex;
		}

		void CloseShadowScope(PaintState& state, u32 index, u32 pushIndex, const Rect& clipRect, const Radii& clipRadii,
							  f32 opacity) {
			ContextImpl& ctx = *state.ctx;
			RenderList& list = *state.list;
			UICommand pop;
			pop.kind = UICommandKind::PopShadow;
			pop.z = list.commands[pushIndex].z;
			list.commands.push_back(pop);

			const Rect seen = RectIntersect(RectIntersect(state.ink, clipRect), ContextRect(ctx));
			if (seen.w <= 0.0f || seen.h <= 0.0f) {
				return;
			}
			const i32 x0 = static_cast<i32>(std::floor(seen.x));
			const i32 y0 = static_cast<i32>(std::floor(seen.y));
			const i32 x1 = static_cast<i32>(std::ceil(seen.x + seen.w));
			const i32 y1 = static_cast<i32>(std::ceil(seen.y + seen.h));

			const DropShadow& drop = ctx.ComputedS(index).dropShadow;
			const f32 blur = std::clamp(drop.blur * state.scale, 0.0f, MAX_SHADOW_BLUR_PX);
			const i32 margin = ShadowMarginPx(blur);
			const RectI slot = PackShadowSlot(state, {x1 - x0 + margin * 2, y1 - y0 + margin * 2});
			if (slot.size.x <= 0 || slot.size.y <= 0) {
				static bool warned = false;
				if (!warned) {
					warned = true;
					SLT_LOG_WARN("UI", "drop shadow: the {} px shadow atlas is full (no shadow this frame)",
								 SHADOW_ATLAS_MAX_PX);
				}
				return;
			}

			UIShadowCaster& caster = list.shadowCasters[list.commands[pushIndex].shadowCaster];
			caster.source = {static_cast<f32>(x0), static_cast<f32>(y0), static_cast<f32>(x1 - x0), static_cast<f32>(y1 - y0)};
			caster.slot = slot;
			caster.marginPx = margin;
			caster.blurPx = blur;

			UIQuad& quad = list.quads[list.commands[pushIndex + 1].firstQuad];
			quad.rect = {static_cast<f32>(x0 - margin) + SnapOffset(drop.x, state.scale),
						 static_cast<f32>(y0 - margin) + SnapOffset(drop.y, state.scale),
						 static_cast<f32>(slot.size.x), static_cast<f32>(slot.size.y)};
			quad.uv = {static_cast<f32>(slot.position.x), static_cast<f32>(slot.position.y),
					   static_cast<f32>(slot.size.x), static_cast<f32>(slot.size.y)};
			Color color = drop.color * ctx.E(index).tint;
			color.a *= opacity;
			quad.color0 = color;
			quad.color1 = color;
			quad.clipRect = clipRect;
			quad.clipRadii = clipRadii;
			quad.flags = QUAD_FLAG_GLYPH | (blur > 0.0f ? QUAD_FLAG_DITHER : 0u);
			state.ink = RectUnion(state.ink, quad.rect);
		}

		void BuildElement(PaintState& state, u32 index, Rect clipRect, Radii clipRadii, f32 opacity, i32 inheritedZ) {
			ContextImpl& ctx = *state.ctx;
			Element& element = ctx.E(index);
			const Style& style = ctx.ComputedS(index);
			++ctx.paintStats.elementsVisited;
			if (!HasFlag(element.flags, ElementFlags::Visible) || style.display == Display::None) {
				ctx.Ink(index) = Rect{};
				return;
			}
			++ctx.paintStats.elementsPainted;

			const Rect parentInk = state.ink;

			const Rect drawBounds = SnapRect(ctx.L(index).borderBox);
			state.ink = drawBounds;
			const i32 effectiveZ = inheritedZ + style.zIndex;
			f32 combinedOpacity = opacity * std::clamp(style.opacity, 0.0f, 1.0f);

			Radii physicalRadii = ScaleRadii(style.borderRadius, state.scale);
			bool shouldClip = style.overflow != Overflow::Visible;
			bool hasRadius = !physicalRadii.IsZero();
			bool hasChildren = element.firstChild != INVALID_UI_INDEX;
			auto eventsIt = ctx.events.find(index);
			bool hasCustomDraw = eventsIt != ctx.events.end() && eventsIt->second.customDraw != nullptr;
			bool hasContent = hasChildren || element.textIndex != INVALID_UI_INDEX || hasCustomDraw ||
							  HasAnyFlag(element.flags, ELEMENT_HAS_PICTURE);

			bool opacityLayer = style.opacity < 1.0f && hasChildren;
			Radii maskSupplied = hasRadius ? MaskSuppliedArcs(state, drawBounds, physicalRadii) : ZeroRadii;
			bool maskCoversArcs = hasRadius && RadiiMinus(physicalRadii, maskSupplied).IsZero();
			bool roundedClipLayer = shouldClip && hasRadius && hasContent && !maskCoversArcs;
			bool needsLayer = opacityLayer || roundedClipLayer;

			bool outsetShadowInsideLayer = opacityLayer;
			Radii ownOuterRadii = roundedClipLayer ? ZeroRadii : RadiiMinus(physicalRadii, maskSupplied);

			Rect childClipRect = clipRect;
			Radii childClipRadii = clipRadii;
			if (shouldClip) {
				childClipRect = RectIntersect(clipRect, drawBounds);
				if (needsLayer || maskCoversArcs) {
					childClipRadii = ZeroRadii;
				} else if (hasRadius) {
					childClipRadii = physicalRadii;
				} else {
					childClipRadii = RadiiReachedBy(clipRect, clipRadii, childClipRect);
				}
			}

			bool hasOwnBox = style.background.type != BrushType::None || style.boxShadowCount > 0;
			auto emitOutsetShadows = [&](f32 opacity, const Rect& boxClip, const Radii& boxClipRadii) {
				if (hasOwnBox) {
					EmitOutsetShadows(state, index, drawBounds, opacity, effectiveZ, boxClip, boxClipRadii);
				}
			};
			auto emitBoxItself = [&](f32 opacity, const Rect& boxClip, const Radii& boxClipRadii) {
				if (hasOwnBox) {
					EmitBackgroundAndInsetShadows(state, index, drawBounds, opacity, effectiveZ,
												  boxClip, boxClipRadii, ownOuterRadii);
				}
				EmitBorder(state, index, drawBounds, opacity, effectiveZ, boxClip, boxClipRadii, ownOuterRadii);
			};

			const bool castsShadow = style.dropShadow.color.a > 0.0f;
			const u32 shadowPush = castsShadow ? OpenShadowScope(state, effectiveZ) : 0u;

			if (needsLayer && !outsetShadowInsideLayer) {
				emitOutsetShadows(combinedOpacity, clipRect, clipRadii);
			}

			Rect drawClipRect;
			Radii drawClipRadii;
			f32 drawOpacity;
			if (needsLayer) {
				childClipRect = RectIntersect(childClipRect, ContextRect(ctx));

				UICommand push;
				push.kind = UICommandKind::PushLayer;
				push.z = effectiveZ;
				push.layerBounds = childClipRect;
				state.list->commands.push_back(push);
				drawClipRect = childClipRect;
				drawClipRadii = ZeroRadii;
				drawOpacity = 1.0f;
				childClipRect = drawClipRect;
			} else {
				drawClipRect = childClipRect;
				drawClipRadii = childClipRadii;
				drawOpacity = combinedOpacity;
			}

			if (!needsLayer || outsetShadowInsideLayer) {
				emitOutsetShadows(drawOpacity, drawClipRect, drawClipRadii);
			}
			emitBoxItself(drawOpacity, drawClipRect, drawClipRadii);

			if (hasCustomDraw) {
				PaintScope scope = PaintScopeAccess::Make(state.list, &state.ink, drawClipRect, drawClipRadii, drawOpacity, effectiveZ);
				eventsIt->second.customDraw(*ctx.owner, ctx.HandleFor(index), scope);
			}

			if (element.textIndex != INVALID_UI_INDEX) {
				EmitText(state, index, drawOpacity, effectiveZ, drawClipRect, drawClipRadii);
			}
			if (HasFlag(element.flags, ElementFlags::HasIcon)) {
				EmitIcon(state, index, drawOpacity, effectiveZ, drawClipRect, drawClipRadii);
			} else if (HasFlag(element.flags, ElementFlags::HasImage)) {
				EmitImage(state, index, drawOpacity, effectiveZ, drawClipRect, drawClipRadii);
			}

			const Rect savedMaskRect = state.maskRect;
			const Radii savedMaskRadii = state.maskRadii;
			const bool savedHasMask = state.hasMask;
			if (roundedClipLayer) {
				state.maskRect = drawClipRect;
				state.maskRadii = RadiiReachedBy(drawBounds, physicalRadii, drawClipRect);
				state.hasMask = true;
			}

			f32 childOpacity = needsLayer ? 1.0f : combinedOpacity;
			for (u32 child = element.firstChild; child != INVALID_UI_INDEX; child = ctx.E(child).nextSibling) {
				const Rect& childInk = ctx.Ink(child);
				if (!InkReachesClip(childInk, childClipRect)) {
					state.ink = RectUnion(state.ink, childInk);
					continue;
				}
				BuildElement(state, child, childClipRect, childClipRadii, childOpacity, effectiveZ);
			}

			state.maskRect = savedMaskRect;
			state.maskRadii = savedMaskRadii;
			state.hasMask = savedHasMask;

			if (needsLayer) {
				UICommand pop;
				pop.kind = UICommandKind::PopLayer;
				pop.z = effectiveZ;
				pop.layerBounds = drawClipRect;
				state.list->commands.push_back(pop);

				UIQuad composite;
				composite.rect = drawClipRect;
				composite.uv = {0.0f, 0.0f, 1.0f, 1.0f};
				composite.color0 = Color(1.0f, 1.0f, 1.0f, combinedOpacity);
				composite.color1 = composite.color0;
				composite.radii = RadiiReachedBy(drawBounds, physicalRadii, drawClipRect);
				composite.clipRect = clipRect;
				composite.clipRadii = clipRadii;
				composite.flags = QUAD_FLAG_PREMULTIPLIED;
				u32 quadIndex = static_cast<u32>(state.list->quads.size());
				state.list->quads.push_back(composite);
				UICommand command;
				command.kind = UICommandKind::Quads;
				command.sampler = SamplerKind::LinearClamp;
				command.z = effectiveZ;
				command.texture = LayerCompositeTexture;
				command.firstQuad = quadIndex;
				command.quadCount = 1;
				state.list->commands.push_back(command);
			}

			if (castsShadow) {
				CloseShadowScope(state, index, shadowPush, clipRect, clipRadii, opacityLayer ? combinedOpacity : 1.0f);
			}

			ctx.Ink(index) = state.ink;
			state.ink = RectUnion(parentInk, state.ink);
		}

		bool OpensScope(UICommandKind kind) {
			return kind == UICommandKind::PushLayer || kind == UICommandKind::PushShadow;
		}

		bool ClosesScope(UICommandKind kind) {
			return kind == UICommandKind::PopLayer || kind == UICommandKind::PopShadow;
		}

		void SortScope(PaintStats& stats, Vector<UICommand>& commands, u32 begin, u32 end) {
			++stats.scopesWalked;
			struct Unit {
				u32 begin;
				u32 end;
				i32 z;
			};
			Vector<Unit> units{GetMemoryResource(MemTag::UI)};
			bool anyDifferentZ = false;

			u32 i = begin;
			while (i < end) {
				if (OpensScope(commands[i].kind)) {
					const bool shadow = commands[i].kind == UICommandKind::PushShadow;
					u32 depth = 1;
					u32 j = i + 1;
					while (j < end && depth > 0) {
						if (OpensScope(commands[j].kind)) {
							++depth;
						} else if (ClosesScope(commands[j].kind)) {
							--depth;
						}
						++j;
					}
					u32 unitEnd = (!shadow && j < end && commands[j].kind == UICommandKind::Quads &&
								   commands[j].texture == LayerCompositeTexture)
									  ? j + 1
									  : j;
					SortScope(stats, commands, shadow ? i + 2 : i + 1, j - 1);
					units.push_back({i, unitEnd, commands[i].z});
					i = unitEnd;
				} else {
					units.push_back({i, i + 1, commands[i].z});
					++i;
				}
				if (units.size() > 1 && units[units.size() - 2].z != units.back().z) {
					anyDifferentZ = true;
				}
			}

			if (!anyDifferentZ) {
				return;
			}
			++stats.scopesSorted;

			std::stable_sort(units.begin(), units.end(), [](const Unit& a, const Unit& b) {
				return a.z < b.z;
			});

			Vector<UICommand> scratch{GetMemoryResource(MemTag::UI)};
			scratch.reserve(end - begin);
			for (const Unit& unit : units) {
				for (u32 c = unit.begin; c < unit.end; ++c) {
					scratch.push_back(commands[c]);
				}
			}
			std::copy(scratch.begin(), scratch.end(), commands.begin() + begin);
		}

	} // anonymous namespace

	namespace Paint {

		void BuildRenderList(ContextImpl& ctx, RenderList& out) {
			out.Clear();
			PaintState state;
			state.ctx = &ctx;
			state.list = &out;
			state.scale = std::max(0.0f, ctx.desc.contentScale);
			BuildElement(state, ctx.root, InfiniteClip, ZeroRadii, 1.0f, 0);

			if (state.atlasUsed.x > 0 && state.atlasUsed.y > 0) {
				out.shadowAtlasSizePx = {RoundUpToStep(state.atlasUsed.x), RoundUpToStep(state.atlasUsed.y)};
				const f32 atlasW = static_cast<f32>(out.shadowAtlasSizePx.x);
				const f32 atlasH = static_cast<f32>(out.shadowAtlasSizePx.y);
				for (size_t i = 0; i + 1 < out.commands.size(); ++i) {
					if (out.commands[i].kind != UICommandKind::PushShadow) {
						continue;
					}
					Rect& uv = out.quads[out.commands[i + 1].firstQuad].uv;
					uv = {uv.x / atlasW, uv.y / atlasH, uv.w / atlasW, uv.h / atlasH};
				}
			}
			SortScope(ctx.paintStats, out.commands, 0, static_cast<u32>(out.commands.size()));
		}

	} // namespace Paint

} // namespace Shalltear::UI
