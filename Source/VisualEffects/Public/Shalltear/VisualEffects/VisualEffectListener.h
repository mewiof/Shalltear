#pragma once

#include <string_view>

namespace Shalltear {

	class VisualEffect;
	class VisualEffectEventAttributes;

	class VisualEffectListener {
	public:
		virtual ~VisualEffectListener() = default;

		virtual void OnVisualEffectOutputEvent(VisualEffect& effect, std::string_view name, const VisualEffectEventAttributes& attributes) {
			(void)effect;
			(void)name;
			(void)attributes;
		}
	};

} // namespace Shalltear
