#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <functional>
#include <initializer_list>
#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API Asset;
	class SHALLTEAR_CORE_API Path;
	class SHALLTEAR_CORE_API JSONValue;
	struct SHALLTEAR_CORE_API Buffer;
	struct TypeInfo;

	class AssetProductBuilder;

	class SHALLTEAR_CORE_API AssetImportContext {
	public:
		// key must not change on reimport + be unique per source
		Guid AddProduct(std::string_view key, Asset* product, std::string_view name = {});

		Guid AdoptProduct(std::string_view key, const Guid& identity);

		AssetImportContext(const AssetImportContext&) = delete;
		AssetImportContext& operator=(const AssetImportContext&) = delete;
	private:
		friend class AssetProductBuilder;
		explicit AssetImportContext(AssetProductBuilder& builder) noexcept : _builder(&builder) {}

		AssetProductBuilder* _builder = nullptr;
	};

	class SHALLTEAR_CORE_API IAssetImporter {
	public:
		virtual ~IAssetImporter() = default;

		virtual StringHash GetAssetType() const = 0;

		// AssetDatabase owns the returned pointer
		virtual Asset* Import(const Path& sourcePath) = 0;

		virtual Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) {
			(void)virtualPath;
			(void)settings;
			return Import(sourcePath);
		}

		virtual Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings, AssetImportContext& context) {
			(void)context;
			return ImportWithSettings(sourcePath, virtualPath, settings);
		}

		virtual u32 GetImporterVersion() const { return 1; }

		virtual void SerializeSettings(const Path& sourcePath, JSONValue& out) const {
			(void)sourcePath;
			(void)out;
		}
		virtual void DeserializeSettings(const JSONValue& in) { (void)in; }

		// opt in by overriding all 4

		virtual bool CanCreateAsset() const { return false; }
		virtual std::string_view GetCreateMenuLabel() const { return {}; }
		virtual std::string_view GetCreateDefaultFileName() const { return {}; }
		// caller owns it, not registered
		virtual Asset* CreateAsset() { return nullptr; }

		virtual bool CreateAssetSource(Buffer& out) {
			(void)out;
			return false;
		}

		virtual u32 GetCreateTemplateCount() const { return CanCreateAsset() ? 1u : 0u; }
		virtual std::string_view GetCreateTemplateLabel(u32 index) const {
			(void)index;
			return GetCreateMenuLabel();
		}
		virtual std::string_view GetCreateTemplateFileName(u32 index) const {
			(void)index;
			return GetCreateDefaultFileName();
		}
		virtual bool CreateTemplateSource(u32 index, Buffer& out) { return index == 0 && CreateAssetSource(out); }
	};

	class SHALLTEAR_CORE_API AssetImporterRegistry {
	public:
		static void Register(StringHash typeId, IAssetImporter* importer, std::initializer_list<std::string_view> extensions = {}, const void* owner = nullptr);

		static void RegisterExtension(std::string_view extension, IAssetImporter* importer, const void* owner = nullptr);

		static bool RegisterDeclared(const TypeInfo* type, const void* owner = nullptr);

		static void Unregister(IAssetImporter* importer);

		static u32 UnregisterOwnedBy(const void* owner);

		static IAssetImporter* Find(StringHash typeId);
		static IAssetImporter* FindByExtension(std::string_view ext);

		static void ForEach(const std::function<void(StringHash typeId, IAssetImporter* importer, std::string_view primaryExtension)>& fn);
		static void ForEachImporter(const std::function<void(StringHash typeId, IAssetImporter* importer, std::string_view primaryExtension)>& fn);

		static void Clear();
	};

} // namespace Shalltear
