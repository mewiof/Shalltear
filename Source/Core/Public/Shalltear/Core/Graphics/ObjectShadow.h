#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	// members: renderers on owner/children, minus nested ObjectShadow subtrees
	class SHALLTEAR_CORE_API ObjectShadow : public Component {
	public:
		SLT_REFLECT_COMPONENT(ObjectShadow, Component)
		SLT_REFLECT_FIELD(_priority)
		SLT_END_REFLECT()

		ObjectShadow() = default;
		~ObjectShadow() override;

		// higher picked 1st, then nearest to camera
		i32 GetPriority() const { return _priority; }
		void SetPriority(i32 priority) { _priority = priority; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		i32 _priority = 0;
	};

} // namespace Shalltear
