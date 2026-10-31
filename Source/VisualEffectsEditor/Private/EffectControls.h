#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {

	class VisualEffect;
	namespace UI {
		class Context;
	} // namespace UI

	namespace VisualEffectsEditor::EffectControls {

		void Stop(VisualEffect& effect);
		void PlayPause(VisualEffect& effect);
		void Step(VisualEffect& effect);
		void Restart(VisualEffect& effect);
		bool IsPaused(const VisualEffect& effect);
		void SetRate(VisualEffect& effect, f32 rate);
		f32 GetRate(const VisualEffect& effect);

		void Attach(VisualEffect& effect);
		void Detach(VisualEffect& effect);

		u32 SendToEvery(std::string_view name);

		f32 SliderOfRate(f32 rate);
		f32 RateOfSlider(f32 position);
		std::span<const i32> RatePresets();

		bool WorldBounds(const VisualEffect& effect, AABB& out);

		using Targets = std::function<void(Vector<Handle<VisualEffect>>& out)>;
		using Refresh = std::function<void(UI::Context& context)>;
		Refresh BuildControls(UI::Context& context, UI::ElementHandle parent, Targets targets);

	} // namespace VisualEffectsEditor::EffectControls

} // namespace Shalltear
