#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace AnimationImpl {

		struct BudgetViewpoint {
			Vec3 position;
			Vec3 forward;
			f32 tanHalfFOV = 1.0f;
		};

		f32 BudgetSignificance(std::span<const BudgetViewpoint> views, const Vec3& position, f32 radius, f32 priority);

		f32 BudgetSkeletonRadius(const SkeletonAsset& skeleton);

		u32 BudgetCost(u32 boneCount, u32 boundTracks);

		struct BudgetCandidate {
			u32 row = 0;
			u32 cost = 0;
			f32 significance = 0.0f;
			u32 sinceUpdate = 0;
		};

		u32 BudgetSelect(std::span<const BudgetCandidate> candidates, u32 budget, u32 maxInterval,
						 Vector<u32>& outEvaluate);

	} // namespace AnimationImpl

} // namespace Shalltear
