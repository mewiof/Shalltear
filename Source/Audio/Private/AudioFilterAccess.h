#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioFilters.h"

#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class SceneObject;

	namespace AudioImpl {

		inline constexpr u32 MAX_FILTER_PARAMETERS = 14;

		inline constexpr u32 MAX_SOURCE_FILTERS = 16;

		struct AudioFilterAccess {
			static std::string_view Describe(const AudioFilter& filter, f32* values, u32& count) { return filter.Describe(values, count); }
			static const AnimationCurve* DescribeDistance(const AudioFilter& filter, u32& parameter) { return filter.DescribeDistance(parameter); }
		};

		// name is valid only until it changes
		struct AudioFilterList {
			u32 count = 0;
			u64 components[MAX_SOURCE_FILTERS]{};
			std::string_view effects[MAX_SOURCE_FILTERS];
			u32 firsts[MAX_SOURCE_FILTERS]{};
			u32 counts[MAX_SOURCE_FILTERS]{};
			f32 values[MAX_SOURCE_FILTERS * MAX_FILTER_PARAMETERS];
			const AnimationCurve* distanceCurves[MAX_SOURCE_FILTERS]{};
			u32 distanceParameters[MAX_SOURCE_FILTERS]{};
		};

		void ReadFilters(const SceneObject* owner, u32 limit, AudioFilterList& out);

	} // namespace AudioImpl

} // namespace Shalltear
