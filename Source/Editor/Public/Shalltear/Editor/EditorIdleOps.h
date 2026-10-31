#pragma once

#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	enum class InteractionMode : u8 {
		Default = 0,
		NoThrottling,
		MonitorRefreshRate,
		Custom
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::InteractionMode)
SLT_ENUM_VALUE(Default)
SLT_ENUM_VALUE(NoThrottling)
SLT_ENUM_VALUE(MonitorRefreshRate)
SLT_ENUM_VALUE(Custom)
SLT_END_ENUM()

namespace Shalltear::EditorIdleOps {

	inline constexpr i32 DEFAULT_IDLE_MILLISECONDS = 4;
	inline constexpr f64 DEFAULT_NO_IDLE_FRAME_SECONDS = 0.005;
	inline constexpr i32 CUSTOM_MAX_MILLISECONDS = 33;

	struct IdleInputs {
		InteractionMode mode = InteractionMode::Default;
		i32 customMilliseconds = DEFAULT_IDLE_MILLISECONDS;
		f32 refreshRate = 0.0f;
		f64 lastFrameSeconds = 0.0;
		bool playing = false;
		bool interacting = false;
	};

	SHALLTEAR_EDITOR_API f64 IdleSeconds(const IdleInputs& inputs);

} // namespace Shalltear::EditorIdleOps
