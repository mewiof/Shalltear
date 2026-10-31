#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Serialization/HandleResolver.h"
#include "Shalltear/Core/Types.h"

#include <unordered_map>

namespace Shalltear {

	struct AssetInternalAccess {
		static Vector<Handle<Asset>>& BoundAssetsOf(Asset& asset) { return asset._bound; }
	};

} // namespace Shalltear

namespace Shalltear::AssetInternal {

	struct AssetBinder {
		std::unordered_map<Guid, u64> resolved;

		bool Bind(const Guid& guid, u64* outId, Asset** outRetain) {
			*outRetain = nullptr;
			if (auto known = resolved.find(guid); known != resolved.end()) {
				*outId = known->second;
				return true;
			}
			Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
			if (!asset) {
				return false;
			}
			const u64 id = asset->GetInstanceId();
			resolved.emplace(guid, id);
			*outId = id;
			*outRetain = asset;
			return true;
		}
	};

	class AssetBindScope {
	public:
		AssetBindScope() noexcept {
			HandleResolver::PushScope({nullptr, &AssetBindScope::ToHandle, this});
		}

		~AssetBindScope() {
			HandleResolver::PopScope();
			ReleaseUnattached();
		}

		AssetBindScope(const AssetBindScope&) = delete;
		AssetBindScope& operator=(const AssetBindScope&) = delete;

		void AttachTo(Asset& asset) {
			Vector<Handle<Asset>>& bound = AssetInternalAccess::BoundAssetsOf(asset);
			for (Handle<Asset>& held : _retained) {
				bound.push_back(held);
			}
			_retained.clear();
		}
	private:
		static bool ToHandle(void* user, const Guid& guid, StringHash, u64* outId) {
			auto* self = static_cast<AssetBindScope*>(user);
			if (!IsAssetGuid(guid)) {
				return false;
			}
			Asset* retain = nullptr;
			if (!self->_binder.Bind(guid, outId, &retain)) {
				return false;
			}
			if (retain) {
				self->_retained.push_back(Handle<Asset>(retain));
			}
			return true;
		}

		void ReleaseUnattached() {
			for (Handle<Asset>& held : _retained) {
				if (Asset* asset = held.Get()) {
					AssetDatabase::UnloadAsset(asset);
				}
			}
			_retained.clear();
		}

		AssetBinder _binder;
		Vector<Handle<Asset>> _retained{GetMemoryResource(MemTag::Asset)};
	};

} // namespace Shalltear::AssetInternal
