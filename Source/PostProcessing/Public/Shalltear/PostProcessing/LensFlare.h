#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/LensFlareAsset.h"
#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear {

	class SHALLTEAR_POSTPROCESSING_API LensFlare : public Component {
	public:
		SLT_REFLECT_COMPONENT(LensFlare, Component)
		SLT_REFLECT_FIELD(flare)
		SLT_REFLECT_FIELD(intensity)
		SLT_REFLECT_FIELD(scale)
		SLT_REFLECT_FIELD(attenuationByLightShape)
		SLT_REFLECT_FIELD(attenuationDistance)
		SLT_REFLECT_SUBOBJECT(attenuationDistanceCurve)
		SLT_REFLECT_FIELD(scaleDistance)
		SLT_REFLECT_SUBOBJECT(scaleDistanceCurve)
		SLT_REFLECT_SUBOBJECT(screenAttenuationCurve)
		SLT_REFLECT_FIELD(occlusion, SLT_HEADER("Occlusion"))
		SLT_REFLECT_FIELD(occlusionRadius)
		SLT_REFLECT_FIELD(sampleCount, SLT_RANGE(1, 64))
		SLT_REFLECT_FIELD(occlusionOffset)
		SLT_REFLECT_FIELD(allowOffScreen)
		SLT_END_REFLECT()

		~LensFlare() override;

		Handle<LensFlareAsset> flare;

		f32 intensity = 1.0f;

		f32 scale = 1.0f;

		bool attenuationByLightShape = true;

		f32 attenuationDistance = 100.0f;

		AnimationCurve attenuationDistanceCurve{{Keyframe{.time = 0.0f, .value = 1.0f, .inTangent = -1.0f, .outTangent = -1.0f},
												 Keyframe{.time = 1.0f, .value = 0.0f, .inTangent = -1.0f, .outTangent = -1.0f}},
												GetMemoryResource(MemTag::Scene)};

		f32 scaleDistance = 100.0f;

		AnimationCurve scaleDistanceCurve{{Keyframe{.time = 0.0f, .value = 1.0f, .inTangent = -1.0f, .outTangent = -1.0f},
										   Keyframe{.time = 1.0f, .value = 0.0f, .inTangent = -1.0f, .outTangent = -1.0f}},
										  GetMemoryResource(MemTag::Scene)};

		// time = max(|x|, |y|) of light's NDC position
		AnimationCurve screenAttenuationCurve{{Keyframe{.time = 0.0f, .value = 1.0f}, Keyframe{.time = 1.0f, .value = 1.0f}},
											  GetMemoryResource(MemTag::Scene)};

		bool occlusion = true;

		// world units (degrees for a directional light)
		f32 occlusionRadius = 0.1f;

		i32 sampleCount = 32;

		f32 occlusionOffset = 0.05f;

		bool allowOffScreen = false;

		static u32 GetEnabledCount();
		static LensFlare* GetEnabled(u32 index);
	protected:
		void OnEnable() override;
		void OnDisable() override;
	};

} // namespace Shalltear
