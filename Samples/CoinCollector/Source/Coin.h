#pragma once

#include "Shalltear.h"

// coin player can collect. It spins so it catches the eye.. the player
// is what notices the touch -> collects it (PlayerController)
class Coin : public Behavior {
public:
	SLT_REFLECT_COMPONENT(Coin, Behavior)
	SLT_REFLECT_FIELD(value)
	SLT_REFLECT_FIELD(degreesPerSecond)
	SLT_END_REFLECT()

	// how many coins this one counts as
	i32 value = 1;
	f32 degreesPerSecond = 90.0f;

protected:
	void OnUpdate() override;
};
