#include "SamplePackageBehavior.h"

#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/Time.h"

namespace SamplePackageImpl {

	void SamplePackageBehavior::OnUpdate() {
		++_ticks;
		_seconds += Time::GetDeltaTime();
	}

	void SamplePackageBehavior::OnFixedUpdate() {
		++_fixedTicks;
		_fixedStep = Time::GetDeltaTime();
	}

	void SamplePausedMenuBehavior::OnUpdate() {
		if (Input::WasKeyPressed(Key::Escape)) {
			_paused = !_paused;
		}

		if (_paused != _applied) {
			_applied = _paused;
			Time::SetTimeScale(_paused ? 0.0f : 1.0f);
		}

		_menuSeconds += Time::GetUnscaledDeltaTime();
	}

} // namespace SamplePackageImpl

SLT_REGISTER_TYPE(SamplePackageImpl::SamplePackageBehavior)
SLT_REGISTER_TYPE(SamplePackageImpl::SamplePausedMenuBehavior)
