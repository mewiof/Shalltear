#include "EffectPass.h"

#include "EffectAsset.h"
#include "EffectInstance.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectBinder.h"
#include "Shalltear/VisualEffects/VisualEffectListener.h"

#include <algorithm>
#include <span>

namespace Shalltear::VisualEffectsImpl {

	struct BinderAccess {
		static void Elapse(VisualEffectBinder& binder, f32 deltaTime) { binder._sinceUpdate += deltaTime; }
		static f32 Take(VisualEffectBinder& binder) {
			const f32 since = binder._sinceUpdate;
			binder._sinceUpdate = 0.0f;
			return since;
		}
	};

	struct EventAttributesAccess {
		static void Assign(VisualEffectEventAttributes& to, std::span<const EffectCarried> carried, const f32* values, u32 count) {
			Vector<VisualEffectEventAttributes::Entry>& entries = to._entries;
			entries.resize(carried.size() + 1);
			u32 at = 0;
			const auto put = [&entries](size_t i, std::string_view name, const Vec4& value) {
				if (entries[i].attribute != name) {
					entries[i].attribute = str(name);
				}
				entries[i].value = value;
			};
			for (size_t i = 0; i < carried.size(); ++i) {
				f32 v[4] = {};
				for (u32 c = 0; c < carried[i].components; ++c) {
					v[c] = values[at++];
				}
				put(i, carried[i].attribute, Vec4(v[0], v[1], v[2], v[3]));
			}
			put(carried.size(), "spawnCount", Vec4(static_cast<f32>(count), 0.0f, 0.0f, 0.0f));
		}
	};

	namespace {

		Vector<Handle<VisualEffect>>* _effects = nullptr;
		Vector<Handle<VisualEffectBinder>>* _binders = nullptr;

		struct Delivery {
			Vector<EffectSent> sent{GetMemoryResource(MemTag::Graphics)};
			Vector<f32> values{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectFlowSent> flows{GetMemoryResource(MemTag::Graphics)};
			Vector<Handle<Component>> listeners{GetMemoryResource(MemTag::Graphics)};
			VisualEffectEventAttributes attributes;
			str name;
		};
		Delivery* _delivery = nullptr;

		EffectClock _clock;
		const VisualEffectsSettings* _settings = nullptr;
		const VisualEffectsSettings _defaultSettings;
		Vector<f32>* _updates = nullptr;
		EffectFrameCounts _advanced;

		Vector<Handle<VisualEffect>>& Effects() {
			if (!_effects) {
				_effects = New<Vector<Handle<VisualEffect>>>(MemTag::Graphics, GetMemoryResource(MemTag::Graphics));
			}
			return *_effects;
		}

		Vector<Handle<VisualEffectBinder>>& Binders() {
			if (!_binders) {
				_binders = New<Vector<Handle<VisualEffectBinder>>>(MemTag::Graphics, GetMemoryResource(MemTag::Graphics));
			}
			return *_binders;
		}

		void LateUpdateStage(const FrameStageArgs&, void*) {
			const f32 deltaTime = Time::GetDeltaTime();
			UpdateBinders(deltaTime);
			AdvanceEffects(TickEffectClock(deltaTime, Time::GetUnscaledDeltaTime()));
			DeliverEffectEvents();
		}

	} // anonymous namespace

	void RegisterEffect(VisualEffect& effect) {
		Vector<Handle<VisualEffect>>& effects = Effects();
		const Handle<VisualEffect> handle(&effect);
		if (std::find(effects.begin(), effects.end(), handle) == effects.end()) {
			effects.push_back(handle);
		}
	}

	void UnregisterEffect(VisualEffect& effect) {
		if (_effects) {
			std::erase(*_effects, Handle<VisualEffect>(&effect));
		}
	}

	u32 GetEffectCount() { return _effects ? static_cast<u32>(_effects->size()) : 0u; }

	void RegisterBinder(VisualEffectBinder& binder) {
		Vector<Handle<VisualEffectBinder>>& binders = Binders();
		const Handle<VisualEffectBinder> handle(&binder);
		if (std::find(binders.begin(), binders.end(), handle) == binders.end()) {
			binders.push_back(handle);
		}
	}

	void UnregisterBinder(VisualEffectBinder& binder) {
		if (_binders) {
			std::erase(*_binders, Handle<VisualEffectBinder>(&binder));
		}
	}

	u32 GetBinderCount() { return _binders ? static_cast<u32>(_binders->size()) : 0u; }

