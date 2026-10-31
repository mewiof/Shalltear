#include "UI/UIInternal.h"
#include "Shalltear/Core/Profiler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Shalltear::UI {

	namespace {

		constexpr f32 INF = LAYOUT_INF;

		enum class MeasureMode : u8 {
			Query,
			Realize,
		};

		struct MeasureResult {
			Vec2 desiredSize{};
			bool autoWidth = true;
			bool autoHeight = true;
		};

		f32 ResolveLength(const Length& length, f32 basis, f32 scale, bool definite) {
			switch (length.unit) {
			case Length::Unit::Pixel:
				return length.value * scale;
			case Length::Unit::Percent:
				return definite ? basis * length.value : NAN;
			case Length::Unit::Auto:
				return NAN;
			}
			return NAN;
		}

		Edges ResolveEdges(const Edges& edges, f32 scale) {
			return SnapEdgesToPixels(edges, scale);
		}

		Align EffectiveAlign(const Style& parent, const Style& child) {
			return child.alignSelf == Align::Auto ? parent.alignItems : child.alignSelf;
		}

		bool Laid(const Element& element, const Style& style) {
			return HasFlag(element.flags, ElementFlags::Visible) && style.display != Display::None;
		}

		bool InFlow(const Element& element, const Style& style) {
			return Laid(element, style) && style.position != Position::Absolute;
		}

		bool CrossStretchOwnsRealization(const Constraint& content, const Style& parentStyle,
										 const Style& childStyle, bool isRow) {
			if (content.DefiniteCross(isRow)) {
				return false;
			}
			const Length& crossLength = isRow ? childStyle.height : childStyle.width;
			return EffectiveAlign(parentStyle, childStyle) == Align::Stretch &&
				   crossLength.unit != Length::Unit::Pixel;
		}

		MeasureResult Measure(ContextImpl& ctx, u32 index, const Constraint& constraint, MeasureMode mode);

		Constraint FlowChildConstraint(const Constraint& content, const Style& parentStyle,
									   const Style& childStyle, const Edges& childMargin, bool isRow) {
			Constraint out = content;
			bool crossAuto = isRow ? childStyle.height.IsAuto() : childStyle.width.IsAuto();
			if (!crossAuto || !content.DefiniteCross(isRow) ||
				EffectiveAlign(parentStyle, childStyle) != Align::Stretch) {
				return out;
			}
			if (isRow) {
				out.available.y = std::max(0.0f, content.available.y - childMargin.top - childMargin.bottom);
				out.forceY = true;
			} else {
				out.available.x = std::max(0.0f, content.available.x - childMargin.left - childMargin.right);
				out.forceX = true;
			}
			return out;
		}

		constexpr f32 SHRINK_EPSILON = 0.5f;

		void ResolveMainClamps(const Style& childStyle, f32 basis, f32 scale, bool isRow, bool definite,
							   f32& outMin, f32& outMax) {
			f32 minMain = ResolveLength(isRow ? childStyle.minWidth : childStyle.minHeight, basis, scale, definite);
			f32 maxMain = ResolveLength(isRow ? childStyle.maxWidth : childStyle.maxHeight, basis, scale, definite);
			outMin = std::isnan(minMain) ? 0.0f : std::max(0.0f, minMain);
			outMax = std::isnan(maxMain) ? INF : std::max(outMin, maxMain);
		}

		f32 FlexBaseSize(ContextImpl& ctx, u32 child, const Style& childStyle, const Constraint& content,
						 f32 scale, bool isRow) {
			f32 styleMain = ResolveLength(isRow ? childStyle.width : childStyle.height, content.Main(isRow), scale,
										  content.DefiniteMain(isRow));
			if (!std::isnan(styleMain)) {
				return styleMain;
			}
			Vec2 intrinsic = Measure(ctx, child, Constraint::Intrinsic(), MeasureMode::Query).desiredSize;
			return isRow ? intrinsic.x : intrinsic.y;
		}

		void ResolveFlexibleLengths(Vector<FlexItem>& items, size_t first, size_t count,
									f32 availableMain, bool growing) {
			for (size_t i = 0; i < count; ++i) {
				FlexItem& item = items[first + i];
				item.target = item.base;
				item.frozen = item.factor <= 0.0f;
			}
			for (size_t round = 0; round <= count; ++round) {
				f32 frozenMain = 0.0f;
				f32 unfrozenBase = 0.0f;
				f32 marginTotal = 0.0f;
				f32 totalFactor = 0.0f;
				for (size_t i = 0; i < count; ++i) {
					const FlexItem& item = items[first + i];
					marginTotal += item.mainMargin;
					if (item.frozen) {
						frozenMain += item.target;
					} else {
						unfrozenBase += item.base;
						totalFactor += item.factor;
					}
				}
				if (totalFactor <= 0.0f) {
					return;
				}
				f32 freeSpace = availableMain - marginTotal - frozenMain - unfrozenBase;
				if ((growing && freeSpace <= SHRINK_EPSILON) ||
					(!growing && freeSpace >= -SHRINK_EPSILON)) {
					return;
				}

				bool froze = false;
				for (size_t i = 0; i < count; ++i) {
					FlexItem& item = items[first + i];
					if (item.frozen) {
						continue;
					}
					f32 candidate = item.base + freeSpace * (item.factor / totalFactor);
					f32 clamped = std::clamp(candidate, item.minMain, item.maxMain);
					item.target = clamped;
					if (clamped != candidate) {
						item.frozen = true;
						froze = true;
					}
				}
				if (!froze) {
					return;
				}
			}
		}

		size_t MeasureChildren(ContextImpl& ctx, u32 index, const Constraint& content, Vec2& outSize,
							   MeasureMode mode) {
			const Style& style = ctx.ComputedS(index);
			const f32 scale = std::max(0.0f, ctx.desc.contentScale);
			const bool isRow = style.flexDirection == FlexDirection::Row;
			const f32 availableMain = content.Main(isRow);
			const bool resolvable = content.DefiniteMain(isRow) && availableMain != INF;
			const bool realizing = mode == MeasureMode::Realize;

			const size_t first = ctx.flexScratch.size();

			for (u32 child = ctx.E(index).firstChild; child != INVALID_UI_INDEX; child = ctx.E(child).nextSibling) {
				const Style& childStyle = ctx.ComputedS(child);
				if (!Laid(ctx.E(child), childStyle)) {
					continue;
				}
				Edges childMargin = ResolveEdges(childStyle.margin, scale);
				if (childStyle.position == Position::Absolute) {
					if (realizing) {
						Measure(ctx, child, content, MeasureMode::Realize);
					}
					continue;
				}

				const bool stretchOwns = CrossStretchOwnsRealization(content, style, childStyle, isRow);

				FlexItem item;
				item.index = child;
				item.mainMargin = isRow ? (childMargin.left + childMargin.right)
										: (childMargin.top + childMargin.bottom);
				ResolveMainClamps(childStyle, availableMain, scale, isRow, content.DefiniteMain(isRow),
								  item.minMain, item.maxMain);
				item.base = std::clamp(FlexBaseSize(ctx, child, childStyle, content, scale, isRow),
									   item.minMain, item.maxMain);
				item.target = item.base;
				item.stretchOwns = realizing && stretchOwns;
				ctx.flexScratch.push_back(item);
			}
			const size_t count = ctx.flexScratch.size() - first;

			if (resolvable && count > 0) {
				f32 outerTotal = 0.0f;
				for (size_t i = 0; i < count; ++i) {
					const FlexItem& item = ctx.flexScratch[first + i];
					outerTotal += item.base + item.mainMargin;
				}
				bool growing = outerTotal < availableMain - SHRINK_EPSILON;
				bool shrinking = outerTotal > availableMain + SHRINK_EPSILON;
				if (growing || shrinking) {
					for (size_t i = 0; i < count; ++i) {
						FlexItem& item = ctx.flexScratch[first + i];
						const Style& childStyle = ctx.ComputedS(item.index);
						item.factor = growing ? std::max(0.0f, childStyle.flexGrow)
											  : std::max(0.0f, childStyle.flexShrink) * item.base;
					}
					ResolveFlexibleLengths(ctx.flexScratch, first, count, availableMain, growing);
				}
			}

			f32 mainSize = 0.0f;
			f32 crossSize = 0.0f;
			for (size_t i = 0; i < count; ++i) {
				u32 child = ctx.flexScratch[first + i].index;
				f32 target = ctx.flexScratch[first + i].target;
				f32 base = ctx.flexScratch[first + i].base;
				const bool stretchOwns = ctx.flexScratch[first + i].stretchOwns;
				const Style& childStyle = ctx.ComputedS(child);
				Edges childMargin = ResolveEdges(childStyle.margin, scale);

				Constraint resolved = FlowChildConstraint(content, style, childStyle, childMargin, isRow);
				const bool moved = std::abs(target - base) > SHRINK_EPSILON;
				if (moved) {
					if (isRow) {
						resolved.available.x = target;
						resolved.definiteX = true;
						resolved.forceX = true;
					} else {
						resolved.available.y = target;
						resolved.definiteY = true;
						resolved.forceY = true;
					}
				}
				ctx.flexScratch[first + i].resolved = resolved;

				const MeasureMode childMode = (realizing && !stretchOwns) ? MeasureMode::Realize : MeasureMode::Query;
				Vec2 childSize = Measure(ctx, child, resolved, childMode).desiredSize;

				f32 childMain = isRow ? (childSize.x + childMargin.left + childMargin.right)
									  : (childSize.y + childMargin.top + childMargin.bottom);
				f32 childCross = isRow ? (childSize.y + childMargin.top + childMargin.bottom)
									   : (childSize.x + childMargin.left + childMargin.right);
				mainSize += childMain;
				crossSize = std::max(crossSize, childCross);
			}

			outSize = isRow ? Vec2{mainSize, crossSize} : Vec2{crossSize, mainSize};
			return count;
		}

		void StretchChildrenIntoCross(ContextImpl& ctx, u32 index, size_t first, size_t count, f32 finalCross) {
			const Style& style = ctx.ComputedS(index);
			const f32 scale = std::max(0.0f, ctx.desc.contentScale);
			const bool isRow = style.flexDirection == FlexDirection::Row;

			for (size_t i = 0; i < count; ++i) {
				if (!ctx.flexScratch[first + i].stretchOwns) {
					continue;
				}
				u32 child = ctx.flexScratch[first + i].index;
				const Style& childStyle = ctx.ComputedS(child);
				Edges childMargin = ResolveEdges(childStyle.margin, scale);
				Constraint stretched = ctx.flexScratch[first + i].resolved;
				if (isRow) {
					stretched.available.y = std::max(0.0f, finalCross - childMargin.top - childMargin.bottom);
					stretched.definiteY = true;
					stretched.forceY = true;
				} else {
					stretched.available.x = std::max(0.0f, finalCross - childMargin.left - childMargin.right);
					stretched.definiteX = true;
					stretched.forceX = true;
				}
				Measure(ctx, child, stretched, MeasureMode::Realize);
			}
		}

		MeasureResult StoreRealization(MeasureCache& cache, const LayoutData& layout, const Constraint& constraint) {
			u8 slot = MeasureCache::NO_SLOT;
			for (u8 i = 0; i < cache.count; ++i) {
				if (cache.entries[i].key == constraint) {
					slot = i;
					break;
				}
			}
			if (slot == MeasureCache::NO_SLOT) {
				slot = cache.next;
				cache.next = static_cast<u8>((cache.next + 1) % MeasureCache::CAPACITY);
				if (cache.count < MeasureCache::CAPACITY) {
					++cache.count;
				}
			}
			MeasureCache::Entry& entry = cache.entries[slot];
			entry.key = constraint;
			entry.desiredSize = layout.desiredSize;
			entry.autoWidth = layout.autoWidth;
			entry.autoHeight = layout.autoHeight;
			cache.realized = slot;
			cache.lastWritten = slot;
			return {entry.desiredSize, entry.autoWidth, entry.autoHeight};
		}

		MeasureResult Measure(ContextImpl& ctx, u32 index, const Constraint& constraint, MeasureMode mode) {
			Element& element = ctx.E(index);
			const Style& style = ctx.ComputedS(index);
			LayoutData& layout = ctx.L(index);
			MeasureCache& cache = ctx.M(index);

			++ctx.layoutStats.measureCalls;

			const bool intrinsic = constraint.IsIntrinsic();
			if (intrinsic && mode == MeasureMode::Query && cache.intrinsicValid) {
				return {cache.intrinsicSize, cache.intrinsicAutoWidth, cache.intrinsicAutoHeight};
			}
			for (u8 i = 0; i < cache.count; ++i) {
				const MeasureCache::Entry& entry = cache.entries[i];
				if (!(entry.key == constraint)) {
					continue;
				}
				if (mode == MeasureMode::Realize && i != cache.realized) {
					break;
				}
				return {entry.desiredSize, entry.autoWidth, entry.autoHeight};
			}
			++ctx.layoutStats.measureWork;

			if (!HasFlag(element.flags, ElementFlags::Visible) || style.display == Display::None) {
				if (mode == MeasureMode::Query) {
					return {};
				}
				layout.desiredSize = {0.0f, 0.0f};
				layout.autoWidth = true;
				layout.autoHeight = true;
				return StoreRealization(cache, layout, constraint);
			}

			const f32 scale = std::max(0.0f, ctx.desc.contentScale);
			Edges padding = ResolveEdges(style.padding, scale);
			Edges border = ResolveEdges(style.borderWidth, scale);
			Edges margin = ResolveEdges(style.margin, scale);

			f32 decoW = padding.left + padding.right + border.left + border.right;
			f32 decoH = padding.top + padding.bottom + border.top + border.bottom;

			const Vec2 availableSize = constraint.available;
			Constraint childConstraint;
			childConstraint.available = {
				availableSize.x == INF ? INF : std::max(0.0f, availableSize.x - decoW - margin.left - margin.right),
				availableSize.y == INF ? INF : std::max(0.0f, availableSize.y - decoH - margin.top - margin.bottom)};

			f32 styleW = ResolveLength(style.width, availableSize.x, scale, constraint.definiteX);
			f32 styleH = ResolveLength(style.height, availableSize.y, scale, constraint.definiteY);
			if (constraint.forceX) {
				styleW = availableSize.x;
			}
			if (constraint.forceY) {
				styleH = availableSize.y;
			}
			const bool autoWidth = std::isnan(styleW);
			const bool autoHeight = std::isnan(styleH);
			childConstraint.definiteX = !autoWidth;
			childConstraint.definiteY = !autoHeight;
			if (!autoWidth) {
				childConstraint.available.x = std::max(0.0f, styleW - decoW);
			}
			if (!autoHeight) {
				childConstraint.available.y = std::max(0.0f, styleH - decoH);
			}

			Vec2 measuredContent{};
			const size_t flexFirst = ctx.flexScratch.size();
			size_t flexCount = 0;
			if (element.textIndex != INVALID_UI_INDEX) {
				f32 wrapWidth = 0.0f;
				if (ctx.EffectiveTextOverflow(index) == TextOverflow::Clip) {
					wrapWidth = 0.0f;
				} else if (!std::isnan(styleW)) {
					wrapWidth = std::max(0.0f, styleW - decoW);
				} else if (!style.maxWidth.IsAuto()) {
					f32 maxW = ResolveLength(style.maxWidth, availableSize.x, scale, constraint.definiteX);
					if (!std::isnan(maxW)) {
						wrapWidth = std::max(0.0f, maxW - decoW);
					}
				} else if (childConstraint.available.x != INF) {
					wrapWidth = childConstraint.available.x;
				}
				measuredContent = ctx.MeasureTextExtent(index, wrapWidth, mode == MeasureMode::Realize);
			} else if (HasAnyFlag(element.flags, ELEMENT_HAS_PICTURE)) {
				measuredContent = ctx.MeasureIconExtent(index);
			} else {
				if (style.overflow == Overflow::Scroll) {
					childConstraint.available.y = INF;
					childConstraint.definiteY = false;
				}
				flexCount = MeasureChildren(ctx, index, childConstraint, measuredContent, mode);
			}

			f32 finalW = autoWidth ? measuredContent.x : styleW - decoW;
			f32 finalH = autoHeight ? measuredContent.y : styleH - decoH;

			if (!constraint.forceX) {
				f32 minW = ResolveLength(style.minWidth, availableSize.x, scale, constraint.definiteX);
				f32 maxW = ResolveLength(style.maxWidth, availableSize.x, scale, constraint.definiteX);
				if (!std::isnan(minW)) {
					finalW = std::max(finalW, minW - decoW);
				}
				if (!std::isnan(maxW)) {
					finalW = std::min(finalW, maxW - decoW);
				}
			}
			if (!constraint.forceY) {
				f32 minH = ResolveLength(style.minHeight, availableSize.y, scale, constraint.definiteY);
				f32 maxH = ResolveLength(style.maxHeight, availableSize.y, scale, constraint.definiteY);
				if (!std::isnan(minH)) {
					finalH = std::max(finalH, minH - decoH);
				}
				if (!std::isnan(maxH)) {
					finalH = std::min(finalH, maxH - decoH);
				}
			}
			finalW = std::max(0.0f, finalW);
			finalH = std::max(0.0f, finalH);

			const MeasureResult result{{finalW + decoW, finalH + decoH}, autoWidth, autoHeight};

			if (mode == MeasureMode::Query) {
				ctx.flexScratch.resize(flexFirst);
				if (intrinsic) {
					cache.intrinsicSize = result.desiredSize;
					cache.intrinsicAutoWidth = result.autoWidth;
					cache.intrinsicAutoHeight = result.autoHeight;
					cache.intrinsicValid = true;
				}
				return result;
			}

			layout.autoWidth = autoWidth;
			layout.autoHeight = autoHeight;
			layout.desiredSize = result.desiredSize;

			bool isRow = style.flexDirection == FlexDirection::Row;
			if (flexCount > 0 && !childConstraint.DefiniteCross(isRow)) {
				StretchChildrenIntoCross(ctx, index, flexFirst, flexCount, isRow ? finalH : finalW);
			}
			ctx.flexScratch.resize(flexFirst);

			return StoreRealization(cache, layout, constraint);
		}

		void Arrange(ContextImpl& ctx, u32 index, Rect finalRect);

		void ArrangeChildren(ContextImpl& ctx, u32 index) {
			Element& element = ctx.E(index);
			const Style& style = ctx.ComputedS(index);
			const LayoutData& layout = ctx.L(index);
			const f32 scale = std::max(0.0f, ctx.desc.contentScale);

			Rect contentArea = layout.content;
			f32 cursorX = contentArea.x;
			f32 cursorY = contentArea.y;

			const bool isRow = style.flexDirection == FlexDirection::Row;

			f32 totalFlowSize = 0.0f;
			f32 maxCrossSize = 0.0f;
			i32 flowChildCount = 0;
			for (u32 child = element.firstChild; child != INVALID_UI_INDEX; child = ctx.E(child).nextSibling) {
				const Style& childStyle = ctx.ComputedS(child);
				if (!InFlow(ctx.E(child), childStyle)) {
					continue;
				}
				const LayoutData& childLayout = ctx.L(child);
				Edges childMargin = ResolveEdges(childStyle.margin, scale);
				f32 childW = childLayout.desiredSize.x + childMargin.left + childMargin.right;
				f32 childH = childLayout.desiredSize.y + childMargin.top + childMargin.bottom;
				totalFlowSize += isRow ? childW : childH;
				maxCrossSize = std::max(maxCrossSize, isRow ? childH : childW);
				++flowChildCount;
			}

			if (style.overflow == Overflow::Scroll) {
				f32 contentW = isRow ? totalFlowSize : maxCrossSize;
				f32 contentH = isRow ? maxCrossSize : totalFlowSize;
				element.maxScroll = {
					std::max(0.0f, contentW - contentArea.w),
					std::max(0.0f, contentH - contentArea.h)};
				element.scrollOffset.x = std::clamp(element.scrollOffset.x, 0.0f, element.maxScroll.x);
				element.scrollOffset.y = std::clamp(element.scrollOffset.y, 0.0f, element.maxScroll.y);
				cursorX -= element.scrollOffset.x;
				cursorY -= element.scrollOffset.y;

				ctx.scrollMoved.push_back(ctx.HandleFor(index));
			} else {
				element.maxScroll = {};
			}

			f32 freeSpace = (isRow ? contentArea.w : contentArea.h) - totalFlowSize;
			f32 startOffset = 0.0f;
			f32 gap = 0.0f;
			const bool overflowing = freeSpace < 0.0f;
			const bool distribute = !overflowing || style.overflow != Overflow::Scroll;
			if (flowChildCount > 0 && distribute) {
				switch (style.justifyContent) {
				case Align::Center:
					startOffset = freeSpace * 0.5f;
					break;
				case Align::End:
					startOffset = freeSpace;
					break;
				case Align::SpaceBetween:
					if (!overflowing && flowChildCount > 1) {
						gap = freeSpace / static_cast<f32>(flowChildCount - 1);
					}
					break;
				case Align::SpaceAround:
					if (overflowing) {
						startOffset = freeSpace * 0.5f;
					} else {
						gap = freeSpace / static_cast<f32>(flowChildCount);
						startOffset = gap * 0.5f;
					}
					break;
				default:
					break;
				}
			}
			if (isRow) {
				cursorX += startOffset;
			} else {
				cursorY += startOffset;
			}

			for (u32 child = element.firstChild; child != INVALID_UI_INDEX; child = ctx.E(child).nextSibling) {
				const Style& childStyle = ctx.ComputedS(child);
				if (!Laid(ctx.E(child), childStyle)) {
					continue;
				}
				LayoutData& childLayout = ctx.L(child);
				Edges childMargin = ResolveEdges(childStyle.margin, scale);
				Rect childRect{};

				if (childStyle.position == Position::Absolute) {
					Rect container = layout.paddingBox;
					f32 childW = childLayout.desiredSize.x;
					f32 childH = childLayout.desiredSize.y;
					f32 x = 0.0f;
					f32 y = 0.0f;

					bool hasLeft = childStyle.left.has_value();
					bool hasRight = childStyle.right.has_value();
					bool hasTop = childStyle.top.has_value();
					bool hasBottom = childStyle.bottom.has_value();

					if (hasLeft) {
						x = ResolveLength(*childStyle.left, container.w, scale, true);
						if (hasRight && childLayout.autoWidth) {
							f32 right = ResolveLength(*childStyle.right, container.w, scale, true);
							childW = std::max(0.0f, container.w - x - right - childMargin.left - childMargin.right);
						}
					} else if (hasRight) {
						f32 right = ResolveLength(*childStyle.right, container.w, scale, true);
						x = container.w - right - childW;
					}
					if (hasTop) {
						y = ResolveLength(*childStyle.top, container.h, scale, true);
						if (hasBottom && childLayout.autoHeight) {
							f32 bottom = ResolveLength(*childStyle.bottom, container.h, scale, true);
							childH = std::max(0.0f, container.h - y - bottom - childMargin.top - childMargin.bottom);
						}
					} else if (hasBottom) {
						f32 bottom = ResolveLength(*childStyle.bottom, container.h, scale, true);
						y = container.h - bottom - childH;
					}

					childRect = {container.x + x + childMargin.left, container.y + y + childMargin.top, childW, childH};
				} else {
					f32 childW = childLayout.desiredSize.x;
					f32 childH = childLayout.desiredSize.y;
					Align align = EffectiveAlign(style, childStyle);

					if (isRow) {
						f32 childY = cursorY + childMargin.top;
						if (align == Align::Stretch && childLayout.autoHeight) {
							childH = std::max(0.0f, contentArea.h - childMargin.top - childMargin.bottom);
						} else if (align == Align::Center) {
							f32 outer = childH + childMargin.top + childMargin.bottom;
							childY = contentArea.y - (style.overflow == Overflow::Scroll ? element.scrollOffset.y : 0.0f) +
									 (contentArea.h - outer) * 0.5f + childMargin.top;
						} else if (align == Align::End) {
							childY = contentArea.y + contentArea.h - childH - childMargin.bottom;
							if (style.overflow == Overflow::Scroll) {
								childY -= element.scrollOffset.y;
							}
						}
						childRect = {cursorX + childMargin.left, childY, childW, childH};
						cursorX += childMargin.left + childW + childMargin.right + gap;
					} else {
						f32 childX = cursorX + childMargin.left;
						if (align == Align::Stretch && childLayout.autoWidth) {
							childW = std::max(0.0f, contentArea.w - childMargin.left - childMargin.right);
						} else if (align == Align::Center) {
							f32 outer = childW + childMargin.left + childMargin.right;
							childX = contentArea.x - (style.overflow == Overflow::Scroll ? element.scrollOffset.x : 0.0f) +
									 (contentArea.w - outer) * 0.5f + childMargin.left;
						} else if (align == Align::End) {
							childX = contentArea.x + contentArea.w - childW - childMargin.right;
							if (style.overflow == Overflow::Scroll) {
								childX -= element.scrollOffset.x;
							}
						}
						childRect = {childX, cursorY + childMargin.top, childW, childH};
						cursorY += childMargin.top + childH + childMargin.bottom + gap;
					}
				}

				Arrange(ctx, child, childRect);
			}
		}

		void Arrange(ContextImpl& ctx, u32 index, Rect finalRect) {
			Element& element = ctx.E(index);
			const Style& style = ctx.ComputedS(index);
			LayoutData& layout = ctx.L(index);

			++ctx.layoutStats.arrangeCalls;

			if (!HasFlag(element.flags, ElementFlags::Visible) || style.display == Display::None) {
				return;
			}

			if (!HasFlag(element.flags, ElementFlags::DirtyLayout) && layout.borderBox == finalRect) {
				return;
			}
			++ctx.layoutStats.arrangeWork;

			ctx.subtreeInk[index] = UnknownInk;

			const f32 scale = std::max(0.0f, ctx.desc.contentScale);
			Edges padding = ResolveEdges(style.padding, scale);
			Edges border = ResolveEdges(style.borderWidth, scale);
			Edges margin = ResolveEdges(style.margin, scale);

			layout.borderBox = finalRect;
			layout.marginBox = {
				finalRect.x - margin.left,
				finalRect.y - margin.top,
				finalRect.w + margin.left + margin.right,
				finalRect.h + margin.top + margin.bottom};
			layout.paddingBox = {
				finalRect.x + border.left,
				finalRect.y + border.top,
				finalRect.w - border.left - border.right,
				finalRect.h - border.top - border.bottom};
			layout.content = {
				layout.paddingBox.x + padding.left,
				layout.paddingBox.y + padding.top,
				layout.paddingBox.w - padding.left - padding.right,
				layout.paddingBox.h - padding.top - padding.bottom};

			ArrangeChildren(ctx, index);
			element.flags &= ~ElementFlags::DirtyLayout;
		}

	} // anonymous namespace

	namespace Layout {

		bool RemeasureInPlace(ContextImpl& ctx, u32 index) {
			LayoutData& layout = ctx.L(index);
			MeasureCache& cache = ctx.M(index);
			if (cache.lastWritten == MeasureCache::NO_SLOT) {
				return true;
			}

			const Constraint constraint = cache.entries[cache.lastWritten].key;

			const Style& style = ctx.ComputedS(index);
			auto selfSized = [](const Length& length, bool definiteBasis) {
				return length.unit == Length::Unit::Pixel ||
					   (length.unit == Length::Unit::Percent && definiteBasis);
			};
			const bool blindX = constraint.forceX && !selfSized(style.width, constraint.definiteX);
			const bool blindY = constraint.forceY && !selfSized(style.height, constraint.definiteY);
			if (blindX || blindY) {
				return true;
			}

			const Vec2 sizeBefore = layout.desiredSize;
			const bool autoWidthBefore = layout.autoWidth;
			const bool autoHeightBefore = layout.autoHeight;

			Measure(ctx, index, constraint, MeasureMode::Realize);

			const LayoutData& after = ctx.L(index);
			return after.desiredSize != sizeBefore ||
				   after.autoWidth != autoWidthBefore ||
				   after.autoHeight != autoHeightBefore;
		}

		void UpdateLayout(ContextImpl& ctx) {
			SLT_PROFILE_ZONE("UI.Layout");
			Vec2 viewport = {static_cast<f32>(ctx.desc.sizePx.x), static_cast<f32>(ctx.desc.sizePx.y)};
			++ctx.layoutStats.passes;
			Measure(ctx, ctx.root, Constraint{viewport, true, true, true, true}, MeasureMode::Realize);
			Arrange(ctx, ctx.root, {0.0f, 0.0f, viewport.x, viewport.y});
		}

	} // namespace Layout

} // namespace Shalltear::UI
