#include "Asset/AssetSource.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include "Asset/AssetBinder.h"

#include <format>

namespace Shalltear {

	bool PackageAssetSource::Mount(const Path& physicalPath, str& outError) {
		if (!_reader.Mount(physicalPath, outError)) {
			return false;
		}

		for (u32 i = 0; i < _reader.GetTypeCount(); ++i) {
			const PakTypeEntry* type = _reader.GetType(static_cast<u16>(i));
			if (type && type->recordVersion != PAK_RECORD_VERSION) {
				outError = std::format("'{}' has type {:#x} at record version {} (this build reads {})",
									   physicalPath.ToString(), type->typeNameHash, type->recordVersion,
									   PAK_RECORD_VERSION);
				_reader.Unmount();
				return false;
			}
		}

		_path = physicalPath.ToString();
		return true;
	}

	Asset* PackageAssetSource::Load(const Guid& guid) {
		SLT_PROFILE_ZONE("Asset.LoadCooked");

		u32 index = _reader.FindAsset(guid);
		if (index == PAK_INVALID_INDEX) {
			SLT_LOG_ERROR("Asset", "the package holds no asset {}", guid.ToString());
			return nullptr;
		}

		const PakAssetEntry* entry = _reader.GetAsset(index);
		const PakTypeEntry* type = entry ? _reader.GetType(entry->typeIndex) : nullptr;
		if (!entry || !type) {
			SLT_LOG_ERROR("Asset", "asset {} has no readable idx entry in the package", guid.ToString());
			return nullptr;
		}

		std::string_view name = _reader.GetName(index);

		const TypeInfo* info = TypeRegistry::FindType(StringHash(type->typeNameHash));
		if (!info || !TypeRegistry::IsDerivedFrom(info, Asset::GetReflectType())) {
			SLT_LOG_ERROR("Asset", "'{}' has type {:#x} (not an asset type this build knows)",
						  name.empty() ? guid.ToString() : str(name), type->typeNameHash);
			return nullptr;
		}
		Object* object = TypeRegistry::CreateObjectInstance(info, MemTag::Asset);
		if (!object) {
			SLT_LOG_ERROR("Asset", "'{}' has type '{}' (not constructible by reflection)",
						  name.empty() ? guid.ToString() : str(name),
						  info->displayName ? info->displayName : "?");
			return nullptr;
		}
		Asset* asset = static_cast<Asset*>(object);

		asset->assetId = guid;
		asset->virtualPath = str(name);

		CookReader descriptor = _reader.GetDescriptor(index);

		Buffer payloadBytes;
		PakPayloadLocator locator;
		bool located = false;
		if (entry->regionIndex != PAK_NO_REGION) {
			str error;
			const PakRegion* region = _reader.GetRegion(entry->regionIndex);
			located = region && region->residency == static_cast<u8>(PakResidency::Streamed);
			const bool found = located ? _reader.LocateRegion(entry->regionIndex, locator.offset, locator.size, error)
									   : _reader.ReadRegion(entry->regionIndex, payloadBytes, error);
			if (!found) {
				SLT_LOG_ERROR("Asset", "'{}' couldn't be read from the package: {}",
							  name.empty() ? guid.ToString() : str(name), error);
				TypeRegistry::DestroyObjectInstance(asset);
				return nullptr;
			}
			locator.physicalPath = located ? _path : str();
		}
		CookReader payload(payloadBytes.data, payloadBytes.size, {}, located ? &locator : nullptr);

		AssetInternal::AssetBindScope bind;
		if (!asset->LoadCooked(descriptor, payload)) {
			SLT_LOG_ERROR("Asset", "'{}' couldn't load its cooked record", name.empty() ? guid.ToString() : str(name));
			TypeRegistry::DestroyObjectInstance(asset);
			return nullptr;
		}
		bind.AttachTo(*asset);
		return asset;
	}

} // namespace Shalltear
