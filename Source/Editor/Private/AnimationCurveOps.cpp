#include "Shalltear/Editor/AnimationCurveOps.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>
#include <iterator>

namespace Shalltear::AnimationCurveOps {

	namespace {

		bool IsKey(const AnimationCurve& curve, i32 index) {
			return index >= 0 && static_cast<size_t>(index) < curve.keys.size();
		}

		bool IsAutomatic(KeyframeTangentMode mode) {
			return mode == KeyframeTangentMode::ClampedAuto || mode == KeyframeTangentMode::Auto;
		}

		size_t InsertionPoint(const AnimationCurve& curve, f32 time) {
			const auto at = std::upper_bound(curve.keys.begin(), curve.keys.end(), time, [](f32 value, const Keyframe& key) { return value < key.time; });
			return static_cast<size_t>(std::distance(curve.keys.begin(), at));
		}

		f32 Automatic(const AnimationCurve& curve, size_t i, KeyframeTangentMode mode) {
			const size_t count = curve.keys.size();
			if (count < 2) {
				return 0.0f;
			}
			const size_t before = i == 0 ? i : i - 1;
			const size_t after = i + 1 >= count ? i : i + 1;
			const Keyframe& low = curve.keys[before];
			const Keyframe& high = curve.keys[after];
			return Math::AutoSlope(low.time, low.value, curve.keys[i].value, high.time, high.value, i > 0 && i + 1 < count,
								   mode == KeyframeTangentMode::ClampedAuto);
		}

		f32 Toward(const Keyframe& from, const Keyframe& to) {
			const f32 run = to.time - from.time;
			return run != 0.0f ? (to.value - from.value) / run : 0.0f;
		}

	} // anonymous namespace

	void UpdateTangents(AnimationCurve& curve) {
		const size_t count = curve.keys.size();
		for (size_t i = 0; i < count; ++i) {
			Keyframe& key = curve.keys[i];
			if (!key.broken && IsAutomatic(key.inTangentMode)) {
				key.inTangent = key.outTangent = Automatic(curve, i, key.inTangentMode);
				continue;
			}
			if (IsAutomatic(key.inTangentMode)) {
				key.inTangent = Automatic(curve, i, key.inTangentMode);
			} else if (key.inTangentMode == KeyframeTangentMode::Linear && i > 0) {
				key.inTangent = Toward(curve.keys[i - 1], key);
			}
			if (IsAutomatic(key.outTangentMode)) {
				key.outTangent = Automatic(curve, i, key.outTangentMode);
			} else if (key.outTangentMode == KeyframeTangentMode::Linear && i + 1 < count) {
				key.outTangent = Toward(key, curve.keys[i + 1]);
			}
		}
	}

	i32 AddKey(AnimationCurve& curve, f32 time, f32 value) {
		for (size_t i = 0; i < curve.keys.size(); ++i) {
			if (curve.keys[i].time == time) {
				curve.keys[i].value = value;
				UpdateTangents(curve);
				return static_cast<i32>(i);
			}
		}
		Keyframe key;
		key.time = time;
		key.value = value;
		key.inTangentMode = KeyframeTangentMode::ClampedAuto;
		key.outTangentMode = KeyframeTangentMode::ClampedAuto;
		const size_t at = InsertionPoint(curve, time);
		curve.keys.insert(curve.keys.begin() + static_cast<std::ptrdiff_t>(at), key);
		UpdateTangents(curve);
		return static_cast<i32>(at);
	}

	i32 MoveKey(AnimationCurve& curve, i32 index, f32 time, f32 value) {
		if (!IsKey(curve, index)) {
			return -1;
		}
		Keyframe key = curve.keys[static_cast<size_t>(index)];
		curve.keys.erase(curve.keys.begin() + index);
		key.time = time;
		key.value = value;
		const size_t at = InsertionPoint(curve, time);
		curve.keys.insert(curve.keys.begin() + static_cast<std::ptrdiff_t>(at), key);
		UpdateTangents(curve);
		return static_cast<i32>(at);
	}

	bool RemoveKey(AnimationCurve& curve, i32 index) {
		if (!IsKey(curve, index)) {
			return false;
		}
		curve.keys.erase(curve.keys.begin() + index);
		UpdateTangents(curve);
		return true;
	}

	void SetSmooth(AnimationCurve& curve, i32 index, KeyframeTangentMode mode) {
		if (!IsKey(curve, index) || !(IsAutomatic(mode) || mode == KeyframeTangentMode::Free)) {
			return;
		}
		Keyframe& key = curve.keys[static_cast<size_t>(index)];
		key.broken = false;
		key.inTangentMode = mode;
		key.outTangentMode = mode;
		if (mode == KeyframeTangentMode::Free) {
			const f32 in = Math::IsFinite(key.inTangent) ? key.inTangent : 0.0f;
			const f32 out = Math::IsFinite(key.outTangent) ? key.outTangent : 0.0f;
			key.inTangent = key.outTangent = (in + out) * 0.5f;
		}
		UpdateTangents(curve);
	}

	void Flatten(AnimationCurve& curve, i32 index) {
		if (!IsKey(curve, index)) {
			return;
		}
		SetSmooth(curve, index, KeyframeTangentMode::Free);
		Keyframe& key = curve.keys[static_cast<size_t>(index)];
		key.inTangent = 0.0f;
		key.outTangent = 0.0f;
	}

	void SetBroken(AnimationCurve& curve, i32 index, bool broken) {
		if (!IsKey(curve, index)) {
			return;
		}
		Keyframe& key = curve.keys[static_cast<size_t>(index)];
		if (broken) {
			key.broken = true;
			key.inTangentMode = IsAutomatic(key.inTangentMode) ? KeyframeTangentMode::Free : key.inTangentMode;
			key.outTangentMode = IsAutomatic(key.outTangentMode) ? KeyframeTangentMode::Free : key.outTangentMode;
			UpdateTangents(curve);
			return;
		}
		if (key.inTangentMode == key.outTangentMode && (IsAutomatic(key.inTangentMode) || key.inTangentMode == KeyframeTangentMode::Free)) {
			key.broken = false;
			UpdateTangents(curve);
			return;
		}
		SetSmooth(curve, index, KeyframeTangentMode::Free);
	}

	void SetSideMode(AnimationCurve& curve, i32 index, KeySide side, KeyframeTangentMode mode) {
		if (!IsKey(curve, index)) {
			return;
		}
		Keyframe& key = curve.keys[static_cast<size_t>(index)];
		key.broken = true;
		if (side != KeySide::Out) {
			key.inTangentMode = mode;
		}
		if (side != KeySide::In) {
			key.outTangentMode = mode;
		}
		UpdateTangents(curve);
	}

	void SetTangent(AnimationCurve& curve, i32 index, KeySide side, f32 slope) {
		if (!IsKey(curve, index) || !Math::IsFinite(slope)) {
			return;
		}
		Keyframe& key = curve.keys[static_cast<size_t>(index)];
		const bool in = !key.broken || side != KeySide::Out;
		const bool out = !key.broken || side != KeySide::In;
		if (in) {
			key.inTangent = slope;
			key.inTangentMode = KeyframeTangentMode::Free;
		}
		if (out) {
			key.outTangent = slope;
			key.outTangentMode = KeyframeTangentMode::Free;
		}
		UpdateTangents(curve);
	}

} // namespace Shalltear::AnimationCurveOps
