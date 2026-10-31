#include "Shalltear/Core/Math/AnimationCurve.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>

SLT_REGISTER_TYPE(Shalltear::Keyframe)
SLT_REGISTER_TYPE(Shalltear::AnimationCurve)

namespace Shalltear {

	namespace {

		f32 Wrap(f32 time, f32 first, f32 span, AnimationCurveWrapMode mode) {
			if (!Math::IsFinite(time) || mode == AnimationCurveWrapMode::Clamp) {
				return time < first ? first : first + span;
			}
			if (mode == AnimationCurveWrapMode::Loop) {
				return first + Math::Repeat(time - first, span);
			}
			const f32 back = Math::Repeat(time - first, span * 2.0f);
			return first + (back > span ? span * 2.0f - back : back);
		}

	} // anonymous namespace

	f32 AnimationCurve::Evaluate(f32 time) const {
		if (keys.empty()) {
			return 0.0f;
		}
		const Keyframe& head = keys.front();
		const Keyframe& tail = keys.back();
		const f32 span = tail.time - head.time;
		if (keys.size() == 1 || !(span > 0.0f) || time != time) {
			return head.value;
		}
		f32 at = time;
		if (at < head.time) {
			at = Wrap(at, head.time, span, preWrapMode);
		} else if (at > tail.time) {
			at = Wrap(at, head.time, span, postWrapMode);
		}

		const auto after = std::upper_bound(keys.begin(), keys.end(), at, [](f32 value, const Keyframe& key) { return value < key.time; });
		if (after == keys.end()) {
			return tail.value;
		}
		if (after == keys.begin()) {
			return head.value;
		}
		const Keyframe& from = *(after - 1);
		const Keyframe& to = *after;
		if (from.outTangentMode == KeyframeTangentMode::Constant || to.inTangentMode == KeyframeTangentMode::Constant ||
			!Math::IsFinite(from.outTangent) || !Math::IsFinite(to.inTangent)) {
			return from.value;
		}
		const f32 length = to.time - from.time;
		const Math::HermiteWeights w = Math::HermiteBasis((at - from.time) / length);
		return from.value * w.h00 + from.outTangent * (w.h10 * length) + to.value * w.h01 + to.inTangent * (w.h11 * length);
	}

} // namespace Shalltear
