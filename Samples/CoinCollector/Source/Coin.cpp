#include "Coin.h"

SLT_REGISTER_TYPE(Coin)

void Coin::OnUpdate() {
	SceneObject *object = GetOwner().Get();
	if (!object) {
		return;
	}
	// step goes on the LEFT (which turns the coin about the world's up
	// axis). The coin is tipped on its edge, so its own up axis points sideways
	// (a step on the right would roll it instead)
	const Quat step = Quat::AngleAxis(degreesPerSecond * Time::GetDeltaTime(), Vec3::Up());
	object->SetLocalRotation((step * object->GetLocalRotation()).Normalized());
}
