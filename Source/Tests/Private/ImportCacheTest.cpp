#include "TestsPCH.h"

#include "ProductLookup.h"

#include "Asset/ImportArtifact.h"

#include "ScopedImportCostThreshold.h"

#include "FuzzHarness.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Asset/TextAsset.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/StyleSheetAsset.h"
#include "Shalltear/Core/UI/UIDocument.h"

using namespace Shalltear;

namespace {

	LinearAllocator MakeArena() {
		return LinearAllocator(64 * 1024, MemTag::Core);
	}

	Vector<u8> MakeBytes() {
		return Vector<u8>{GetMemoryResource(MemTag::Asset)};
	}

	struct Cooked {
		PakStringPool strings;
		Vector<u8> desc{GetMemoryResource(MemTag::Asset)};
		Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
	};

	bool CookInto(const Asset& asset, Cooked& out) {
		CookWriter descriptor(out.desc, out.strings);
		CookWriter payload(out.payload, out.strings);
		return asset.Cook(descriptor, payload);
	}

	Vector<u8> WriteEnvelope(const Cooked& cooked, u64 key, u64 typeNameHash, u32 importerVersion = 1) {
		ImportArtifactWriteDesc desc;
		desc.key = key;
		desc.sourceSize = 4096;
		desc.sourceMtime = 0x0123456789ABCDEFull;
		desc.typeNameHash = typeNameHash;
		desc.settingsHash = 0xC0FFEEull;
		desc.importerVersion = importerVersion;
		desc.strings = &cooked.strings.Texts();
		desc.desc = cooked.desc.empty() ? nullptr : cooked.desc.data();
		desc.descSize = cooked.desc.size();
		desc.payload = cooked.payload.empty() ? nullptr : cooked.payload.data();
		desc.payloadSize = cooked.payload.size();

		Vector<u8> file = MakeBytes();
		ImportArtifactWrite(file, desc);
		return file;
	}

	struct CookedType {
		const TypeInfo* type;
		const char* name;
	};

	Vector<CookedType> CookedAssetTypes() {
		Vector<CookedType> types{GetMemoryResource(MemTag::Asset)};
		types.push_back({TextAsset::GetReflectType(), "TextAsset"});
		types.push_back({StyleSheetAsset::GetReflectType(), "StyleSheetAsset"});
		types.push_back({MeshAsset::GetReflectType(), "MeshAsset"});
		types.push_back({TextureAsset::GetReflectType(), "TextureAsset"});
		types.push_back({FontAsset::GetReflectType(), "FontAsset"});
		types.push_back({MaterialAsset::GetReflectType(), "MaterialAsset"});
		types.push_back({ShaderAsset::GetReflectType(), "ShaderAsset"});
		types.push_back({UIDocumentAsset::GetReflectType(), "UIDocumentAsset"});
		types.push_back({SceneAsset::GetReflectType(), "SceneAsset"});
		types.push_back({PrefabAsset::GetReflectType(), "PrefabAsset"});
		return types;
	}

} // anonymous namespace

TEST(ImportCacheKeyTest, IdenticalInputsProduceTheSameKey) {
	const u8 source[] = {'h', 'e', 'l', 'l', 'o'};
	u64 a = ImportArtifactComputeKey(ImportArtifactHashSource(source, sizeof(source)), 7, 1);
	u64 b = ImportArtifactComputeKey(ImportArtifactHashSource(source, sizeof(source)), 7, 1);
	EXPECT_EQ(a, b);
}

TEST(ImportCacheKeyTest, AChangedSourceByteChangesTheKey) {
	const u8 before[] = {'h', 'e', 'l', 'l', 'o'};
	const u8 after[] = {'h', 'e', 'l', 'l', 'p'};
	u64 a = ImportArtifactComputeKey(ImportArtifactHashSource(before, sizeof(before)), 7, 1);
	u64 b = ImportArtifactComputeKey(ImportArtifactHashSource(after, sizeof(after)), 7, 1);
	EXPECT_NE(a, b);
}

TEST(ImportCacheKeyTest, AChangedSettingValueChangesTheKey) {
	LinearAllocator arena = MakeArena();
	JSONValue before = JSONParse(R"({"scaleFactor":1.0})", arena);
	JSONValue after = JSONParse(R"({"scaleFactor":2.0})", arena);
	u64 a = ImportArtifactComputeKey(11, ImportArtifactHashSettings(before), 1);
	u64 b = ImportArtifactComputeKey(11, ImportArtifactHashSettings(after), 1);
	EXPECT_NE(a, b);
}

TEST(ImportCacheKeyTest, ABumpedImporterVersionChangesTheKey) {
	u64 a = ImportArtifactComputeKey(11, 7, 1);
	u64 b = ImportArtifactComputeKey(11, 7, 2);
	EXPECT_NE(a, b);
}

TEST(ImportCacheKeyTest, ReformattingSettingsDoesNotChangeKey) {
	LinearAllocator arena = MakeArena();
	JSONValue tight = JSONParse(R"({"a":1,"b":"x"})", arena);
	JSONValue loose = JSONParse("{\n\t\"a\" : 1,\n\t\"b\" : \"x\"\n}", arena);
	EXPECT_EQ(ImportArtifactHashSettings(tight), ImportArtifactHashSettings(loose));
}

TEST(ImportCacheKeyTest, DifferentSettingsShapesDoNotCollide) {
	LinearAllocator arena = MakeArena();
	JSONValue empty = JSONParse("{}", arena);
	JSONValue null = JSONParse("null", arena);
	EXPECT_NE(ImportArtifactHashSettings(empty), ImportArtifactHashSettings(null));
}

TEST(ImportCacheKeyTest, AnEmptySourceHashesStably) {
	EXPECT_EQ(ImportArtifactHashSource(nullptr, 0), ImportArtifactHashSource(nullptr, 0));
	const u8 one[] = {0};
	EXPECT_NE(ImportArtifactHashSource(nullptr, 0), ImportArtifactHashSource(one, 1));
}

TEST(ImportCacheKeyTest, TheFoldIsStableAcrossBuilds) {
	EXPECT_EQ(0x56e59a536c7a4535ull, ImportArtifactComputeKey(1, 2, 3));
}

TEST(ImportCacheKeyTest, KeyDependsOnNothingBut3Inputs) {
	u64 first = ImportArtifactComputeKey(0xAAAA, 0xBBBB, 5);
	u64 again = ImportArtifactComputeKey(0xAAAA, 0xBBBB, 5);
	EXPECT_EQ(first, again);
	EXPECT_NE(first, ImportArtifactComputeKey(0xAAAB, 0xBBBB, 5));
	EXPECT_NE(first, ImportArtifactComputeKey(0xAAAA, 0xBBBC, 5));
	EXPECT_NE(first, ImportArtifactComputeKey(0xAAAA, 0xBBBB, 6));
}

TEST(ImportCacheEnvelopeTest, TheHeaderSurvivesARoundTrip) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});

	Vector<u8> file = WriteEnvelope(cooked, 0xFEEDFACEDEADBEEFull, 0x1122334455667788ull, 42);

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	EXPECT_EQ(0xFEEDFACEDEADBEEFull, view.header.key);
	EXPECT_EQ(0x1122334455667788ull, view.header.typeNameHash);
	EXPECT_EQ(42u, view.header.importerVersion);
	EXPECT_EQ(4096u, view.header.sourceSize);
	EXPECT_EQ(0x0123456789ABCDEFull, view.header.sourceMtime);
	EXPECT_EQ(0xC0FFEEull, view.header.settingsHash);
	EXPECT_EQ(PAK_RECORD_VERSION, view.header.recordVersion);
	EXPECT_EQ(static_cast<u8>(PakCodec::None), view.header.codec);
	EXPECT_EQ(IMPORT_ARTIFACT_VERSION_MAJOR, view.header.versionMajor);
	EXPECT_EQ(IMPORT_ARTIFACT_HEADER_SIZE, view.header.headerSize);
}

TEST(ImportCacheEnvelopeTest, EnvelopeRoundTripsBytes) {
	Cooked cooked;
	cooked.desc.assign({9, 8, 7});
	cooked.payload.assign({1, 2, 3, 4, 5, 6, 7});

	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	ASSERT_EQ(cooked.desc.size(), view.descSize);
	ASSERT_EQ(cooked.payload.size(), view.payloadSize);
	EXPECT_EQ(0, std::memcmp(cooked.desc.data(), view.desc, view.descSize));
	EXPECT_EQ(0, std::memcmp(cooked.payload.data(), view.payload, view.payloadSize));
}

TEST(ImportCacheEnvelopeTest, AnArtifactWithNoPayloadReadsBack) {
	Cooked cooked;
	cooked.desc.assign({1});

	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	EXPECT_EQ(0u, view.payloadSize);
	EXPECT_EQ(0u, view.header.paySize);
	CookReader payload = view.Payload();
	EXPECT_EQ(0u, payload.Remaining());
}

TEST(ImportCacheEnvelopeTest, ArtifactWithEmptyStringTableReadsBack) {
	Cooked cooked;
	cooked.desc.assign({1, 2});

	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	EXPECT_EQ(0u, view.header.strsSize);
	EXPECT_EQ(0u, view.strings.count);
	EXPECT_TRUE(view.strings.Get(0).empty());
}

TEST(ImportCacheEnvelopeTest, PerAssetStringTableRoundTripsIndices) {
	Cooked cooked;
	{
		CookWriter descriptor(cooked.desc, cooked.strings);
		descriptor.WriteString("albedo");
		descriptor.WriteString("roughness");
		descriptor.WriteString("albedo");
	}

	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	ASSERT_EQ(2u, view.strings.count);

	CookReader descriptor = view.Descriptor();
	EXPECT_EQ("albedo", descriptor.ReadString());
	EXPECT_EQ("roughness", descriptor.ReadString());
	EXPECT_EQ("albedo", descriptor.ReadString());
	EXPECT_FALSE(descriptor.Failed());
}

