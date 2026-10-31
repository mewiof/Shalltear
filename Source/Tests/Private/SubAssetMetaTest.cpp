#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class KnobImporter : public IAssetImporter {
	public:
		str lastSettings;

		StringHash GetAssetType() const override { return StringHash("KnobAsset"); }
		Asset* Import(const Path& sourcePath) override {
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override {
			(void)virtualPath;
			lastSettings = JSONSerialize(settings, false);
			return Import(sourcePath);
		}
		void SerializeSettings(const Path& sourcePath, JSONValue& out) const override {
			out.AddMember("knob", JSONValue::MakeNumber(1.0));
			out.AddMember("bytes", JSONValue::MakeNumber(static_cast<f64>(FileSystem::ReadFile(sourcePath, MemTag::Core).size)));
		}
	};

	class SubAssetMetaFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		KnobImporter _importer;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_submeta_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");

			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			AssetImporterRegistry::Register(StringHash("KnobAsset"), &_importer, {".knob"});
		}

		void TearDown() override {
			AssetImporterRegistry::Unregister(&_importer);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteSource(const str& virtualPath, const str& content) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::filesystem::path fsPath(resolved.ToString());
			std::error_code ec;
			std::filesystem::create_directories(fsPath.parent_path(), ec);
			FileSystem::WriteTextFile(resolved, content);
		}

		str ReadMeta(const str& virtualPath) {
			return FileSystem::ReadTextFile(FileSystem::Resolve(Path(virtualPath + ".meta")), MemTag::Core);
		}

		void WriteMeta(const str& virtualPath, const str& text) {
			FileSystem::WriteTextFile(FileSystem::Resolve(Path(virtualPath + ".meta")), text);
		}

		void InjectUnknowns(const str& virtualPath) {
			str text = ReadMeta(virtualPath);
			LinearAllocator arena(text.size() + 64, MemTag::Core);
			JSONValue root = JSONParse(text, arena);
			ASSERT_TRUE(root.IsObject());

			JSONValue products = JSONValue::MakeArray();
			JSONValue entry = JSONValue::MakeObject();
			entry.AddMember("key", JSONValue::MakeString("Meshes/Body"));
			entry.AddMember("guid", JSONValue::MakeString("deadbeef"));
			products.Push(std::move(entry));

			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("clipRanges", JSONValue::MakeString("Walk:0-30"));

			JSONValue importer = JSONValue::MakeObject();
			importer.AddMember("type", JSONValue::MakeString(root["importer"]["type"].AsString()));
			importer.AddMember("settings", std::move(settings));

			JSONValue rebuilt = JSONValue::MakeObject();
			rebuilt.AddMember("assetId", JSONValue::MakeString(root["assetId"].AsString()));
			rebuilt.AddMember("type", JSONValue::MakeString(root["type"].AsString()));
			rebuilt.AddMember("importer", std::move(importer));
			rebuilt.AddMember("products", std::move(products));

			WriteMeta(virtualPath, JSONSerialize(rebuilt, true));
		}

		void ExpectUnknownsSurvive(const str& virtualPath, const char* what) {
			str text = ReadMeta(virtualPath);
			LinearAllocator arena(text.size() + 64, MemTag::Core);
			JSONValue root = JSONParse(text, arena);
			ASSERT_TRUE(root.IsObject()) << what;

			const JSONValue& products = root["products"];
			EXPECT_TRUE(products.IsArray()) << what;
			if (products.IsArray() && products.Size() == 1) {
				EXPECT_EQ(products[static_cast<size_t>(0)]["key"].AsString(), "Meshes/Body") << what;
				EXPECT_EQ(products[static_cast<size_t>(0)]["guid"].AsString(), "deadbeef") << what;
			} else {
				ADD_FAILURE() << what << ": product table lost its entries";
			}

			EXPECT_EQ(root["importer"]["settings"]["clipRanges"].AsString(), "Walk:0-30") << what;

			EXPECT_TRUE(Guid::FromString(root["assetId"].AsString()).IsValid()) << what;
		}
	};

} // namespace

