#include "Shalltear/Core/Asset/AssetImporter.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/StringUtil.h"

#include "Module/EngineStateTable.h"

#include <mutex>
#include <unordered_map>

namespace Shalltear {

	namespace {

		struct ImporterRegistryData {
			std::unordered_map<StringHash, IAssetImporter*> byType;
			std::unordered_map<str, IAssetImporter*> extToImporter;
			std::unordered_map<IAssetImporter*, const void*> importerOwner;
			std::mutex mutex;
		};

		ImporterRegistryData* _boundData = nullptr;

		ImporterRegistryData& GetData() noexcept {
			if (_boundData)
				return *_boundData;
			static ImporterRegistryData data;
			return data;
		}

		void StampOwnerLocked(ImporterRegistryData& data, IAssetImporter* importer, const void* owner) {
			if (owner) {
				data.importerOwner[importer] = owner;
			}
		}

		str NormalizeExtension(std::string_view ext) {
			str lowered = StringUtil::ToLower(ext);
			if (!lowered.empty() && lowered.front() != '.') {
				lowered.insert(lowered.begin(), '.');
			}
			return lowered;
		}

	} // anonymous namespace

	void AssetImporterRegistry::Register(StringHash typeId, IAssetImporter* importer, std::initializer_list<std::string_view> extensions, const void* owner) {
		if (typeId.GetValue() == 0 || importer == nullptr) {
			return;
		}

		auto& data = GetData();
		std::lock_guard lock(data.mutex);

		if (!data.byType.contains(typeId)) {
			data.byType[typeId] = importer;
		}
		for (std::string_view ext : extensions) {
			data.extToImporter[NormalizeExtension(ext)] = importer;
		}
		StampOwnerLocked(data, importer, owner);
	}

	void AssetImporterRegistry::RegisterExtension(std::string_view extension, IAssetImporter* importer, const void* owner) {
		if (extension.empty() || importer == nullptr) {
			return;
		}
		auto& data = GetData();
		std::lock_guard lock(data.mutex);
		data.extToImporter[NormalizeExtension(extension)] = importer;
		StampOwnerLocked(data, importer, owner);
	}

	void AssetImporterRegistry::Unregister(IAssetImporter* importer) {
		if (importer == nullptr) {
			return;
		}
		auto& data = GetData();
		std::lock_guard lock(data.mutex);

		for (auto it = data.byType.begin(); it != data.byType.end();) {
			if (it->second == importer) {
				it = data.byType.erase(it);
			} else {
				++it;
			}
		}
		for (auto it = data.extToImporter.begin(); it != data.extToImporter.end();) {
			if (it->second == importer) {
				it = data.extToImporter.erase(it);
			} else {
				++it;
			}
		}
		data.importerOwner.erase(importer);
	}

	u32 AssetImporterRegistry::UnregisterOwnedBy(const void* owner) {
		if (!owner) {
			return 0;
		}

		auto& data = GetData();
		std::lock_guard lock(data.mutex);

		Vector<IAssetImporter*> doomed{GetMemoryResource(MemTag::Asset)};
		for (const auto& [importer, registeredOwner] : data.importerOwner) {
			if (registeredOwner == owner) {
				doomed.push_back(importer);
			}
		}
		for (IAssetImporter* importer : doomed) {
			for (auto it = data.byType.begin(); it != data.byType.end();) {
				it = (it->second == importer) ? data.byType.erase(it) : std::next(it);
			}
			for (auto it = data.extToImporter.begin(); it != data.extToImporter.end();) {
				it = (it->second == importer) ? data.extToImporter.erase(it) : std::next(it);
			}
			data.importerOwner.erase(importer);
		}
		return static_cast<u32>(doomed.size());
	}

	IAssetImporter* AssetImporterRegistry::Find(StringHash typeId) {
		auto& data = GetData();
		std::lock_guard lock(data.mutex);
		auto it = data.byType.find(typeId);
		return (it != data.byType.end()) ? it->second : nullptr;
	}

	IAssetImporter* AssetImporterRegistry::FindByExtension(std::string_view ext) {
		auto& data = GetData();
		std::lock_guard lock(data.mutex);

		auto extIt = data.extToImporter.find(NormalizeExtension(ext));
		return (extIt != data.extToImporter.end()) ? extIt->second : nullptr;
	}

	void AssetImporterRegistry::ForEach(const std::function<void(StringHash typeId, IAssetImporter* importer, std::string_view primaryExtension)>& fn) {
		if (!fn) {
			return;
		}

		struct Row {
			StringHash typeId;
			IAssetImporter* importer;
			str primaryExtension;
		};
		Vector<Row> rows{GetMemoryResource(MemTag::Asset)};
		{
			auto& data = GetData();
			std::lock_guard lock(data.mutex);
			rows.reserve(data.byType.size());
			for (const auto& [typeId, importer] : data.byType) {
				str primary;
				for (const auto& [ext, registered] : data.extToImporter) {
					if (registered == importer && (primary.empty() || ext < primary)) {
						primary = ext;
					}
				}
				rows.push_back({typeId, importer, std::move(primary)});
			}
		}

		for (const Row& row : rows) {
			fn(row.typeId, row.importer, row.primaryExtension);
		}
	}

	void AssetImporterRegistry::ForEachImporter(const std::function<void(StringHash typeId, IAssetImporter* importer, std::string_view primaryExtension)>& fn) {
		if (!fn) {
			return;
		}
		struct Row {
			StringHash typeId;
			IAssetImporter* importer;
			str primaryExtension;
		};
		Vector<Row> rows{GetMemoryResource(MemTag::Asset)};
		{
			auto& data = GetData();
			std::lock_guard lock(data.mutex);
			auto rowOf = [&rows](IAssetImporter* importer) -> Row* {
				for (Row& row : rows) {
					if (row.importer == importer) {
						return &row;
					}
				}
				return nullptr;
			};
			for (const auto& [typeId, importer] : data.byType) {
				if (!rowOf(importer)) {
					rows.push_back({typeId, importer, {}});
				}
			}
			for (const auto& [ext, importer] : data.extToImporter) {
				Row* row = rowOf(importer);
				if (!row) {
					rows.push_back({importer->GetAssetType(), importer, ext});
					continue;
				}
				if (row->primaryExtension.empty() || ext < row->primaryExtension) {
					row->primaryExtension = ext;
				}
			}
		}
		for (const Row& row : rows) {
			fn(row.typeId, row.importer, row.primaryExtension);
		}
	}

	void AssetImporterRegistry::Clear() {
		auto& data = GetData();
		std::lock_guard lock(data.mutex);
		data.byType.clear();
		data.extToImporter.clear();
		data.importerOwner.clear();
	}

	namespace ModuleInternal {

		void CaptureAssetImporterEngineState(EngineStateTable& table) {
			table.assetImporters = &GetData();
		}

		void BindAssetImporterEngineState(const EngineStateTable& table) {
			_boundData = static_cast<ImporterRegistryData*>(table.assetImporters);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