TEST(ImportCacheEnvelopeTest, CookableTypesRoundTrip) {
	Vector<CookedType> types = CookedAssetTypes();
	ASSERT_EQ(10u, types.size());

	u32 roundTripped = 0;
	str refusedToCook;
	for (const CookedType& entry : types) {
		Object* object = TypeRegistry::CreateObjectInstance(entry.type, MemTag::Asset);
		ASSERT_NE(nullptr, object) << entry.name;
		Asset* original = static_cast<Asset*>(object);

		Cooked cooked;
		if (!CookInto(*original, cooked)) {
			refusedToCook += entry.name;
			refusedToCook += " ";
			TypeRegistry::DestroyObjectInstance(object);
			continue;
		}

		Vector<u8> file = WriteEnvelope(cooked, 0xABCDEF, original->GetAssetTypeId().GetValue());

		ImportArtifactView view;
		ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view)) << entry.name;
		EXPECT_EQ(original->GetAssetTypeId().GetValue(), view.header.typeNameHash);

		Object* restoredObject = TypeRegistry::CreateObjectInstance(entry.type, MemTag::Asset);
		ASSERT_NE(nullptr, restoredObject);
		Asset* restored = static_cast<Asset*>(restoredObject);

		CookReader descriptor = view.Descriptor();
		CookReader payload = view.Payload();
		EXPECT_TRUE(restored->LoadCooked(descriptor, payload)) << entry.name;

		Cooked again;
		if (CookInto(*restored, again)) {
			EXPECT_EQ(cooked.desc.size(), again.desc.size()) << entry.name;
			if (cooked.desc.size() == again.desc.size() && !cooked.desc.empty()) {
				EXPECT_EQ(0, std::memcmp(cooked.desc.data(), again.desc.data(), cooked.desc.size())) << entry.name;
			}
			EXPECT_EQ(cooked.payload.size(), again.payload.size()) << entry.name;
			if (cooked.payload.size() == again.payload.size() && !cooked.payload.empty()) {
				EXPECT_EQ(0, std::memcmp(cooked.payload.data(), again.payload.data(), cooked.payload.size())) << entry.name;
			}
		}

		TypeRegistry::DestroyObjectInstance(restoredObject);
		TypeRegistry::DestroyObjectInstance(object);
		++roundTripped;
	}

	EXPECT_EQ("FontAsset UIDocumentAsset SceneAsset PrefabAsset ", refusedToCook);
	EXPECT_EQ(6u, roundTripped);
}

TEST(ImportCacheEnvelopeTest, UIDocumentRoundTrips) {
	UIDocumentAsset original;
	str error;
	ASSERT_TRUE(original.Parse(R"(<ui version="1">
	<panel id="root" class="hud dark" style="flex-grow: 1;">
		<row>Score</row>
	</panel>
</ui>)",
							   error))
		<< error;

	Cooked cooked;
	ASSERT_TRUE(CookInto(original, cooked));
	ASSERT_FALSE(cooked.payload.empty());

	Vector<u8> file = WriteEnvelope(cooked, 0xD0C, original.GetAssetTypeId().GetValue());

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	ASSERT_EQ(cooked.payload.size(), view.payloadSize);
	EXPECT_EQ(0, std::memcmp(cooked.payload.data(), view.payload, view.payloadSize));

	UIDocumentAsset restored;
	CookReader descriptor = view.Descriptor();
	CookReader payload = view.Payload();
	EXPECT_TRUE(restored.LoadCooked(descriptor, payload));

	Cooked again;
	ASSERT_TRUE(CookInto(restored, again));
	ASSERT_EQ(cooked.payload.size(), again.payload.size());
	EXPECT_EQ(0, std::memcmp(cooked.payload.data(), again.payload.data(), cooked.payload.size()));
}

TEST(ImportCacheEnvelopeTest, ADocumentShapedRecordRoundTrips) {
	Cooked cooked;
	for (u32 i = 0; i < 4096; ++i) {
		cooked.payload.push_back(static_cast<u8>(i * 31u));
	}
	ASSERT_TRUE(cooked.desc.empty());

	Vector<u8> file = WriteEnvelope(cooked, 0x5CE7E, 0xBEEF);

	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));
	EXPECT_EQ(0u, view.descSize);
	ASSERT_EQ(4096u, view.payloadSize);
	EXPECT_EQ(0, std::memcmp(cooked.payload.data(), view.payload, view.payloadSize));

	CookReader descriptor = view.Descriptor();
	EXPECT_EQ(0u, descriptor.Remaining());
}

namespace {

	class DocumentArtifactFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			_root = std::filesystem::temp_directory_path() /
					("shalltear_artifactdoc_" + std::to_string(counter.fetch_add(1)));
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
			std::filesystem::create_directories(_root / "Assets", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::CollectGarbage();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void ExpectRoundTrip(const Asset& original, Asset& restored, const char* what) {
			Cooked cooked;
			ASSERT_TRUE(CookInto(original, cooked)) << what;
			ASSERT_FALSE(cooked.payload.empty()) << what;

			Vector<u8> file = WriteEnvelope(cooked, 0xD0C5, original.GetAssetTypeId().GetValue());

			ImportArtifactView view;
			ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view)) << what;
			ASSERT_EQ(cooked.payload.size(), view.payloadSize);
			EXPECT_EQ(0, std::memcmp(cooked.payload.data(), view.payload, view.payloadSize));

			CookReader descriptor = view.Descriptor();
			CookReader payload = view.Payload();
			ASSERT_TRUE(restored.LoadCooked(descriptor, payload)) << what;

			Cooked again;
			ASSERT_TRUE(CookInto(restored, again));
			ASSERT_EQ(cooked.payload.size(), again.payload.size()) << what;
			EXPECT_EQ(0, std::memcmp(cooked.payload.data(), again.payload.data(), cooked.payload.size())) << what;
			EXPECT_EQ(cooked.desc.size(), again.desc.size()) << what;
		}
	private:
		size_t _sceneBytesBaseline = 0;
	};

} // namespace

TEST_F(DocumentArtifactFixture, SceneAssetRoundTripsThroughTheEnvelope) {
	Handle<Scene> scene = SceneSystem::CreateScene("Level1");
	Handle<SceneObject> player = SceneSystem::CreateObject("Player", scene);
	player->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
	Handle<SceneObject> arm = SceneSystem::CreateObject("Arm", scene, player);
	arm->SetActive(false);

	const char* scenePath = "project://Assets/Level1.scene";
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path(scenePath), false));
	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path(scenePath));
	ASSERT_TRUE(guid.IsValid());

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(nullptr, loaded);

	SceneAsset restored;
	ExpectRoundTrip(*loaded, restored, "SceneAsset");

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(DocumentArtifactFixture, PrefabAssetRoundTripsThroughTheEnvelope) {
	Handle<Scene> scene = SceneSystem::CreateScene("Stage");
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", scene);
	unit->SetLocalPosition(Vec3(3.0f, 4.0f, 5.0f));
	Handle<SceneObject> muzzle = SceneSystem::CreateObject("Muzzle", scene, unit);
	muzzle->SetLocalPosition(Vec3(0.0f, 0.0f, 1.5f));

	Guid guid = SceneSystem::SavePrefab(unit, Path("project://Assets/Unit.prefab"));
	ASSERT_TRUE(guid.IsValid());

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(nullptr, loaded);

	PrefabAsset restored;
	ExpectRoundTrip(*loaded, restored, "PrefabAsset");
	EXPECT_NE(nullptr, restored.GetFragment());

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(DocumentArtifactFixture, ADocumentTypeLeavesNoArtifactBehind) {
	Handle<Scene> scene = SceneSystem::CreateScene("NoArtifact");
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", scene);

	Guid prefabGuid = SceneSystem::SavePrefab(unit, Path("project://Assets/Unit.prefab"));
	ASSERT_TRUE(prefabGuid.IsValid());
	const char* scenePath = "project://Assets/Level.scene";
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path(scenePath), false));
	AssetDatabase::ScanProjectDirectory();
	Guid sceneGuid = AssetDatabase::LookupGuid(Path(scenePath));
	ASSERT_TRUE(sceneGuid.IsValid());

	Asset* loadedPrefab = AssetDatabase::LoadAssetByGuid(prefabGuid);
	Asset* loadedScene = AssetDatabase::LoadAssetByGuid(sceneGuid);
	ASSERT_NE(nullptr, loadedPrefab);
	ASSERT_NE(nullptr, loadedScene);

	EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(ImportArtifactPath(prefabGuid).ToString())));
	EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(ImportArtifactPath(sceneGuid).ToString())));

	AssetDatabase::UnloadAsset(loadedPrefab);
	AssetDatabase::UnloadAsset(loadedScene);
}

TEST(ImportCacheEnvelopeTest, AnEmptyOrShortBufferIsRefused) {
	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(nullptr, 0, view));

	Vector<u8> tiny = MakeBytes();
	tiny.assign(IMPORT_ARTIFACT_HEADER_SIZE - 1, 0);
	EXPECT_FALSE(ImportArtifactRead(tiny.data(), tiny.size(), view));
}

TEST(ImportCacheEnvelopeTest, AMangledMagicIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	file[5] = 0x0D;

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, ATruncatedEnvelopeIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3});
	cooked.payload.assign({4, 5, 6, 7, 8});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	for (size_t cut = 1; cut < file.size(); ++cut) {
		Vector<u8> truncated = MakeBytes();
		truncated.assign(file.begin(), file.begin() + static_cast<ptrdiff_t>(file.size() - cut));
		ImportArtifactView view;
		EXPECT_FALSE(ImportArtifactRead(truncated.data(), truncated.size(), view)) << cut;
	}
}

TEST(ImportCacheEnvelopeTest, BitFlipInBodyIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3});
	cooked.payload.assign({4, 5, 6, 7});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	for (size_t at = IMPORT_ARTIFACT_HEADER_SIZE; at < file.size(); ++at) {
		Vector<u8> flipped = MakeBytes();
		flipped.assign(file.begin(), file.end());
		flipped[at] ^= 0x01;
		ImportArtifactView view;
		EXPECT_FALSE(ImportArtifactRead(flipped.data(), flipped.size(), view)) << at << " was accepted";
	}
}

TEST(ImportCacheEnvelopeTest, ACRCRepairedSizeLieIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.descSize += 8;
	header.crc32c = PakCrc32c(file.data() + IMPORT_ARTIFACT_HEADER_SIZE, file.size() - IMPORT_ARTIFACT_HEADER_SIZE);
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, SizesThatWrapWhenSummedAreRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.paySize = UINT64_MAX - 16;
	header.descSize = 64;
	header.crc32c = PakCrc32c(file.data() + IMPORT_ARTIFACT_HEADER_SIZE, file.size() - IMPORT_ARTIFACT_HEADER_SIZE);
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, AnUnknownCodecIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.codec = static_cast<u8>(PakCodec::LZ4);
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, AnUnknownFlagIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.flags = static_cast<u8>(IMPORT_ARTIFACT_FLAGS_KNOWN | 0x80);
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, OversizedProductCountRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.prodCount = 1;
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, AFutureFormatVersionIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.versionMajor = IMPORT_ARTIFACT_VERSION_MAJOR + 1;
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, AnUnalignedSectionSizeIsRefused) {
	Cooked cooked;
	cooked.desc.assign({1, 2, 3, 4});
	Vector<u8> file = WriteEnvelope(cooked, 1, 2);

	ImportArtifactHeader header{};
	std::memcpy(&header, file.data(), sizeof(header));
	header.inputsSize = 3;
	std::memcpy(file.data(), &header, sizeof(header));

	ImportArtifactView view;
	EXPECT_FALSE(ImportArtifactRead(file.data(), file.size(), view));
}

