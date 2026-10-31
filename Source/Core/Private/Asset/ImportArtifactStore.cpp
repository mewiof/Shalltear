#include "Asset/ImportArtifactStore.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include "Asset/AssetBinder.h"
#include "Asset/ImportArtifact.h"
#include "FileAccessRecorder.h"

#include <filesystem>

namespace Shalltear {

	namespace {

		struct SourceStamp {
			u64 size = 0;
			u64 mtime = 0;
		};

		SourceStamp ReadSourceStamp(const Path& physSource) {
			SourceStamp stamp;
			stamp.size = static_cast<u64>(FileSystem::GetFileSize(physSource));
			stamp.mtime = FileSystem::GetLastWriteTime(physSource);
			return stamp;
		}

		struct CookedRecord {
			PakStringPool strings;
			Vector<u8> desc{GetMemoryResource(MemTag::Asset)};
			Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
			Vector<u8> importState{GetMemoryResource(MemTag::Asset)};
			Vector<u8> inputs{GetMemoryResource(MemTag::Asset)};
			Vector<u8> outputs{GetMemoryResource(MemTag::Asset)};
		};

		bool OutputsPresent(const ImportArtifactView& view) {
			CookReader outputs = view.Outputs();
			for (u32 i = 0; i < view.header.outputCount; ++i) {
				u32 index = 0;
				if (!outputs.Read(index)) {
					return false;
				}
				std::string_view path = view.strings.Get(index);
				if (path.empty() || !FileSystem::Exists(Path(str(path)))) {
					return false;
				}
			}
			return true;
		}

		struct InputStampPatch {
			size_t at = 0;
			u64 size = 0;
			u64 mtime = 0;
		};

		bool InputsFresh(const ImportArtifactView& view, Vector<InputStampPatch>& refresh) {
			CookReader inputs = view.Inputs();
			for (u32 i = 0; i < view.header.inputCount; ++i) {
				u32 index = 0;
				u64 size = 0;
				u64 mtime = 0;
				u64 contentHash = 0;
				if (!inputs.Read(index) || !inputs.Read(size) || !inputs.Read(mtime) || !inputs.Read(contentHash)) {
					return false;
				}
				std::string_view text = view.strings.Get(index);
				if (text.empty()) {
					return false;
				}

				Path recorded{str(text)};
				u64 nowSize = static_cast<u64>(FileSystem::GetFileSize(recorded));
				u64 nowMtime = FileSystem::GetLastWriteTime(recorded);
				if (nowSize == size && nowMtime == mtime) {
					continue;
				}
				Buffer bytes = FileSystem::ReadFile(recorded, MemTag::Asset);
				if (ImportArtifactHashSource(bytes.data, bytes.size) != contentHash) {
					return false;
				}
				refresh.push_back({IMPORT_ARTIFACT_HEADER_SIZE + static_cast<size_t>(i) * IMPORT_ARTIFACT_INPUT_STRIDE +
									   IMPORT_ARTIFACT_INPUT_SIZE_OFFSET,
								   nowSize, nowMtime});
			}
			return true;
		}

		bool ConfirmByContent(const ImportArtifactRequest& request, const ImportArtifactHeader& header) {
			Buffer source = FileSystem::ReadFile(request.physSource, MemTag::Asset);
			u64 sourceHash = ImportArtifactHashSource(source.data, source.size);
			u64 key = ImportArtifactComputeKey(sourceHash, request.settingsHash, request.importerVersion);
			return key == header.key;
		}

		void RefreshStamps(const Path& artifactPath, Buffer& bytes, const SourceStamp* source,
						   const Vector<InputStampPatch>& inputs) {
			if (!bytes || bytes.size < IMPORT_ARTIFACT_HEADER_SIZE || (!source && inputs.empty())) {
				return;
			}
			ImportArtifactHeader header{};
			std::memcpy(&header, bytes.data, sizeof(header));
			if (source) {
				header.sourceSize = source->size;
				header.sourceMtime = source->mtime;
			}

			for (const InputStampPatch& patch : inputs) {
				if (patch.at + 2 * sizeof(u64) > bytes.size) {
					continue;
				}
				std::memcpy(bytes.data + patch.at, &patch.size, sizeof(u64));
				std::memcpy(bytes.data + patch.at + sizeof(u64), &patch.mtime, sizeof(u64));
			}

			if (!inputs.empty()) {
				header.crc32c = PakCrc32c(bytes.data + IMPORT_ARTIFACT_HEADER_SIZE, bytes.size - IMPORT_ARTIFACT_HEADER_SIZE);
			}
			std::memcpy(bytes.data, &header, sizeof(header));
			FileSystem::WriteFile(artifactPath, bytes);
		}

