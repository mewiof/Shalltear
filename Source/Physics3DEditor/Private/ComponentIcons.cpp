#include "Physics3DEditor.h"

#include "Shalltear/Core/Reflection/TypeInfo.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorIcons.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Joints.h"
#include "Shalltear/Physics3D/Rigidbody.h"

namespace Shalltear::Physics3DEditor {

	namespace {

		constexpr std::string_view ColliderMark = "engine://Icons/Components/BoxCollider.svg";
		constexpr std::string_view BoxMark = "engine://Icons/Components/BoxCollider.svg";
		constexpr std::string_view SphereMark = "engine://Icons/Components/SphereCollider.svg";
		constexpr std::string_view CapsuleMark = "engine://Icons/Components/CapsuleCollider.svg";
		constexpr std::string_view MeshMark = "engine://Icons/Components/MeshCollider.svg";
		constexpr std::string_view RigidbodyMark = "engine://Icons/Components/Rigidbody.svg";

		constexpr std::string_view JointMark = "engine://Icons/Components/Joint.svg";
		constexpr std::string_view HingeMark = "engine://Icons/Components/HingeJoint.svg";
		constexpr std::string_view FixedMark = "engine://Icons/Components/FixedJoint.svg";
		constexpr std::string_view SpringMark = "engine://Icons/Components/SpringJoint.svg";
		constexpr std::string_view CharacterMark = "engine://Icons/Components/CharacterJoint.svg";
		constexpr std::string_view PrismaticMark = "engine://Icons/Components/PrismaticJoint.svg";

		constexpr std::string_view Category = "Physics";

	} // anonymous namespace

	void RegisterComponentIcons(const void* owner) {
		using EditorIcons::ComponentIcons::Register;
		Register(Collider::GetReflectType()->name, ColliderMark, owner);
		Register(BoxCollider::GetReflectType()->name, BoxMark, owner);
		Register(SphereCollider::GetReflectType()->name, SphereMark, owner);
		Register(CapsuleCollider::GetReflectType()->name, CapsuleMark, owner);
		Register(MeshCollider::GetReflectType()->name, MeshMark, owner);
		Register(Rigidbody::GetReflectType()->name, RigidbodyMark, owner);
		Register(Joint::GetReflectType()->name, JointMark, owner);
		Register(HingeJoint::GetReflectType()->name, HingeMark, owner);
		Register(FixedJoint::GetReflectType()->name, FixedMark, owner);
		Register(SpringJoint::GetReflectType()->name, SpringMark, owner);
		Register(CharacterJoint::GetReflectType()->name, CharacterMark, owner);
		Register(PrismaticJoint::GetReflectType()->name, PrismaticMark, owner);

		ComponentMenu::Register(Collider::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(Joint::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(Rigidbody::GetReflectType()->name, Category, owner);
	}

} // namespace Shalltear::Physics3DEditor
