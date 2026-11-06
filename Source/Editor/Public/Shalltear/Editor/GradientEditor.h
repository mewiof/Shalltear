#pragma once

#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>

namespace Shalltear::GradientEditor {

	// return the key's index after sorting
	SHALLTEAR_EDITOR_API i32 AddKey(Gradient& gradient, bool alpha, f32 time);
	SHALLTEAR_EDITOR_API i32 MoveKey(Gradient& gradient, bool alpha, i32 index, f32 time);
	SHALLTEAR_EDITOR_API bool RemoveKey(Gradient& gradient, bool alpha, i32 index);

	SHALLTEAR_EDITOR_API void PaintPreview(UI::Context& context, UI::ElementHandle part, UI::PaintScope& paint, const Gradient& gradient);

	SHALLTEAR_EDITOR_API UI::ElementHandle Open(UI::Context& context, UI::ElementHandle anchor, const Gradient& gradient,
												std::function<void(const Gradient&)> onChange, std::function<void()> onEditBegin = {},
												std::function<void()> onEditEnd = {});

} // namespace Shalltear::GradientEditor
