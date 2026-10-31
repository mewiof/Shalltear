#include "Asset/AssetMigration.h"

#include "Asset/AssetBinder.h"
#include "Asset/AssetDatabaseData.h"
#include "Serialization/SerializerInternal.h"
#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

namespace Shalltear {

	namespace SI = SerializerInternal;

	namespace AssetInternal {

		namespace {

			void CaptureBound(Asset& asset, AssetDatabaseData& data, std::vector<Guid>& out) {
				for (Handle<Asset>& held : AssetInternalAccess::BoundAssetsOf(asset)) {
					Asset* target = held.Get();
					if (!target) {
						continue;
					}
					if (auto known = data.instanceToGuid.find(target->GetInstanceId()); known != data.instanceToGuid.end()) {
						out.push_back(known->second);
					}
				}
			}

			void RestoreBound(Asset& asset, AssetDatabaseData& data, const std::vector<Guid>& ids) {
				Vector<Handle<Asset>>& bound = AssetInternalAccess::BoundAssetsOf(asset);
				for (const Guid& id : ids) {
					auto found = data.guidToAsset.find(id);
					if (found == data.guidToAsset.end() || !found->second) {
						continue;
					}
					bound.push_back(Handle<Asset>(found->second));
				}
			}

		} // anonymous namespace

		void CaptureAndDemolishModuleAssets(const void* moduleImage, AssetMigrationSnapshot& out) {
			auto& data = GetData();
			std::lock_guard lock(data.mutex);
			if (!data.initialized || !moduleImage) {
				return;
			}

			for (const auto& [guid, asset] : data.guidToAsset) {
				if (!asset) {
					continue;
				}
				const TypeInfo* type = asset->GetType();
				if (!type || type->ownerModule != moduleImage) {
					continue;
				}
				AssetMigrationEnvelope envelope;
				envelope.assetId = guid;
				envelope.instanceId = asset->GetInstanceId();
				envelope.typeNameHash = type->name.GetValue();
				envelope.refCount = asset->GetRefCount();
				envelope.loadState = asset->GetLoadState();
				envelope.wasParked = data.parked.contains(asset);
				CaptureBound(*asset, data, envelope.bound);
				out.envelopes.push_back(std::move(envelope));
			}

			if (out.envelopes.empty()) {
				return;
			}

			for (AssetMigrationEnvelope& envelope : out.envelopes) {
				auto* asset = static_cast<Asset*>(ObjectRegistry::Resolve(envelope.instanceId));
				const TypeInfo* type = TypeRegistry::FindType(StringHash(envelope.typeNameHash));
				if (!asset || !type) {
					continue;
				}
				SI::BinaryWriter writer;
				SI::BinaryWriteObject(writer, asset, type, 0);
				envelope.blob.assign(writer.Data().begin(), writer.Data().end());
			}

			for (AssetMigrationEnvelope& envelope : out.envelopes) {
				auto* asset = static_cast<Asset*>(ObjectRegistry::Resolve(envelope.instanceId));
				if (!asset) {
					continue;
				}

				data.guidToAsset[envelope.assetId] = nullptr;

				// leave the drain before the pointer dies (or it frees twice)
				data.parked.erase(asset);

				const bool detached = ObjectRegistry::DetachPreservingSlot(*asset);
				SLT_CORE_ASSERT_MSG(detached, "asset migration failed to detach an asset's registry slot");
				TypeRegistry::DestroyObjectInstance(asset);
			}
		}