TEST_F(SubAssetMetaFixture, AnUnknownMemberSurvivesAReimport) {
	WriteSource("project://Assets/rig.knob", "content");
	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/rig.knob"));
	ASSERT_NE(first, nullptr);
	AssetDatabase::UnloadAsset(first);
	AssetDatabase::CollectGarbage();

	InjectUnknowns("project://Assets/rig.knob");

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/rig.knob"));
	ASSERT_NE(second, nullptr);
	AssetDatabase::UnloadAsset(second);
	AssetDatabase::CollectGarbage();

	ExpectUnknownsSurvive("project://Assets/rig.knob", "re-import");
}

TEST_F(SubAssetMetaFixture, AnUnknownMemberSurvivesAScan) {
	WriteSource("project://Assets/rig.knob", "content");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/rig.knob"));
	ASSERT_NE(asset, nullptr);
	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();

	InjectUnknowns("project://Assets/rig.knob");
	AssetDatabase::ScanProjectDirectory();

	ExpectUnknownsSurvive("project://Assets/rig.knob", "scan");
}

TEST_F(SubAssetMetaFixture, AnUnknownMemberSurvivesARebuild) {
	WriteSource("project://Assets/rig.knob", "content");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/rig.knob"));
	ASSERT_NE(asset, nullptr);
	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();

	InjectUnknowns("project://Assets/rig.knob");
	AssetDatabase::RebuildDatabase();

	ExpectUnknownsSurvive("project://Assets/rig.knob", "rebuild");
}

TEST_F(SubAssetMetaFixture, AnUnknownMemberSurvivesASaveAsset) {
	WriteSource("project://Assets/rig.knob", "content");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/rig.knob"));
	ASSERT_NE(asset, nullptr);

	InjectUnknowns("project://Assets/rig.knob");

	AssetDatabase::SaveAsset(asset);

	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();

	ExpectUnknownsSurvive("project://Assets/rig.knob", "SaveAsset");
}

TEST_F(SubAssetMetaFixture, ScanAndLoadMintSame) {
	WriteSource("project://Assets/byload.knob", "content");
	WriteSource("project://Assets/byscan.knob", "content");

	AssetDatabase::ScanProjectDirectory();
	str scanned = ReadMeta("project://Assets/byscan.knob");

	std::error_code ec;
	std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(Path("project://Assets/byload.knob.meta")).ToString()), ec);
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/byload.knob"));
	ASSERT_NE(asset, nullptr);
	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();
	str loaded = ReadMeta("project://Assets/byload.knob");

	LinearAllocator arenaA(scanned.size() + 64, MemTag::Core);
	LinearAllocator arenaB(loaded.size() + 64, MemTag::Core);
	JSONValue a = JSONParse(scanned, arenaA);
	JSONValue b = JSONParse(loaded, arenaB);
	ASSERT_TRUE(a.IsObject());
	ASSERT_TRUE(b.IsObject());

	EXPECT_EQ(a.MemberCount(), b.MemberCount());
	for (size_t i = 0; i < a.MemberCount() && i < b.MemberCount(); ++i) {
		EXPECT_EQ(a.KeyAt(i), b.KeyAt(i)) << "member " << i;
	}
	EXPECT_EQ(a["type"].AsString(), b["type"].AsString());
	EXPECT_EQ(a["importer"]["type"].AsString(), b["importer"]["type"].AsString());
	EXPECT_EQ(JSONSerialize(a["importer"]["settings"], false), JSONSerialize(b["importer"]["settings"], false));

	EXPECT_EQ(a["importer"]["settings"]["knob"].AsNumber(0.0), 1.0);
	EXPECT_EQ(b["importer"]["settings"]["knob"].AsNumber(0.0), 1.0);
}

TEST_F(SubAssetMetaFixture, FreshSidecarsDefaultsAreChosenForFile) {
	WriteSource("project://Assets/short.knob", "abc");
	WriteSource("project://Assets/long.knob", str(1000, 'x'));
	AssetDatabase::ScanProjectDirectory();
	WriteSource("project://Assets/loaded.knob", str(77, 'y'));
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/loaded.knob"));
	ASSERT_NE(asset, nullptr);
	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();

	for (const auto& [file, bytes] : {std::pair<const char*, f64>{"project://Assets/short.knob", 3.0}, {"project://Assets/long.knob", 1000.0}, {"project://Assets/loaded.knob", 77.0}}) {
		const str text = ReadMeta(file);
		LinearAllocator arena(text.size() + 64, MemTag::Core);
		const JSONValue root = JSONParse(text, arena);
		ASSERT_TRUE(root.IsObject()) << file;
		EXPECT_EQ(root["importer"]["settings"]["bytes"].AsNumber(-1.0), bytes) << file;
	}
}

