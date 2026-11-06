#include "VisualEffectsEditor.h"

#include "Shalltear/Core/Reflection/TypeInfo.h"

#include "Shalltear/Editor/EditorIcons.h"

#include "Shalltear/VisualEffects/LineRenderer.h"
#include "Shalltear/VisualEffects/TrailRenderer.h"
#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectBinder.h"

namespace Shalltear::VisualEffectsEditor {

	namespace {

		constexpr std::string_view EffectMark = "engine://Icons/Components/VisualEffect.svg";
		constexpr std::string_view BinderMark = "engine://Icons/Components/VisualEffectBinder.svg";
		constexpr std::string_view PositionMark = "engine://Icons/Components/VisualEffectPositionBinder.svg";
		constexpr std::string_view PreviousPositionMark = "engine://Icons/Components/VisualEffectPreviousPositionBinder.svg";
		constexpr std::string_view VelocityMark = "engine://Icons/Components/VisualEffectVelocityBinder.svg";
		constexpr std::string_view TransformMark = "engine://Icons/Components/VisualEffectTransformBinder.svg";
		constexpr std::string_view EnabledMark = "engine://Icons/Components/VisualEffectEnabledBinder.svg";
		constexpr std::string_view LineMark = "engine://Icons/Components/LineRenderer.svg";
		constexpr std::string_view TrailMark = "engine://Icons/Components/TrailRenderer.svg";

	} // anonymous namespace

	void RegisterComponentIcons(const void* owner) {
		using EditorIcons::ComponentIcons::Register;
		Register(VisualEffect::GetReflectType()->name, EffectMark, owner);
		Register(VisualEffectBinder::GetReflectType()->name, BinderMark, owner);
		Register(VisualEffectPositionBinder::GetReflectType()->name, PositionMark, owner);
		Register(VisualEffectPreviousPositionBinder::GetReflectType()->name, PreviousPositionMark, owner);
		Register(VisualEffectVelocityBinder::GetReflectType()->name, VelocityMark, owner);
		Register(VisualEffectTransformBinder::GetReflectType()->name, TransformMark, owner);
		Register(VisualEffectEnabledBinder::GetReflectType()->name, EnabledMark, owner);
		Register(LineRenderer::GetReflectType()->name, LineMark, owner);
		Register(TrailRenderer::GetReflectType()->name, TrailMark, owner);
	}

} // namespace Shalltear::VisualEffectsEditor
