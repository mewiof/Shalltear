#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/VisualEffectsAPI.h"

namespace Shalltear {

	class VisualEffect;

	namespace VisualEffectsImpl {
		struct BinderAccess;
	} // namespace VisualEffectsImpl

	enum class VisualEffectBinderSpace : u8 {
		Automatic,
		World,
		Local
	};

	enum class VisualEffectEnabledCheck : u8 {
		ActiveInHierarchy,
		ActiveSelf
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::VisualEffectBinderSpace)
SLT_ENUM_VALUE(Automatic)
SLT_ENUM_VALUE(World)
SLT_ENUM_VALUE(Local)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::VisualEffectEnabledCheck)
SLT_ENUM_VALUE(ActiveInHierarchy)
SLT_ENUM_VALUE(ActiveSelf)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectBinder : public Component {
	public:
		SLT_REFLECT_COMPONENT_ABSTRACT(VisualEffectBinder, Component)
		SLT_END_REFLECT()

		virtual bool IsValid(const VisualEffect& effect) const = 0;
		virtual void UpdateBinding(VisualEffect& effect, f32 deltaTime) = 0;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct VisualEffectsImpl::BinderAccess;
		f32 _sinceUpdate = 0.0f;
	};

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectPositionBinder : public VisualEffectBinder {
	public:
		SLT_REFLECT_COMPONENT(VisualEffectPositionBinder, VisualEffectBinder)
		SLT_REFLECT_FIELD(property)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(space)
		SLT_END_REFLECT()

		str property = "Position";
		Handle<SceneObject> target;
		VisualEffectBinderSpace space = VisualEffectBinderSpace::Automatic;

		bool IsValid(const VisualEffect& effect) const override;
		void UpdateBinding(VisualEffect& effect, f32 deltaTime) override;
	};

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectPreviousPositionBinder : public VisualEffectBinder {
	public:
		SLT_REFLECT_COMPONENT(VisualEffectPreviousPositionBinder, VisualEffectBinder)
		SLT_REFLECT_FIELD(property)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(space)
		SLT_END_REFLECT()

		str property = "PreviousPosition";
		Handle<SceneObject> target;
		VisualEffectBinderSpace space = VisualEffectBinderSpace::Automatic;

		bool IsValid(const VisualEffect& effect) const override;
		void UpdateBinding(VisualEffect& effect, f32 deltaTime) override;
	protected:
		void OnEnable() override;
	private:
		Vec3 _previous{};
		bool _hasPrevious = false;
	};

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectVelocityBinder : public VisualEffectBinder {
	public:
		SLT_REFLECT_COMPONENT(VisualEffectVelocityBinder, VisualEffectBinder)
		SLT_REFLECT_FIELD(property)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(space)
		SLT_END_REFLECT()

		str property = "Velocity";
		Handle<SceneObject> target;
		VisualEffectBinderSpace space = VisualEffectBinderSpace::Automatic;

		bool IsValid(const VisualEffect& effect) const override;
		void UpdateBinding(VisualEffect& effect, f32 deltaTime) override;
	protected:
		void OnEnable() override;
	private:
		Vec3 _previous{};
		bool _hasPrevious = false;
	};

	// degrees, Z -> X -> Y
	class SHALLTEAR_VISUALEFFECTS_API VisualEffectTransformBinder : public VisualEffectBinder {
	public:
		SLT_REFLECT_COMPONENT(VisualEffectTransformBinder, VisualEffectBinder)
		SLT_REFLECT_FIELD(property)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(space)
		SLT_END_REFLECT()

		str property = "Transform";
		Handle<SceneObject> target;
		VisualEffectBinderSpace space = VisualEffectBinderSpace::Automatic;

		bool IsValid(const VisualEffect& effect) const override;
		void UpdateBinding(VisualEffect& effect, f32 deltaTime) override;
	};

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectEnabledBinder : public VisualEffectBinder {
	public:
		SLT_REFLECT_COMPONENT(VisualEffectEnabledBinder, VisualEffectBinder)
		SLT_REFLECT_FIELD(property)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(check)
		SLT_END_REFLECT()

		str property = "Enabled";
		Handle<SceneObject> target;
		VisualEffectEnabledCheck check = VisualEffectEnabledCheck::ActiveInHierarchy;

		bool IsValid(const VisualEffect& effect) const override;
		void UpdateBinding(VisualEffect& effect, f32 deltaTime) override;
	};

} // namespace Shalltear
