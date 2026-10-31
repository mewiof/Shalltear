#include "Shalltear/Editor/EditorDialogs.h"

#include "SceneFileFlows.h"

namespace Shalltear::EditorDialogs {

	UI::ElementHandle OpenConfirm(UI::Context& context, str message, str acceptLabel, std::function<void()> onAccept) {
		return SceneFileFlows::OpenConfirmPopup(context, std::move(message), std::move(acceptLabel), std::move(onAccept), "", {});
	}

	UI::ElementHandle OpenTextInput(UI::Context& context, str title, str initialText, str acceptLabel,
									std::function<void(const str&)> onAccept) {
		return SceneFileFlows::OpenTextInputPopup(context, std::move(title), std::move(initialText), std::move(onAccept), std::move(acceptLabel));
	}

} // namespace Shalltear::EditorDialogs
