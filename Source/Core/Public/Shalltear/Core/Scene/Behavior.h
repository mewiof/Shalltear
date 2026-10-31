#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <type_traits>

namespace Shalltear {

	enum class BehaviorTick : u8 {
		None = 0,
		Update = 1 << 0,
		FixedUpdate = 1 << 1,
		LateUpdate = 1 << 2,
	};
	SLT_ENUM_CLASS_FLAGS(BehaviorTick)

	class SHALLTEAR_CORE_API Behavior : public Component {
	public:
		~Behavior() override;

		SLT_REFLECT_COMPONENT_ABSTRACT(Behavior, Component)
		SLT_END_REFLECT()

		BehaviorTick GetTickMask() const;
	protected:
		Behavior() { _isScript = true; }

		virtual void OnUpdate() {}
		virtual void OnFixedUpdate() {}
		virtual void OnLateUpdate() {}
	private:
		friend class Scene;
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		u32 _tickSlot[3] = {~0u, ~0u, ~0u};
	};

	inline constexpr StringHash BehaviorTicksAttribute = StringHash("BehaviorTicks");

	namespace BehaviorDetail {

		template <typename C>
		constexpr bool DeclaredBelowBehavior(void (C::*)()) {
			return !std::is_same_v<C, Behavior>;
		}

	} // namespace BehaviorDetail

	template <typename T>
	struct ComponentReflectTraits<T, std::enable_if_t<std::is_base_of_v<Behavior, T>>> {
		static void Describe(ReflectBuilder& builder) {
			constexpr BehaviorTick ticks =
				(BehaviorDetail::DeclaredBelowBehavior(&T::OnUpdate) ? BehaviorTick::Update : BehaviorTick::None) |
				(BehaviorDetail::DeclaredBelowBehavior(&T::OnFixedUpdate) ? BehaviorTick::FixedUpdate : BehaviorTick::None) |
				(BehaviorDetail::DeclaredBelowBehavior(&T::OnLateUpdate) ? BehaviorTick::LateUpdate : BehaviorTick::None);
			builder.AddAttribute(BehaviorTicksAttribute, Variant(static_cast<i64>(ticks)));
		}
	};

} // namespace Shalltear