TEST(ImportCacheEnvelopeTest, ArtifactPathShardsByGuid) {
	str path = ImportArtifactPath(Guid{0xAB12345678901234ull, 0xCDEFull}).ToString();
	EXPECT_NE(str::npos, path.find("/ab/ab12345678901234-000000000000cdef.slart"));
}

TEST(ImportCacheEnvelopeTest, ArtifactPathIsStableForOneAsset) {
	Guid a{7, 0};
	Guid b{8, 0};
	EXPECT_EQ(ImportArtifactPath(a).ToString(), ImportArtifactPath(a).ToString());
	EXPECT_NE(ImportArtifactPath(a).ToString(), ImportArtifactPath(b).ToString());
	EXPECT_NE(ImportArtifactPath(a).ToString(), ImportArtifactPath(Guid{7, 1}).ToString());
}

namespace {

	void SpendEnoughToBeWorthCaching() {
		auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(3 * IMPORT_ARTIFACT_COST_THRESHOLD_US);
		while (std::chrono::steady_clock::now() < until) {
		}
	}

	class CacheCountingImporter : public IAssetImporter {
	public:
		int imports = 0;
		u32 version = 1;
		bool cheap = false;

		StringHash GetAssetType() const override { return StringHash("CachedAsset"); }
		u32 GetImporterVersion() const override { return version; }
		Asset* Import(const Path& sourcePath) override {
			++imports;
			if (!cheap) {
				SpendEnoughToBeWorthCaching();
			}
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}
	};

	class ImportCacheStoreFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		CacheCountingImporter _importer;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_artifacts_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");

			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetImporterRegistry::Register(StringHash("CachedAsset"), &_importer, {".cached"});
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			AssetImporterRegistry::Unregister(&_importer);
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Restart() {
			AssetDatabase::Terminate();
			AssetDatabase::Initialize();
		}

		void WriteSource(const char* virtualPath, const str& content) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, content);
		}

		std::filesystem::path Physical(const char* virtualPath) const {
			return std::filesystem::path(FileSystem::Resolve(Path(virtualPath)).ToString());
		}

		Guid GuidOf(const char* virtualPath) const { return AssetDatabase::LookupGuid(Path(virtualPath)); }

		std::filesystem::path ArtifactFor(const Guid& guid) const {
			return std::filesystem::path(ImportArtifactPath(guid).ToString());
		}

		bool ReadArtifactHeader(const Guid& guid, ImportArtifactHeader& out) const {
			Buffer bytes = FileSystem::ReadFile(Path(ArtifactFor(guid).generic_string()), MemTag::Asset);
			ImportArtifactView view;
			if (!ImportArtifactRead(bytes.data, bytes.size, view)) {
				return false;
			}
			out = view.header;
			return true;
		}

		void WriteSettings(const char* virtualPath, const str& settingsJSON) {
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(Path(virtualPath)));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Core);
			size_t at = text.find("\"settings\"");
			ASSERT_NE(str::npos, at);
			size_t open = text.find('{', at);
			size_t close = text.find('}', open);
			ASSERT_NE(str::npos, close);
			text = text.substr(0, open) + settingsJSON + text.substr(close + 1);
			FileSystem::WriteTextFile(physMeta, text);
		}
	};

} // namespace

TEST_F(ImportCacheStoreFixture, WarmCacheSkipsImporter) {
	WriteSource("project://Assets/note.cached", "the quick brown fox");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/note.cached"));
	ASSERT_NE(nullptr, first);
	EXPECT_EQ(1, _importer.imports);
	Guid guid = first->assetId;
	ASSERT_TRUE(guid.IsValid());
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(guid)));

	Restart();

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/note.cached"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(1, _importer.imports);

	EXPECT_EQ(guid, second->assetId);
	EXPECT_EQ("project://Assets/note.cached", second->virtualPath);
	EXPECT_EQ("the quick brown fox", static_cast<TextAsset*>(second)->content);
	EXPECT_EQ(AssetLoadState::Loaded, second->GetLoadState());
}

TEST_F(ImportCacheStoreFixture, ArtifactRecordsSourceStamp) {
	WriteSource("project://Assets/stamped.cached", "abcdef");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/stamped.cached"));
	ASSERT_NE(nullptr, asset);

	ImportArtifactHeader header{};
	ASSERT_TRUE(ReadArtifactHeader(asset->assetId, header));
	Path source = FileSystem::Resolve(Path("project://Assets/stamped.cached"));
	EXPECT_EQ(static_cast<u64>(FileSystem::GetFileSize(source)), header.sourceSize);
	EXPECT_EQ(FileSystem::GetLastWriteTime(source), header.sourceMtime);
	EXPECT_EQ(1u, header.importerVersion);
}

TEST_F(ImportCacheStoreFixture, TouchedSourceStillHits) {
	WriteSource("project://Assets/touched.cached", "unchanged content");
	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/touched.cached"));
	ASSERT_NE(nullptr, first);
	Guid guid = first->assetId;

	ImportArtifactHeader before{};
	ASSERT_TRUE(ReadArtifactHeader(guid, before));

	Restart();

	std::error_code ec;
	std::filesystem::last_write_time(Physical("project://Assets/touched.cached"),
									 std::filesystem::file_time_type::clock::now() + std::chrono::hours(1), ec);
	ASSERT_FALSE(ec);
	u64 movedMtime = FileSystem::GetLastWriteTime(FileSystem::Resolve(Path("project://Assets/touched.cached")));
	ASSERT_NE(before.sourceMtime, movedMtime);

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/touched.cached"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(1, _importer.imports);
	EXPECT_EQ("unchanged content", static_cast<TextAsset*>(second)->content);

	ImportArtifactHeader after{};
	ASSERT_TRUE(ReadArtifactHeader(guid, after));
	EXPECT_EQ(movedMtime, after.sourceMtime);
	EXPECT_EQ(before.key, after.key);
}

TEST_F(ImportCacheStoreFixture, EditingASourceMissesAndReimports) {
	WriteSource("project://Assets/edited.cached", "before");
	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/edited.cached"));
	ASSERT_NE(nullptr, first);
	EXPECT_EQ(1, _importer.imports);

	Restart();
	WriteSource("project://Assets/edited.cached", "after the edit");

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/edited.cached"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(2, _importer.imports);
	EXPECT_EQ("after the edit", static_cast<TextAsset*>(second)->content);
}

TEST_F(ImportCacheStoreFixture, ChangedImporterSettingsMissAndReimport) {
	WriteSource("project://Assets/settings.cached", "content");
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/settings.cached")));
	EXPECT_EQ(1, _importer.imports);

	Restart();
	WriteSettings("project://Assets/settings.cached", R"({"scaleFactor":2.0})");

	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/settings.cached")));
	EXPECT_EQ(2, _importer.imports);
}

TEST_F(ImportCacheStoreFixture, ABumpedImporterVersionMissesAndReimports) {
	WriteSource("project://Assets/versioned.cached", "content");
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/versioned.cached")));
	EXPECT_EQ(1, _importer.imports);

	Restart();
	_importer.version = 2;

	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/versioned.cached")));
	EXPECT_EQ(2, _importer.imports);
}

TEST_F(ImportCacheStoreFixture, CorruptArtifactFallsThroughToImporter) {
	WriteSource("project://Assets/corrupt.cached", "the real content");
	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/corrupt.cached"));
	ASSERT_NE(nullptr, first);
	Guid guid = first->assetId;

	Restart();

	std::filesystem::path artifact = ArtifactFor(guid);
	ASSERT_TRUE(std::filesystem::exists(artifact));
	{
		std::ofstream truncate(artifact, std::ios::binary | std::ios::trunc);
		truncate << "not an artifact";
	}

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/corrupt.cached"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(2, _importer.imports);
	EXPECT_EQ(guid, second->assetId);
	EXPECT_EQ("the real content", static_cast<TextAsset*>(second)->content);

	ImportArtifactHeader header{};
	EXPECT_TRUE(ReadArtifactHeader(guid, header));
}

TEST_F(ImportCacheStoreFixture, UnreadableArtifactDeleted) {
	WriteSource("project://Assets/cheap.cached", "the real content");
	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/cheap.cached"));
	ASSERT_NE(nullptr, first);
	Guid guid = first->assetId;
	std::filesystem::path artifact = ArtifactFor(guid);
	ASSERT_TRUE(std::filesystem::exists(artifact));

	Restart();

	_importer.cheap = true;
	{
		std::ofstream truncate(artifact, std::ios::binary | std::ios::trunc);
		truncate << "not an artifact";
	}

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/cheap.cached"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ("the real content", static_cast<TextAsset*>(second)->content);
	EXPECT_FALSE(std::filesystem::exists(artifact));
}

TEST_F(ImportCacheStoreFixture, BitFlippedArtifactFallsThroughToImporter) {
	WriteSource("project://Assets/flipped.cached", "the real content");
	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/flipped.cached"));
	ASSERT_NE(nullptr, first);
	Guid guid = first->assetId;

	Restart();

	std::filesystem::path artifact = ArtifactFor(guid);
	Buffer bytes = FileSystem::ReadFile(Path(artifact.generic_string()), MemTag::Asset);
	ASSERT_TRUE(bytes);
	ASSERT_GT(bytes.size, IMPORT_ARTIFACT_HEADER_SIZE);
	bytes.data[IMPORT_ARTIFACT_HEADER_SIZE] ^= 0x01;
	ASSERT_TRUE(FileSystem::WriteFile(Path(artifact.generic_string()), bytes));

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/flipped.cached"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(2, _importer.imports);
	EXPECT_EQ("the real content", static_cast<TextAsset*>(second)->content);
}

