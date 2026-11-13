#include "AnimationEditor.h"

#include "Shalltear/Core/Reflection/TypeInfo.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorIcons.h"

#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Animation/AnimatorController.h"
#include "Shalltear/Animation/SpringBone.h"

namespace Shalltear::AnimationEditor {

	namespace {

		constexpr std::string_view AnimatorMark = "engine://Icons/Components/Animator.svg";
		constexpr std::string_view ControllerMark = "engine://Icons/Components/AnimatorController.svg";
		constexpr std::string_view SpringBoneMark = "engine://Icons/Components/SpringBone.svg";
		constexpr std::string_view SpringBoneColliderMark = "engine://Icons/Components/SpringBoneCollider.svg";

		constexpr std::string_view Category = "Animation";

	} // anonymous namespace

	void RegisterComponentPresentation(const void* owner) {
		EditorIcons::ComponentIcons::Register(Animator::GetReflectType()->name, AnimatorMark, owner);
		EditorIcons::ComponentIcons::Register(AnimatorStateMachine::GetReflectType()->name,
											  ControllerMark, owner);
		EditorIcons::ComponentIcons::Register(SpringBone::GetReflectType()->name, SpringBoneMark, owner);
		EditorIcons::ComponentIcons::Register(SpringBoneCollider::GetReflectType()->name, SpringBoneColliderMark, owner);

		ComponentMenu::Register(Animator::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(AnimatorStateMachine::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(SpringBone::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(SpringBoneCollider::GetReflectType()->name, Category, owner);
	}

} // namespace Shalltear::AnimationEditor
