#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Types.h"

#include "EffectPass.h"
#include "ParticleSimulation.h"
#include "Shalltear/VisualEffects/VisualEffect.h"
#include "Shalltear/VisualEffects/VisualEffectAsset.h"
#include "Shalltear/VisualEffects/VisualEffectBinder.h"

#if defined(SLT_WITH_EDITOR)
#include "VisualEffectsEditor.h"
#endif

namespace Shalltear::VisualEffectsImpl {

	constexpr const char* PACKAGE_DISPLAY_NAME = "Visual Effects";

	void RegisterPackage(const void* owner) {
		VisualEffectAsset::GetReflectType();
		VisualEffect::GetReflectType();
		VisualEffectPositionBinder::GetReflectType();
		VisualEffectPreviousPositionBinder::GetReflectType();
		VisualEffectVelocityBinder::GetReflectType();
		VisualEffectTransformBinder::GetReflectType();
		VisualEffectEnabledBinder::GetReflectType();
#if defined(SLT_WITH_EDITOR)
		VisualEffectsEditor::RegisterImporter(owner);
#endif
		RegisterSimulation(owner);
		RegisterEffectStage(owner);
		SLT_LOG_INFO("VisualEffects", "{} up: the GPU particle simulation", PACKAGE_DISPLAY_NAME);
	}

	void ShutdownPackage() {
#if defined(SLT_WITH_EDITOR)
		VisualEffectsEditor::ShutdownEditor();
#endif
		ShutdownEffects();
		ShutdownSimulation();
	}

} // namespace Shalltear::VisualEffectsImpl

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(VisualEffects,
									&Shalltear::VisualEffectsImpl::RegisterPackage,
									&Shalltear::VisualEffectsImpl::ShutdownPackage)
