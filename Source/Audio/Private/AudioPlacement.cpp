#include "AudioPlacement.h"

#include <cmath>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			f32 Clamp(f32 value, f32 low, f32 high) {
				return value >= low ? (value <= high ? value : high) : low;
			}

			bool IsFinite(const Vec3& value) {
				return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
			}

		} // anonymous namespace

		f32 ComputeDistanceShare(f32 distance, f32 maxDistance) {
			if (!(distance > 0.0f)) {
				return 0.0f;
			}
			if (!(maxDistance > 0.0f) || !(distance < maxDistance)) {
				return 1.0f;
			}
			return distance / maxDistance;
		}

		f32 ComputeDistanceAttenuation(AudioRolloffMode mode, f32 distance, f32 minDistance, f32 maxDistance, f32 rolloffScale,
									   const AnimationCurve* custom) {
			if (mode == AudioRolloffMode::Custom) {
				if (!custom || custom->keys.empty()) {
					return 1.0f;
				}
				return Clamp(custom->Evaluate(ComputeDistanceShare(distance, maxDistance)), 0.0f, 1.0f);
			}
			const f32 minimum = minDistance > 0.0f ? minDistance : 0.0f;
			if (!(distance > minimum)) {
				return 1.0f;
			}
			if (mode == AudioRolloffMode::Linear) {
				const f32 maximum = maxDistance > minimum ? maxDistance : minimum;
				if (distance >= maximum) {
					return 0.0f;
				}
				return (maximum - distance) / (maximum - minimum);
			}
			const f32 scale = rolloffScale > 0.0f ? rolloffScale : 0.0f;
			const f32 denominator = minimum + scale * (distance - minimum);
			return denominator > 0.0f ? Clamp(minimum / denominator, 0.0f, 1.0f) : 1.0f;
		}

		f32 ComputeDopplerRatio(const Vec3& sourcePosition, const Vec3& sourceVelocity, const Vec3& listenerPosition, const Vec3& listenerVelocity,
								f32 dopplerFactor, f32 level) {
			const f32 strength = level > 0.0f ? level : 0.0f;
			const f32 factor = dopplerFactor > 0.0f ? dopplerFactor : 0.0f;
			if (strength == 0.0f || factor == 0.0f) {
				return 1.0f;
			}
			const Vec3 line = listenerPosition - sourcePosition;
			const f32 length = line.Length();
			if (!(length > 0.0f) || !std::isfinite(length)) {
				return 1.0f;
			}
			const Vec3 toward = line * (1.0f / length);
			const f32 limit = SPEED_OF_SOUND * 0.5f;
			const f32 source = Clamp(Vec3::Dot(sourceVelocity, toward) * factor, -limit, limit);
			const f32 listener = Clamp(Vec3::Dot(listenerVelocity, toward) * factor, -limit, limit);
			const f32 ratio = (SPEED_OF_SOUND - listener) / (SPEED_OF_SOUND - source);
			return Clamp(1.0f + (ratio - 1.0f) * strength, 1.0f / MAX_DOPPLER_RATIO, MAX_DOPPLER_RATIO);
		}

		i32 FindNearestListener(std::span<const AudioListenerPose> listeners, const Vec3& position) {
			i32 nearest = -1;
			f32 best = 0.0f;
			for (size_t index = 0; index < listeners.size(); ++index) {
				const f32 distance = (listeners[index].position - position).LengthSquared();
				if (nearest < 0 || distance < best) {
					nearest = static_cast<i32>(index);
					best = distance;
				}
			}
			return nearest;
		}

		i32 FindHearingListener(std::span<const AudioListenerPose> listeners, bool placed, const Vec3& position) {
			if (listeners.empty()) {
				return -1;
			}
			return placed ? FindNearestListener(listeners, IsFinite(position) ? position : Vec3{}) : 0;
		}

		AudioBus ChooseListenerRoute(std::span<const AudioListenerPose> listeners, bool placed, const Vec3& position, bool bypass) {
			const i32 hearing = FindHearingListener(listeners, placed, position);
			return hearing < 0 ? AudioBus{} : RouteThrough(listeners[static_cast<size_t>(hearing)], bypass);
		}

		AudioBus RouteThrough(const AudioListenerPose& listener, bool bypass) {
			return !bypass && listener.filters.IsValid() ? listener.filters : listener.output;
		}

		f32 ComputeZoneSendLevel(f32 reverbZoneMix) {
			const f32 mix = Clamp(reverbZoneMix, 0.0f, 1.1f);
			return mix <= 1.0f ? mix : std::pow(10.0f, (mix - 1.0f) * 5.0f);
		}

		AudioVoicePlacement ComputePlacement(const AudioSpatialSettings& settings, const Vec3& position, const Vec3& velocity,
											 std::span<const AudioListenerPose> listeners, f32 rolloffScale, f32 dopplerFactor,
											 bool spatializerAttenuates, const AudioDistanceCurves* curves, f32* share) {
			AudioVoicePlacement placement;
			placement.blend = Clamp(settings.spatialBlend, 0.0f, 1.0f);
			placement.params.spread = Clamp(settings.spread, 0.0f, 360.0f);
			placement.params.occlusion = Clamp(settings.occlusion, 0.0f, 1.0f);

			const Vec3 source = IsFinite(position) ? position : Vec3{};
			const Vec3 moving = IsFinite(velocity) ? velocity : Vec3{};
			const i32 nearest = FindNearestListener(listeners, source);
			f32 distance = 0.0f;
			const AudioListenerPose* listener = nearest < 0 ? nullptr : &listeners[static_cast<size_t>(nearest)];
			if (listener) {
				placement.params.position = listener->rotation.Conjugated() * (source - listener->position);
				distance = placement.params.position.Length();
			}

			const f32 at = ComputeDistanceShare(distance, settings.maxDistance);
			if (share) {
				*share = at;
			}
			if (curves && !curves->spatialBlend.keys.empty()) {
				placement.blend = Clamp(curves->spatialBlend.Evaluate(at), 0.0f, 1.0f);
			}
			if (curves && !curves->spread.keys.empty()) {
				placement.params.spread = Clamp(curves->spread.Evaluate(at) * 360.0f, 0.0f, 360.0f);
			}
			if (!listener) {
				return placement;
			}

			placement.params.distanceAttenuation = ComputeDistanceAttenuation(settings.rolloffMode, distance, settings.minDistance, settings.maxDistance,
																			  rolloffScale, curves ? &curves->customRolloff : nullptr);
			placement.gain = spatializerAttenuates ? 1.0f : placement.params.distanceAttenuation;
			placement.doppler = ComputeDopplerRatio(source, moving, listener->position, listener->velocity, dopplerFactor,
													Clamp(settings.dopplerLevel, 0.0f, 5.0f) * placement.blend);
			return placement;
		}

	} // namespace AudioImpl

} // namespace Shalltear
