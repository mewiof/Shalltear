#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Reflection.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class DeclaredProbePotion : public DataAsset {
	public:
		SLT_REFLECT_CLASS(DeclaredProbePotion)
		SLT_REFLECT_PARENT(DataAsset)
		SLT_CREATE_ASSET_MENU("Probe Potion", ".probepotion")
		SLT_REFLECT_FIELD(healAmount)
		SLT_REFLECT_FIELD(displayName)
		SLT_END_REFLECT()

		i32 healAmount = 10;
		str displayName = "Potion";
	};

	class DeclaredProbeElixir : public DeclaredProbePotion {
	public:
		SLT_REFLECT_CLASS(DeclaredProbeElixir)
		SLT_REFLECT_PARENT(DeclaredProbePotion)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()
	};

	class DeclaredProbeImpostor : public DataAsset {
	public:
		SLT_REFLECT_CLASS(DeclaredProbeImpostor)
		SLT_REFLECT_PARENT(DataAsset)
		SLT_CREATE_ASSET_MENU("Probe Impostor", ".probepotion")
		SLT_END_REFLECT()
	};

	class DeclaredProbeUndeclared : public DataAsset {
	public:
		SLT_REFLECT_CLASS(DeclaredProbeUndeclared)
		SLT_REFLECT_PARENT(DataAsset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()
	};

	class HandWrittenPotionImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return DeclaredProbePotion::GetReflectType()->name; }
		Asset* Import(const Path&) override { return nullptr; }
	};

	class DeclaredAssetImporterFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		int _owner = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_declared_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetImporterRegistry::UnregisterOwnedBy(&_owner);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool RegisterPotion() { return AssetImporterRegistry::RegisterDeclared(DeclaredProbePotion::GetReflectType(), &_owner); }
	};

} // namespace

SLT_REGISTER_TYPE(DeclaredProbePotion)
SLT_REGISTER_TYPE(DeclaredProbeElixir)
SLT_REGISTER_TYPE(DeclaredProbeImpostor)
SLT_REGISTER_TYPE(DeclaredProbeUndeclared)

TEST_F(DeclaredAssetImporterFixture, MenuMacroDeclaresImporter) {
	ASSERT_TRUE(RegisterPotion());

	IAssetImporter* importer = AssetImporterRegistry::Find(DeclaredProbePotion::GetReflectType()->name);
	ASSERT_NE(importer, nullptr);
	EXPECT_EQ(importer->GetAssetType(), DeclaredProbePotion::GetReflectType()->name);
	EXPECT_TRUE(importer->CanCreateAsset());
	EXPECT_EQ(importer->GetCreateMenuLabel(), "Probe Potion");
	EXPECT_EQ(importer->GetCreateDefaultFileName(), "New Probe Potion.probepotion");
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".probepotion"), importer);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".PROBEPOTION"), importer);
}

TEST_F(DeclaredAssetImporterFixture, MenuMacroMakesTypeCreatable) {
	Object* potion = TypeRegistry::CreateObjectInstance(DeclaredProbePotion::GetReflectType(), MemTag::Asset);
	ASSERT_NE(potion, nullptr);
	TypeRegistry::DestroyObjectInstance(potion);

	EXPECT_EQ(TypeRegistry::CreateObjectInstance(DataAsset::GetReflectType(), MemTag::Asset), nullptr);
}

TEST_F(DeclaredAssetImporterFixture, DataAssetReportsConcreteType) {
	DeclaredProbePotion potion;
	EXPECT_EQ(potion.GetAssetTypeId(), DeclaredProbePotion::GetReflectType()->name);
}

TEST_F(DeclaredAssetImporterFixture, CreateWritesLoadableText) {
	ASSERT_TRUE(RegisterPotion());

	const Path path("project://Items/Healing.probepotion");
	ASSERT_TRUE(AssetDatabase::CreateAsset(path, DeclaredProbePotion::GetReflectType()->name).IsValid());

	const str written = FileSystem::ReadTextFile(path, MemTag::Asset);
	EXPECT_NE(written.find("healAmount"), str::npos) << written;

	auto* created = dynamic_cast<DeclaredProbePotion*>(AssetDatabase::LoadAsset(path));
	ASSERT_NE(created, nullptr);
	EXPECT_EQ(created->healAmount, 10);
	created->healAmount = 42;
	created->displayName = "Elixir";
	AssetDatabase::SaveAsset(created);
	AssetDatabase::UnloadAsset(created);

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	ASSERT_TRUE(RegisterPotion());
	auto* reloaded = dynamic_cast<DeclaredProbePotion*>(AssetDatabase::LoadAsset(path));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(reloaded->healAmount, 42);
	EXPECT_EQ(reloaded->displayName, "Elixir");
	AssetDatabase::UnloadAsset(reloaded);
}

TEST_F(DeclaredAssetImporterFixture, TheOwnersSweepTakesTheDeclaredImporter) {
	ASSERT_TRUE(RegisterPotion());
	ASSERT_TRUE(RegisterPotion());

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(&_owner), 1u);
	EXPECT_EQ(AssetImporterRegistry::Find(DeclaredProbePotion::GetReflectType()->name), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".probepotion"), nullptr);
}

TEST_F(DeclaredAssetImporterFixture, SubclassWithoutMenuRegistersNothing) {
	EXPECT_FALSE(AssetImporterRegistry::RegisterDeclared(DeclaredProbeElixir::GetReflectType(), &_owner));
	EXPECT_EQ(AssetImporterRegistry::Find(DeclaredProbeElixir::GetReflectType()->name), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".probepotion"), nullptr);
}

TEST_F(DeclaredAssetImporterFixture, ATypeWithNoMenuRegistersNothing) {
	EXPECT_FALSE(AssetImporterRegistry::RegisterDeclared(DeclaredProbeUndeclared::GetReflectType(), &_owner));
	EXPECT_FALSE(AssetImporterRegistry::RegisterDeclared(nullptr, &_owner));
	EXPECT_EQ(AssetImporterRegistry::Find(DeclaredProbeUndeclared::GetReflectType()->name), nullptr);
}

TEST_F(DeclaredAssetImporterFixture, SecondTypeClaimingSameExtensionIsRefused) {
	ASSERT_TRUE(RegisterPotion());
	IAssetImporter* first = AssetImporterRegistry::FindByExtension(".probepotion");

	EXPECT_FALSE(AssetImporterRegistry::RegisterDeclared(DeclaredProbeImpostor::GetReflectType(), &_owner));
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".probepotion"), first);
	EXPECT_EQ(AssetImporterRegistry::Find(DeclaredProbeImpostor::GetReflectType()->name), nullptr);
}

TEST_F(DeclaredAssetImporterFixture, ManualImporterKeptForType) {
	HandWrittenPotionImporter handWritten;
	AssetImporterRegistry::Register(DeclaredProbePotion::GetReflectType()->name, &handWritten, {".handpotion"}, &_owner);

	EXPECT_FALSE(RegisterPotion());
	EXPECT_EQ(AssetImporterRegistry::Find(DeclaredProbePotion::GetReflectType()->name), &handWritten);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".probepotion"), nullptr);

	AssetImporterRegistry::Unregister(&handWritten);
}
