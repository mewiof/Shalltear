#pragma once

#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/FrameLoop.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	class EditorFrameClock {
	public:
		enum class FrameKind : u8 {
			Frozen,
			Gameplay,
			Step,
		};

		static FrameKind KindFromGate(bool tickGameplay, bool paused) {
			if (!tickGameplay) {
				return FrameKind::Frozen;
			}
			return paused ? FrameKind::Step : FrameKind::Gameplay;
		}

		SHALLTEAR_EDITOR_API u32 BeginFrame(FrameKind kind, f64 wallDeltaSeconds);

		f32 GetAlpha() const { return _fixed.GetAlpha(); }

		SHALLTEAR_EDITOR_API void DropBankedTime();
	private:
		FixedTimestep _fixed{DEFAULT_FIXED_DELTA_TIME, DEFAULT_MAXIMUM_DELTA_TIME};
	};

} // namespace Shalltear
