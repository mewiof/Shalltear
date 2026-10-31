#include "Shalltear/Editor/EditorFrameClock.h"

namespace Shalltear {

	u32 EditorFrameClock::BeginFrame(FrameKind kind, f64 wallDeltaSeconds) {
		const f64 stepSeconds = static_cast<f64>(Time::GetFrame().fixedDeltaTime);
		Time::BeginFrame(kind == FrameKind::Step ? stepSeconds : wallDeltaSeconds);

		const FrameTime& frame = Time::GetFrame();
		_fixed.Configure(frame.fixedDeltaTime, frame.maximumDeltaTime);

		if (kind == FrameKind::Frozen) {
			return 0;
		}

		return _fixed.Advance(static_cast<f64>(frame.ComputeGameDelta()));
	}

	void EditorFrameClock::DropBankedTime() {
		_fixed.Reset();
	}

} // namespace Shalltear
