#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	struct GraphView {
		Vec2 pan{40.0f, 40.0f};
		f32 zoom = 1.0f;
	};

	enum class GraphPortSide : u8 {
		Input,
		Output,
		FlowInput,
		FlowOutput
	};

	struct GraphCubic {
		Vec2 p0{};
		Vec2 p1{};
		Vec2 p2{};
		Vec2 p3{};
	};

	namespace GraphCanvasOps {

		inline constexpr f32 NODE_HEADER_HEIGHT = 24.0f;
		inline constexpr f32 PORT_ROW_HEIGHT = 20.0f;
		inline constexpr f32 PORT_RADIUS = 5.0f;
		inline constexpr f32 PORT_INSET = 5.0f;
		inline constexpr f32 PORT_PICK_RADIUS = 9.0f;
		inline constexpr f32 WIRE_PICK_RADIUS = 7.0f;

		SHALLTEAR_EDITOR_API Vec2 GraphToLocal(const GraphView& view, Vec2 graph);
		SHALLTEAR_EDITOR_API Vec2 LocalToGraph(const GraphView& view, Vec2 local);
		SHALLTEAR_EDITOR_API UI::Rect NodeRect(const GraphView& view, Vec2 graph, Vec2 size);

		SHALLTEAR_EDITOR_API void ZoomAtPoint(GraphView& view, Vec2 local, f32 notches, f32 minZoom, f32 maxZoom);

		SHALLTEAR_EDITOR_API bool FrameBounds(GraphView& view, Vec2 lower, Vec2 upper, Vec2 viewSize, f32 margin, f32 minZoom);

		SHALLTEAR_EDITOR_API Vec2 ClipToBox(const UI::Rect& box, Vec2 from);

		SHALLTEAR_EDITOR_API f32 DistanceToSegment(Vec2 a, Vec2 b, Vec2 point);

		SHALLTEAR_EDITOR_API UI::Rect RectBetween(Vec2 a, Vec2 b);
		SHALLTEAR_EDITOR_API bool RectsOverlap(const UI::Rect& a, const UI::Rect& b);
		SHALLTEAR_EDITOR_API bool SegmentTouchesRect(Vec2 a, Vec2 b, const UI::Rect& box);

		SHALLTEAR_EDITOR_API GraphCubic PortWire(Vec2 from, Vec2 to, f32 zoom);
		SHALLTEAR_EDITOR_API Vec2 Evaluate(const GraphCubic& curve, f32 t);

		// pixelsPerUnit = 1 unit in physical px
		SHALLTEAR_EDITOR_API u32 SegmentCount(const GraphCubic& curve, f32 pixelsPerUnit);

		SHALLTEAR_EDITOR_API void Tessellate(const GraphCubic& curve, u32 segments, Vector<Vec2>& out);

		SHALLTEAR_EDITOR_API f32 DistanceToCubic(const GraphCubic& curve, u32 segments, Vec2 point);

		SHALLTEAR_EDITOR_API Vec2 PortAnchor(const UI::Rect& nodeBox, GraphPortSide side, u32 row, f32 zoom,
											 f32 header = NODE_HEADER_HEIGHT);

		SHALLTEAR_EDITOR_API f32 PortNodeHeight(u32 rows);

		SHALLTEAR_EDITOR_API i32 PickNearest(std::span<const Vec2> anchors, Vec2 point, f32 radius);

		inline constexpr f32 PREVIEW_INSET = 4.0f;
		inline constexpr f32 PREVIEW_STRIP_HEIGHT = 12.0f;
		inline constexpr f32 INLINE_COMPONENT_WIDTH = 34.0f;
		inline constexpr f32 INLINE_TOGGLE_WIDTH = 16.0f;
		inline constexpr f32 INLINE_LABEL_WIDTH = 40.0f;
		inline constexpr f32 INLINE_CURVE_WIDTH = 68.0f;
		inline constexpr f32 INLINE_GAP = 6.0f;

		SHALLTEAR_EDITOR_API f32 PreviewNodeHeight(u32 rows, f32 width, bool expanded);
		SHALLTEAR_EDITOR_API UI::Rect PreviewWell(const UI::Rect& nodeBox, u32 rows, f32 zoom);
		SHALLTEAR_EDITOR_API UI::Rect PreviewStrip(const UI::Rect& nodeBox, f32 zoom);
		SHALLTEAR_EDITOR_API UI::Rect InlineField(const UI::Rect& nodeBox, u32 row, f32 width, f32 zoom,
												  f32 header = NODE_HEADER_HEIGHT);

		inline constexpr f32 GROUP_TITLE_HEIGHT = 28.0f;
		inline constexpr f32 GROUP_PADDING = 16.0f;
		inline constexpr f32 GROUP_EMPTY_WIDTH = 200.0f;
		inline constexpr f32 GROUP_EMPTY_HEIGHT = 60.0f;
		inline constexpr f32 NOTE_MIN_WIDTH = 80.0f;
		inline constexpr f32 NOTE_MIN_HEIGHT = 48.0f;
		inline constexpr f32 NOTE_GRIP = 12.0f;

		SHALLTEAR_EDITOR_API bool GroupFrame(std::span<const UI::Rect> members, f32 zoom, UI::Rect& out);
		SHALLTEAR_EDITOR_API UI::Rect GroupTitleBar(const UI::Rect& frame, f32 zoom);
		SHALLTEAR_EDITOR_API bool OutlineFrame(std::span<const UI::Rect> members, f32 zoom, UI::Rect& out);
		SHALLTEAR_EDITOR_API UI::Rect OutlineTitleBar(const UI::Rect& frame, f32 zoom);

		inline constexpr f32 SETTING_ROW_HEIGHT = 20.0f;
		inline constexpr f32 FLOW_STRIP_HEIGHT = 16.0f;
		inline constexpr f32 BLOCK_HEADER_HEIGHT = 22.0f;
		inline constexpr f32 STACK_PADDING = 6.0f;
		inline constexpr f32 BLOCK_GAP = 4.0f;
		inline constexpr f32 STACK_EMPTY_HEIGHT = BLOCK_HEADER_HEIGHT;
		inline constexpr f32 INLINE_INSIDE_INSET = 6.0f;

		struct GraphNodeLayout {
			f32 header = 0.0f;
			f32 settings = 0.0f;
			f32 rows = 0.0f;
			f32 stack = 0.0f;
			f32 height = 0.0f;
		};

		SHALLTEAR_EDITOR_API GraphNodeLayout NodeLayout(bool flowIn, u32 settings, u32 rows, f32 stackHeight, bool flowOut);
		SHALLTEAR_EDITOR_API f32 BlockHeight(u32 settings, u32 rows);
		SHALLTEAR_EDITOR_API f32 StackHeight(std::span<const f32> blocks);
		SHALLTEAR_EDITOR_API f32 BlockTop(std::span<const f32> blocks, u32 index);
		SHALLTEAR_EDITOR_API u32 StackGap(std::span<const f32> blocks, f32 y);
		SHALLTEAR_EDITOR_API f32 StackGapLine(std::span<const f32> blocks, u32 gap);

		SHALLTEAR_EDITOR_API Vec2 FlowAnchor(const UI::Rect& nodeBox, GraphPortSide side, u32 index, u32 count, f32 zoom);
		SHALLTEAR_EDITOR_API GraphCubic FlowWire(Vec2 from, Vec2 to, f32 zoom);
		SHALLTEAR_EDITOR_API UI::Rect InlineFieldInside(const UI::Rect& box, u32 row, f32 width, f32 zoom, f32 header = NODE_HEADER_HEIGHT);

	} // namespace GraphCanvasOps

} // namespace Shalltear
