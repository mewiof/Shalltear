#pragma once

#include "EffectClock.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class VisualEffect;
	class VisualEffectAsset;
	class VisualEffectBinder;

	namespace VisualEffectsImpl {

		void RegisterEffect(VisualEffect& effect);
		void UnregisterEffect(VisualEffect& effect);

		void RegisterBinder(VisualEffectBinder& binder);
		void UnregisterBinder(VisualEffectBinder& binder);
		void UpdateBinders(f32 deltaTime);
		u32 GetBinderCount();

		EffectFrameTime TickEffectClock(f32 deltaTime, f32 unscaledDeltaTime);
		const VisualEffectsSettings& GetEffectSettings();

		void AdvanceEffects(const EffectFrameTime& time);
		void DeliverEffectEvents();
		void ReleaseEffectsOf(const VisualEffectAsset& asset);
		u32 GetEffectCount();

		struct EffectFrameCounts {
			u32 effects = 0;
			u32 updates = 0;
			u32 systemUpdates = 0;
			u32 culled = 0;
			u32 dispatches = 0;
			u32 copies = 0;
			u32 draws = 0;
			u32 batches = 0;
			u32 alive = 0;
			u32 dropped = 0;
		};
		EffectFrameCounts GetEffectFrameCounts();
		void GetEffects(Vector<Handle<VisualEffect>>& out);

		void RegisterEffectStage(const void* owner);
		void ShutdownEffects();

	} // namespace VisualEffectsImpl

} // namespace Shalltear
