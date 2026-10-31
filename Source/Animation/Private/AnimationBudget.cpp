#include "AnimationBudget.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Matrix.h"

#include <algorithm>

namespace Shalltear {

	namespace AnimationImpl {

		namespace {

			constexpr f32 BEHIND_SCALE = 0.15f;

			constexpr f32 MAX_SCREEN_RATIO = 64.0f;

			constexpr u32 WAIT_SPREAD_CAP = 64;

		} // anonymous namespace

		f32 BudgetSignificance(std::span<const BudgetViewpoint> views, const Vec3& position, f32 radius, f32 priority) {
			f32 best = 0.0f;
			for (const BudgetViewpoint& view : views) {
				const Vec3 toObject = position - view.position;
				const f32 distanceSq = toObject.LengthSquared();

				f32 ratio;
				if (!(distanceSq > 0.0001f)) {
					ratio = MAX_SCREEN_RATIO;
				} else {
					const f32 distance = Math::Sqrt(distanceSq);
					ratio = Math::Clamp((radius / distance) / view.tanHalfFOV, 0.0f, MAX_SCREEN_RATIO);
					if (Vec3::Dot(toObject, view.forward) < 0.0f) {
						ratio *= BEHIND_SCALE;
					}
				}
				best = Math::Max(best, ratio);
			}
			return best + priority;
		}

		f32 BudgetSkeletonRadius(const SkeletonAsset& skeleton) {
			const size_t boneCount = skeleton.restPose.size();
			if (boneCount == 0 || skeleton.parents.size() != boneCount) {
				return 1.0f;
			}

			Vector<Mat4> model{GetMemoryResource(MemTag::Scene)};
			model.resize(boneCount);
			f32 furthestSq = 0.0f;
			for (size_t bone = 0; bone < boneCount; ++bone) {
				const Mat4 local = skeleton.restPose[bone].ToMatrix();
				const i16 parent = skeleton.parents[bone];
				model[bone] = (parent >= 0 && static_cast<size_t>(parent) < bone)
								  ? model[static_cast<size_t>(parent)] * local
								  : local;
				const Vec3 position{model[bone](0, 3), model[bone](1, 3), model[bone](2, 3)};
				furthestSq = Math::Max(furthestSq, position.LengthSquared());
			}
			const f32 furthest = Math::Sqrt(furthestSq);
			return furthest > 0.0f ? furthest : 1.0f;
		}

		u32 BudgetCost(u32 boneCount, u32 boundTracks) {
			const u64 total = static_cast<u64>(boneCount) + static_cast<u64>(boundTracks);
			return total > 0 ? static_cast<u32>(Math::Min<u64>(total, 0xFFFFFFFFull)) : 1u;
		}

		u32 BudgetSelect(std::span<const BudgetCandidate> candidates, u32 budget, u32 maxInterval,
						 Vector<u32>& outEvaluate) {
			outEvaluate.clear();
			if (candidates.empty()) {
				return 0;
			}

			if (budget == 0) {
				for (const BudgetCandidate& candidate : candidates) {
					outEvaluate.push_back(candidate.row);
				}
				return 0;
			}

			u64 total = 0;
			for (const BudgetCandidate& candidate : candidates) {
				total += candidate.cost;
			}
			if (total <= budget) {
				for (const BudgetCandidate& candidate : candidates) {
					outEvaluate.push_back(candidate.row);
				}
				return static_cast<u32>(total);
			}

			Vector<u32> order{GetMemoryResource(MemTag::Scene)};
			order.reserve(candidates.size());
			for (u32 index = 0; index < static_cast<u32>(candidates.size()); ++index) {
				order.push_back(index);
			}
			std::sort(order.begin(), order.end(), [&](u32 a, u32 b) {
				if (candidates[a].significance != candidates[b].significance) {
					return candidates[a].significance > candidates[b].significance;
				}
				return a < b;
			});

			Vector<u8> taken{GetMemoryResource(MemTag::Scene)};
			taken.assign(candidates.size(), 0u);
			u64 spent = 0;
			for (u32 index : order) {
				const BudgetCandidate& candidate = candidates[index];
				if (spent + candidate.cost > budget) {
					continue;
				}
				spent += candidate.cost;
				taken[index] = 1u;
				outEvaluate.push_back(candidate.row);
			}

			const u32 dueAt = maxInterval > 1 ? maxInterval - 1 : 0;
			u32 topWait = dueAt;
			for (const BudgetCandidate& candidate : candidates) {
				topWait = Math::Max(topWait, Math::Min(candidate.sinceUpdate, dueAt + WAIT_SPREAD_CAP));
			}
			for (u32 wait = topWait + 1; wait-- > dueAt;) {
				for (u32 index = 0; index < static_cast<u32>(candidates.size()); ++index) {
					if (taken[index]) {
						continue;
					}
					const u32 waited = Math::Min(candidates[index].sinceUpdate, topWait);
					if (waited != wait) {
						continue;
					}
					taken[index] = 1u;
					spent += candidates[index].cost;
					outEvaluate.push_back(candidates[index].row);
				}
			}

			return static_cast<u32>(Math::Min<u64>(spent, 0xFFFFFFFFull));
		}

	} // namespace AnimationImpl

} // namespace Shalltear
