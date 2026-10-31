#pragma once

#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

namespace Shalltear::UI {

	enum class ElementFlags : u16 {
		None = 0,

		DirtyStyle = 1 << 0,
		DirtyLayout = 1 << 1,

		Visible = 1 << 2,
		Hoverable = 1 << 3,
		Focusable = 1 << 4,

		Pressed = 1 << 5,
		Hovered = 1 << 6,
		Focused = 1 << 7,
		DropHover = 1 << 8,

		ContentDirty = 1 << 9,

		HasIcon = 1 << 10,

		HasImage = 1 << 11,

		InheritsPointerState = 1 << 12,
	};
	SLT_ENUM_CLASS_FLAGS(ElementFlags)

	inline constexpr u32 MAX_ELEMENT_CLASSES = 4;

	struct Element {
		u32 parent = INVALID_UI_INDEX;
		u32 firstChild = INVALID_UI_INDEX;
		u32 lastChild = INVALID_UI_INDEX;
		u32 nextSibling = INVALID_UI_INDEX;

		StringHash id{};
		StringHash typeTag{};
		StringHash classes[MAX_ELEMENT_CLASSES]{};
		u8 classCount = 0;

		ElementFlags flags = ElementFlags::DirtyStyle | ElementFlags::DirtyLayout |
							 ElementFlags::Visible | ElementFlags::Hoverable;

		Vec2 scrollOffset{};
		Vec2 maxScroll{};

		// SRGB
		Color tint = Color::White();

		u32 textIndex = INVALID_UI_INDEX;
		u32 textInputIndex = INVALID_UI_INDEX;

		u32 generation = 1;

		u64 userTag = 0;
	};

	struct LayoutData {
		Vec2 desiredSize{};

		bool autoWidth = true;
		bool autoHeight = true;

		Rect content{};
		Rect paddingBox{};
		Rect borderBox{};
		Rect marginBox{};
	};

	struct LayoutStats {
		u64 measureCalls = 0;
		u64 measureWork = 0;
		u64 arrangeCalls = 0;
		u64 arrangeWork = 0;
		u64 passes = 0;
	};

	struct PaintStats {
		u64 elementsVisited = 0;
		u64 elementsPainted = 0;
		u64 scopesWalked = 0;
		u64 scopesSorted = 0;
	};

	struct TreeStats {
		u64 linkOps = 0;
		u64 linkWork = 0;
	};

	struct StyleStats {
		u64 recalcs = 0;
		u64 ruleTests = 0;
	};

} // namespace Shalltear::UI