TEST_F(SubAssetMetaFixture, EarlySidecarTakesDefaults) {
	AssetImporterRegistry::Unregister(&_importer);
	WriteSource("project://Assets/early.knob", str(1000, 'x'));
	WriteSource("project://Assets/authored.knob", str(1000, 'x'));
	AssetDatabase::ScanProjectDirectory();

	const auto parse = [](const str& text, LinearAllocator& arena) { return JSONParse(text, arena); };
	const str unclaimed = ReadMeta("project://Assets/early.knob");
	LinearAllocator unclaimedArena(unclaimed.size() + 64, MemTag::Core);
	const JSONValue before = parse(unclaimed, unclaimedArena);
	ASSERT_TRUE(before.IsObject());
	ASSERT_EQ(before["type"].AsString(), "0000000000000000");
	ASSERT_EQ(before["importer"]["settings"].MemberCount(), 0u);
	const Guid guid = Guid::FromString(before["assetId"].AsString());

	const str authoredMint = ReadMeta("project://Assets/authored.knob");
	LinearAllocator authoredMintArena(authoredMint.size() + 64, MemTag::Core);
	const str authoredId = str(parse(authoredMint, authoredMintArena)["assetId"].AsString());
	WriteMeta("project://Assets/authored.knob", "{\"assetId\": \"" + authoredId +
													"\", \"type\": \"0000000000000000\", \"importer\": {\"type\": \"0000000000000000\", "
													"\"settings\": {\"knob\": 7, \"note\": \"mine\"}}}");

	AssetImporterRegistry::Register(StringHash("KnobAsset"), &_importer, {".knob"});
	WriteSource("project://Assets/control.knob", str(1000, 'x'));
	for (const char* file : {"project://Assets/control.knob", "project://Assets/early.knob"}) {
		Asset* asset = AssetDatabase::LoadAsset(Path(file));
		ASSERT_NE(asset, nullptr) << file;
		AssetDatabase::UnloadAsset(asset);
	}
	const str handed = _importer.lastSettings;
	AssetDatabase::CollectGarbage();

	const str controlText = ReadMeta("project://Assets/control.knob");
	const str completedText = ReadMeta("project://Assets/early.knob");
	LinearAllocator controlArena(controlText.size() + 64, MemTag::Core);
	LinearAllocator completedArena(completedText.size() + 64, MemTag::Core);
	const JSONValue control = parse(controlText, controlArena);
	const JSONValue completed = parse(completedText, completedArena);
	ASSERT_TRUE(control.IsObject() && completed.IsObject());
	EXPECT_EQ(Guid::FromString(completed["assetId"].AsString()), guid);
	EXPECT_EQ(completed["type"].AsString(), control["type"].AsString());
	EXPECT_EQ(completed["importer"]["type"].AsString(), control["importer"]["type"].AsString());
	EXPECT_EQ(JSONSerialize(completed["importer"]["settings"], false), JSONSerialize(control["importer"]["settings"], false));
	EXPECT_EQ(handed, JSONSerialize(control["importer"]["settings"], false));

	Asset* again = AssetDatabase::LoadAsset(Path("project://Assets/early.knob"));
	ASSERT_NE(again, nullptr);
	AssetDatabase::UnloadAsset(again);
	AssetDatabase::CollectGarbage();
	EXPECT_EQ(ReadMeta("project://Assets/early.knob"), completedText);

	Asset* authored = AssetDatabase::LoadAsset(Path("project://Assets/authored.knob"));
	ASSERT_NE(authored, nullptr);
	AssetDatabase::UnloadAsset(authored);
	AssetDatabase::CollectGarbage();
	const str authoredText = ReadMeta("project://Assets/authored.knob");
	LinearAllocator authoredArena(authoredText.size() + 64, MemTag::Core);
	const JSONValue kept = parse(authoredText, authoredArena)["importer"]["settings"];
	EXPECT_EQ(kept["knob"].AsNumber(0.0), 7.0);
	EXPECT_EQ(kept["note"].AsString(), "mine");
	EXPECT_EQ(kept["bytes"].AsNumber(-1.0), 1000.0);
}
