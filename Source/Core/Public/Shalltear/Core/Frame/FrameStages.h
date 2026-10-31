#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	// declaration order = frame order
#define SLT_FRAME_STAGE_LIST(X) \
	X(FixedUpdate)              \
	X(Update)                   \
	X(UIUpdate)                 \
	X(LateUpdate)               \
	X(EndFrame)                 \
	X(Render)

	enum class FrameStage : u8 {
#define SLT_DETAIL_FRAME_STAGE_ENUMERATOR(Name) Name,
		SLT_FRAME_STAGE_LIST(SLT_DETAIL_FRAME_STAGE_ENUMERATOR)
#undef SLT_DETAIL_FRAME_STAGE_ENUMERATOR
	};

	inline constexpr FrameStage FRAME_STAGES[] = {
#define SLT_DETAIL_FRAME_STAGE_VALUE(Name) FrameStage::Name,
		SLT_FRAME_STAGE_LIST(SLT_DETAIL_FRAME_STAGE_VALUE)
#undef SLT_DETAIL_FRAME_STAGE_VALUE
	};

	inline constexpr u32 FRAME_STAGE_COUNT = static_cast<u32>(sizeof(FRAME_STAGES) / sizeof(FRAME_STAGES[0]));

	inline constexpr std::string_view GetFrameStageName(FrameStage stage) {
		switch (stage) {
#define SLT_DETAIL_FRAME_STAGE_NAME(Name) \
	case FrameStage::Name:                \
		return #Name;
			SLT_FRAME_STAGE_LIST(SLT_DETAIL_FRAME_STAGE_NAME)
#undef SLT_DETAIL_FRAME_STAGE_NAME
		}
		return {};
	}

	inline constexpr bool FindFrameStage(std::string_view name, FrameStage& out) {
		for (FrameStage stage : FRAME_STAGES) {
			if (GetFrameStageName(stage) == name) {
				out = stage;
				return true;
			}
		}
		return false;
	}

	struct FrameStageArgs {};

	using FrameStageFn = void (*)(const FrameStageArgs& args, void* user);

	enum class FrameStagePosition : u8 {
		Early = 0,
		Normal,
		Late,
	};

	namespace FrameStages {

		SHALLTEAR_CORE_API bool Register(FrameStage stage, FrameStagePosition position, FrameStageFn fn, void* user, const void* owner);

		SHALLTEAR_CORE_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_CORE_API void Run(FrameStage stage, const FrameStageArgs& args);

	} // namespace FrameStages

} // namespace Shalltear
