#pragma once

#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Types.h"

namespace SamplePackageImpl {

	using namespace Shalltear;

	class SamplePackageBehavior : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(SamplePackageBehavior, Behavior)
		SLT_REFLECT_FIELD(_ticks)
		SLT_REFLECT_FIELD(_seconds)
		SLT_REFLECT_FIELD(_fixedTicks)
		SLT_REFLECT_FIELD(_fixedStep)
		SLT_END_REFLECT()

		i32 GetTicks() const { return _ticks; }
		f32 GetSeconds() const { return _seconds; }

		i32 GetFixedTicks() const { return _fixedTicks; }
		f32 GetFixedStep() const { return _fixedStep; }
	protected:
		void OnUpdate() override;
		void OnFixedUpdate() override;
	private:
		i32 _ticks = 0;
		f32 _seconds = 0.0f;
		i32 _fixedTicks = 0;
		f32 _fixedStep = 0.0f;
	};

	class SamplePausedMenuBehavior : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(SamplePausedMenuBehavior, Behavior)
		SLT_REFLECT_FIELD(_paused)
		SLT_REFLECT_FIELD(_menuSeconds)
		SLT_END_REFLECT()

		bool IsPaused() const { return _paused; }
		void SetPaused(bool paused) { _paused = paused; }

		f32 GetMenuSeconds() const { return _menuSeconds; }
	protected:
		void OnUpdate() override;
	private:
		bool _paused = false;
		f32 _menuSeconds = 0.0f;

		bool _applied = false;
	};

} // namespace SamplePackageImpl