	void UpdateBinders(f32 deltaTime) {
		if (!_binders) {
			return;
		}
		std::erase_if(*_binders, [](const Handle<VisualEffectBinder>& handle) { return handle.Get() == nullptr; });
		const Vector<Handle<VisualEffectBinder>> binders = *_binders;
		for (const Handle<VisualEffectBinder>& handle : binders) {
			VisualEffectBinder* binder = handle.Get();
			if (!binder || !binder->IsEffectivelyEnabled()) {
				continue;
			}
			BinderAccess::Elapse(*binder, deltaTime);
			SceneObject* owner = binder->GetOwner().Get();
			VisualEffect* effect = owner ? owner->GetComponent<VisualEffect>().Get() : nullptr;
			if (effect && binder->IsValid(*effect)) {
				binder->UpdateBinding(*effect, BinderAccess::Take(*binder));
			}
		}
	}

	void GetEffects(Vector<Handle<VisualEffect>>& out) {
		out.clear();
		if (!_effects) {
			return;
		}
		for (const Handle<VisualEffect>& handle : *_effects) {
			const VisualEffect* effect = handle.Get();
			if (effect && effect->IsEffectivelyEnabled()) {
				out.push_back(handle);
			}
		}
	}

	EffectFrameTime TickEffectClock(f32 deltaTime, f32 unscaledDeltaTime) {
		return _clock.Advance(deltaTime, unscaledDeltaTime, GetEffectSettings());
	}

	const VisualEffectsSettings& GetEffectSettings() { return _settings ? *_settings : _defaultSettings; }

	void AdvanceEffects(const EffectFrameTime& time) {
		SLT_PROFILE_ZONE("Effects.Update");
		_advanced = {};
		if (!_effects) {
			return;
		}
		if (!_updates) {
			_updates = New<Vector<f32>>(MemTag::Graphics, GetMemoryResource(MemTag::Graphics));
		}
		std::erase_if(*_effects, [](const Handle<VisualEffect>& handle) { return handle.Get() == nullptr; });
		GetSimulation().SetBatchEmptyLifetime(GetEffectSettings().batchEmptyLifetime);
		const Vector<Handle<VisualEffect>> effects = *_effects;
		for (const Handle<VisualEffect>& handle : effects) {
			VisualEffect* effect = handle.Get();
			if (!effect || !effect->IsEffectivelyEnabled()) {
				continue;
			}
			EffectInstance* instance = EffectComponentAccess::Instance(*effect);
			const VisualEffectAsset* asset = effect->asset.Get();
			if (!instance || !asset) {
				continue;
			}
			instance->SetSeeding(effect->startSeed, effect->resetSeedOnPlay);
			if (!instance->IsBuiltFrom(*asset) || instance->AllowsInstancing() != effect->allowInstancing) {
				instance->SetInstancing(effect->allowInstancing);
				if (!instance->Build(*asset)) {
					continue;
				}
				instance->QueueEvent(effect->initialEventName, nullptr);
			}
			if (!instance->TakeAdvance()) {
				continue;
			}
			const EffectTiming& timing = EffectAssetAccess::Description(*asset).timing;
			const SceneObject* owner = effect->GetOwner().Get();
			const Mat4 localToWorld = owner ? owner->GetWorldMatrix() : Mat4::Identity();
			const std::span<const VisualEffectPropertyOverride> overrides{effect->properties.data(), effect->properties.size()};
			const bool culled = effect->IsCulled() && !instance->IsFresh();
			if (culled && timing.cullSimulation) {
				instance->Hold(localToWorld, overrides, !timing.cullBoundsUpdate);
				_advanced.culled += 1;
				continue;
			}
			_updates->clear();
			EffectUpdates(timing, time, effect->playRate * instance->GetControlRate(), *_updates);
			const u32 ran = instance->Advance(*_updates, localToWorld, overrides, !(culled && timing.cullBoundsUpdate));
			_advanced.effects += 1;
			_advanced.updates += ran;
			_advanced.systemUpdates += ran * instance->GetSystemCount();
		}
	}

	EffectFrameCounts GetEffectFrameCounts() {
		EffectFrameCounts counts = _advanced;
		const ParticleSimulation::FrameCounts& simulated = GetSimulation().GetFrameCounts();
		counts.dispatches = simulated.dispatches;
		counts.copies = simulated.copies;
		counts.draws = simulated.draws;
		counts.batches = simulated.batches;
		counts.alive = simulated.alive;
		counts.dropped = simulated.dropped;
		return counts;
	}