		struct OpenArtifact {
			Buffer bytes;
			ImportArtifactView view;
			Path path;
			SourceStamp stamp;
			bool stampMoved = false;
			Vector<InputStampPatch> refresh{GetMemoryResource(MemTag::Asset)};
		};

		bool OpenValidated(const ImportArtifactRequest& request, OpenArtifact& out) {
			if (!request.guid.IsValid() || !request.virtualKey) {
				return false;
			}

			out.path = ImportArtifactPath(request.guid);
			if (!FileSystem::Exists(out.path)) {
				return false;
			}

			out.bytes = FileSystem::ReadFile(out.path, MemTag::Asset);
			if (!ImportArtifactRead(out.bytes.data, out.bytes.size, out.view)) {
				SLT_LOG_WARN("Asset", "discarding an unreadable import artifact for '{}'. Re-importing", *request.virtualKey);
				std::error_code ec;
				std::filesystem::remove(std::filesystem::path(out.path.ToString()), ec);
				return false;
			}

			if (out.view.header.importerVersion != request.importerVersion) {
				return false;
			}
			if (out.view.header.settingsHash != request.settingsHash) {
				return false;
			}

			out.stamp = ReadSourceStamp(request.physSource);
			out.stampMoved = out.stamp.size != out.view.header.sourceSize || out.stamp.mtime != out.view.header.sourceMtime;
			if (out.stampMoved && !ConfirmByContent(request, out.view.header)) {
				return false;
			}

			if (!OutputsPresent(out.view)) {
				return false;
			}
			return InputsFresh(out.view, out.refresh);
		}

		PakPayloadLocator LocatorOf(const OpenArtifact& open, const u8* payload, size_t size) {
			PakPayloadLocator locator;
			locator.physicalPath = FileSystem::Resolve(open.path).ToString();
			locator.offset = payload ? static_cast<u64>(payload - open.bytes.data) : 0;
			locator.size = size;
			return locator;
		}

		void RecordUse(OpenArtifact& open) {
			bool rewrote = open.stampMoved || !open.refresh.empty();
			RefreshStamps(open.path, open.bytes, open.stampMoved ? &open.stamp : nullptr, open.refresh);
			if (!rewrote) {
				ImportArtifactMarkUsed(open.path);
			}
		}

		Asset* Reconstruct(u64 typeNameHash, const Guid& identity, const str& virtualPath, CookReader descriptor,
						   CookReader payload, CookReader importState) {
			const TypeInfo* info = TypeRegistry::FindType(StringHash(typeNameHash));
			if (!info || !TypeRegistry::IsDerivedFrom(info, Asset::GetReflectType())) {
				return nullptr;
			}
			Object* object = TypeRegistry::CreateObjectInstance(info, MemTag::Asset);
			if (!object) {
				return nullptr;
			}
			Asset* asset = static_cast<Asset*>(object);

			asset->assetId = identity;
			asset->virtualPath = virtualPath;

			AssetInternal::AssetBindScope bind;
			if (!asset->LoadCooked(descriptor, payload)) {
				SLT_LOG_WARN("Asset", "'{}' couldn't load its import artifact. Re-importing", virtualPath);
				TypeRegistry::DestroyObjectInstance(asset);
				return nullptr;
			}

			if (!asset->LoadImportState(importState)) {
				SLT_LOG_WARN("Asset", "'{}' couldn't load the import state in its artifact. Re-importing", virtualPath);
				TypeRegistry::DestroyObjectInstance(asset);
				return nullptr;
			}
			bind.AttachTo(*asset);

			// again after the fill (LoadCooked restored the artifact's copy)
			asset->assetId = identity;
			asset->virtualPath = virtualPath;
			return asset;
		}

	} // namespace

