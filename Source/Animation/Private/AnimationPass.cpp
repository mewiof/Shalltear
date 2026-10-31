#include "AnimationPass.h"

#include "AnimationBudget.h"
#include "AnimationSettings.h"
#include "PoseOps.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"

#include <algorithm>

namespace Shalltear {

	namespace AnimationImpl {

		namespace {

			struct AnimatorRow {
				Handle<Animator> animator;

				u64 skeletonId = 0;
				f32 radius = 1.0f;

				u32 sinceUpdate = 0;
				f32 owedTime = 0.0f;

				bool staggered = false;

				Vector<BoneTransform> target{GetMemoryResource(MemTag::Scene)};
				f32 remaining = 0.0f;
			};

			struct PassState {
				Vector<AnimatorRow> animators{GetMemoryResource(MemTag::Scene)};

				Vector<u32> due{GetMemoryResource(MemTag::Scene)};

				Vector<BudgetCandidate> candidates{GetMemoryResource(MemTag::Scene)};
				Vector<u32> selected{GetMemoryResource(MemTag::Scene)};
				Vector<u8> evaluating{GetMemoryResource(MemTag::Scene)};
				Vector<BudgetViewpoint> viewpoints{GetMemoryResource(MemTag::Scene)};

				Vector<BoneTransform> poseScratch{GetMemoryResource(MemTag::Scene)};

				u32 staggerCursor = 0;
			};

			PassState* _state = nullptr;

			PassState& State() {
				if (!_state) {
					_state = New<PassState>(MemTag::Scene);
				}
				return *_state;
			}

			bool BelongsToStage(const Animator& animator, bool fixedStage) {
				const bool wantsFixed = animator.GetUpdateMode() == AnimatorUpdateMode::AnimatePhysics;
				return wantsFixed == fixedStage;
			}

			f32 DeltaFor(const Animator& animator) {
				return animator.GetUpdateMode() == AnimatorUpdateMode::UnscaledTime ? Time::GetUnscaledDeltaTime()
																					: Time::GetDeltaTime();
			}

			void GatherViewpoints(Vector<BudgetViewpoint>& out) {
				out.clear();
				for (Handle<Camera> handle : RenderSystem::GetCameras()) {
					const Camera* camera = handle.Get();
					if (!camera) {
						continue;
					}
					const SceneObject* owner = camera->GetOwner().Get();
					if (!owner) {
						continue;
					}
					BudgetViewpoint& view = out.emplace_back();
					view.position = owner->GetWorldPosition();
					view.forward = owner->TransformDirection(Vec3::Forward()).Normalized();
					const f32 fov = Math::Clamp(camera->GetFieldOfViewDeg(), 1.0f, 179.0f);
					view.tanHalfFOV = std::tan(fov * 0.5f * Math::DEG_TO_RAD);
				}
			}

			f32 RowSignificance(AnimatorRow& row, Animator& animator, std::span<const BudgetViewpoint> views) {
				const SceneObject* owner = animator.GetOwner().Get();
				if (!owner) {
					return animator.GetPriority();
				}

				if (const Skeleton* rig = animator.GetSkeleton().Get()) {
					const SkeletonAsset* skeleton = rig->GetSkeleton().Get();
					const u64 id = skeleton ? rig->GetSkeleton().GetInstanceId() : 0;
					if (id != row.skeletonId) {
						row.skeletonId = id;
						row.radius = skeleton ? BudgetSkeletonRadius(*skeleton) : 1.0f;
					}
				} else if (row.skeletonId != 0) {
					row.skeletonId = 0;
					row.radius = 1.0f;
				}

				const Vec3 scale = owner->GetLossyScale();
				const f32 widest = Math::Max(Math::Abs(scale.x), Math::Max(Math::Abs(scale.y), Math::Abs(scale.z)));
				return BudgetSignificance(views, owner->GetWorldPosition(), row.radius * widest, animator.GetPriority());
			}

			void ClearInterpolation(AnimatorRow& row) {
				row.target.clear();
				row.target.shrink_to_fit();
				row.remaining = 0.0f;
			}