	void DeliverEffectEvents() {
		if (!_effects) {
			return;
		}
		if (!_delivery) {
			_delivery = New<Delivery>(MemTag::Graphics);
		}
		Delivery& delivery = *_delivery;
		const Vector<Handle<VisualEffect>> effects = *_effects;
		for (const Handle<VisualEffect>& handle : effects) {
			VisualEffect* effect = handle.Get();
			EffectInstance* instance = effect ? EffectComponentAccess::Instance(*effect) : nullptr;
			if (!instance) {
				continue;
			}
			instance->TakeSent(delivery.sent, delivery.values);
			instance->TakeFlowSent(delivery.flows);
			const Handle<VisualEffectAsset> asset = instance->GetAsset();
			const SceneObject* owner = effect->GetOwner().Get();
			if ((delivery.sent.empty() && delivery.flows.empty()) || !asset.Get() || !owner) {
				continue;
			}
			const u32 generation = EffectAssetAccess::Generation(*asset.Get());
			delivery.listeners.clear();
			for (u32 index = 0; index < owner->GetComponentCount(); ++index) {
				Handle<Component> held = owner->GetComponentAt(index);
				Component* component = held.Get();
				if (component && !component->IsPendingDestroy() && dynamic_cast<VisualEffectListener*>(component)) {
					delivery.listeners.push_back(held);
				}
			}
			const auto current = [&]() -> const VisualEffectAsset* {
				const VisualEffectAsset* held = asset.Get();
				return held && EffectAssetAccess::Generation(*held) == generation && handle.Get() ? held : nullptr;
			};
			const auto deliver = [&](const str& name, const VisualEffectEventAttributes& attributes) {
				for (const Handle<Component>& listening : delivery.listeners) {
					Component* component = listening.Get();
					VisualEffect* target = handle.Get();
					VisualEffectListener* listener = component ? dynamic_cast<VisualEffectListener*>(component) : nullptr;
					if (!target) {
						break;
					}
					if (listener && !component->IsPendingDestroy() && component->IsEffectivelyEnabled()) {
						listener->OnVisualEffectOutputEvent(*target, name, attributes);
					}
				}
			};
			for (const EffectFlowSent& sent : delivery.flows) {
				if (!current()) {
					break;
				}
				deliver(sent.name, sent.attributes);
			}
			for (const EffectSent& sent : delivery.sent) {
				const VisualEffectAsset* held = current();
				if (!held) {
					break;
				}
				const EffectSystem& system = EffectAssetAccess::Description(*held).systems[sent.system];
				const Vector<EffectOutputEvent>& events = sent.event.update ? system.updateOutputEvents : system.initializeOutputEvents;
				const Vector<EffectCarried>& carried = sent.event.update ? system.updateCarried : system.initializeCarried;
				delivery.name = events[sent.event.event].name;
				EventAttributesAccess::Assign(delivery.attributes, carried, delivery.values.data() + sent.event.values, sent.event.count);
				deliver(delivery.name, delivery.attributes);
			}
		}
	}

	void ReleaseEffectsOf(const VisualEffectAsset& asset) {
		if (!_effects) {
			return;
		}
		for (const Handle<VisualEffect>& handle : *_effects) {
			VisualEffect* effect = handle.Get();
			EffectInstance* instance = effect ? EffectComponentAccess::Instance(*effect) : nullptr;
			if (instance && instance->IsBuiltFrom(asset)) {
				instance->Release();
			}
		}
	}

	void RegisterEffectStage(const void* owner) {
		_settings = Settings::DeclareCategory<VisualEffectsSettings>(EFFECT_SETTINGS_PATH, SettingScope::Project);
		if (!_settings) {
			SLT_LOG_ERROR("VisualEffects", "couldn't register the {} settings category", EFFECT_SETTINGS_PATH);
		}
		FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &LateUpdateStage, nullptr, owner);
	}

	void ShutdownEffects() {
		if (_effects) {
			Delete(_effects);
			_effects = nullptr;
		}
		if (_binders) {
			Delete(_binders);
			_binders = nullptr;
		}
		if (_delivery) {
			Delete(_delivery);
			_delivery = nullptr;
		}
		if (_updates) {
			Delete(_updates);
			_updates = nullptr;
		}
		_settings = nullptr;
		_clock.Reset();
		_advanced = {};
	}

} // namespace Shalltear::VisualEffectsImpl
