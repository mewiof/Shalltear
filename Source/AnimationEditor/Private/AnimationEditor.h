#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class AnimatorController;
	class AnimatorStateMachine;

	namespace AnimationEditor {

		AnimatorStateMachine* FindPlayingComponent(const AnimatorController* controller);

		void RegisterAnimatorControllerInspector(const void* owner);

		void RegisterModelRigInspector(const void* owner);

		void RegisterModelAnimationInspector(const void* owner);

		void ShutdownModelAnimationInspector();

		void RegisterComponentPresentation(const void* owner);

		void RegisterAssetBadges(const void* owner);

		void RegisterAnimatorOpener(const void* owner);

		void RegisterEditorExtensions(const void* owner);

	} // namespace AnimationEditor

} // namespace Shalltear
