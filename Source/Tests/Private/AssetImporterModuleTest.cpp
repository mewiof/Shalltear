#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace Shalltear;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";

	constexpr const char* PROBE_TYPE = "HotReloadProbeAsset";
	constexpr const char* PROBE_EXT = ".hotreloadprobe";
	constexpr const char* PROBE_EXT_PRODUCER = ".hotreloadprobe2";

	constexpr const char* DECLARED_TYPE = "HotReloadDeclaredAsset";
	constexpr const char* DECLARED_EXT = ".hrdeclared";

	HMODULE ImageOf(const void* address) {
		HMODULE image = nullptr;
		::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							 static_cast<LPCSTR>(address), &image);
		return image;
	}

	class AssetImporterModuleFixture : public ::testing::Test {
	protected:
		ModuleHandle _module;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			AssetDatabase::Initialize();
			ModuleSystem::Initialize();

			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
		}

		void TearDown() override {
			ModuleSystem::Terminate();
			AssetDatabase::Terminate();
		}

		const void* ModuleBase() const { return ModuleSystem::GetNativeHandle(_module); }
	};

} // anonymous namespace

TEST_F(AssetImporterModuleFixture, ModuleRegistersImporterViaEntryPoint) {
	IAssetImporter* byType = AssetImporterRegistry::Find(StringHash(PROBE_TYPE));
	ASSERT_NE(byType, nullptr);

	EXPECT_EQ(AssetImporterRegistry::FindByExtension(PROBE_EXT), byType);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(PROBE_EXT_PRODUCER), byType);

	EXPECT_EQ(byType->GetImporterVersion(), 1u);
}

TEST_F(AssetImporterModuleFixture, UnloadingModuleTakesImporterRegistrationsWithIt) {
	ASSERT_NE(AssetImporterRegistry::Find(StringHash(PROBE_TYPE)), nullptr);

	const void* base = ModuleBase();
	ModuleSystem::Unload(_module);
	_module = {};
	ASSERT_FALSE(ModuleSystem::IsLoaded(_module));

	EXPECT_EQ(AssetImporterRegistry::Find(StringHash(PROBE_TYPE)), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(PROBE_EXT), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(PROBE_EXT_PRODUCER), nullptr);

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(base), 0u);
}

TEST_F(AssetImporterModuleFixture, SweptModuleLeavesCoresImportersRegistered) {
	IAssetImporter* text = AssetImporterRegistry::Find(StringHash("TextAsset"));
	IAssetImporter* texture = AssetImporterRegistry::Find(StringHash("TextureAsset"));
	ASSERT_NE(text, nullptr);
	ASSERT_NE(texture, nullptr);

	ModuleSystem::Unload(_module);
	_module = {};

	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("TextAsset")), text);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("TextureAsset")), texture);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".png"), texture);
}

TEST_F(AssetImporterModuleFixture, ReloadRepointsRowsAtNewImage) {
	ASSERT_EQ(AssetImporterRegistry::Find(StringHash(PROBE_TYPE))->GetImporterVersion(), 1u);

	Path v2(V2_DLL);
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v2));

	IAssetImporter* reloaded = AssetImporterRegistry::Find(StringHash(PROBE_TYPE));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(reloaded->GetImporterVersion(), 2u);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(PROBE_EXT), reloaded);
}

TEST_F(AssetImporterModuleFixture, DeclaredDataAssetNeedsNoCode) {
	IAssetImporter* importer = AssetImporterRegistry::Find(StringHash(DECLARED_TYPE));
	ASSERT_NE(importer, nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(DECLARED_EXT), importer);
	EXPECT_TRUE(importer->CanCreateAsset());
	EXPECT_EQ(importer->GetCreateMenuLabel(), "Hot Reload Declared");
	EXPECT_EQ(importer->GetCreateDefaultFileName(), "New Hot Reload Declared.hrdeclared");
	EXPECT_EQ(ImageOf(importer), static_cast<HMODULE>(const_cast<void*>(ModuleBase())));
}

TEST_F(AssetImporterModuleFixture, UnloadingModuleTakesDeclaredImporterWithIt) {
	ASSERT_NE(AssetImporterRegistry::Find(StringHash(DECLARED_TYPE)), nullptr);

	ModuleSystem::Unload(_module);
	_module = {};

	EXPECT_EQ(AssetImporterRegistry::Find(StringHash(DECLARED_TYPE)), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(DECLARED_EXT), nullptr);
}

TEST_F(AssetImporterModuleFixture, ReloadRegistersDeclaredImporterOfNewImage) {
	ASSERT_NE(AssetImporterRegistry::Find(StringHash(DECLARED_TYPE)), nullptr);

	Path v2(V2_DLL);
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v2));

	IAssetImporter* reloaded = AssetImporterRegistry::Find(StringHash(DECLARED_TYPE));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(ImageOf(reloaded), static_cast<HMODULE>(const_cast<void*>(ModuleBase())));
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(DECLARED_EXT), reloaded);
}
