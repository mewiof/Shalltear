#pragma once

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>

namespace Shalltear::CurveEditor {

	struct View {
		f32 timeMin = 0.0f;
		f32 timeMax = 1.0f;
		f32 valueMin = 0.0f;
		f32 valueMax = 1.0f;
	};

	SHALLTEAR_EDITOR_API View Frame(const AnimationCurve& curve);

	SHALLTEAR_EDITOR_API UI::ElementHandle Open(UI::Context& context, UI::ElementHandle anchor, const AnimationCurve& curve,
												std::function<void(const AnimationCurve&)> onChange, std::function<void()> onEditBegin = {},
												std::function<void()> onEditEnd = {});

} // namespace Shalltear::CurveEditor
