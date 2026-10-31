#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection.h"

#include <string_view>
#include <type_traits>

namespace Shalltear {

	class SHALLTEAR_CORE_API DataAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(DataAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_END_REFLECT()

		DataAsset() = default;

		~DataAsset() override;

		StringHash GetAssetTypeId() const override;

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
	};

	class SHALLTEAR_CORE_API DeclaredAssetImporter final : public IAssetImporter {
	public:
		using TypeFn = const TypeInfo* (*)();
		using CreateFn = Asset* (*)();

		DeclaredAssetImporter(TypeFn type, CreateFn create, std::string_view menuLabel, std::string_view extension,
							  std::string_view defaultFileName) noexcept;

		StringHash GetAssetType() const override;
		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return _menuLabel; }
		std::string_view GetCreateDefaultFileName() const override { return _defaultFileName; }
		Asset* CreateAsset() override { return _create(); }

		std::string_view GetExtension() const noexcept { return _extension; }

		static void Declare(ReflectBuilder& builder, DeclaredAssetImporter* importer);

		static DeclaredAssetImporter* FindDeclared(const TypeInfo* type);
	private:
		TypeFn _type;
		CreateFn _create;
		std::string_view _menuLabel;
		std::string_view _extension;
		std::string_view _defaultFileName;
	};

} // namespace Shalltear

#define SLT_CREATE_ASSET_MENU(MenuLabel, Extension)                                                  \
	SLT_REFLECT_CREATABLE()                                                                          \
	static_assert(std::is_base_of_v<::Shalltear::Asset, Self>,                                       \
				  "SLT_CREATE_ASSET_MENU belongs in the reflection block of an Asset subclass");     \
	static ::Shalltear::DeclaredAssetImporter _sltDeclaredImporter(                                  \
		+[]() -> const ::Shalltear::TypeInfo* { return Self::GetReflectType(); },                    \
		+[]() -> ::Shalltear::Asset* { return ::Shalltear::New<Self>(::Shalltear::MemTag::Asset); }, \
		MenuLabel, Extension, "New " MenuLabel Extension);                                           \
	::Shalltear::DeclaredAssetImporter::Declare(builder, &_sltDeclaredImporter);