TEST_F(ImportCacheStoreFixture, CopiedArtifactCannotRenameAssetItServes) {
	WriteSource("project://Assets/left.cached", "identical bytes");
	WriteSource("project://Assets/right.cached", "identical bytes");

	Asset* left = AssetDatabase::LoadAsset(Path("project://Assets/left.cached"));
	ASSERT_NE(nullptr, left);
	Guid leftGuid = left->assetId;
	Asset* right = AssetDatabase::LoadAsset(Path("project://Assets/right.cached"));
	ASSERT_NE(nullptr, right);
	Guid rightGuid = right->assetId;
	ASSERT_NE(leftGuid, rightGuid);
	int importsBefore = _importer.imports;

	Restart();

	std::error_code ec;
	std::filesystem::create_directories(ArtifactFor(rightGuid).parent_path(), ec);
	std::filesystem::copy_file(ArtifactFor(leftGuid), ArtifactFor(rightGuid),
							   std::filesystem::copy_options::overwrite_existing, ec);
	ASSERT_FALSE(ec);

	Asset* served = AssetDatabase::LoadAsset(Path("project://Assets/right.cached"));
	ASSERT_NE(nullptr, served);
	ASSERT_EQ(importsBefore, _importer.imports);

	EXPECT_EQ(rightGuid, served->assetId);
	EXPECT_EQ("project://Assets/right.cached", served->virtualPath);
}

TEST_F(ImportCacheStoreFixture, DeletedSourceFailsWarmLoad) {
	WriteSource("project://Assets/doomed.cached", "here for now");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/doomed.cached"));
	ASSERT_NE(nullptr, asset);
	Guid guid = asset->assetId;
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(guid)));

	Restart();
	std::error_code ec;
	std::filesystem::remove(Physical("project://Assets/doomed.cached"), ec);

	EXPECT_EQ(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/doomed.cached")));
}

TEST(ImportCacheImportStateTest, TheDefaultCarriesNothingAndSucceeds) {
	TextAsset asset;
	Cooked cooked;
	CookWriter importState(cooked.payload, cooked.strings);
	EXPECT_TRUE(asset.CookImportState(importState));
	EXPECT_TRUE(cooked.payload.empty());

	CookReader empty;
	EXPECT_TRUE(asset.LoadImportState(empty));
}

TEST(ImportCacheImportStateTest, MeshCarriesPerSlotDefaultMaterials) {
	MeshAsset original;
	original.defaultMaterials.push_back(Guid{0x1111, 0x2222});
	original.defaultMaterials.push_back(Guid{});
	original.defaultMaterials.push_back(Guid{0x3333, 0x4444});

	Cooked cooked;
	CookWriter writer(cooked.payload, cooked.strings);
	ASSERT_TRUE(original.CookImportState(writer));

	Cooked cookedRecord;
	ASSERT_TRUE(CookInto(original, cookedRecord));
	MeshAsset viaCookOnly;
	{
		CookReader descriptor(cookedRecord.desc.data(), cookedRecord.desc.size(), {});
		CookReader payload(cookedRecord.payload.data(), cookedRecord.payload.size(), {});
		ASSERT_TRUE(viaCookOnly.LoadCooked(descriptor, payload));
	}
	EXPECT_TRUE(viaCookOnly.defaultMaterials.empty());

	MeshAsset restored;
	CookReader reader(cooked.payload.data(), cooked.payload.size(), {});
	ASSERT_TRUE(restored.LoadImportState(reader));
	ASSERT_EQ(3u, restored.defaultMaterials.size());
	EXPECT_EQ(Guid(0x1111, 0x2222), restored.defaultMaterials[0]);
	EXPECT_FALSE(restored.defaultMaterials[1].IsValid());
	EXPECT_EQ(Guid(0x3333, 0x4444), restored.defaultMaterials[2]);
}

TEST(ImportCacheImportStateTest, AShaderCarriesItsHotReloadDependencies) {
	ShaderAsset original;
	original.dependencies.push_back({"C:/proj/Assets/lit.hlsl", 0xAAAA});
	original.dependencies.push_back({"C:/engine/Shaders/ShalltearCommon.hlsli", 0xBBBB});
	original.memberRanges.push_back({"roughness", 0.0f, 1.0f});
	original.memberHints.push_back({"emissive", ShaderHint::HDR});
	ShaderMemberGroup group;
	group.name = "Surface Inputs";
	group.members.push_back("baseColor");
	group.members.push_back("roughness");
	original.memberGroups.push_back(std::move(group));
	original.keywordBindings.push_back({"RIM_LIGHT", "rimStrength"});
	original.keywordBindings.push_back({"_MODE_B", "mode", 1, "Beta Two"});
	original.memberLabels.push_back({"baseColor", "Base Tint"});
	original.memberHints.push_back({"useRim", ShaderHint::Toggle});
	original.memberHints.push_back({"secret", ShaderHint::Hidden});
	original.skipOptimizations = true;
	original.depthProgram = ShaderDepthProgram::Clipping;

	Cooked cooked;
	CookWriter writer(cooked.payload, cooked.strings);
	ASSERT_TRUE(original.CookImportState(writer));

	Vector<u8> file = WriteEnvelope(cooked, 1, 2);
	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(file.data(), file.size(), view));

	ShaderAsset restored;
	CookReader reader(cooked.payload.data(), cooked.payload.size(), view.strings);
	ASSERT_TRUE(restored.LoadImportState(reader));
	ASSERT_EQ(2u, restored.dependencies.size());
	EXPECT_EQ("C:/proj/Assets/lit.hlsl", restored.dependencies[0].physicalPath);
	EXPECT_EQ(0xAAAAu, restored.dependencies[0].contentHash);
	EXPECT_EQ("C:/engine/Shaders/ShalltearCommon.hlsli", restored.dependencies[1].physicalPath);
	EXPECT_EQ(0xBBBBu, restored.dependencies[1].contentHash);

	ASSERT_EQ(1u, restored.memberRanges.size());
	EXPECT_EQ("roughness", restored.memberRanges[0].name);
	EXPECT_FLOAT_EQ(1.0f, restored.memberRanges[0].maxValue);
	EXPECT_EQ(ShaderHint::HDR, restored.FindMemberHint("emissive"));
	ASSERT_EQ(1u, restored.memberGroups.size());
	EXPECT_EQ("Surface Inputs", restored.memberGroups[0].name);
	ASSERT_EQ(2u, restored.memberGroups[0].members.size());
	EXPECT_EQ("baseColor", restored.memberGroups[0].members[0]);
	EXPECT_EQ("rimStrength", restored.FindKeywordMember("RIM_LIGHT"));
	ASSERT_EQ(2u, restored.keywordBindings.size());
	EXPECT_EQ(-1, restored.keywordBindings[0].value);
	EXPECT_EQ(1, restored.keywordBindings[1].value);
	EXPECT_EQ("Beta Two", restored.keywordBindings[1].entry);
	EXPECT_EQ("Base Tint", restored.FindMemberLabel("baseColor"));
	EXPECT_EQ(ShaderHint::Toggle, restored.FindMemberHint("useRim"));
	EXPECT_EQ(ShaderHint::Hidden, restored.FindMemberHint("secret"));
	EXPECT_TRUE(restored.skipOptimizations);
	EXPECT_EQ(ShaderDepthProgram::Clipping, restored.depthProgram);

	EXPECT_EQ(0u, restored.version);
}

TEST(ImportCacheImportStateTest, PartialDependencyListRefused) {
	ShaderAsset original;
	original.dependencies.push_back({"C:/proj/Assets/lit.hlsl", 0xAAAA});
	original.dependencies.push_back({"C:/proj/Assets/other.hlsli", 0xBBBB});

	Cooked cooked;
	CookWriter writer(cooked.payload, cooked.strings);
	ASSERT_TRUE(original.CookImportState(writer));
	ASSERT_GT(cooked.payload.size(), 4u);

	ShaderAsset restored;
	restored.dependencies.push_back({"C:/proj/Assets/untouched.hlsl", 0xCCCC});
	CookReader reader(cooked.payload.data(), cooked.payload.size() - 4, {});
	EXPECT_FALSE(restored.LoadImportState(reader));
	ASSERT_EQ(1u, restored.dependencies.size());
	EXPECT_EQ("C:/proj/Assets/untouched.hlsl", restored.dependencies[0].physicalPath);
}

TEST(ImportCacheImportStateTest, TheDocumentTypesRefuseAnArtifactOutright) {
	SceneAsset scene;
	PrefabAsset prefab;

	Cooked sink;
	CookWriter sceneWriter(sink.payload, sink.strings);
	EXPECT_FALSE(scene.CookImportState(sceneWriter));
	CookWriter prefabWriter(sink.payload, sink.strings);
	EXPECT_FALSE(prefab.CookImportState(prefabWriter));

	EXPECT_TRUE(sink.payload.empty());
}

TEST(ImportCacheKeyTest, ImporterVersionDefaultsTo1) {
	struct DefaultImporter : IAssetImporter {
		StringHash GetAssetType() const override { return StringHash("TextAsset"); }
		Asset* Import(const Path&) override { return nullptr; }
	};
	struct BumpedImporter : DefaultImporter {
		u32 GetImporterVersion() const override { return 4; }
	};

	DefaultImporter unversioned;
	BumpedImporter bumped;
	EXPECT_EQ(1u, unversioned.GetImporterVersion());
	EXPECT_EQ(4u, bumped.GetImporterVersion());
	EXPECT_NE(ImportArtifactComputeKey(1, 2, unversioned.GetImporterVersion()),
			  ImportArtifactComputeKey(1, 2, bumped.GetImporterVersion()));
}

namespace {

	class TouchingImporter : public IAssetImporter {
	public:
		int imports = 0;
		Vector<str> reads{GetMemoryResource(MemTag::Asset)};
		Vector<str> writes{GetMemoryResource(MemTag::Asset)};
		Vector<str> loads{GetMemoryResource(MemTag::Asset)};
		StringHash assetType = StringHash("TouchedAsset");

		StringHash GetAssetType() const override { return assetType; }
		Asset* Import(const Path& sourcePath) override {
			++imports;
			SpendEnoughToBeWorthCaching();
			for (const str& load : loads) {
				if (Asset* inner = AssetDatabase::LoadAsset(Path(load))) {
					AssetDatabase::UnloadAsset(inner);
				}
			}
			for (const str& write : writes) {
				if (!FileSystem::Exists(Path(write))) {
					Path resolved = FileSystem::Resolve(Path(write));
					std::error_code ec;
					std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
					FileSystem::WriteTextFile(resolved, "generated");
				}
			}
			str extra;
			for (const str& read : reads) {
				extra += FileSystem::ReadTextFile(Path(read), MemTag::Asset);
			}
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			asset->content += "|" + extra;
			return asset;
		}
	};

