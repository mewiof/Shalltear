#pragma once

#include "Shalltear/Core/Types.h"

#include <compare>

namespace Shalltear {

	struct AudioTime {
		u64 frames = 0;

		friend constexpr bool operator==(AudioTime, AudioTime) = default;
		friend constexpr auto operator<=>(AudioTime, AudioTime) = default;
	};

} // namespace Shalltear
