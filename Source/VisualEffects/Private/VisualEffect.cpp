#include "Shalltear/VisualEffects/VisualEffect.h"

#include "EffectAsset.h"
#include "EffectInstance.h"
#include "EffectPass.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <algorithm>
#include <bit>

SLT_REGISTER_TYPE(Shalltear::VisualEffect)

namespace Shalltear {

	using namespace VisualEffectsImpl;

	namespace {

		constexpr std::string_view ON_PLAY = "OnPlay";
		constexpr std::string_view ON_STOP = "OnStop";
		constexpr std::string_view PARTICLE_ID = "particleId";

		template <typename Write>
		u32 ReadParticles(const EffectInstance* instance, std::string_view system, std::string_view attribute, u32 width, u32 offset, size_t room,
						  Write&& write) {
			const EffectSystem* described = nullptr;
			const ParticleCpuSystem* cpu = instance ? instance->FindCpuSystem(system, described) : nullptr;
			if (!cpu) {
				return 0;
			}
			const EffectStored* stored = nullptr;
			for (const EffectStored& held : described->stored) {
				stored = held.attribute == attribute ? &held : stored;
			}
			if (!stored || stored->components != width) {
				return 0;
			}
			const bool id = attribute == PARTICLE_ID;
			const std::span<const u32> alive = cpu->GetAlive();
			u32 written = 0;
			for (size_t i = offset; i < alive.size() && written < room; ++i, ++written) {
				f32 values[4] = {};
				for (u32 c = 0; c < width; ++c) {
					const f32 value = cpu->Read(stored->slot + c, alive[i]);
					values[c] = id ? static_cast<f32>(std::bit_cast<u32>(value)) : value;
				}
				write(written, values);
			}
			return written;
		}

	} // anonymous namespace

	void VisualEffectEventAttributes::Set(std::string_view attribute, const Vec4& value) {
		for (Entry& entry : _entries) {
			if (entry.attribute == attribute) {
				entry.value = value;
				return;
			}
		}
		_entries.push_back({str(attribute), value});
	}

	void VisualEffectEventAttributes::SetFloat(std::string_view attribute, f32 value) { Set(attribute, Vec4(value, 0.0f, 0.0f, 0.0f)); }

	void VisualEffectEventAttributes::SetVector3(std::string_view attribute, const Vec3& value) {
		Set(attribute, Vec4(value.x, value.y, value.z, 0.0f));
	}

	void VisualEffectEventAttributes::SetBool(std::string_view attribute, bool value) { Set(attribute, Vec4(value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f)); }

	bool VisualEffectEventAttributes::Find(std::string_view attribute, Vec4& out) const {
		for (const Entry& entry : _entries) {
			if (entry.attribute == attribute) {
				out = entry.value;
				return true;
			}
		}
		return false;
	}

	VisualEffect::VisualEffect() = default;

	VisualEffect::~VisualEffect() {
		if (_instance) {
			Delete(_instance);
			_instance = nullptr;
		}
	}

	void VisualEffect::OnEnable() {
		ProceduralRenderer::OnEnable();
		if (!_instance) {
			_instance = New<EffectInstance>(MemTag::Graphics);
		}
		RegisterEffect(*this);
	}

	void VisualEffect::OnDisable() {
		UnregisterEffect(*this);
		if (_instance) {
			Delete(_instance);
			_instance = nullptr;
		}
		ProceduralRenderer::OnDisable();
	}

	void VisualEffect::Play() { SendEvent(ON_PLAY); }

	void VisualEffect::Stop() { SendEvent(ON_STOP); }

	void VisualEffect::SendEvent(std::string_view name) {
		if (_instance) {
			_instance->QueueEvent(name, nullptr);
		}
	}

	void VisualEffect::SendEvent(std::string_view name, const VisualEffectEventAttributes& attributes) {
		if (_instance) {
			_instance->QueueEvent(name, &attributes);
		}
	}

	void VisualEffect::Reinit() {
		if (_instance) {
			_instance->Reset();
			_instance->QueueEvent(initialEventName, nullptr);
		}
	}

