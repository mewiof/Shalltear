#include "PlayerController.h"

#include "Coin.h"

SLT_REGISTER_TYPE(PlayerController)

PlayerController::PlayerController() {
	// one action with two axes. x = left/right, y = forward/back.
	// Every binding adds share (W + D together move diagonally)
	_move = _controls.RegisterAction("Move"_hash, ActionType::Axis2D);
	_controls.Bind(_move, {.code = u16(Key::D), .scale = 1.0f, .component = 0});
	_controls.Bind(_move, {.code = u16(Key::A), .scale = -1.0f, .component = 0});
	_controls.Bind(_move, {.code = u16(Key::W), .scale = 1.0f, .component = 1});
	_controls.Bind(_move, {.code = u16(Key::S), .scale = -1.0f, .component = 1});
	// left stick feeds the same two axes
	_controls.Bind(_move, {.source = InputBinding::Source::GamepadAxis, .code = u16(GamepadAxis::LeftX), .component = 0});
	_controls.Bind(_move, {.source = InputBinding::Source::GamepadAxis, .code = u16(GamepadAxis::LeftY), .component = 1});
}

void PlayerController::OnEnable() {
	_controls.Enable();
}

void PlayerController::OnDisable() {
	_controls.Disable();
}

void PlayerController::OnUpdate() {
	// reads this frame's input into the map. Once per frame/before query
	_controls.Evaluate();
	Vec2 input = _controls.GetAxis2D(_move);
	// a key + the stick together can add up past full speed
	if (input.Length() > 1.0f) {
		input = input.Normalized();
	}

	Rigidbody *body = GetOwner()->GetComponent<Rigidbody>().Get();
	if (!body) {
		return;
	}
	// set horizontal speed, keep the vertical one (so the player still
	// falls). The physics engine moves the body. Moving the object directly
	// would skip it through walls
	Vec3 velocity = body->GetLinearVelocity();
	velocity.x = input.x * speed;
	velocity.z = input.y * speed;
	body->SetLinearVelocity(velocity);
}

// runs when player touches a trigger collider (like a coin's)
void PlayerController::OnTriggerEnter(Handle<Collider> other) {
	// what did we touch?
	Collider *collider = other.Get();
	SceneObject *object = collider ? collider->GetOwner().Get() : nullptr;
	Coin *coin = object ? object->GetComponent<Coin>().Get() : nullptr;
	if (!coin) {
		return;
	}

	_coins += coin->value;
	// the sound plays on the PLAYER (which stays). A sound playing on the coin
	// would stop the moment the coin is destroyed
	if (AudioSource *audio = GetOwner()->GetComponent<AudioSource>().Get()) {
		audio->PlayOneShot(pickupSound.Get());
	}
	// the coin disappears at the end of this frame
	SceneSystem::Destroy(collider->GetOwner());
}
