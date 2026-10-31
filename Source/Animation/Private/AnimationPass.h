#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Animation/Animator.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace AnimationImpl {

		void RegisterAnimator(Handle<Animator> animator);
		void UnregisterAnimator(Handle<Animator> animator);

		void RegisterStages(const void* owner);

		void ShutdownPass();

		u32 GetRegisteredAnimatorCount();

	} // namespace AnimationImpl

} // namespace Shalltear
