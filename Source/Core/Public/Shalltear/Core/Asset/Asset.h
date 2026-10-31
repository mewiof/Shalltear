#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

#include <atomic>

namespace Shalltear {

	class CookWriter;
	class CookReader;

	enum class AssetLoadState : u8 {
		Unloaded,
		Loading,
		Loaded,
		Failed,
		HotReloading
	};

	class SHALLTEAR_CORE_API Asset : public Object {
	public:
		SLT_REFLECT_CLASS(Asset)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(assetId)
		SLT_REFLECT_FIELD(virtualPath)
		SLT_END_REFLECT()

		Asset() = default;

		~Asset() override;

		Guid assetId;
		str virtualPath;

		AssetLoadState GetLoadState() const noexcept { return _loadState.load(std::memory_order_acquire); }
		int GetRefCount() const noexcept { return _refCount.load(std::memory_order_acquire); }

		virtual bool Import(const Path& sourcePath) {
			(void)sourcePath;
			return false;
		}

		virtual bool Export(const Path& destPath) const {
			(void)destPath;
			return false;
		}

		virtual StringHash GetAssetTypeId() const { return Asset::GetReflectType()->name; }

		virtual bool CanAdoptReimport() const { return false; }

		virtual void AdoptReimport(Asset& fresh) { (void)fresh; }

		virtual bool Cook(CookWriter& descriptor, CookWriter& payload) const;

		virtual bool LoadCooked(CookReader& descriptor, CookReader& payload);

		virtual bool CookImportState(CookWriter& importState) const {
			(void)importState;
			return true;
		}
		virtual bool LoadImportState(CookReader& importState) {
			(void)importState;
			return true;
		}

		void AddRef() noexcept { _refCount.fetch_add(1, std::memory_order_acq_rel); }

		int ReleaseRef() noexcept { return _refCount.fetch_sub(1, std::memory_order_acq_rel) - 1; }

		void SetLoadState(AssetLoadState state) noexcept { _loadState.store(state, std::memory_order_release); }
		void NotifyLoaded() {
			SetLoadState(AssetLoadState::Loaded);
			OnLoad();
		}
		void NotifyUnloaded();
	protected:
		virtual void OnLoad() {}
		virtual void OnUnload() {}

		std::atomic<AssetLoadState> _loadState{AssetLoadState::Unloaded};
		std::atomic<int> _refCount{0};
	private:
		Vector<Handle<Asset>> _bound{GetMemoryResource(MemTag::Asset)};

		friend struct AssetInternalAccess;
	};

} // namespace Shalltear
