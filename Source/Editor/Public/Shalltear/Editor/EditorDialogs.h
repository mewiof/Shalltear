#pragma once

#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>

namespace Shalltear::EditorDialogs {

	SHALLTEAR_EDITOR_API UI::ElementHandle OpenConfirm(UI::Context& context, str message, str acceptLabel, std::function<void()> onAccept);

	SHALLTEAR_EDITOR_API UI::ElementHandle OpenTextInput(UI::Context& context, str title, str initialText, str acceptLabel,
														 std::function<void(const str&)> onAccept);

} // namespace Shalltear::EditorDialogs
