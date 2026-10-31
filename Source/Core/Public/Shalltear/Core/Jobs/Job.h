#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct JobHandle {
		u32 index = 0;
		u32 generation = 0;

		bool IsValid() const { return index != 0 || generation != 0; }

		bool operator==(const JobHandle& other) const {
			return index == other.index && generation == other.generation;
		}
		bool operator!=(const JobHandle& other) const {
			return !(*this == other);
		}
	};

} // namespace Shalltear