	Asset* ImportArtifactStore::TryLoad(const ImportArtifactRequest& request) {
		SLT_PROFILE_ZONE("Asset.ImportArtifact");

		OpenArtifact open;
		if (!OpenValidated(request, open)) {
			return nullptr;
		}

		if (!open.view.HasMain()) {
			return nullptr;
		}

		RecordUse(open);
		const PakPayloadLocator locator = LocatorOf(open, open.view.payload, open.view.payloadSize);
		Asset* asset = Reconstruct(open.view.header.typeNameHash, request.guid, *request.virtualKey, open.view.Descriptor(),
								   CookReader(open.view.payload, open.view.payloadSize, open.view.strings, &locator), open.view.ImportState());
		if (!asset) {
			return nullptr;
		}

		SLT_LOG_TRACE("Asset", "'{}' served from import artifact {:016x}", *request.virtualKey, open.view.header.key);
		return asset;
	}

	ImportArtifactProductResult ImportArtifactStore::TryLoadProduct(const ImportArtifactRequest& request, std::string_view key,
																	const Guid& productGuid, const str& displayPath, Asset*& out) {
		SLT_PROFILE_ZONE("Asset.ImportArtifactProduct");
		out = nullptr;

		if (!productGuid.IsValid() || key.empty()) {
			return ImportArtifactProductResult::Miss;
		}
		OpenArtifact open;
		if (!OpenValidated(request, open)) {
			return ImportArtifactProductResult::Miss;
		}

		ImportArtifactProductView product;
		if (!ImportArtifactFindProduct(open.view, key, product)) {
			RecordUse(open);
			return ImportArtifactProductResult::NotProduced;
		}

		RecordUse(open);
		const PakPayloadLocator locator = LocatorOf(open, product.payload, product.payloadSize);
		out = Reconstruct(product.typeNameHash, productGuid, displayPath, CookReader(product.desc, product.descSize, open.view.strings),
						  CookReader(product.payload, product.payloadSize, open.view.strings, &locator),
						  CookReader(product.importState, product.importStateSize, open.view.strings));
		if (!out) {
			return ImportArtifactProductResult::Miss;
		}
		SLT_LOG_TRACE("Asset", "'{}' of '{}' served from import artifact {:016x}", key, *request.virtualKey, open.view.header.key);
		return ImportArtifactProductResult::Loaded;
	}

	namespace {

		u64 g_costThresholdMicros = IMPORT_ARTIFACT_COST_THRESHOLD_US;

	} // namespace

	u64 ImportArtifactStore::GetCostThresholdMicros() {
		return g_costThresholdMicros;
	}

	void ImportArtifactStore::SetCostThresholdMicros(u64 micros) {
		g_costThresholdMicros = micros;
	}

