#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/WindowEvents.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	enum class InteractionMode : u8 {
		Default = 0,
		NoThrottling
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::InteractionMode)
SLT_ENUM_VALUE(Default)
SLT_ENUM_VALUE(NoThrottling)
SLT_END_ENUM()

namespace Shalltear {

	class Window;

	class EditorPresentPolicy : public Object {
	public:
		// before the 1st window (Created never fires again)
		SHALLTEAR_EDITOR_API void Install();

		SHALLTEAR_EDITOR_API void SetMode(InteractionMode mode);

		SHALLTEAR_EDITOR_API void OnWindowCreated(const WindowCreatedEvent& event);
	private:
		Vector<Handle<Window>> _windows{GetMemoryResource(MemTag::Editor)};
		InteractionMode _mode = InteractionMode::Default;
	};

} // namespace Shalltear
