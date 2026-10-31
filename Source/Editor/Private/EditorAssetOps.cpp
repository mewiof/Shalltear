#include "Shalltear/Editor/EditorAssetOps.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		struct AssetOpsState {
			HashSet<u64> dirty{GetMemoryResource(MemTag::Editor)};
			HashMap<u64, u64> written{GetMemoryResource(MemTag::Editor)};
		};

		AssetOpsState* _state = nullptr;

		Asset* ResolveAsset(u64 id) {
			Object* object = ObjectRegistry::Resolve(id);
			if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), Asset::GetReflectType())) {
				return nullptr;
			}
			return static_cast<Asset*>(object);
		}

		void ReleaseAndForget(u64 id) {
			if (Asset* asset = ResolveAsset(id)) {
				AssetDatabase::UnloadAsset(asset);
			}
			_state->dirty.erase(id);
		}

		u64 HashFile(const Path& virtualPath) {
			Buffer bytes = FileSystem::ReadFile(FileSystem::Resolve(virtualPath), MemTag::Editor);
			if (!bytes) {
				return 0;
			}
			u64 hash = 0xcbf29ce484222325ull;
			for (size_t i = 0; i < bytes.size; ++i) {
				hash ^= bytes.data[i];
				hash *= 0x00000100000001b3ull;
			}
			return hash;
		}

		bool HasFile(const Asset& asset) {
			if (!asset.assetId.IsValid()) {
				return true;
			}
			const Guid source = AssetDatabase::GetProductSource(asset.assetId);
			return !AssetDatabase::LookupPath(source.IsValid() ? source : asset.assetId).ToString().empty();
		}

		void Prune() {
			Vector<u64> orphaned{GetMemoryResource(MemTag::Editor)};
			std::erase_if(_state->dirty, [&orphaned](u64 id) {
				const Asset* asset = ResolveAsset(id);
				if (asset && !HasFile(*asset)) {
					orphaned.push_back(id);
				}
				return asset == nullptr;
			});
			for (u64 id : orphaned) {
				ReleaseAndForget(id);
			}
		}

	} // anonymous namespace

	void EditorAssetOps::Initialize() {
		if (!_state) {
			_state = New<AssetOpsState>(MemTag::Editor);
		}
	}

	void EditorAssetOps::Terminate() {
		if (!_state) {
			return;
		}
		DiscardAllDirty();
		Delete(_state);
		_state = nullptr;
	}

	void EditorAssetOps::MarkDirty(Handle<Asset> asset) {
		Asset* live = asset.Get();
		if (!_state || !live) {
			return;
		}
		if (!_state->dirty.insert(asset.GetInstanceId()).second) {
			return;
		}
		live->AddRef();
	}

	bool EditorAssetOps::AnyDirty() {
		if (!_state) {
			return false;
		}
		Prune();
		return !_state->dirty.empty();
	}

	bool EditorAssetOps::IsDirty(Handle<Asset> asset) {
		return _state && asset.Get() && _state->dirty.contains(asset.GetInstanceId());
	}

	void EditorAssetOps::SaveAsset(Handle<Asset> asset) {
		Asset* live = asset.Get();
		if (!live) {
			return;
		}
		AssetDatabase::SaveAsset(live);
		if (_state) {
			// before the release (may take the last reference)
			const Path path(live->virtualPath);
			_state->written[asset.GetInstanceId()] = HashFile(path);
			ReleaseAndForget(asset.GetInstanceId());
		}
	}

	void EditorAssetOps::SaveAllDirty() {
		if (!_state) {
			return;
		}
		Prune();
		Vector<u64> pending{GetMemoryResource(MemTag::Editor)};
		pending.assign(_state->dirty.begin(), _state->dirty.end());
		for (u64 id : pending) {
			if (Asset* asset = ResolveAsset(id)) {
				SaveAsset(Handle<Asset>(asset));
			} else {
				_state->dirty.erase(id);
			}
		}
	}

	void EditorAssetOps::DiscardAllDirty() {
		if (!_state) {
			return;
		}
		Vector<u64> pending{GetMemoryResource(MemTag::Editor)};
		pending.assign(_state->dirty.begin(), _state->dirty.end());
		for (u64 id : pending) {
			ReleaseAndForget(id);
		}
	}

	u32 EditorAssetOps::ReloadChanged(std::span<const Path> changed, const ReloadConflict& onConflict) {
		u32 reimported = 0;
		for (const Path& path : changed) {
			Asset* loaded = AssetDatabase::FindLoadedByGuid(AssetDatabase::LookupGuid(path));
			if (!loaded || loaded->GetRefCount() == 0 || !loaded->CanAdoptReimport()) {
				continue;
			}
			const u64 id = loaded->GetInstanceId();
			if (!_state || !_state->dirty.contains(id)) {
				reimported += AssetDatabase::ReimportAsset(path) ? 1u : 0u;
				continue;
			}
			auto wrote = _state->written.find(id);
			if ((wrote != _state->written.end() && wrote->second == HashFile(path)) || !onConflict) {
				continue;
			}
			onConflict(path, [id]() {
				if (Asset* asset = ResolveAsset(id)) {
					RevertToFile(Handle<Asset>(asset));
				}
			});
		}
		return reimported;
	}

	bool EditorAssetOps::RevertToFile(Handle<Asset> asset) {
		Asset* live = asset.Get();
		if (!live || !live->CanAdoptReimport() || live->virtualPath.empty()) {
			return false;
		}
		const Path path(live->virtualPath);
		if (!AssetDatabase::ReimportAsset(path)) {
			return false;
		}
		if (_state && _state->dirty.contains(asset.GetInstanceId())) {
			ReleaseAndForget(asset.GetInstanceId());
		}
		return true;
	}

} // namespace Shalltear
