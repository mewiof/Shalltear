#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/VisualEffectAsset.h"
#include "Shalltear/VisualEffects/VisualEffectsAPI.h"

#include <span>
#include <string_view>

namespace Shalltear {

	namespace VisualEffectsImpl {
		class EffectInstance;
		struct EffectComponentAccess;
		struct EventAttributesAccess;
	} // namespace VisualEffectsImpl

	struct VisualEffectPropertyOverride {
		SLT_REFLECT_VALUE(VisualEffectPropertyOverride)
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_FIELD(value)
		SLT_REFLECT_FIELD(overridden)
		SLT_END_REFLECT()

		str name;
		Vec4 value{};
		bool overridden = true;
	};

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectEventAttributes {
	public:
		void SetFloat(std::string_view attribute, f32 value);
		void SetVector3(std::string_view attribute, const Vec3& value);
		void SetBool(std::string_view attribute, bool value);

		bool Find(std::string_view attribute, Vec4& out) const;
	private:
		friend struct VisualEffectsImpl::EventAttributesAccess;
		struct Entry {
			str attribute;
			Vec4 value{};
		};
		void Set(std::string_view attribute, const Vec4& value);
		Vector<Entry> _entries{GetMemoryResource(MemTag::Scene)};
	};

	class SHALLTEAR_VISUALEFFECTS_API VisualEffect : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(VisualEffect, ProceduralRenderer)
		SLT_REFLECT_FIELD(asset)
		SLT_REFLECT_FIELD(startSeed)
		SLT_REFLECT_FIELD(resetSeedOnPlay)
		SLT_REFLECT_FIELD(initialEventName)
		SLT_REFLECT_FIELD(playRate)
		SLT_REFLECT_FIELD(allowInstancing)
		SLT_REFLECT_ARRAY(properties)
		SLT_END_REFLECT()

		VisualEffect();
		~VisualEffect() override;

		Handle<VisualEffectAsset> asset;
		u32 startSeed = 0;
		bool resetSeedOnPlay = true;
		str initialEventName = "OnPlay";
		f32 playRate = 1.0f;
		bool allowInstancing = true;
		Vector<VisualEffectPropertyOverride> properties{GetMemoryResource(MemTag::Scene)};

		void Play();
		void Stop();
		void SendEvent(std::string_view name);
		void SendEvent(std::string_view name, const VisualEffectEventAttributes& attributes);
		void Reinit();

		bool SetFloat(std::string_view property, f32 value);
		bool SetInt(std::string_view property, i32 value);
		bool SetBool(std::string_view property, bool value);
		bool SetVector2(std::string_view property, const Vec2& value);
		bool SetVector3(std::string_view property, const Vec3& value);
		bool SetVector4(std::string_view property, const Vec4& value);

		u32 GetParticleCount(std::string_view system) const;
		u32 GetParticles(std::string_view system, std::string_view attribute, std::span<f32> out, u32 offset = 0) const;
		u32 GetParticles(std::string_view system, std::string_view attribute, std::span<Vec3> out, u32 offset = 0) const;

		void Declare(ProceduralPlan& plan) override;
		void Record(ProceduralContext& context) override;
		void RecordView(ComputeContext& context, const ProceduralView& view) override;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct VisualEffectsImpl::EffectComponentAccess;
		bool SetProperty(std::string_view property, const Vec4& value, u32 components, bool integral);

		VisualEffectsImpl::EffectInstance* _instance = nullptr;
	};

} // namespace Shalltear