			void EvaluateThrottled(PassState& state, u32 index, f32 delta) {
				Animator* animator = state.animators[index].animator.Get();
				if (!animator) {
					return;
				}
				Skeleton* rig = animator->GetSkeleton().Get();
				if (!rig) {
					ClearInterpolation(state.animators[index]);
					animator->Evaluate(delta);
					return;
				}

				const std::span<const BoneTransform> lagging = rig->GetLocalPose();
				state.poseScratch.assign(lagging.begin(), lagging.end());

				animator->Evaluate(delta);

				AnimatorRow& row = state.animators[index];
				animator = row.animator.Get();
				rig = animator ? animator->GetSkeleton().Get() : nullptr;
				if (!rig) {
					return;
				}
				const std::span<const BoneTransform> reached = rig->GetLocalPose();
				row.target.assign(reached.begin(), reached.end());
				row.remaining = delta > 0.0f ? delta : 0.0f;

				if (state.poseScratch.size() != row.target.size() || row.remaining <= 0.0f) {
					ClearInterpolation(row);
					return;
				}
				for (size_t bone = 0; bone < state.poseScratch.size(); ++bone) {
					rig->SetBoneLocalPose(static_cast<u32>(bone), state.poseScratch[bone]);
				}
				rig->UpdatePose();
			}

			bool AdvanceInterpolation(AnimatorRow& row, Animator& animator, f32 delta) {
				if (row.target.empty() || !(row.remaining > 0.0f) || !(delta > 0.0f)) {
					return false;
				}
				Skeleton* rig = animator.GetSkeleton().Get();
				if (!rig) {
					return false;
				}
				const std::span<const BoneTransform> current = rig->GetLocalPose();
				if (current.size() != row.target.size()) {
					return false;
				}

				const f32 step = Math::Clamp01(delta / row.remaining);
				for (size_t bone = 0; bone < current.size(); ++bone) {
					rig->SetBoneLocalPose(static_cast<u32>(bone),
										  BlendBone(current[bone], row.target[bone], step));
				}
				rig->UpdatePose();
				row.remaining -= delta;
				return true;
			}