	class ImportCacheInvalidationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		TouchingImporter _importer;
		TouchingImporter _inner;
		bool _mountedEngine = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_invalidation_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetImporterRegistry::Register(StringHash("TouchedAsset"), &_importer, {".touched"});
			_inner.assetType = StringHash("InnerTouchedAsset");
			AssetImporterRegistry::Register(StringHash("InnerTouchedAsset"), &_inner, {".inner"});
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			AssetImporterRegistry::Unregister(&_inner);
			AssetImporterRegistry::Unregister(&_importer);
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Restart() {
			AssetDatabase::Terminate();
			AssetDatabase::Initialize();
		}

		static std::filesystem::path FindRepoDir(const char* relative) {
			std::error_code ec;
			std::filesystem::path probe = std::filesystem::current_path(ec);
			for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
				std::filesystem::path candidate = probe / relative;
				if (std::filesystem::exists(candidate, ec)) {
					return candidate;
				}
				std::filesystem::path parent = probe.parent_path();
				if (parent == probe) {
					break;
				}
				probe = parent;
			}
			return {};
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = FindRepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name, std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		void WriteSource(const char* virtualPath, const str& content) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, content);
		}

		std::filesystem::path Physical(const char* virtualPath) const {
			return std::filesystem::path(FileSystem::Resolve(Path(virtualPath)).ToString());
		}

		std::filesystem::path ArtifactFor(const Guid& guid) const {
			return std::filesystem::path(ImportArtifactPath(guid).ToString());
		}

		struct RecordedInput {
			str path;
			u64 size = 0;
			u64 mtime = 0;
			u64 contentHash = 0;
		};

		bool ReadArtifact(const Guid& guid, Vector<RecordedInput>& inputs, Vector<str>& outputs) const {
			inputs.clear();
			outputs.clear();
			Buffer bytes = FileSystem::ReadFile(Path(ArtifactFor(guid).generic_string()), MemTag::Asset);
			ImportArtifactView view;
			if (!ImportArtifactRead(bytes.data, bytes.size, view)) {
				return false;
			}
			CookReader inputCursor = view.Inputs();
			for (u32 i = 0; i < view.header.inputCount; ++i) {
				u32 index = 0;
				RecordedInput record;
				if (!inputCursor.Read(index) || !inputCursor.Read(record.size) || !inputCursor.Read(record.mtime) ||
					!inputCursor.Read(record.contentHash)) {
					return false;
				}
				record.path = str(view.strings.Get(index));
				inputs.push_back(record);
			}
			CookReader outputCursor = view.Outputs();
			for (u32 i = 0; i < view.header.outputCount; ++i) {
				u32 index = 0;
				if (!outputCursor.Read(index)) {
					return false;
				}
				outputs.push_back(str(view.strings.Get(index)));
			}
			return true;
		}

		static bool Names(const Vector<RecordedInput>& inputs, std::string_view path) {
			for (const RecordedInput& input : inputs) {
				if (input.path == path) {
					return true;
				}
			}
			return false;
		}
	};

} // namespace

TEST_F(ImportCacheInvalidationFixture, EditingAModelInvalidatesItsCachedProducts) {
	ASSERT_TRUE(_mountedEngine);
	ASSERT_TRUE(StageFixture("Fox.glb"));

	Path source("project://Fox.glb");
	Asset* model = AssetDatabase::LoadAsset(source);
	ASSERT_NE(nullptr, model);
	Guid modelGuid = model->assetId;
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(modelGuid)));

	Guid beforeMaterial = ProductLookup::Find(source, "Materials/fox_material");
	ASSERT_TRUE(beforeMaterial.IsValid());
	Asset* mesh = ProductLookup::Load(source, "Meshes/fox1");
	ASSERT_NE(nullptr, mesh);
	ASSERT_FALSE(static_cast<MeshAsset*>(mesh)->defaultMaterials.empty());
	EXPECT_EQ(beforeMaterial, static_cast<MeshAsset*>(mesh)->defaultMaterials[0]);

	Restart();

	std::filesystem::path physical = Physical("project://Fox.glb");
	Buffer bytes = FileSystem::ReadFile(Path(physical.generic_string()), MemTag::Asset);
	ASSERT_GT(bytes.size, 0u);
	str needle = "\"name\":\"fox_material\"";
	u8* found = std::search(bytes.data, bytes.data + bytes.size, needle.begin(), needle.end());
	ASSERT_NE(bytes.data + bytes.size, found);
	str replacement = "\"name\":\"foo_material\"";
	ASSERT_EQ(needle.size(), replacement.size());
	std::memcpy(found, replacement.data(), replacement.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(physical.generic_string()), bytes));

	Asset* remodel = AssetDatabase::LoadAsset(source);
	ASSERT_NE(nullptr, remodel);

	Guid afterMaterial = ProductLookup::Find(source, "Materials/foo_material");
	ASSERT_TRUE(afterMaterial.IsValid());
	EXPECT_NE(beforeMaterial, afterMaterial);

	Asset* remesh = ProductLookup::Load(source, "Meshes/fox1");
	ASSERT_NE(nullptr, remesh);
	ASSERT_FALSE(static_cast<MeshAsset*>(remesh)->defaultMaterials.empty());
	EXPECT_EQ(afterMaterial, static_cast<MeshAsset*>(remesh)->defaultMaterials[0]);
}

TEST_F(ImportCacheInvalidationFixture, ExtractedMaterialKeepsOthersCached) {
	ASSERT_TRUE(_mountedEngine);
	ASSERT_TRUE(StageFixture("Fox.glb"));

	WriteSource("project://Fox.glb.materials/0_fox_material.mat", "");
	{
		MaterialAsset staged;
		staged.SetFloat("userEditedMarker", 0.5f);
		ASSERT_TRUE(staged.Export(Path(Physical("project://Fox.glb.materials/0_fox_material.mat").generic_string())));
	}
	Guid extracted = AssetDatabase::EnsureAssetIdentity(Path("project://Fox.glb.materials/0_fox_material.mat"));
	ASSERT_TRUE(extracted.IsValid());

	Path source("project://Fox.glb");
	Asset* model = AssetDatabase::LoadAsset(source);
	ASSERT_NE(nullptr, model);
	Guid modelGuid = model->assetId;
	EXPECT_EQ(extracted, ProductLookup::Find(source, "Materials/fox_material"));
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(modelGuid)));

	Buffer envelope = FileSystem::ReadFile(Path(ArtifactFor(modelGuid).generic_string()), MemTag::Asset);
	ImportArtifactView view;
	ASSERT_TRUE(ImportArtifactRead(envelope.data, envelope.size, view));
	EXPECT_FALSE(view.HasMain());
	EXPECT_EQ(3u, view.header.prodCount);

	Asset* mesh = ProductLookup::Load(source, "Meshes/fox1");
	ASSERT_NE(nullptr, mesh);
	ASSERT_FALSE(static_cast<MeshAsset*>(mesh)->defaultMaterials.empty());
	EXPECT_EQ(extracted, static_cast<MeshAsset*>(mesh)->defaultMaterials[0]);
}

TEST_F(ImportCacheInvalidationFixture, WarmShaderArtifactRestoresDeclaredDefaults) {
	auto shaderSource = [](const char* value) {
		return str("#pragma shalltear_default baseColor ") + value + "\n" +
			   "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
			   "struct VSOutput { float4 positionCS : SV_Position; };\n"
			   "VSOutput VSMain(uint vertexId : SV_VertexID) { VSOutput o; o.positionCS = float4(0, 0, 0, 1); return o; }\n"
			   "float4 PSMain(VSOutput input) : SV_Target { return baseColor; }\n";
	};
	WriteSource("project://Shaders/Defaults.hlsl", shaderSource("1 1 1 1"));

	Asset* first = AssetDatabase::LoadAsset(Path("project://Shaders/Defaults.hlsl"));
	ASSERT_NE(nullptr, first);
	ShaderAsset* shader = static_cast<ShaderAsset*>(first);
	if (shader->variants.empty()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	ASSERT_EQ(1u, shader->memberDefaults.size());
	EXPECT_FLOAT_EQ(1.0f, shader->memberDefaults[0].numbers[0]);
	Guid guid = first->assetId;
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(guid)));

	std::filesystem::path physical = Physical("project://Shaders/Defaults.hlsl");
	std::error_code ec;
	auto mtime = std::filesystem::last_write_time(physical, ec);
	ASSERT_FALSE(ec);
	str poisoned = shaderSource("9 9 9 9");
	ASSERT_EQ(poisoned.size(), shaderSource("1 1 1 1").size());
	FileSystem::WriteTextFile(Path(physical.generic_string()), poisoned);
	std::filesystem::last_write_time(physical, mtime, ec);
	ASSERT_FALSE(ec);

	Restart();
	Asset* second = AssetDatabase::LoadAsset(Path("project://Shaders/Defaults.hlsl"));
	ASSERT_NE(nullptr, second);
	ShaderAsset* warm = static_cast<ShaderAsset*>(second);
	ASSERT_EQ(1u, warm->memberDefaults.size());
	EXPECT_FLOAT_EQ(1.0f, warm->memberDefaults[0].numbers[0]);
}

TEST_F(ImportCacheInvalidationFixture, EditingAnIncludeInvalidatesTheShaderArtifact) {
	WriteSource("project://Shaders/Params.hlsli", "#define TINT float4(1, 0, 0, 1)\n");
	WriteSource("project://Shaders/Lit.hlsl",
				"#include \"Params.hlsli\"\n"
				"struct VSOutput { float4 positionCS : SV_Position; };\n"
				"VSOutput VSMain(uint vertexId : SV_VertexID) { VSOutput o; o.positionCS = float4(0, 0, 0, 1); return o; }\n"
				"float4 PSMain(VSOutput input) : SV_Target { return TINT; }\n");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Shaders/Lit.hlsl"));
	ASSERT_NE(nullptr, first);
	ShaderAsset* shader = static_cast<ShaderAsset*>(first);
	if (shader->variants.empty()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	Guid shaderGuid = first->assetId;
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(shaderGuid)));

	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(shaderGuid, inputs, outputs));
	EXPECT_TRUE(Names(inputs, "project://Shaders/Params.hlsli"));

	Restart();
	str edited = "#define TINT float4(0, 1, 0, 1)\n// and a second line, so the size moves too\n";
	WriteSource("project://Shaders/Params.hlsli", edited);
	u64 editedHash = StringHash(std::string_view(edited)).GetValue();

	Asset* second = AssetDatabase::LoadAsset(Path("project://Shaders/Lit.hlsl"));
	ASSERT_NE(nullptr, second);
	ShaderAsset* reloaded = static_cast<ShaderAsset*>(second);

	bool sawEditedInclude = false;
	for (const ShaderAsset::Dependency& dependency : reloaded->dependencies) {
		if (dependency.physicalPath.find("Params.hlsli") != str::npos) {
			sawEditedInclude = dependency.contentHash == editedHash;
		}
	}
	EXPECT_TRUE(sawEditedInclude);
}

