#include "Shalltear/Editor/GraphCanvasOps.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear::GraphCanvasOps {

	namespace {

		constexpr f32 ZOOM_STEP = 1.15f;

		constexpr f32 HANDLE_FLOOR = 40.0f;

		constexpr f32 SEGMENT_TARGET_PX = 10.0f;
		constexpr u32 MIN_SEGMENTS = 4;
		constexpr u32 MAX_SEGMENTS = 64;

	} // anonymous namespace

	Vec2 GraphToLocal(const GraphView& view, Vec2 graph) {
		return view.pan + graph * view.zoom;
	}

	Vec2 LocalToGraph(const GraphView& view, Vec2 local) {
		return (local - view.pan) / view.zoom;
	}

	UI::Rect NodeRect(const GraphView& view, Vec2 graph, Vec2 size) {
		const Vec2 at = GraphToLocal(view, graph);
		return UI::Rect{at.x, at.y, size.x * view.zoom, size.y * view.zoom};
	}

	void ZoomAtPoint(GraphView& view, Vec2 local, f32 notches, f32 minZoom, f32 maxZoom) {
		const Vec2 graph = LocalToGraph(view, local);
		view.zoom = Math::Clamp(view.zoom * std::pow(ZOOM_STEP, notches), minZoom, maxZoom);
		view.pan = local - graph * view.zoom;
	}

	bool FrameBounds(GraphView& view, Vec2 lower, Vec2 upper, Vec2 viewSize, f32 margin, f32 minZoom) {
		if (!(viewSize.x > 16.0f) || !(viewSize.y > 16.0f)) {
			return false;
		}
		const f32 spanX = Math::Max(upper.x - lower.x, 1.0f) + margin * 2.0f;
		const f32 spanY = Math::Max(upper.y - lower.y, 1.0f) + margin * 2.0f;
		view.zoom = Math::Clamp(Math::Min(viewSize.x / spanX, viewSize.y / spanY), minZoom, 1.0f);
		const Vec2 centre((lower.x + upper.x) * 0.5f, (lower.y + upper.y) * 0.5f);
		view.pan = Vec2(viewSize.x * 0.5f - centre.x * view.zoom, viewSize.y * 0.5f - centre.y * view.zoom);
		return true;
	}

	Vec2 ClipToBox(const UI::Rect& box, Vec2 from) {
		const Vec2 centre(box.x + box.w * 0.5f, box.y + box.h * 0.5f);
		const Vec2 delta = centre - from;
		if (Math::Abs(delta.x) < 1e-4f && Math::Abs(delta.y) < 1e-4f) {
			return centre;
		}
		const f32 halfW = box.w * 0.5f;
		const f32 halfH = box.h * 0.5f;
		f32 scale = 1.0f;
		if (Math::Abs(delta.x) > 1e-4f) {
			scale = Math::Min(scale, halfW / Math::Abs(delta.x));
		}
		if (Math::Abs(delta.y) > 1e-4f) {
			scale = Math::Min(scale, halfH / Math::Abs(delta.y));
		}
		return centre - delta * scale;
	}

	f32 DistanceToSegment(Vec2 a, Vec2 b, Vec2 point) {
		const Vec2 span = b - a;
		const f32 lengthSq = span.x * span.x + span.y * span.y;
		if (lengthSq < 1e-6f) {
			return (point - a).Length();
		}
		f32 t = ((point - a).x * span.x + (point - a).y * span.y) / lengthSq;
		t = Math::Clamp(t, 0.0f, 1.0f);
		return (point - (a + span * t)).Length();
	}

	UI::Rect RectBetween(Vec2 a, Vec2 b) {
		return UI::Rect{Math::Min(a.x, b.x), Math::Min(a.y, b.y), Math::Abs(b.x - a.x), Math::Abs(b.y - a.y)};
	}

	bool RectsOverlap(const UI::Rect& a, const UI::Rect& b) {
		return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
	}

	bool SegmentTouchesRect(Vec2 a, Vec2 b, const UI::Rect& box) {
		if (!(box.w > 0.0f) || !(box.h > 0.0f)) {
			return false;
		}
		f32 enter = 0.0f;
		f32 leave = 1.0f;
		auto clip = [&](f32 toward, f32 room) {
			if (toward == 0.0f) {
				return room >= 0.0f;
			}
			const f32 at = room / toward;
			if (toward < 0.0f) {
				if (at > leave) {
					return false;
				}
				enter = Math::Max(enter, at);
			} else {
				if (at < enter) {
					return false;
				}
				leave = Math::Min(leave, at);
			}
			return true;
		};
		const Vec2 span = b - a;
		return clip(-span.x, a.x - box.x) && clip(span.x, box.x + box.w - a.x) && clip(-span.y, a.y - box.y) &&
			   clip(span.y, box.y + box.h - a.y) && enter <= leave;
	}

	GraphCubic PortWire(Vec2 from, Vec2 to, f32 zoom) {
		const f32 reach = Math::Max(Math::Abs(to.x - from.x) * 0.5f, Math::Abs(to.y - from.y) * 0.25f);
		const f32 handle = Math::Max(reach, HANDLE_FLOOR * zoom);
		return GraphCubic{from, Vec2(from.x + handle, from.y), Vec2(to.x - handle, to.y), to};
	}

	Vec2 Evaluate(const GraphCubic& curve, f32 t) {
		const f32 u = 1.0f - t;
		const f32 b0 = u * u * u;
		const f32 b1 = 3.0f * u * u * t;
		const f32 b2 = 3.0f * u * t * t;
		const f32 b3 = t * t * t;
		return curve.p0 * b0 + curve.p1 * b1 + curve.p2 * b2 + curve.p3 * b3;
	}

	u32 SegmentCount(const GraphCubic& curve, f32 pixelsPerUnit) {
		const f32 polygon = (curve.p1 - curve.p0).Length() + (curve.p2 - curve.p1).Length() + (curve.p3 - curve.p2).Length();
		const f32 pixels = polygon * Math::Max(pixelsPerUnit, 0.0f);
		const u32 wanted = static_cast<u32>(std::ceil(pixels / SEGMENT_TARGET_PX));
		return Math::Clamp(wanted, MIN_SEGMENTS, MAX_SEGMENTS);
	}

	void Tessellate(const GraphCubic& curve, u32 segments, Vector<Vec2>& out) {
		out.clear();
		const u32 count = Math::Max(segments, 1u);
		for (u32 index = 0; index <= count; ++index) {
			out.push_back(Evaluate(curve, static_cast<f32>(index) / static_cast<f32>(count)));
		}
	}

	f32 DistanceToCubic(const GraphCubic& curve, u32 segments, Vec2 point) {
		const u32 count = Math::Max(segments, 1u);
		f32 best = (point - curve.p0).Length();
		Vec2 previous = curve.p0;
		for (u32 index = 1; index <= count; ++index) {
			const Vec2 next = Evaluate(curve, static_cast<f32>(index) / static_cast<f32>(count));
			best = Math::Min(best, DistanceToSegment(previous, next, point));
			previous = next;
		}
		return best;
	}

	Vec2 PortAnchor(const UI::Rect& nodeBox, GraphPortSide side, u32 row, f32 zoom, f32 header) {
		const f32 y = nodeBox.y + (header + (static_cast<f32>(row) + 0.5f) * PORT_ROW_HEIGHT) * zoom;
		const f32 inset = (PORT_INSET + PORT_RADIUS) * zoom;
		const f32 x = side == GraphPortSide::Input ? nodeBox.x + inset : nodeBox.x + nodeBox.w - inset;
		return Vec2(x, y);
	}

	f32 PortNodeHeight(u32 rows) {
		return NODE_HEADER_HEIGHT + static_cast<f32>(rows) * PORT_ROW_HEIGHT + PORT_ROW_HEIGHT * 0.5f;
	}

	f32 PreviewNodeHeight(u32 rows, f32 width, bool expanded) {
		const f32 well = expanded ? Math::Max(width - PREVIEW_INSET * 2.0f, 0.0f) + PREVIEW_INSET : 0.0f;
		return PortNodeHeight(rows) + well + PREVIEW_STRIP_HEIGHT;
	}

	UI::Rect PreviewWell(const UI::Rect& nodeBox, u32 rows, f32 zoom) {
		const f32 inset = PREVIEW_INSET * zoom;
		const f32 side = Math::Max(nodeBox.w - inset * 2.0f, 0.0f);
		return UI::Rect{nodeBox.x + inset, nodeBox.y + PortNodeHeight(rows) * zoom, side, side};
	}

	UI::Rect PreviewStrip(const UI::Rect& nodeBox, f32 zoom) {
		const f32 height = PREVIEW_STRIP_HEIGHT * zoom;
		return UI::Rect{nodeBox.x, nodeBox.y + nodeBox.h - height, nodeBox.w, height};
	}

	UI::Rect InlineField(const UI::Rect& nodeBox, u32 row, f32 width, f32 zoom, f32 header) {
		const f32 height = (PORT_ROW_HEIGHT - 4.0f) * zoom;
		const f32 centre = nodeBox.y + (header + (static_cast<f32>(row) + 0.5f) * PORT_ROW_HEIGHT) * zoom;
		const f32 w = width * zoom;
		return UI::Rect{nodeBox.x - INLINE_GAP * zoom - w, centre - height * 0.5f, w, height};
	}

	i32 PickNearest(std::span<const Vec2> anchors, Vec2 point, f32 radius) {
		i32 found = -1;
		f32 best = radius;
		for (size_t index = 0; index < anchors.size(); ++index) {
			const f32 distance = (anchors[index] - point).Length();
			if (distance <= best) {
				best = distance;
				found = static_cast<i32>(index);
			}
		}
		return found;
	}

	bool GroupFrame(std::span<const UI::Rect> members, f32 zoom, UI::Rect& out) {
		if (members.empty()) {
			return false;
		}
		f32 left = members[0].x;
		f32 top = members[0].y;
		f32 right = members[0].x + members[0].w;
		f32 bottom = members[0].y + members[0].h;
		for (const UI::Rect& box : members) {
			left = std::min(left, box.x);
			top = std::min(top, box.y);
			right = std::max(right, box.x + box.w);
			bottom = std::max(bottom, box.y + box.h);
		}
		const f32 pad = GROUP_PADDING * zoom;
		const f32 title = GROUP_TITLE_HEIGHT * zoom;
		out = UI::Rect{left - pad, top - pad - title, right - left + pad * 2.0f, bottom - top + pad * 2.0f + title};
		return true;
	}

	UI::Rect GroupTitleBar(const UI::Rect& frame, f32 zoom) {
		return UI::Rect{frame.x, frame.y, frame.w, std::min(frame.h, GROUP_TITLE_HEIGHT * zoom)};
	}

	bool OutlineFrame(std::span<const UI::Rect> members, f32 zoom, UI::Rect& out) {
		if (members.empty()) {
			return false;
		}
		UI::Rect around = members[0];
		for (const UI::Rect& box : members) {
			const f32 right = std::max(around.x + around.w, box.x + box.w);
			const f32 bottom = std::max(around.y + around.h, box.y + box.h);
			around.x = std::min(around.x, box.x);
			around.y = std::min(around.y, box.y);
			around.w = right - around.x;
			around.h = bottom - around.y;
		}
		const f32 pad = GROUP_PADDING * 0.5f * zoom;
		out = UI::Rect{around.x - pad, around.y - pad, around.w + pad * 2.0f, around.h + pad * 2.0f};
		return true;
	}

	UI::Rect OutlineTitleBar(const UI::Rect& frame, f32 zoom) {
		const f32 height = GROUP_TITLE_HEIGHT * zoom;
		return UI::Rect{frame.x, frame.y - height, frame.w, height};
	}

	GraphNodeLayout NodeLayout(bool flowIn, u32 settings, u32 rows, f32 stackHeight, bool flowOut) {
		GraphNodeLayout layout;
		layout.header = flowIn ? FLOW_STRIP_HEIGHT : 0.0f;
		layout.settings = layout.header + NODE_HEADER_HEIGHT;
		layout.rows = layout.settings + static_cast<f32>(settings) * SETTING_ROW_HEIGHT;
		const f32 below = layout.rows + static_cast<f32>(rows) * PORT_ROW_HEIGHT;
		if (stackHeight > 0.0f) {
			layout.stack = below + (settings + rows > 0 ? PORT_ROW_HEIGHT * 0.5f : 0.0f);
			layout.height = layout.stack + stackHeight;
		} else {
			layout.stack = below;
			layout.height = below + PORT_ROW_HEIGHT * 0.5f;
		}
		if (flowOut) {
			layout.height += FLOW_STRIP_HEIGHT;
		}
		return layout;
	}

	f32 BlockHeight(u32 settings, u32 rows) {
		return BLOCK_HEADER_HEIGHT + static_cast<f32>(settings) * SETTING_ROW_HEIGHT + static_cast<f32>(rows) * PORT_ROW_HEIGHT;
	}

	f32 StackHeight(std::span<const f32> blocks) {
		if (blocks.empty()) {
			return STACK_PADDING * 2.0f + STACK_EMPTY_HEIGHT;
		}
		f32 sum = 0.0f;
		for (f32 height : blocks) {
			sum += height;
		}
		return STACK_PADDING * 2.0f + sum + static_cast<f32>(blocks.size() - 1) * BLOCK_GAP;
	}

	f32 BlockTop(std::span<const f32> blocks, u32 index) {
		f32 top = STACK_PADDING;
		for (u32 i = 0; i < index && i < blocks.size(); ++i) {
			top += blocks[i] + BLOCK_GAP;
		}
		return top;
	}

	u32 StackGap(std::span<const f32> blocks, f32 y) {
		for (u32 i = 0; i < blocks.size(); ++i) {
			if (y < BlockTop(blocks, i) + blocks[i] * 0.5f) {
				return i;
			}
		}
		return static_cast<u32>(blocks.size());
	}

	f32 StackGapLine(std::span<const f32> blocks, u32 gap) {
		if (blocks.empty()) {
			return STACK_PADDING;
		}
		if (gap < blocks.size()) {
			return BlockTop(blocks, gap) - (gap == 0 ? STACK_PADDING : BLOCK_GAP) * 0.5f;
		}
		const u32 last = static_cast<u32>(blocks.size() - 1);
		return BlockTop(blocks, last) + blocks[last] + STACK_PADDING * 0.5f;
	}

	Vec2 FlowAnchor(const UI::Rect& nodeBox, GraphPortSide side, u32 index, u32 count, f32 zoom) {
		const f32 share = nodeBox.w / static_cast<f32>(Math::Max(count, 1u));
		const f32 x = nodeBox.x + share * (static_cast<f32>(index) + 0.5f);
		const f32 half = FLOW_STRIP_HEIGHT * 0.5f * zoom;
		const f32 y = side == GraphPortSide::FlowInput ? nodeBox.y + half : nodeBox.y + nodeBox.h - half;
		return Vec2(x, y);
	}

	GraphCubic FlowWire(Vec2 from, Vec2 to, f32 zoom) {
		const f32 reach = Math::Max(Math::Abs(to.y - from.y) * 0.5f, Math::Abs(to.x - from.x) * 0.25f);
		const f32 handle = Math::Max(reach, HANDLE_FLOOR * zoom);
		return GraphCubic{from, Vec2(from.x, from.y + handle), Vec2(to.x, to.y - handle), to};
	}

	UI::Rect InlineFieldInside(const UI::Rect& box, u32 row, f32 width, f32 zoom, f32 header) {
		const f32 height = (PORT_ROW_HEIGHT - 4.0f) * zoom;
		const f32 centre = box.y + (header + (static_cast<f32>(row) + 0.5f) * PORT_ROW_HEIGHT) * zoom;
		const f32 w = width * zoom;
		return UI::Rect{box.x + box.w - INLINE_INSIDE_INSET * zoom - w, centre - height * 0.5f, w, height};
	}

} // namespace Shalltear::GraphCanvasOps
