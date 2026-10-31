#include "Shalltear/Core/Scene/Behavior.h"

#include "Shalltear/Core/Reflection/Walk.h"

namespace Shalltear {

	Behavior::~Behavior() = default;

	BehaviorTick Behavior::GetTickMask() const {
		const AttributeInfo* recorded = FindTypeAttribute(GetType(), BehaviorTicksAttribute);
		const i64* ticks = recorded ? recorded->value.TryGet<i64>() : nullptr;
		return ticks ? static_cast<BehaviorTick>(*ticks) : BehaviorTick::None;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Behavior)