	bool VisualEffect::SetProperty(std::string_view property, const Vec4& value, u32 components, bool integral) {
		const VisualEffectAsset* effect = asset.Get();
		if (!effect) {
			return false;
		}
		for (const EffectProperty& declared : EffectAssetAccess::Description(*effect).properties) {
			if (declared.name != property) {
				continue;
			}
			const bool wholeNumber = declared.type == EffectPropertyType::Integer;
			const bool flag = declared.type == EffectPropertyType::Boolean;
			if (declared.components != components || wholeNumber != integral || (flag && components != 1)) {
				return false;
			}
			for (VisualEffectPropertyOverride& entry : properties) {
				if (entry.name == property) {
					entry.value = value;
					entry.overridden = true;
					return true;
				}
			}
			properties.push_back({str(property), value, true});
			return true;
		}
		return false;
	}

	bool VisualEffect::SetFloat(std::string_view property, f32 value) {
		const VisualEffectAsset* effect = asset.Get();
		if (effect) {
			for (const EffectProperty& declared : EffectAssetAccess::Description(*effect).properties) {
				if (declared.name == property && declared.type == EffectPropertyType::Boolean) {
					return false;
				}
			}
		}
		return SetProperty(property, Vec4(value, 0.0f, 0.0f, 0.0f), 1, false);
	}

	bool VisualEffect::SetInt(std::string_view property, i32 value) { return SetProperty(property, Vec4(static_cast<f32>(value), 0.0f, 0.0f, 0.0f), 1, true); }

	bool VisualEffect::SetBool(std::string_view property, bool value) {
		const VisualEffectAsset* effect = asset.Get();
		if (!effect) {
			return false;
		}
		for (const EffectProperty& declared : EffectAssetAccess::Description(*effect).properties) {
			if (declared.name == property && declared.type != EffectPropertyType::Boolean) {
				return false;
			}
		}
		return SetProperty(property, Vec4(value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f), 1, false);
	}

	bool VisualEffect::SetVector2(std::string_view property, const Vec2& value) { return SetProperty(property, Vec4(value.x, value.y, 0.0f, 0.0f), 2, false); }

	bool VisualEffect::SetVector3(std::string_view property, const Vec3& value) {
		return SetProperty(property, Vec4(value.x, value.y, value.z, 0.0f), 3, false);
	}

	bool VisualEffect::SetVector4(std::string_view property, const Vec4& value) { return SetProperty(property, value, 4, false); }

	u32 VisualEffect::GetParticleCount(std::string_view system) const {
		const EffectSystem* described = nullptr;
		const ParticleCpuSystem* cpu = _instance ? _instance->FindCpuSystem(system, described) : nullptr;
		return cpu ? static_cast<u32>(cpu->GetAlive().size()) : 0u;
	}

	u32 VisualEffect::GetParticles(std::string_view system, std::string_view attribute, std::span<f32> out, u32 offset) const {
		return ReadParticles(_instance, system, attribute, 1, offset, out.size(), [&](u32 at, const f32* values) { out[at] = values[0]; });
	}

	u32 VisualEffect::GetParticles(std::string_view system, std::string_view attribute, std::span<Vec3> out, u32 offset) const {
		return ReadParticles(_instance, system, attribute, 3, offset, out.size(),
							 [&](u32 at, const f32* values) { out[at] = Vec3(values[0], values[1], values[2]); });
	}

	void VisualEffect::Declare(ProceduralPlan& plan) {
		if (_instance) {
			const SceneObject* owner = GetOwner().Get();
			_instance->Declare(plan, owner ? owner->GetWorldMatrix() : Mat4::Identity());
		}
	}

	void VisualEffect::Record(ProceduralContext& context) {
		if (_instance) {
			_instance->Record(context);
		}
	}

	void VisualEffect::RecordView(ComputeContext& context, const ProceduralView& view) {
		if (_instance) {
			_instance->RecordView(context, view);
		}
	}

} // namespace Shalltear