			void RunPass(bool fixedStage) {
				if (!_state) {
					return;
				}
				PassState& state = *_state;

				state.due.clear();
				size_t kept = 0;
				for (size_t i = 0; i < state.animators.size(); ++i) {
					Animator* animator = state.animators[i].animator.Get();
					if (!animator) {
						continue;
					}
					if (kept != i) {
						state.animators[kept] = state.animators[i];
					}
					if (BelongsToStage(*animator, fixedStage)) {
						state.due.push_back(static_cast<u32>(kept));
					}
					++kept;
				}
				state.animators.resize(kept);
				if (state.due.empty()) {
					return;
				}

				for (u32 index : state.due) {
					AnimatorRow& row = state.animators[index];
					row.owedTime += DeltaFor(*row.animator.Get());
				}

				const AnimationSettings& settings = GetSettings();
				const u32 budget = settings.evaluationBudget > 0 ? static_cast<u32>(settings.evaluationBudget) : 0u;
				const u32 maxInterval = static_cast<u32>(Math::Clamp(settings.maxThrottleInterval, 2, 60));

				state.candidates.clear();
				state.candidates.reserve(state.due.size());
				if (budget == 0) {
					for (u32 index : state.due) {
						BudgetCandidate& candidate = state.candidates.emplace_back();
						candidate.row = index;
					}
				} else {
					for (u32 index : state.due) {
						AnimatorRow& row = state.animators[index];
						Animator& animator = *row.animator.Get();
						const Skeleton* rig = animator.GetSkeleton().Get();
						const SkeletonAsset* skeleton = rig ? rig->GetSkeleton().Get() : nullptr;

						u32 tracks = animator.GetBoundTrackCount();
						if (tracks == 0) {
							const AnimationClip* pending = animator.GetClip().Get();
							tracks = pending ? static_cast<u32>(pending->tracks.size()) : 0u;
						}

						BudgetCandidate& candidate = state.candidates.emplace_back();
						candidate.row = index;
						candidate.cost = BudgetCost(skeleton ? skeleton->GetBoneCount() : 0u, tracks);
						candidate.sinceUpdate = row.sinceUpdate;
					}

					u64 total = 0;
					for (const BudgetCandidate& candidate : state.candidates) {
						total += candidate.cost;
					}
					if (total > budget) {
						GatherViewpoints(state.viewpoints);
						const std::span<const BudgetViewpoint> views{state.viewpoints.data(), state.viewpoints.size()};
						for (BudgetCandidate& candidate : state.candidates) {
							AnimatorRow& row = state.animators[candidate.row];
							candidate.significance = RowSignificance(row, *row.animator.Get(), views);
						}
					}
				}

				BudgetSelect(std::span<const BudgetCandidate>(state.candidates.data(), state.candidates.size()),
							 budget, maxInterval, state.selected);

				state.evaluating.assign(state.animators.size(), 0u);
				for (u32 index : state.selected) {
					state.evaluating[index] = 1u;
				}
				for (u32 index : state.due) {
					AnimatorRow& row = state.animators[index];
					if (state.evaluating[index]) {
						continue;
					}
					if (!row.staggered && budget != 0) {
						row.staggered = true;
						row.sinceUpdate = 1u + (state.staggerCursor++ % (maxInterval > 1 ? maxInterval - 1 : 1));
					} else {
						row.sinceUpdate += 1;
					}

					if (Animator* animator = row.animator.Get(); animator && settings.interpolateThrottled) {
						AdvanceInterpolation(row, *animator, DeltaFor(*animator));
					}
				}
				for (u32 index : state.selected) {
					AnimatorRow& row = state.animators[index];
					Animator* animator = row.animator.Get();
					if (!animator) {
						continue;
					}
					const f32 delta = row.owedTime;
					const bool wasThrottled = row.sinceUpdate > 0;
					row.owedTime = 0.0f;
					row.sinceUpdate = 0;
					if (wasThrottled && settings.interpolateThrottled) {
						AdvanceInterpolation(row, *animator, DeltaFor(*animator));
						EvaluateThrottled(state, index, delta);
						continue;
					}
					ClearInterpolation(row);
					animator->Evaluate(delta);
				}
			}

			void UpdateStage(const FrameStageArgs&, void*) { RunPass(false); }
			void FixedStage(const FrameStageArgs&, void*) { RunPass(true); }

		} // anonymous namespace

		void RegisterAnimator(Handle<Animator> animator) {
			if (!animator.IsValid()) {
				return;
			}
			PassState& state = State();
			const u64 id = animator.GetInstanceId();
			AnimatorRow* reuse = nullptr;
			for (AnimatorRow& existing : state.animators) {
				if (existing.animator.GetInstanceId() == id) {
					return;
				}
				if (!reuse && !existing.animator.IsValid()) {
					reuse = &existing;
				}
			}
			if (reuse) {
				*reuse = AnimatorRow{};
				reuse->animator = animator;
				return;
			}
			state.animators.emplace_back().animator = animator;
		}

		void UnregisterAnimator(Handle<Animator> animator) {
			if (!_state) {
				return;
			}
			Vector<AnimatorRow>& list = _state->animators;
			const u64 id = animator.GetInstanceId();
			auto found = std::find_if(list.begin(), list.end(),
									  [id](const AnimatorRow& entry) { return entry.animator.GetInstanceId() == id; });
			if (found != list.end()) {
				*found = AnimatorRow{};
			}
		}

		void RegisterStages(const void* owner) {
			FrameStages::Register(FrameStage::Update, FrameStagePosition::Normal, &UpdateStage, nullptr, owner);
			FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Early, &FixedStage, nullptr, owner);
		}

		void ShutdownPass() {
			if (_state) {
				Delete(_state);
				_state = nullptr;
			}
		}

		u32 GetRegisteredAnimatorCount() {
			if (!_state) {
				return 0u;
			}
			u32 live = 0;
			for (const AnimatorRow& row : _state->animators) {
				live += row.animator.IsValid() ? 1u : 0u;
			}
			return live;
		}

	} // namespace AnimationImpl

} // namespace Shalltear
