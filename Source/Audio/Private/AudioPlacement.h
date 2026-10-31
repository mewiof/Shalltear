#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/Audio.h"

#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "AudioMessages.h"

#include <span>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioListenerPose {
			Vec3 position;
			Quat rotation;
			Vec3 velocity;
			AudioBus filters;
			AudioBus output;
			AudioBus zones;
		};

		inline constexpr f32 SPEED_OF_SOUND = 343.0f;

		f32 ComputeDistanceShare(f32 distance, f32 maxDistance);

		f32 ComputeDistanceAttenuation(AudioRolloffMode mode, f32 distance, f32 minDistance, f32 maxDistance, f32 rolloffScale,
									   const AnimationCurve* custom = nullptr);

		f32 ComputeDopplerRatio(const Vec3& sourcePosition, const Vec3& sourceVelocity, const Vec3& listenerPosition, const Vec3& listenerVelocity,
								f32 dopplerFactor, f32 level);

		i32 FindNearestListener(std::span<const AudioListenerPose> listeners, const Vec3& position);

		i32 FindHearingListener(std::span<const AudioListenerPose> listeners, bool placed, const Vec3& position);

		AudioBus ChooseListenerRoute(std::span<const AudioListenerPose> listeners, bool placed, const Vec3& position, bool bypass);
		AudioBus RouteThrough(const AudioListenerPose& listener, bool bypass);

		f32 ComputeZoneSendLevel(f32 reverbZoneMix);

		AudioVoicePlacement ComputePlacement(const AudioSpatialSettings& settings, const Vec3& position, const Vec3& velocity,
											 std::span<const AudioListenerPose> listeners, f32 rolloffScale, f32 dopplerFactor,
											 bool spatializerAttenuates, const AudioDistanceCurves* curves = nullptr, f32* share = nullptr);

	} // namespace AudioImpl

} // namespace Shalltear
