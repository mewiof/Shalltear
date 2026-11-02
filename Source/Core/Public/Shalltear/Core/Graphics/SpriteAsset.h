#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"

#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API SpriteAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(SpriteAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		SpriteAsset() = default;
		SpriteAsset(const Guid& texture, std::string_view identity);
		~SpriteAsset() override;

		StringHash GetAssetTypeId() const override { return GetReflectType()->name; }

		bool CanAdoptReimport() const override { return true; }
		void AdoptReimport(Asset& fresh) override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		// texture loaded on 1st call + held while sprite is loaded
		const TextureAsset* GetTexture() const;

		// null once gone from texture's table, valid until next reimport
		const TextureSprite* GetDefinition() const;

		std::string_view GetIdentity() const { return _identity; }
	protected:
		void OnUnload() override;
	private:
		Guid _texture;
		str _identity;
		mutable Handle<TextureAsset> _held;
		mutable bool _holds = false;
		mutable bool _refused = false;
	};

} // namespace Shalltear
