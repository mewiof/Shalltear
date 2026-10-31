#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Animation/AnimatorController.h"
#include "Shalltear/Animation/RetargetProfile.h"

#include "AnimationPass.h"
#include "AnimationSettings.h"

#if defined(SLT_WITH_EDITOR)
#include "AnimationEditor.h"
#include "ClipContributor.h"
#endif

namespace Shalltear {

	void RegisterAnimatorControllerImporter(const void* owner);
	void RegisterAnimationClipImporter(const void* owner);

	namespace AnimationImpl {

		constexpr const char* PACKAGE_DISPLAY_NAME = "Animation";

		constexpr const char* SETTINGS_CATEGORY_PATH = "Animation";

		void RegisterPackage(const void* owner) {
			const AnimationSettings* settings =
				Settings::DeclareCategory<AnimationSettings>(SETTINGS_CATEGORY_PATH, SettingScope::Project, false);
			if (!settings) {
				SLT_LOG_ERROR("Animation", "couldn't register the {} settings category", SETTINGS_CATEGORY_PATH);
			}
			AnimationImpl::SetDeclaredSettings(settings);

#if defined(SLT_WITH_EDITOR)
			const bool contributed = AnimationEditor::RegisterClipContributor(owner);
#else
			const bool contributed = false;
#endif

			AnimationImpl::RegisterStages(owner);

			// static player drops types without these
			AnimationClip::GetReflectType();
			Animator::GetReflectType();
			RetargetProfile::GetReflectType();

			AnimatorController::GetReflectType();
			AnimatorStateMachine::GetReflectType();
			RegisterAnimatorControllerImporter(owner);

			RegisterAnimationClipImporter(owner);

			SLT_LOG_INFO("Animation", "{} up: clips/layers/retargeting/controllers, {}", PACKAGE_DISPLAY_NAME,
						 contributed ? "importing from models" : "no importer half in this build");
		}

		void ShutdownPackage() {
#if defined(SLT_WITH_EDITOR)
			AnimationEditor::UnregisterClipContributor(nullptr);
			AnimationEditor::ShutdownModelAnimationInspector();
#endif
			AnimationImpl::SetDeclaredSettings(nullptr);

			AnimationImpl::ShutdownPass();
		}

	} // namespace AnimationImpl

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(Animation,
									&Shalltear::AnimationImpl::RegisterPackage,
									&Shalltear::AnimationImpl::ShutdownPackage)