TEST_F(ImportCacheInvalidationFixture, DeletingAGeneratedSiblingInvalidatesTheArtifact) {
	_importer.writes.push_back("project://Assets/generated/companion.txt");
	WriteSource("project://Assets/host.touched", "the host");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/host.touched"));
	ASSERT_NE(nullptr, first);
	EXPECT_EQ(1, _importer.imports);
	Guid guid = first->assetId;
	ASSERT_TRUE(std::filesystem::exists(Physical("project://Assets/generated/companion.txt")));

	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(guid, inputs, outputs));
	ASSERT_EQ(1u, outputs.size());
	EXPECT_EQ("project://Assets/generated/companion.txt", outputs[0]);

	Restart();
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/host.touched")));
	EXPECT_EQ(1, _importer.imports);

	Restart();
	std::error_code ec;
	std::filesystem::remove(Physical("project://Assets/generated/companion.txt"), ec);
	ASSERT_FALSE(ec);

	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/host.touched")));
	EXPECT_EQ(2, _importer.imports);
	EXPECT_TRUE(std::filesystem::exists(Physical("project://Assets/generated/companion.txt")));
}

TEST_F(ImportCacheInvalidationFixture, NewFileInvalidates) {
	_importer.reads.push_back("project://Assets/optional.txt");
	WriteSource("project://Assets/probe.touched", "base");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/probe.touched"));
	ASSERT_NE(nullptr, first);
	EXPECT_EQ("base|", static_cast<TextAsset*>(first)->content);
	Guid guid = first->assetId;

	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(guid, inputs, outputs));
	ASSERT_TRUE(Names(inputs, "project://Assets/optional.txt"));

	Restart();
	WriteSource("project://Assets/optional.txt", "now it exists");

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/probe.touched"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(2, _importer.imports);
	EXPECT_EQ("base|now it exists", static_cast<TextAsset*>(second)->content);
}

TEST_F(ImportCacheInvalidationFixture, TouchedInputStillHits) {
	WriteSource("project://Assets/table.txt", "unchanged content");
	_importer.reads.push_back("project://Assets/table.txt");
	WriteSource("project://Assets/reader.touched", "base");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/reader.touched"));
	ASSERT_NE(nullptr, first);
	Guid guid = first->assetId;
	Vector<RecordedInput> before{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(guid, before, outputs));
	ASSERT_EQ(1u, before.size());

	Restart();

	std::error_code ec;
	std::filesystem::last_write_time(Physical("project://Assets/table.txt"),
									 std::filesystem::file_time_type::clock::now() + std::chrono::hours(1), ec);
	ASSERT_FALSE(ec);
	u64 movedMtime = FileSystem::GetLastWriteTime(FileSystem::Resolve(Path("project://Assets/table.txt")));
	ASSERT_NE(before[0].mtime, movedMtime);

	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/reader.touched")));
	EXPECT_EQ(1, _importer.imports);

	Vector<RecordedInput> after{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(guid, after, outputs));
	ASSERT_EQ(1u, after.size());
	EXPECT_EQ(movedMtime, after[0].mtime);
	EXPECT_EQ(before[0].contentHash, after[0].contentHash);
}

TEST_F(ImportCacheInvalidationFixture, EditingAnInputMissesAndReimports) {
	WriteSource("project://Assets/table.txt", "before");
	_importer.reads.push_back("project://Assets/table.txt");
	WriteSource("project://Assets/reader.touched", "base");

	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/reader.touched")));
	EXPECT_EQ(1, _importer.imports);

	Restart();
	WriteSource("project://Assets/table.txt", "after the edit");

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/reader.touched"));
	ASSERT_NE(nullptr, second);
	EXPECT_EQ(2, _importer.imports);
	EXPECT_EQ("base|after the edit", static_cast<TextAsset*>(second)->content);
}

TEST_F(ImportCacheInvalidationFixture, LibraryFilesNotRecorded) {
	_importer.writes.push_back("project://Library/scratch/note.txt");
	_importer.reads.push_back("project://Library/scratch/note.txt");
	WriteSource("project://Assets/derived.touched", "base");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/derived.touched"));
	ASSERT_NE(nullptr, first);
	Guid guid = first->assetId;
	ASSERT_TRUE(std::filesystem::exists(Physical("project://Library/scratch/note.txt")));

	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(guid, inputs, outputs));
	EXPECT_TRUE(inputs.empty());
	EXPECT_TRUE(outputs.empty());

	Restart();
	std::error_code ec;
	std::filesystem::remove(Physical("project://Library/scratch/note.txt"), ec);
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/derived.touched")));
	EXPECT_EQ(1, _importer.imports);
}

TEST_F(ImportCacheInvalidationFixture, NestedImportRecordsInputs) {
	WriteSource("project://Assets/inner-table.txt", "the inner table");
	_inner.reads.push_back("project://Assets/inner-table.txt");
	WriteSource("project://Assets/inner.inner", "inner");
	_importer.loads.push_back("project://Assets/inner.inner");
	WriteSource("project://Assets/outer.touched", "outer");

	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/outer.touched")));
	EXPECT_EQ(1, _importer.imports);
	EXPECT_EQ(1, _inner.imports);

	Guid innerGuid = AssetDatabase::LookupGuid(Path("project://Assets/inner.inner"));
	ASSERT_TRUE(innerGuid.IsValid());
	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(innerGuid, inputs, outputs));
	ASSERT_EQ(1u, inputs.size());
	EXPECT_EQ("project://Assets/inner-table.txt", inputs[0].path);

	Restart();
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/inner.inner")));
	EXPECT_EQ(1, _inner.imports);

	Restart();
	WriteSource("project://Assets/inner-table.txt", "the inner table, edited");
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/inner.inner")));
	EXPECT_EQ(2, _inner.imports);
}

TEST_F(ImportCacheInvalidationFixture, EnclosingImportInheritsFilesNestedImportRead) {
	WriteSource("project://Assets/inner-table.txt", "the inner table");
	_inner.reads.push_back("project://Assets/inner-table.txt");
	WriteSource("project://Assets/inner.inner", "inner");
	_importer.loads.push_back("project://Assets/inner.inner");
	WriteSource("project://Assets/outer.touched", "outer");

	Asset* outer = AssetDatabase::LoadAsset(Path("project://Assets/outer.touched"));
	ASSERT_NE(nullptr, outer);
	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(outer->assetId, inputs, outputs));
	EXPECT_TRUE(Names(inputs, "project://Assets/inner-table.txt"));

	Restart();
	WriteSource("project://Assets/inner-table.txt", "the inner table, edited");
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/outer.touched")));
	EXPECT_EQ(2, _importer.imports);
}

TEST_F(ImportCacheInvalidationFixture, SourceSettledByKey) {
	_importer.reads.push_back("project://Assets/table.txt");
	WriteSource("project://Assets/table.txt", "a table");
	WriteSource("project://Assets/self.touched", "base");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/self.touched"));
	ASSERT_NE(nullptr, asset);

	Vector<RecordedInput> inputs{GetMemoryResource(MemTag::Asset)};
	Vector<str> outputs{GetMemoryResource(MemTag::Asset)};
	ASSERT_TRUE(ReadArtifact(asset->assetId, inputs, outputs));
	ASSERT_EQ(1u, inputs.size());
	EXPECT_EQ("project://Assets/table.txt", inputs[0].path);
	EXPECT_FALSE(Names(inputs, "project://Assets/self.touched"));
}

TEST(ImportCachePolicyTest, AThresholdOfZeroWritesEveryArtifact) {
	EXPECT_TRUE(ImportArtifactIsWorthWriting(0, 0));
	EXPECT_TRUE(ImportArtifactIsWorthWriting(1, 0));
	EXPECT_TRUE(ImportArtifactIsWorthWriting(UINT64_MAX, 0));
}

TEST(ImportCachePolicyTest, AnInfiniteThresholdWritesNone) {
	EXPECT_FALSE(ImportArtifactIsWorthWriting(0, UINT64_MAX));
	EXPECT_FALSE(ImportArtifactIsWorthWriting(1000 * 1000, UINT64_MAX));
}

TEST(ImportCachePolicyTest, ShippingThresholdSeparates2MeasuredClasses) {
	constexpr u64 MAT_US = 52;
	constexpr u64 JPG_US = 8520;
	constexpr u64 PNG_US = 11280;
	constexpr u64 GLTF_US = 18900;
	constexpr u64 SLMESH_US = 49100;

	EXPECT_FALSE(ImportArtifactIsWorthWriting(MAT_US, IMPORT_ARTIFACT_COST_THRESHOLD_US));
	EXPECT_TRUE(ImportArtifactIsWorthWriting(JPG_US, IMPORT_ARTIFACT_COST_THRESHOLD_US));
	EXPECT_TRUE(ImportArtifactIsWorthWriting(PNG_US, IMPORT_ARTIFACT_COST_THRESHOLD_US));
	EXPECT_TRUE(ImportArtifactIsWorthWriting(GLTF_US, IMPORT_ARTIFACT_COST_THRESHOLD_US));
	EXPECT_TRUE(ImportArtifactIsWorthWriting(SLMESH_US, IMPORT_ARTIFACT_COST_THRESHOLD_US));
}

namespace {

	class CheapImporter : public IAssetImporter {
	public:
		int imports = 0;

		StringHash GetAssetType() const override { return StringHash("CheapAsset"); }
		Asset* Import(const Path& sourcePath) override {
			++imports;
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->content = sourcePath.ToString();
			return asset;
		}
	};

	class ImportCachePolicyFixture : public ImportCacheStoreFixture {
	protected:
		CheapImporter _cheap;