		void RestoreModuleAssets(AssetMigrationSnapshot& snapshot) {
			auto& data = GetData();
			std::lock_guard lock(data.mutex);
			if (snapshot.envelopes.empty()) {
				return;
			}

			u32 restored = 0;
			u32 preserved = 0;
			std::vector<SI::HandleFixupJob> fixups;

			for (AssetMigrationEnvelope& envelope : snapshot.envelopes) {
				const TypeInfo* type = TypeRegistry::FindType(StringHash(envelope.typeNameHash));
				auto* asset = static_cast<Asset*>(TypeRegistry::CreateObjectInstance(type, MemTag::Asset));

				if (!asset || !ObjectRegistry::RebindPreservedSlot(*asset, envelope.instanceId)) {
					if (asset) {
						TypeRegistry::DestroyObjectInstance(asset);
					}
					ObjectRegistry::ReleaseReservedSlot(envelope.instanceId);
					data.guidToAsset.erase(envelope.assetId);
					data.instanceToGuid.erase(envelope.instanceId);

					AssetDatabaseData::UnresolvedAsset record;
					record.typeNameHash = envelope.typeNameHash;
					record.refCount = envelope.refCount;
					record.loadState = envelope.loadState;
					record.wasParked = envelope.wasParked;
					record.blob = std::move(envelope.blob);
					record.bound = std::move(envelope.bound);
					data.unresolved[envelope.assetId] = std::move(record);
					++preserved;
					continue;
				}

				data.guidToAsset[envelope.assetId] = asset;
				if (envelope.wasParked) {
					data.parked.insert(asset);
				}
				for (int i = 0; i < envelope.refCount; ++i) {
					asset->AddRef();
				}
				asset->SetLoadState(envelope.loadState);

				if (!envelope.blob.empty()) {
					SI::BinaryReader reader(envelope.blob.data(), envelope.blob.size());
					if (!SI::BinaryReadObject(reader, asset, type, 0, fixups)) {
						SLT_LOG_WARN("Asset", "reload: asset {} blob only partially applied (layout drift)",
									 envelope.assetId.ToString());
					}
				}
				++restored;
			}

			for (AssetMigrationEnvelope& envelope : snapshot.envelopes) {
				auto placed = data.guidToAsset.find(envelope.assetId);
				if (placed == data.guidToAsset.end() || !placed->second) {
					continue;
				}
				RestoreBound(*placed->second, data, envelope.bound);
			}
			SI::RunHandleFixups(fixups);

			SLT_LOG_INFO("Asset", "reload migration: {} asset(s) restored, {} preserved unresolved", restored, preserved);
		}

		u32 RebuildPreservedAssets() {
			auto& data = GetData();
			std::lock_guard lock(data.mutex);
			if (data.unresolved.empty()) {
				return 0;
			}

			u32 reclaimed = 0;
			std::vector<SI::HandleFixupJob> fixups;
			std::vector<std::pair<Asset*, std::vector<Guid>>> rebound;

			Vector<Guid> ready{GetMemoryResource(MemTag::Asset)};
			for (const auto& [guid, record] : data.unresolved) {
				if (TypeRegistry::FindType(StringHash(record.typeNameHash))) {
					ready.push_back(guid);
				}
			}

			for (const Guid& guid : ready) {
				auto it = data.unresolved.find(guid);
				if (it == data.unresolved.end()) {
					continue;
				}
				auto live = data.guidToAsset.find(guid);
				if (live != data.guidToAsset.end() && live->second) {
					SLT_LOG_WARN("Asset", "asset {} reloaded while its preserved blob was parked (loaded instance stays)",
								 guid.ToString());
					continue;
				}

				const TypeInfo* type = TypeRegistry::FindType(StringHash(it->second.typeNameHash));
				auto* asset = static_cast<Asset*>(TypeRegistry::CreateObjectInstance(type, MemTag::Asset));
				if (!asset) {
					continue;
				}

				data.guidToAsset[guid] = asset;
				data.instanceToGuid[asset->GetInstanceId()] = guid;
				if (it->second.wasParked) {
					data.parked.insert(asset);
				}
				for (int i = 0; i < it->second.refCount; ++i) {
					asset->AddRef();
				}
				asset->SetLoadState(it->second.loadState);

				if (!it->second.blob.empty()) {
					SI::BinaryReader reader(it->second.blob.data(), it->second.blob.size());
					SI::BinaryReadObject(reader, asset, type, 0, fixups);
				}
				rebound.emplace_back(asset, std::move(it->second.bound));
				data.unresolved.erase(it);
				++reclaimed;
			}

			for (auto& [owner, ids] : rebound) {
				RestoreBound(*owner, data, ids);
			}
			SI::RunHandleFixups(fixups);
			if (reclaimed > 0) {
				SLT_LOG_INFO("Asset", "{} preserved asset(s) rebuilt", reclaimed);
			}
			return reclaimed;
		}

	} // namespace AssetInternal

} // namespace Shalltear
