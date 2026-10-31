#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace Graphics {

		SHALLTEAR_CORE_API u32 GetQualityLevelCount();

		SHALLTEAR_CORE_API i32 GetQualityLevel();

		SHALLTEAR_CORE_API bool SetQualityLevel(i32 level);

		SHALLTEAR_CORE_API void GetQualityProfileIds(Vector<Guid>& out);

	} // namespace Graphics

} // namespace Shalltear