		void SetUp() override {
			ImportCacheStoreFixture::SetUp();
			AssetImporterRegistry::Register(StringHash("CheapAsset"), &_cheap, {".cheap"});
		}

		void TearDown() override {
			AssetImporterRegistry::Unregister(&_cheap);
			ImportCacheStoreFixture::TearDown();
		}
	};

} // namespace

TEST_F(ImportCachePolicyFixture, ImportCheaperThanArtifactIsNotCached) {
	ScopedImportCostThreshold threshold(ScopedImportCostThreshold::CacheNothing);
	WriteSource("project://Assets/trivial.cheap", "two words");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/trivial.cheap"));
	ASSERT_NE(nullptr, asset);
	EXPECT_EQ(1, _cheap.imports);

	EXPECT_FALSE(std::filesystem::exists(ArtifactFor(asset->assetId)));
}

TEST_F(ImportCachePolicyFixture, AnImportWorthItsArtifactIsCached) {
	ScopedImportCostThreshold threshold(ScopedImportCostThreshold::CacheEverything);
	WriteSource("project://Assets/trivial.cheap", "two words");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/trivial.cheap"));
	ASSERT_NE(nullptr, asset);
	EXPECT_EQ(1, _cheap.imports);

	EXPECT_TRUE(std::filesystem::exists(ArtifactFor(asset->assetId)));
}

namespace {

	ImportArtifactEntry MakeEntry(u64 low, u64 bytes, i64 lastUse) {
		ImportArtifactEntry entry;
		entry.guid = Guid(low, 1);
		entry.path = Path(std::format("Library/Artifacts/{}.slart", low));
		entry.bytes = bytes;
		entry.lastUse = lastUse;
		return entry;
	}

	Vector<ImportArtifactEntry> MakeEntries() {
		return Vector<ImportArtifactEntry>{GetMemoryResource(MemTag::Asset)};
	}

} // namespace

TEST(ImportCacheLifecycleTest, PlannerEvictsNothingWhenSetAlreadyFits) {
	Vector<ImportArtifactEntry> entries = MakeEntries();
	entries.push_back(MakeEntry(1, 100, 10));
	entries.push_back(MakeEntry(2, 100, 20));
	EXPECT_EQ(0u, ImportArtifactPlanEviction(entries, 200));
	EXPECT_EQ(0u, ImportArtifactPlanEviction(entries, IMPORT_ARTIFACT_BUDGET_BYTES));
}

TEST(ImportCacheLifecycleTest, PlannerEvictsLeastRecent) {
	Vector<ImportArtifactEntry> entries = MakeEntries();
	entries.push_back(MakeEntry(1, 100, 300));
	entries.push_back(MakeEntry(2, 100, 100));
	entries.push_back(MakeEntry(3, 100, 200));

	u32 evict = ImportArtifactPlanEviction(entries, 150);
	ASSERT_EQ(2u, evict);
	EXPECT_EQ(2u, entries[0].guid.low);
	EXPECT_EQ(3u, entries[1].guid.low);
	EXPECT_EQ(1u, entries[2].guid.low);
}

TEST(ImportCacheLifecycleTest, ThePlannerBreaksATieDeterministically) {
	Vector<ImportArtifactEntry> first = MakeEntries();
	Vector<ImportArtifactEntry> second = MakeEntries();
	for (u64 i = 0; i < 16; ++i) {
		first.push_back(MakeEntry(i, 100, 500));
		second.push_back(MakeEntry(15 - i, 100, 500));
	}

	ASSERT_EQ(8u, ImportArtifactPlanEviction(first, 800));
	ASSERT_EQ(8u, ImportArtifactPlanEviction(second, 800));
	for (size_t i = 0; i < first.size(); ++i) {
		EXPECT_EQ(first[i].guid.low, second[i].guid.low);
	}
}

namespace {

	class ImportCacheLifecycleFixture : public ImportCacheStoreFixture {
	protected:
		void ScanAndSweep() { AssetDatabase::ScanProjectDirectory(); }

		std::filesystem::path ArtifactRoot() const { return std::filesystem::path(ImportArtifactDirectory().ToString()); }

		Vector<ImportArtifactEntry> Artifacts() const {
			Vector<ImportArtifactEntry> entries{GetMemoryResource(MemTag::Asset)};
			ImportArtifactEnumerate(entries);
			return entries;
		}

		void Backdate(const Guid& guid, std::chrono::seconds by) const {
			std::error_code ec;
			std::filesystem::path file = ArtifactFor(guid);
			std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() - by, ec);
			EXPECT_FALSE(ec) << file.generic_string();
		}

		i64 LastUse(const Guid& guid) const {
			std::error_code ec;
			std::filesystem::file_time_type at = std::filesystem::last_write_time(ArtifactFor(guid), ec);
			return ec ? 0 : static_cast<i64>(at.time_since_epoch().count());
		}
	};

} // namespace

TEST_F(ImportCacheLifecycleFixture, ArtifactNoIndexedAssetNamesIsCollected) {
	WriteSource("project://Assets/doomed.cached", "here for now");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/doomed.cached"));
	ASSERT_NE(nullptr, asset);
	Guid guid = asset->assetId;
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(guid)));

	std::error_code ec;
	std::filesystem::remove(Physical("project://Assets/doomed.cached"), ec);
	std::filesystem::remove(Physical("project://Assets/doomed.cached.meta"), ec);

	ScanAndSweep();
	EXPECT_FALSE(std::filesystem::exists(ArtifactFor(guid)));
}

TEST_F(ImportCacheLifecycleFixture, AnIndexedAssetsArtifactSurvivesTheSweep) {
	WriteSource("project://Assets/kept.cached", "still here");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/kept.cached"));
	ASSERT_NE(nullptr, asset);
	Guid guid = asset->assetId;

	ScanAndSweep();
	ASSERT_TRUE(std::filesystem::exists(ArtifactFor(guid)));

	Restart();
	Asset* again = AssetDatabase::LoadAsset(Path("project://Assets/kept.cached"));
	ASSERT_NE(nullptr, again);
	EXPECT_EQ(1, _importer.imports);
}

TEST_F(ImportCacheLifecycleFixture, SweepCollectsUnnamedArtifacts) {
	WriteSource("project://Assets/anchor.cached", "anchor");
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/anchor.cached")));

	std::error_code ec;
	std::filesystem::create_directories(ArtifactRoot() / "zz", ec);
	std::filesystem::path stray = ArtifactRoot() / "zz" / "not-a-guid.slart";
	std::filesystem::path foreign = ArtifactRoot() / "zz" / "README.txt";
	FileSystem::WriteTextFile(Path(stray.generic_string()), "garbage");
	FileSystem::WriteTextFile(Path(foreign.generic_string()), "not ours");

	ScanAndSweep();
	EXPECT_FALSE(std::filesystem::exists(stray));
	EXPECT_TRUE(std::filesystem::exists(foreign));
}

TEST_F(ImportCacheLifecycleFixture, TightBudgetStillLoads) {
	WriteSource("project://Assets/one.cached", "first asset");
	WriteSource("project://Assets/two.cached", "second asset");
	Asset* one = AssetDatabase::LoadAsset(Path("project://Assets/one.cached"));
	Asset* two = AssetDatabase::LoadAsset(Path("project://Assets/two.cached"));
	ASSERT_NE(nullptr, one);
	ASSERT_NE(nullptr, two);
	Guid oldest = one->assetId;
	Guid newest = two->assetId;
	Backdate(oldest, std::chrono::hours(9));
	Backdate(newest, std::chrono::hours(3));

	Vector<ImportArtifactEntry> before = Artifacts();
	ASSERT_EQ(2u, before.size());
	u64 largest = std::max(before[0].bytes, before[1].bytes);

	ImportArtifactSweepResult swept = ImportArtifactSweep(largest, [](const Guid&) { return true; });
	EXPECT_EQ(0u, swept.orphans);
	EXPECT_EQ(1u, swept.evicted);
	EXPECT_LE(swept.bytesAfter, largest);
	EXPECT_FALSE(std::filesystem::exists(ArtifactFor(oldest)));
	EXPECT_TRUE(std::filesystem::exists(ArtifactFor(newest)));

	Restart();
	Asset* reloaded = AssetDatabase::LoadAsset(Path("project://Assets/one.cached"));
	ASSERT_NE(nullptr, reloaded);
	EXPECT_EQ("first asset", static_cast<TextAsset*>(reloaded)->content);
	EXPECT_EQ(3, _importer.imports);
}

TEST_F(ImportCacheLifecycleFixture, BudgetEvictsLeastRecentlyUsed) {
	WriteSource("project://Assets/hot.cached", "loaded every session");
	WriteSource("project://Assets/cold.cached", "loaded once, long ago");
	Asset* hot = AssetDatabase::LoadAsset(Path("project://Assets/hot.cached"));
	Asset* cold = AssetDatabase::LoadAsset(Path("project://Assets/cold.cached"));
	ASSERT_NE(nullptr, hot);
	ASSERT_NE(nullptr, cold);
	Guid hotGuid = hot->assetId;
	Guid coldGuid = cold->assetId;

	Backdate(hotGuid, std::chrono::hours(8));
	Backdate(coldGuid, std::chrono::hours(7));

	Restart();
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/hot.cached")));
	EXPECT_EQ(2, _importer.imports);
	EXPECT_GT(LastUse(hotGuid), LastUse(coldGuid));

	Vector<ImportArtifactEntry> entries = Artifacts();
	ASSERT_EQ(2u, entries.size());
	ImportArtifactSweepResult swept = ImportArtifactSweep(std::max(entries[0].bytes, entries[1].bytes), [](const Guid&) { return true; });
	EXPECT_EQ(1u, swept.evicted);
	EXPECT_TRUE(std::filesystem::exists(ArtifactFor(hotGuid)));
	EXPECT_FALSE(std::filesystem::exists(ArtifactFor(coldGuid)));
}

TEST_F(ImportCacheLifecycleFixture, BucketHitSkipsRewrite) {
	WriteSource("project://Assets/recent.cached", "used a moment ago");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/recent.cached"));
	ASSERT_NE(nullptr, asset);
	Guid guid = asset->assetId;

	Backdate(guid, std::chrono::seconds(static_cast<i64>(IMPORT_ARTIFACT_USE_BUCKET_SECONDS / 4)));
	i64 before = LastUse(guid);

	Restart();
	ASSERT_NE(nullptr, AssetDatabase::LoadAsset(Path("project://Assets/recent.cached")));
	EXPECT_EQ(1, _importer.imports);
	EXPECT_EQ(before, LastUse(guid));
}

