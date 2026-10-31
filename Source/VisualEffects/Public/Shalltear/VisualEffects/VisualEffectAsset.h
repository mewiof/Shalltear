#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/VisualEffectsAPI.h"

namespace Shalltear {

	namespace VisualEffectsImpl {
		struct EffectAssetState;
		struct EffectAssetAccess;
	} // namespace VisualEffectsImpl

	class SHALLTEAR_VISUALEFFECTS_API VisualEffectAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(VisualEffectAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		VisualEffectAsset();
		~VisualEffectAsset() override;
		VisualEffectAsset(const VisualEffectAsset&) = delete;
		VisualEffectAsset& operator=(const VisualEffectAsset&) = delete;

		StringHash GetAssetTypeId() const override { return VisualEffectAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
		bool CookImportState(CookWriter& importState) const override;
		bool LoadImportState(CookReader& importState) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	private:
		friend struct VisualEffectsImpl::EffectAssetAccess;
		VisualEffectsImpl::EffectAssetState* _state = nullptr;
	};

} // namespace Shalltear
