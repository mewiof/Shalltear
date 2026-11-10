#pragma once

#include "Shalltear/VisualEffects/VisualEffectsAPI.h"

#include <string_view>

namespace Shalltear {

	class VisualEffect;
	class VisualEffectEventAttributes;

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectListener {
	public:
		// key function (type info must live in 1 lib for dynamic_cast)
		virtual ~VisualEffectListener();

		virtual void OnVisualEffectOutputEvent(VisualEffect& effect, std::string_view name, const VisualEffectEventAttributes& attributes) {
			(void)effect;
			(void)name;
			(void)attributes;
		}
	};

} // namespace Shalltear
