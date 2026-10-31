#pragma once

#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Window/WindowEvents.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	class EditorPresentPolicy : public Object {
	public:
		// before the 1st window (Created never fires again)
		SHALLTEAR_EDITOR_API void Install();

		SHALLTEAR_EDITOR_API void OnWindowCreated(const WindowCreatedEvent& event);
	};

} // namespace Shalltear
