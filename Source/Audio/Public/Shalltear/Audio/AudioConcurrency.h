#pragma once

#include "Shalltear/Audio/AudioAPI.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class AudioConcurrencyRule : u8 {
		PreventNew = 0,
		StopOldest,
		StopFarthestThenPreventNew,
		StopFarthestThenOldest,
		StopLowestPriority,
		StopQuietest,
		StopLowestPriorityThenPreventNew,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AudioConcurrencyRule)
SLT_ENUM_VALUE(PreventNew)
SLT_ENUM_VALUE(StopOldest)
SLT_ENUM_VALUE(StopFarthestThenPreventNew)
SLT_ENUM_VALUE(StopFarthestThenOldest)
SLT_ENUM_VALUE(StopLowestPriority)
SLT_ENUM_VALUE(StopQuietest)
SLT_ENUM_VALUE(StopLowestPriorityThenPreventNew)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_AUDIO_API AudioConcurrency : public Asset {
	public:
		SLT_REFLECT_CLASS(AudioConcurrency)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(maxCount, SLT_RANGE(1, 4096))
		SLT_REFLECT_FIELD(limitToOwner)
		SLT_REFLECT_FIELD(resolutionRule)
		SLT_REFLECT_FIELD(retriggerTime, SLT_RANGE(0.0f, 60.0f))
		SLT_REFLECT_FIELD(voiceStealReleaseTime, SLT_RANGE(0.0f, 10.0f))
		SLT_END_REFLECT()

		AudioConcurrency() = default;
		~AudioConcurrency() override;

		i32 maxCount = 16;

		bool limitToOwner = false;

		AudioConcurrencyRule resolutionRule = AudioConcurrencyRule::StopFarthestThenOldest;

		// DSP clock
		f32 retriggerTime = 0.0f;

		f32 voiceStealReleaseTime = 0.0f;

		StringHash GetAssetTypeId() const override { return AudioConcurrency::GetReflectType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
	};

} // namespace Shalltear
