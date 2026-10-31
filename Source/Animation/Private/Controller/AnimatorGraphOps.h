#pragma once

#include "Shalltear/Animation/AnimatorController.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	// mutations bump graphRevision, layout edits don't
	namespace AnimatorGraphOps {

		i32 AddState(AnimatorController& controller, i32 machine, std::string_view name, Vec2 position);

		bool RemoveState(AnimatorController& controller, i32 state);

		i32 SetStateClip(AnimatorController& controller, i32 state, const Guid& clipGuid,
						 Handle<AnimationClip> clip);

		i32 AddTransition(AnimatorController& controller, i32 fromState, i32 toState);
		bool RemoveTransition(AnimatorController& controller, i32 fromState, i32 index);

		i32 AddAnyStateTransition(AnimatorController& controller, i32 machine, i32 toState);
		bool RemoveAnyStateTransition(AnimatorController& controller, i32 machine, i32 index);

		bool SetDefaultState(AnimatorController& controller, i32 machine, i32 state);

		i32 AddMachine(AnimatorController& controller, i32 parent, std::string_view name, Vec2 position);

		bool RemoveMachine(AnimatorController& controller, i32 machine);

		u32 MachineAncestry(const AnimatorController& controller, i32 machine, i32* out, u32 capacity);

		bool MachineContains(const AnimatorController& controller, i32 ancestor, i32 machine);

		bool StatePath(const AnimatorController& controller, i32 state, str& out);

		i32 AddParameter(AnimatorController& controller, std::string_view name, AnimatorParameterKind kind);

		bool RemoveParameter(AnimatorController& controller, i32 parameter);

		bool RenameParameter(AnimatorController& controller, i32 parameter, std::string_view name);

		bool MoveParameter(AnimatorController& controller, i32 parameter, i32 index);

		i32 AddLayer(AnimatorController& controller, std::string_view name);

		bool RemoveLayer(AnimatorController& controller, i32 layer);

		bool RenameLayer(AnimatorController& controller, i32 layer, std::string_view name);

		bool SetLayerWeight(AnimatorController& controller, i32 layer, f32 weight);

		bool SetLayerBlend(AnimatorController& controller, i32 layer, AnimationBlendMode blend);

		i32 AddLayerMaskEntry(AnimatorController& controller, i32 layer, std::string_view bone, f32 weight);
		bool RemoveLayerMaskEntry(AnimatorController& controller, i32 layer, i32 entry);
		bool SetLayerMaskEntryWeight(AnimatorController& controller, i32 layer, i32 entry, f32 weight);

		bool NeedsAutoLayout(const AnimatorController& controller, i32 machine);

		bool AutoLayout(AnimatorController& controller, i32 machine);

		str CaptureSnapshot(const AnimatorController& controller);

		bool RestoreSnapshot(AnimatorController& controller, std::string_view snapshot);

	} // namespace AnimatorGraphOps

} // namespace Shalltear
