#include "Shalltear/Core/Graphics/SpriteAsset.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"

namespace Shalltear {

	SpriteAsset::SpriteAsset(const Guid& texture, std::string_view identity) : _texture(texture), _identity(identity) {}

	SpriteAsset::~SpriteAsset() = default;

	void SpriteAsset::AdoptReimport(Asset& fresh) {
		const SpriteAsset& imported = static_cast<const SpriteAsset&>(fresh);
		if (imported._texture == _texture && imported._identity == _identity) {
			return;
		}
		OnUnload();
		_texture = imported._texture;
		_identity = imported._identity;
	}

	bool SpriteAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;
		if (!_texture.IsValid() || _identity.empty()) {
			return false;
		}
		descriptor.WriteAssetRef(_texture);
		descriptor.WriteString(_identity);
		return true;
	}

	bool SpriteAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;
		Guid texture;
		descriptor.Read(texture);
		const std::string_view identity = descriptor.ReadString();
		if (descriptor.Failed() || !texture.IsValid() || identity.empty()) {
			return false;
		}
		_texture = texture;
		_identity = str(identity);
		return true;
	}

	const TextureAsset* SpriteAsset::GetTexture() const {
		if (const TextureAsset* live = _held.Get()) {
			return live;
		}
		_holds = false;
		if (!_texture.IsValid() || _refused) {
			return nullptr;
		}
		Asset* resolved = AssetDatabase::LoadAssetByGuid(_texture);
		if (!resolved || resolved->GetAssetTypeId() != TextureAsset::GetReflectType()->name) {
			if (resolved) {
				AssetDatabase::UnloadAsset(resolved);
			}
			_refused = true;
			SLT_LOG_WARN("Graphics", "sprite '{}' uses texture {} (not a loadable texture, draws nothing)", _identity, _texture.ToString());
			return nullptr;
		}
		auto* loaded = static_cast<TextureAsset*>(resolved);
		_held = Handle<TextureAsset>(loaded);
		_holds = true;
		return loaded;
	}

	const TextureSprite* SpriteAsset::GetDefinition() const {
		const TextureAsset* texture = GetTexture();
		return texture ? texture->FindSprite(_identity) : nullptr;
	}

	void SpriteAsset::OnUnload() {
		TextureAsset* texture = _held.Get();
		if (texture && _holds) {
			AssetDatabase::UnloadAsset(texture);
		}
		_held = {};
		_holds = false;
		_refused = false;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::SpriteAsset)
