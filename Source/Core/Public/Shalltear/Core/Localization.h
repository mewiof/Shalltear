#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include <functional>
#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API StringTableAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(StringTableAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		StringTableAsset() = default;
		~StringTableAsset() override;

		Vector<str> languages{GetMemoryResource(MemTag::Asset)};
		Vector<str> keys{GetMemoryResource(MemTag::Asset)};
		// row major, keys.size() * languages.size(), empty when untranslated
		Vector<str> cells{GetMemoryResource(MemTag::Asset)};

		u32 version = 0;

		// header "key,<language>,...", then 1 row per key
		bool Parse(std::string_view csv, str& error);

		i32 FindLanguage(std::string_view code) const;
		std::string_view Find(std::string_view key, u32 language) const;

		bool Import(const Path& sourcePath) override;
		StringHash GetAssetTypeId() const override { return StringTableAsset::GetReflectType()->name; }

		bool CanAdoptReimport() const override { return true; }
		void AdoptReimport(Asset& fresh) override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	private:
		struct KeyHash {
			using is_transparent = void;
			size_t operator()(std::string_view key) const { return std::hash<std::string_view>{}(key); }
		};
		HashMap<str, u32, KeyHash, std::equal_to<>> _rows{GetMemoryResource(MemTag::Asset)};

		void IndexRows();
	};

	class SHALLTEAR_CORE_API StringTableImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("StringTableAsset"); }
		Asset* Import(const Path& sourcePath) override;
		bool IsAlwaysCooked() const override { return true; }

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Localization/String Table"; }
		std::string_view GetCreateDefaultFileName() const override { return "New String Table.csv"; }
		bool CreateAssetSource(Buffer& out) override;
	};

	namespace Localization {

		// BCP 47 tag, as in table header ("en", "pt-BR")
		SHALLTEAR_CORE_API void SetLanguage(std::string_view code);
		SHALLTEAR_CORE_API void UseSystemLanguage();
		SHALLTEAR_CORE_API str GetLanguage();
		SHALLTEAR_CORE_API str GetSystemLanguage();
		SHALLTEAR_CORE_API str GetDefaultLanguage();

		SHALLTEAR_CORE_API bool TryTranslate(std::string_view key, str& out);
		SHALLTEAR_CORE_API str Translate(std::string_view key);

		// searched before project's tables, latest 1st
		SHALLTEAR_CORE_API void AddTable(Handle<StringTableAsset> table);
		SHALLTEAR_CORE_API void RemoveTable(Handle<StringTableAsset> table);
		SHALLTEAR_CORE_API void ReloadTables();

		SHALLTEAR_CORE_API u32 GetGeneration();

		SHALLTEAR_CORE_API str NormalizeLanguage(std::string_view code);

	} // namespace Localization

} // namespace Shalltear
