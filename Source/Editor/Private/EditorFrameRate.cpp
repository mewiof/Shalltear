#include "Shalltear/Editor/EditorFrameRate.h"

namespace Shalltear::EditorFrameRate {

	void Meter::Add(f32 deltaSeconds) {
		++_frames;
		if (deltaSeconds > 0.0f) {
			_seconds += deltaSeconds;
		}
	}

	f32 Meter::GetFps() const {
		if (_frames == 0 || _seconds <= 0.0f) {
			return 0.0f;
		}
		return static_cast<f32>(_frames) / _seconds;
	}

	f32 Meter::GetMeanMs() const {
		if (_frames == 0 || _seconds <= 0.0f) {
			return 0.0f;
		}
		return _seconds * 1000.0f / static_cast<f32>(_frames);
	}

	void Meter::Reset() {
		_seconds = 0.0f;
		_frames = 0;
	}

} // namespace Shalltear::EditorFrameRate
