#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessProfile.h"
#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear {

	class SHALLTEAR_POSTPROCESSING_API PostProcessVolume : public Component {
	public:
		SLT_REFLECT_COMPONENT(PostProcessVolume, Component)
		SLT_REFLECT_FIELD(isGlobal)
		SLT_REFLECT_FIELD(size)
		SLT_REFLECT_FIELD(blendDistance)
		SLT_REFLECT_FIELD(weight, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(priority)
		SLT_REFLECT_FIELD(profile)
		SLT_END_REFLECT()

		~PostProcessVolume() override;

		bool isGlobal = true;

		// full extents
		Vec3 size{10.0f, 10.0f, 10.0f};

		f32 blendDistance = 0.0f;

		f32 weight = 1.0f;

		f32 priority = 0.0f;

		Handle<PostProcessProfile> profile;

		static u32 GetEnabledCount();
		static PostProcessVolume* GetEnabled(u32 index);
	protected:
		void OnEnable() override;
		void OnDisable() override;
	};

} // namespace Shalltear
