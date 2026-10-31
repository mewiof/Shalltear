#pragma once

#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear::EditorFrameRate {

	class SHALLTEAR_EDITOR_API Meter {
	public:
		void Add(f32 deltaSeconds);

		f32 GetWindowSeconds() const { return _seconds; }

		f32 GetFps() const;
		f32 GetMeanMs() const;

		void Reset();
	private:
		f32 _seconds = 0.0f;
		u32 _frames = 0;
	};

} // namespace Shalltear::EditorFrameRate