	void ImportArtifactStore::Fill(const ImportArtifactRequest& request, const Asset& asset,
								   std::span<const ImportArtifactProduct> products, const FileAccessRecording& accesses,
								   u64 importMicros) {
		SLT_PROFILE_ZONE("Asset.WriteImportArtifact");

		if (!request.guid.IsValid() || !FileSystem::Exists(request.physSource)) {
			return;
		}

		if (!ImportArtifactIsWorthWriting(importMicros, GetCostThresholdMicros())) {
			return;
		}

		CookedRecord record;
		bool hasMain = true;
		{
			CookWriter descriptor(record.desc, record.strings);
			CookWriter payload(record.payload, record.strings);
			CookWriter importState(record.importState, record.strings);
			hasMain = asset.Cook(descriptor, payload) && asset.CookImportState(importState);
		}
		if (!hasMain) {
			record.desc.clear();
			record.payload.clear();
			record.importState.clear();
		}
		if (!hasMain && products.empty()) {
			return;
		}

		Vector<u8> productTable{GetMemoryResource(MemTag::Asset)};
		Vector<u8> productBlob{GetMemoryResource(MemTag::Asset)};
		u32 productCount = 0;
		{
			CookWriter table(productTable, record.strings);
			for (const ImportArtifactProduct& product : products) {
				if (!product.asset || product.key.empty()) {
					return;
				}
				Vector<u8> desc{GetMemoryResource(MemTag::Asset)};
				Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
				Vector<u8> importState{GetMemoryResource(MemTag::Asset)};
				{
					CookWriter productDesc(desc, record.strings);
					CookWriter productPayload(payload, record.strings);
					CookWriter productState(importState, record.strings);
					if (!product.asset->Cook(productDesc, productPayload) || !product.asset->CookImportState(productState)) {
						return;
					}
				}

				table.WriteString(product.key);
				table.Write(product.asset->GetAssetTypeId().GetValue());
				table.Write(static_cast<u32>(desc.size()));
				table.Write(static_cast<u64>(payload.size()));
				table.Write(static_cast<u32>(importState.size()));
				productBlob.insert(productBlob.end(), desc.begin(), desc.end());
				productBlob.insert(productBlob.end(), payload.begin(), payload.end());
				productBlob.insert(productBlob.end(), importState.begin(), importState.end());
				++productCount;
			}
		}

		str sourceKey = FileSystem::Virtualize(request.physSource).ToString();

		u32 inputCount = 0;
		u32 outputCount = 0;
		{
			CookWriter inputs(record.inputs, record.strings);
			for (const FileAccessRead& read : accesses.reads) {
				if (read.path == sourceKey || ImportArtifactIsDerivedData(read.path)) {
					continue;
				}
				inputs.WriteString(read.path);
				inputs.Write(read.size);
				inputs.Write(read.mtime);
				inputs.Write(read.contentHash);
				++inputCount;
			}

			CookWriter outputs(record.outputs, record.strings);
			for (const str& write : accesses.writes) {
				if (ImportArtifactIsDerivedData(write)) {
					continue;
				}
				outputs.WriteString(write);
				++outputCount;
			}
		}

		Buffer source = FileSystem::ReadFile(request.physSource, MemTag::Asset);
		u64 sourceHash = ImportArtifactHashSource(source.data, source.size);
		SourceStamp stamp = ReadSourceStamp(request.physSource);
		u64 key = ImportArtifactComputeKey(sourceHash, request.settingsHash, request.importerVersion);

		Path artifactPath = ImportArtifactPath(request.guid);

		if (FileSystem::Exists(artifactPath)) {
			Buffer existing = FileSystem::ReadFile(artifactPath, MemTag::Asset);
			ImportArtifactView view;
			if (ImportArtifactRead(existing.data, existing.size, view) && view.header.key == key &&
				view.header.settingsHash == request.settingsHash && view.header.importerVersion == request.importerVersion &&
				view.HasMain() == hasMain && view.header.prodCount == productCount) {
				ImportArtifactMarkUsed(artifactPath);
				return;
			}
		}

		ImportArtifactWriteDesc desc;
		desc.key = key;
		desc.sourceSize = stamp.size;
		desc.sourceMtime = stamp.mtime;
		desc.typeNameHash = asset.GetAssetTypeId().GetValue();
		desc.settingsHash = request.settingsHash;
		desc.importerVersion = request.importerVersion;
		desc.strings = &record.strings.Texts();
		desc.inputs = record.inputs.empty() ? nullptr : record.inputs.data();
		desc.inputsSize = record.inputs.size();
		desc.inputCount = inputCount;
		desc.outputs = record.outputs.empty() ? nullptr : record.outputs.data();
		desc.outputsSize = record.outputs.size();
		desc.outputCount = outputCount;
		desc.desc = record.desc.empty() ? nullptr : record.desc.data();
		desc.descSize = record.desc.size();
		desc.payload = record.payload.empty() ? nullptr : record.payload.data();
		desc.payloadSize = record.payload.size();
		desc.importState = record.importState.empty() ? nullptr : record.importState.data();
		desc.importStateSize = record.importState.size();
		desc.hasMain = hasMain;
		desc.products = productTable.empty() ? nullptr : productTable.data();
		desc.productsSize = productTable.size();
		desc.productCount = productCount;
		desc.productBlob = productBlob.empty() ? nullptr : productBlob.data();
		desc.productBlobSize = productBlob.size();

		Vector<u8> file{GetMemoryResource(MemTag::Asset)};
		ImportArtifactWrite(file, desc);

		FileSystem::CreateDirectory(Path(artifactPath.GetDirectory()));

		Buffer out(file.size(), MemTag::Asset);
		if (!out) {
			return;
		}
		std::memcpy(out.data, file.data(), file.size());
		if (!FileSystem::WriteFile(artifactPath, out)) {
			SLT_LOG_WARN("Asset", "couldn't write the import artifact for '{}'",
						 request.virtualKey ? *request.virtualKey : str());
		}
	}

} // namespace Shalltear
