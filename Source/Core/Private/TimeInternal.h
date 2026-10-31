#pragma once

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Time.h"

namespace Shalltear::TimeInternal {

	class StageScope {
	public:
		explicit StageScope(FrameStage stage) noexcept;
		~StageScope();

		StageScope(const StageScope&) = delete;
		StageScope& operator=(const StageScope&) = delete;
		StageScope(StageScope&&) = delete;
		StageScope& operator=(StageScope&&) = delete;
	private:
		FrameTime _saved;
		bool _stamped = false;
	};

} // namespace Shalltear::TimeInternal
