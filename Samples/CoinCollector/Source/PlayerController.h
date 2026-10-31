#pragma once

#include "Shalltear.h"

// moves the player with the keyboard/a gamepad + collects the coins it touches
//
// Behavior FIRST -> PhysicsListener second. A component -> hears physics
// events is a Behavior -> also implements the listener (in that order)
class PlayerController : public Behavior, public PhysicsListener {
public:
	PlayerController();

	SLT_REFLECT_COMPONENT(PlayerController, Behavior)
	SLT_REFLECT_FIELD(speed)
	SLT_REFLECT_FIELD(pickupSound)
	SLT_END_REFLECT()

	// how fast the player runs, in m/s
	f32 speed = 6.0f;
	// played by the player's AudioSource each time a coin is collected
	Handle<AudioClip> pickupSound;

	i32 GetCoins() const { return _coins; }

protected:
	void OnEnable() override;
	void OnDisable() override;
	void OnUpdate() override;
	void OnTriggerEnter(Handle<Collider> other) override;

private:
	// set of actions, each with the keys/gamepad controls bound to it.
	// Reads input only while enabled -> OnEnable/OnDisable turn it on/off
	// with the component
	InputActionMap _controls;
	ActionId _move;
	i32 _coins = 0;
};