namespace {

	std::vector<u8> ReadAllBytes(const std::filesystem::path& file) {
		std::ifstream stream(file, std::ios::binary);
		return std::vector<u8>((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	}

	void WriteAllBytes(const std::filesystem::path& file, const std::vector<u8>& bytes) {
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		if (!bytes.empty()) {
			stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
	}

	void CorruptBytes(std::vector<u8>& bytes, Fuzz::Rng& rng) {
		if (bytes.empty()) {
			return;
		}
		u32 size = static_cast<u32>(bytes.size());
		switch (rng.Below(6)) {
		case 0:
			bytes.resize(rng.Below(size));
			break;
		case 1:
			bytes[rng.Below(size)] ^= static_cast<u8>(1u << rng.Below(8));
			break;
		case 2:
			for (u8& byte : bytes) {
				byte = rng.Byte();
			}
			break;
		case 3:
			bytes.clear();
			break;
		case 4:
			for (size_t i = 0; i < 8 && i < bytes.size(); ++i) {
				bytes[i] = rng.Byte();
			}
			break;
		default: {
			u32 at = rng.Below(size);
			u32 count = rng.Below(size - at) + 1;
			std::fill(bytes.begin() + at, bytes.begin() + at + count, u8(0));
			break;
		}
		}
	}

} // namespace

TEST(ImportCacheFuzzTest, CorruptEnvelopeRefused) {
	TextAsset original;
	original.content = "a payload long enough that a truncation has somewhere to land";
	Cooked cooked;
	ASSERT_TRUE(CookInto(original, cooked));
	Vector<u8> good = WriteEnvelope(cooked, 0xFEEDFACE, original.GetAssetTypeId().GetValue());

	Fuzz::GuardedInput guard(good.size() + 64);
	ASSERT_TRUE(guard.IsValid());
	Fuzz::Rng rng(0x5EED1234);

	u32 accepted = 0;
	for (int iteration = 0; iteration < 512; ++iteration) {
		std::vector<u8> mutated(good.begin(), good.end());
		CorruptBytes(mutated, rng);

		const u8* placed = guard.Place(mutated.data(), mutated.size(), 8);
		ImportArtifactView view;
		if (!ImportArtifactRead(placed, mutated.size(), view)) {
			continue;
		}
		++accepted;
		CookReader descriptor = view.Descriptor();
		std::string_view text = descriptor.ReadString();
		(void)text.size();
		CookReader payload = view.Payload();
		u64 scratch = 0;
		while (payload.Read(scratch)) {
		}
		CookReader inputs = view.Inputs();
		for (u32 i = 0; i < view.header.inputCount; ++i) {
			u32 index = 0;
			inputs.Read(index);
			view.strings.Get(index);
		}
		CookReader outputs = view.Outputs();
		for (u32 i = 0; i < view.header.outputCount; ++i) {
			u32 index = 0;
			outputs.Read(index);
			view.strings.Get(index);
		}
	}
	EXPECT_GT(accepted, 0u);
	EXPECT_LT(accepted, 512u);
}

namespace {

	struct AssetSnapshot {
		str virtualPath;
		Guid guid;
		u64 typeId = 0;
		bool cooked = false;
		u64 descHash = 0;
		u64 payloadHash = 0;
		u64 importStateHash = 0;
		size_t descSize = 0;
		size_t payloadSize = 0;
		size_t importStateSize = 0;
	};

	AssetSnapshot SnapshotOf(const Asset& asset) {
		AssetSnapshot snapshot;
		snapshot.virtualPath = asset.virtualPath;
		snapshot.guid = asset.assetId;
		snapshot.typeId = asset.GetAssetTypeId().GetValue();

		PakStringPool strings;
		Vector<u8> desc{GetMemoryResource(MemTag::Asset)};
		Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
		Vector<u8> importState{GetMemoryResource(MemTag::Asset)};
		{
			CookWriter descriptor(desc, strings);
			CookWriter body(payload, strings);
			snapshot.cooked = asset.Cook(descriptor, body);
		}
		if (snapshot.cooked) {
			CookWriter state(importState, strings);
			snapshot.cooked = asset.CookImportState(state);
		}
		snapshot.descHash = PakContentHash(desc.data(), desc.size());
		snapshot.payloadHash = PakContentHash(payload.data(), payload.size());
		snapshot.importStateHash = PakContentHash(importState.data(), importState.size());
		snapshot.descSize = desc.size();
		snapshot.payloadSize = payload.size();
		snapshot.importStateSize = importState.size();
		return snapshot;
	}

	class ImportCacheFuzzFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mountedEngine = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_artifactfuzz_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::error_code ec;
			std::filesystem::create_directories(_root / "Assets", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = FindRepo("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Restart() {
			AssetDatabase::Terminate();
			AssetDatabase::Initialize();
		}

		static std::filesystem::path FindRepo(const char* relative) {
			std::error_code ec;
			std::filesystem::path probe = std::filesystem::current_path(ec);
			for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
				std::filesystem::path candidate = probe / relative;
				if (std::filesystem::exists(candidate, ec)) {
					return candidate;
				}
				std::filesystem::path parent = probe.parent_path();
				if (parent == probe) {
					break;
				}
				probe = parent;
			}
			return {};
		}

		bool StageContent() {
			std::filesystem::path fixtures = FindRepo("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			for (const char* name : {"hierarchy.gltf", "checker.png", "fixture_shader.hlsl"}) {
				std::filesystem::copy_file(fixtures / name, _root / "Assets" / name,
										   std::filesystem::copy_options::overwrite_existing, ec);
				if (ec) {
					return false;
				}
			}
			return true;
		}

		void WriteScene() {
			SceneSystem::Initialize();
			Handle<Scene> scene = SceneSystem::CreateScene("Corrupted");
			Handle<SceneObject> root = SceneSystem::CreateObject("Root", scene);
			root->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
			SceneSystem::CreateObject("Child", scene, root);
			EXPECT_TRUE(SceneSystem::SaveScene(scene, Path("project://Assets/Level.scene"), false));
			SceneSystem::Terminate();
		}

		Vector<AssetSnapshot> LoadEverything() {
			Vector<AssetSnapshot> out{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::LoadAsset(Path("project://Assets/hierarchy.gltf"));

			Vector<str> paths{GetMemoryResource(MemTag::Asset)};
			std::error_code ec;
			for (std::filesystem::recursive_directory_iterator it(_root / "Assets", ec), end; it != end; it.increment(ec)) {
				if (ec) {
					break;
				}
				std::error_code fileEc;
				if (!it->is_regular_file(fileEc) || fileEc) {
					continue;
				}
				std::filesystem::path relative = std::filesystem::relative(it->path(), _root, fileEc);
				if (fileEc) {
					continue;
				}
				str virtualPath = "project://" + relative.generic_string();
				if (!AssetDatabase::IsAssetPath(Path(virtualPath))) {
					continue;
				}
				paths.push_back(virtualPath);
			}
			std::sort(paths.begin(), paths.end());

			for (const str& path : paths) {
				Asset* asset = AssetDatabase::LoadAsset(Path(path));
				EXPECT_NE(nullptr, asset) << "'" << path;
				if (asset) {
					out.push_back(SnapshotOf(*asset));
				}
			}

			Path model("project://Assets/hierarchy.gltf");
			for (const char* key : {"Materials/MatA", "Materials/MatC", "Materials/MatUnused", "Meshes/Body", "Meshes/Visor"}) {
				Asset* product = ProductLookup::Load(model, key);
				EXPECT_NE(nullptr, product) << "'" << key;
				if (product) {
					AssetSnapshot snapshot = SnapshotOf(*product);
					snapshot.virtualPath = key;
					out.push_back(snapshot);
				}
			}
			return out;
		}

		void CorruptEveryArtifact(u64 seed, u32& corrupted) {
			Fuzz::Rng rng(seed);
			Vector<ImportArtifactEntry> artifacts{GetMemoryResource(MemTag::Asset)};
			ImportArtifactEnumerate(artifacts);
			for (const ImportArtifactEntry& artifact : artifacts) {
				std::filesystem::path file(artifact.path.ToString());
				std::vector<u8> bytes = ReadAllBytes(file);
				CorruptBytes(bytes, rng);
				WriteAllBytes(file, bytes);
				++corrupted;
			}
		}

		static void ExpectSame(const Vector<AssetSnapshot>& expected, const Vector<AssetSnapshot>& actual, u64 seed) {
			ASSERT_EQ(expected.size(), actual.size()) << "seed " << seed;
			for (size_t i = 0; i < expected.size(); ++i) {
				const AssetSnapshot& a = expected[i];
				const AssetSnapshot& b = actual[i];
				EXPECT_EQ(a.virtualPath, b.virtualPath) << "seed " << seed;
				EXPECT_EQ(a.guid, b.guid) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.typeId, b.typeId) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.cooked, b.cooked) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.descSize, b.descSize) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.payloadSize, b.payloadSize) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.importStateSize, b.importStateSize) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.descHash, b.descHash) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.payloadHash, b.payloadHash) << "seed " << seed << ": " << a.virtualPath;
				EXPECT_EQ(a.importStateHash, b.importStateHash) << "seed " << seed << ": " << a.virtualPath;
			}
		}
	};

} // namespace

TEST_F(ImportCacheFuzzFixture, ProjectLoadsIdenticallyWithEveryArtifactCorrupted) {
	ASSERT_TRUE(_mountedEngine);
	ASSERT_TRUE(StageContent());
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/notes.txt")), "a text asset with content");
	WriteScene();

	Vector<AssetSnapshot> baseline = LoadEverything();
	ASSERT_GE(baseline.size(), 8u);

	Vector<ImportArtifactEntry> filled{GetMemoryResource(MemTag::Asset)};
	ImportArtifactEnumerate(filled);
	ASSERT_GE(filled.size(), 2u);

	for (u64 seed : {0x1ull, 0xA5A5A5A5ull, 0xDEADBEEFull, 0x0F0F0F0Full, 0x9E3779B9ull, 0xFFFFFFFFull}) {
		Restart();
		u32 corrupted = 0;
		CorruptEveryArtifact(seed, corrupted);
		ASSERT_GT(corrupted, 0u) << "seed " << seed;

		Vector<AssetSnapshot> actual = LoadEverything();
		ExpectSame(baseline, actual, seed);
		if (::testing::Test::HasFailure()) {
			return;
		}
	}
}
