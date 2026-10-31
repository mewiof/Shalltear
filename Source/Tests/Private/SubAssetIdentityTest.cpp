#include "TestsPCH.h"

#include "Asset/ImportArtifact.h"

#include "ScopedImportCostThreshold.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class RefusingAsset : public TextAsset {
	public:
		bool CookImportState(CookWriter& importState) const override {
			(void)importState;
			return false;
		}
	};

	class ProbeImporter : public IAssetImporter {
	public:
		enum class Keying {
			ByName,
			ByIndex
		};

		Keying keying = Keying::ByName;

		bool nameProducts = false;

		bool failAfterProducts = false;

		bool refuseMainRecord = false;

		bool refuseProductRecord = false;

		u32 version = 1;

		std::vector<Guid> lastProductGuids;

		Guid askBeforeProducts;
		Guid askAfterProducts;
		Asset* beforeAnswer = nullptr;
		Asset* afterAnswer = nullptr;

		u32 importCount = 0;

		StringHash GetAssetType() const override { return StringHash("ProbeAsset"); }
		u32 GetImporterVersion() const override { return version; }

		Asset* Import(const Path& sourcePath) override {
			TextAsset* asset = refuseMainRecord ? New<RefusingAsset>(MemTag::Asset) : New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}

		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
								  AssetImportContext& context) override {
			(void)virtualPath;
			(void)settings;
			++importCount;

			if (askBeforeProducts.IsValid() && importCount == 1) {
				beforeAnswer = AssetDatabase::LoadAssetByGuid(askBeforeProducts);
			}

			str text = FileSystem::ReadTextFile(sourcePath, MemTag::Core);
			std::vector<str> names;
			std::vector<str> payloads;
			size_t cursor = 0;
			while (cursor < text.size()) {
				size_t lineEnd = text.find('\n', cursor);
				std::string_view line(text.data() + cursor, (lineEnd == str::npos ? text.size() : lineEnd) - cursor);
				cursor = (lineEnd == str::npos) ? text.size() : lineEnd + 1;
				size_t colon = line.find(':');
				if (colon == std::string_view::npos) {
					continue;
				}
				names.emplace_back(line.substr(0, colon));
				payloads.emplace_back(line.substr(colon + 1));
			}

			lastProductGuids.clear();
			for (size_t i = 0; i < names.size(); ++i) {
				TextAsset* product = (refuseProductRecord && i == 0) ? New<RefusingAsset>(MemTag::Asset) : New<TextAsset>(MemTag::Asset);
				product->content = payloads[i];
				lastProductGuids.push_back(context.AddProduct(KeyFor(names, i), product, nameProducts ? std::string_view(names[i]) : std::string_view()));
			}

			if (askAfterProducts.IsValid()) {
				afterAnswer = AssetDatabase::LoadAssetByGuid(askAfterProducts);
			}

			if (failAfterProducts) {
				return nullptr;
			}
			return Import(sourcePath);
		}
	private:
		str KeyFor(const std::vector<str>& names, size_t index) const {
			if (keying == Keying::ByIndex) {
				return std::format("Meshes/{}", index);
			}
			u32 ordinal = 0;
			for (size_t i = 0; i < index; ++i) {
				if (names[i] == names[index]) {
					++ordinal;
				}
			}
			if (ordinal == 0) {
				return std::format("Meshes/{}", names[index]);
			}
			return std::format("Meshes/{}#{}", names[index], ordinal + 1);
		}
	};

	class CapturedLog {
	public:
		CapturedLog() {
			JobSystem::Initialize();
			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);
			_sink = std::make_shared<Sink>();
			LogManager::AddSink(_sink);
		}

		~CapturedLog() {
			LogManager::Terminate();
			LogManager::RemoveAllSinks();
			JobSystem::Terminate();
		}

		CapturedLog(const CapturedLog&) = delete;
		CapturedLog& operator=(const CapturedLog&) = delete;

		u32 CountMatching(std::string_view needle) const { return _sink->CountMatching(needle); }

		void Barrier(const char* tag) {
			SLT_LOG_INFO("SubAssetTest", "subasset-log-barrier-{}", tag);
			for (int spin = 0; spin < 2000 && _sink->CountMatching(tag) == 0; ++spin) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			EXPECT_GT(_sink->CountMatching(tag), 0u) << tag << "'";
		}

		void BarrierAndClear(const char* tag) {
			Barrier(tag);
			_sink->Clear();
		}
	private:
		class Sink : public ILogSink {
		public:
			void ReceiveLog(const LogEvent& event) override {
				std::lock_guard<std::mutex> lock(_mutex);
				_messages.push_back(str(event.message));
			}
			void Flush() override {}

			u32 CountMatching(std::string_view needle) const {
				std::lock_guard<std::mutex> lock(_mutex);
				u32 count = 0;
				for (const str& message : _messages) {
					if (message.find(needle) != str::npos) {
						++count;
					}
				}
				return count;
			}

			void Clear() {
				std::lock_guard<std::mutex> lock(_mutex);
				_messages.clear();
			}
		private:
			mutable std::mutex _mutex;
			std::vector<str> _messages;
		};

		std::shared_ptr<Sink> _sink;
	};

	class SubAssetIdentityFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		ProbeImporter _importer;

		ScopedImportCostThreshold _noCache{ScopedImportCostThreshold::CacheNothing};

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_subid_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");

			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			AssetImporterRegistry::Register(StringHash("ProbeAsset"), &_importer, {".probe"});
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

		void ImportAndDrop(const str& virtualPath) {
			Asset* asset = AssetDatabase::LoadAsset(Path(virtualPath));
			ASSERT_NE(asset, nullptr) << "importing " << virtualPath;
			AssetDatabase::UnloadAsset(asset);
			AssetDatabase::CollectGarbage();
		}

		Guid ProductGuid(const str& virtualPath, std::string_view key) {
			str text = ReadMeta(virtualPath);
			LinearAllocator arena(text.size() + 64, MemTag::Core);
			JSONValue root = JSONParse(text, arena);
			const JSONValue& products = root["products"];
			if (!products.IsArray()) {
				return Guid{};
			}
			for (size_t i = 0; i < products.Size(); ++i) {
				if (products[i]["key"].AsString() == key) {
					return Guid::FromString(products[i]["guid"].AsString());
				}
			}
			return Guid{};
		}

		std::vector<str> ProductKeys(const str& virtualPath) {
			std::vector<str> keys;
			str text = ReadMeta(virtualPath);
			LinearAllocator arena(text.size() + 64, MemTag::Core);
			JSONValue root = JSONParse(text, arena);
			const JSONValue& products = root["products"];
			if (!products.IsArray()) {
				return keys;
			}
			for (size_t i = 0; i < products.Size(); ++i) {
				keys.emplace_back(products[i]["key"].AsString());
			}
			return keys;
		}

		str ContentOf(const Guid& guid) {
			Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
			if (!asset) {
				return "<unresolved>";
			}
			str content = static_cast<TextAsset*>(asset)->content;
			AssetDatabase::UnloadAsset(asset);
			return content;
		}
	};

} // namespace

TEST_F(SubAssetIdentityFixture, ReorderKeepsEachProductsGuidWithContent) {
	WriteSource("project://Assets/hierarchy.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/hierarchy.probe");

	Guid body = ProductGuid("project://Assets/hierarchy.probe", "Meshes/Body");
	Guid visor = ProductGuid("project://Assets/hierarchy.probe", "Meshes/Visor");
	ASSERT_TRUE(body.IsValid());
	ASSERT_TRUE(visor.IsValid());
	ASSERT_NE(body, visor);
	EXPECT_EQ(ContentOf(body), "6");
	EXPECT_EQ(ContentOf(visor), "3");

	WriteSource("project://Assets/hierarchy.probe", "Visor:3\nBody:6\n");
	ImportAndDrop("project://Assets/hierarchy.probe");

	EXPECT_EQ(ProductGuid("project://Assets/hierarchy.probe", "Meshes/Body"), body);
	EXPECT_EQ(ProductGuid("project://Assets/hierarchy.probe", "Meshes/Visor"), visor);

	EXPECT_EQ(ContentOf(body), "6");
	EXPECT_EQ(ContentOf(visor), "3");
}

TEST_F(SubAssetIdentityFixture, IndexKeysMoveGuids) {
	_importer.keying = ProbeImporter::Keying::ByIndex;

	WriteSource("project://Assets/hierarchy.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/hierarchy.probe");

	Guid first = ProductGuid("project://Assets/hierarchy.probe", "Meshes/0");
	ASSERT_TRUE(first.IsValid());
	ASSERT_EQ(ContentOf(first), "6");

	WriteSource("project://Assets/hierarchy.probe", "Visor:3\nBody:6\n");
	ImportAndDrop("project://Assets/hierarchy.probe");

	EXPECT_EQ(ProductGuid("project://Assets/hierarchy.probe", "Meshes/0"), first);
	EXPECT_EQ(ContentOf(first), "3");
}

TEST_F(SubAssetIdentityFixture, RenameKeepsOldKey) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	WriteSource("project://Assets/rig.probe", "Torso:6\n");
	ImportAndDrop("project://Assets/rig.probe");

	std::vector<str> keys = ProductKeys("project://Assets/rig.probe");
	ASSERT_EQ(keys.size(), 2u);
	EXPECT_EQ(keys[0], "Meshes/Body");
	EXPECT_EQ(keys[1], "Meshes/Torso");

	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	Guid torso = ProductGuid("project://Assets/rig.probe", "Meshes/Torso");
	ASSERT_TRUE(torso.IsValid());
	EXPECT_NE(torso, body);

	EXPECT_EQ(ContentOf(torso), "6");
	EXPECT_EQ(ContentOf(body), "<unresolved>");
}

TEST_F(SubAssetIdentityFixture, RemovedProductReferenceNamed) {
	CapturedLog log;

	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	WriteSource("project://Assets/rig.probe", "Torso:6\n");
	ImportAndDrop("project://Assets/rig.probe");

	log.BarrierAndClear("before");
	EXPECT_EQ(AssetDatabase::LoadAssetByGuid(body), nullptr);
	log.Barrier("after");

	EXPECT_EQ(log.CountMatching("'Meshes/Body' is no longer in"), 1u);
	EXPECT_EQ(log.CountMatching("'project://Assets/rig.probe'"), 1u);
}

TEST_F(SubAssetIdentityFixture, DeletingAndRestoringProductBringsGuidBack) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	WriteSource("project://Assets/rig.probe", "Visor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	EXPECT_EQ(ContentOf(body), "<unresolved>");

	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");

	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	EXPECT_EQ(ContentOf(body), "6");
	EXPECT_EQ(ProductKeys("project://Assets/rig.probe").size(), 2u);
}

TEST_F(SubAssetIdentityFixture, ProductGuidSurvivesReimport) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	str first = ReadMeta("project://Assets/rig.probe");

	ImportAndDrop("project://Assets/rig.probe");
	ImportAndDrop("project://Assets/rig.probe");

	EXPECT_EQ(ReadMeta("project://Assets/rig.probe"), first);
}

TEST_F(SubAssetIdentityFixture, OrdinalSeparates2ProductsSharingName) {
	WriteSource("project://Assets/dup.probe", "Body:6\nBody:3\n");
	ImportAndDrop("project://Assets/dup.probe");

	std::vector<str> keys = ProductKeys("project://Assets/dup.probe");
	ASSERT_EQ(keys.size(), 2u);
	EXPECT_EQ(keys[0], "Meshes/Body");
	EXPECT_EQ(keys[1], "Meshes/Body#2");
	EXPECT_EQ(ContentOf(ProductGuid("project://Assets/dup.probe", "Meshes/Body")), "6");
	EXPECT_EQ(ContentOf(ProductGuid("project://Assets/dup.probe", "Meshes/Body#2")), "3");
}

TEST_F(SubAssetIdentityFixture, ImporterThatRepeatsKeyLosesSecondProduct) {
	class ColliderImporter : public ProbeImporter {
	public:
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
								  AssetImportContext& context) override {
			(void)virtualPath;
			(void)settings;
			++importCount;
			TextAsset* first = New<TextAsset>(MemTag::Asset);
			first->content = "first";
			context.AddProduct("Meshes/Same", first);
			TextAsset* second = New<TextAsset>(MemTag::Asset);
			second->content = "second";
			context.AddProduct("Meshes/Same", second);
			return Import(sourcePath);
		}
	};

	ColliderImporter collider;
	AssetImporterRegistry::Unregister(&_importer);
	AssetImporterRegistry::Register(StringHash("ProbeAsset"), &collider, {".probe"});

	WriteSource("project://Assets/collide.probe", "ignored:0\n");
	ImportAndDrop("project://Assets/collide.probe");

	std::vector<str> keys = ProductKeys("project://Assets/collide.probe");
	ASSERT_EQ(keys.size(), 1u);
	EXPECT_EQ(ContentOf(ProductGuid("project://Assets/collide.probe", "Meshes/Same")), "first");

	AssetImporterRegistry::Unregister(&collider);
	AssetImporterRegistry::Register(StringHash("ProbeAsset"), &_importer, {".probe"});
}

TEST_F(SubAssetIdentityFixture, ProductTableSurvivesUnrelatedSaveAsset) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	ASSERT_NE(source, nullptr);
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	AssetDatabase::SaveAsset(source);
	AssetDatabase::UnloadAsset(source);
	AssetDatabase::CollectGarbage();

	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
}

TEST_F(SubAssetIdentityFixture, SidecarRebuildsProductIndex) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	AssetDatabase::ScanProjectDirectory();
	EXPECT_EQ(ContentOf(body), "6");

	AssetDatabase::CollectGarbage();
	AssetDatabase::RebuildDatabase();
	EXPECT_EQ(ContentOf(body), "6");
}

TEST_F(SubAssetIdentityFixture, ProductHasNoPath) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	ASSERT_TRUE(body.IsValid());
	ASSERT_TRUE(source.IsValid());

	EXPECT_FALSE(AssetDatabase::LookupGuid(Path("project://Assets/rig.probe : Meshes/Body")).IsValid());
	EXPECT_TRUE(AssetDatabase::LookupPath(body).ToString().empty());

	Asset* product = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(product, nullptr);
	EXPECT_EQ(product->assetId, body);
	EXPECT_NE(product->assetId, source);
	AssetDatabase::UnloadAsset(product);
}

TEST_F(SubAssetIdentityFixture, ProductResolvesUnpinned) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	ASSERT_TRUE(body.IsValid());

	AssetDatabase::CollectGarbage();

	Asset* product = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(product, nullptr);
	EXPECT_EQ(static_cast<TextAsset*>(product)->content, "6");
	AssetDatabase::UnloadAsset(product);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 3u);

	EXPECT_FALSE(AssetDatabase::LookupPath(source).ToString().empty());
	EXPECT_EQ(ContentOf(body), "6");
}

TEST_F(SubAssetIdentityFixture, TwoSourcesMayShareAProductKey) {
	WriteSource("project://Assets/a.probe", "Body:6\n");
	WriteSource("project://Assets/b.probe", "Body:9\n");
	ImportAndDrop("project://Assets/a.probe");
	ImportAndDrop("project://Assets/b.probe");

	Guid fromA = ProductGuid("project://Assets/a.probe", "Meshes/Body");
	Guid fromB = ProductGuid("project://Assets/b.probe", "Meshes/Body");
	ASSERT_TRUE(fromA.IsValid());
	ASSERT_TRUE(fromB.IsValid());
	EXPECT_NE(fromA, fromB);
	EXPECT_EQ(ContentOf(fromA), "6");
	EXPECT_EQ(ContentOf(fromB), "9");
}

TEST_F(SubAssetIdentityFixture, FailedImportLeavesNoProducts) {
	_importer.failAfterProducts = true;
	WriteSource("project://Assets/broken.probe", "Body:6\nVisor:3\n");

	EXPECT_EQ(AssetDatabase::LoadAsset(Path("project://Assets/broken.probe")), nullptr);
	EXPECT_TRUE(ProductKeys("project://Assets/broken.probe").empty());

	ASSERT_EQ(_importer.lastProductGuids.size(), 2u);
	for (const Guid& orphan : _importer.lastProductGuids) {
		ASSERT_TRUE(orphan.IsValid());
		EXPECT_EQ(AssetDatabase::LoadAssetByGuid(orphan), nullptr);
	}
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);

	_importer.failAfterProducts = false;
	ImportAndDrop("project://Assets/broken.probe");
	EXPECT_EQ(ProductKeys("project://Assets/broken.probe").size(), 2u);
}

TEST_F(SubAssetIdentityFixture, ProductRequestSkipsReimport) {
	CapturedLog log;

	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid visor = ProductGuid("project://Assets/rig.probe", "Meshes/Visor");
	ASSERT_TRUE(visor.IsValid());

	AssetDatabase::CollectGarbage();
	_importer.importCount = 0;
	_importer.askBeforeProducts = visor;

	log.BarrierAndClear("before");
	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	ASSERT_NE(source, nullptr);
	log.Barrier("after");

	EXPECT_EQ(_importer.importCount, 1u);
	EXPECT_EQ(_importer.beforeAnswer, nullptr);
	EXPECT_EQ(log.CountMatching("'Meshes/Visor' was asked for while"), 1u);
	EXPECT_EQ(log.CountMatching("is already being imported"), 0u);

	AssetDatabase::UnloadAsset(source);
	AssetDatabase::CollectGarbage();

	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Visor"), visor);
	EXPECT_EQ(ProductKeys("project://Assets/rig.probe").size(), 2u);
}

TEST_F(SubAssetIdentityFixture, ImporterAskingForSourceIsRefused) {
	class SelfLoadingImporter : public ProbeImporter {
	public:
		Asset* selfAnswer = nullptr;
		u32 entries = 0;

		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
								  AssetImportContext& context) override {
			(void)settings;
			(void)context;
			++entries;
			if (entries == 1) {
				selfAnswer = AssetDatabase::LoadAsset(virtualPath);
			}
			return Import(sourcePath);
		}
	};

	SelfLoadingImporter selfish;
	AssetImporterRegistry::Unregister(&_importer);
	AssetImporterRegistry::Register(StringHash("ProbeAsset"), &selfish, {".probe"});

	WriteSource("project://Assets/selfish.probe", "Body:6\n");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/selfish.probe"));
	EXPECT_NE(asset, nullptr);
	EXPECT_EQ(selfish.entries, 1u);
	EXPECT_EQ(selfish.selfAnswer, nullptr);

	AssetDatabase::UnloadAsset(asset);
	AssetDatabase::CollectGarbage();
	AssetImporterRegistry::Unregister(&selfish);
	AssetImporterRegistry::Register(StringHash("ProbeAsset"), &_importer, {".probe"});
}

TEST_F(SubAssetIdentityFixture, ProductAlreadyAddedIsAnswerableMidImport) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	AssetDatabase::CollectGarbage();
	_importer.importCount = 0;
	_importer.askAfterProducts = body;

	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	ASSERT_NE(source, nullptr);
	ASSERT_NE(_importer.afterAnswer, nullptr);
	EXPECT_EQ(static_cast<TextAsset*>(_importer.afterAnswer)->content, "6");
	EXPECT_EQ(_importer.importCount, 1u);

	AssetDatabase::UnloadAsset(_importer.afterAnswer);
	AssetDatabase::UnloadAsset(source);
	AssetDatabase::CollectGarbage();
}

TEST_F(SubAssetIdentityFixture, DrainCollectsSiblingProducts) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\nHelm:9\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	AssetDatabase::CollectGarbage();

	Asset* product = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(product, nullptr);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 3u);
	EXPECT_EQ(static_cast<TextAsset*>(product)->content, "6");

	AssetDatabase::UnloadAsset(product);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
}

TEST_F(SubAssetIdentityFixture, DrainedProductRebuilt) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	ASSERT_NE(source, nullptr);
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	Guid visor = ProductGuid("project://Assets/rig.probe", "Meshes/Visor");
	Asset* held = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(held, nullptr);
	ASSERT_EQ(AssetDatabase::CollectGarbage(), 1u);

	_importer.importCount = 0;
	Asset* rebuilt = AssetDatabase::LoadAssetByGuid(visor);
	ASSERT_NE(rebuilt, nullptr);
	EXPECT_EQ(static_cast<TextAsset*>(rebuilt)->content, "3");
	EXPECT_EQ(_importer.importCount, 1u);

	Asset* bodyAgain = AssetDatabase::LoadAssetByGuid(body);
	EXPECT_EQ(bodyAgain, held);
	Asset* sourceAgain = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	EXPECT_EQ(sourceAgain, source);

	AssetDatabase::UnloadAsset(sourceAgain);
	AssetDatabase::UnloadAsset(bodyAgain);
	AssetDatabase::UnloadAsset(rebuilt);
	AssetDatabase::UnloadAsset(held);
	AssetDatabase::UnloadAsset(source);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 3u);
}

TEST_F(SubAssetIdentityFixture, SavingSourceMakesProducts) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	ASSERT_NE(source, nullptr);
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	Asset* held = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(held, nullptr);

	_importer.importCount = 0;
	static_cast<TextAsset*>(source)->content = "Body:6\nArm:4\n";
	AssetDatabase::SaveAsset(source);

	EXPECT_EQ(_importer.importCount, 1u);
	Guid arm = ProductGuid("project://Assets/rig.probe", "Meshes/Arm");
	ASSERT_TRUE(arm.IsValid());
	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	EXPECT_EQ(ContentOf(arm), "4");

	Asset* bodyAgain = AssetDatabase::LoadAssetByGuid(body);
	EXPECT_EQ(bodyAgain, held);
	Asset* sourceAgain = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	EXPECT_EQ(sourceAgain, source);

	AssetDatabase::UnloadAsset(sourceAgain);
	AssetDatabase::UnloadAsset(bodyAgain);
	AssetDatabase::UnloadAsset(held);
	AssetDatabase::UnloadAsset(source);
	AssetDatabase::CollectGarbage();
}

TEST_F(SubAssetIdentityFixture, ProductReadByName) {
	_importer.keying = ProbeImporter::Keying::ByIndex;
	_importer.nameProducts = true;
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/rig.probe"));
	ASSERT_NE(source, nullptr);
	const Guid guid = ProductGuid("project://Assets/rig.probe", "Meshes/0");
	ASSERT_TRUE(guid.IsValid());
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(AssetDatabase::LookupGuid(Path("project://Assets/rig.probe")), products);
	ASSERT_EQ(products.size(), 1u);
	EXPECT_EQ(products[0].name, "Body");
	Asset* held = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(held, nullptr);
	EXPECT_EQ(held->virtualPath, "project://Assets/rig.probe : Body");

	static_cast<TextAsset*>(source)->content = "Torso:6\n";
	AssetDatabase::SaveAsset(source);
	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/0"), guid);
	AssetDatabase::GetProducts(AssetDatabase::LookupGuid(Path("project://Assets/rig.probe")), products);
	ASSERT_EQ(products.size(), 1u);
	EXPECT_EQ(products[0].name, "Torso");
	EXPECT_EQ(held->virtualPath, "project://Assets/rig.probe : Torso");
	AssetDatabase::UnloadAsset(held);
	AssetDatabase::UnloadAsset(source);
	AssetDatabase::CollectGarbage();

	_importer.nameProducts = false;
	WriteSource("project://Assets/plain.probe", "Arm:4\n");
	ImportAndDrop("project://Assets/plain.probe");
	AssetDatabase::GetProducts(AssetDatabase::LookupGuid(Path("project://Assets/plain.probe")), products);
	ASSERT_EQ(products.size(), 1u);
	EXPECT_TRUE(products[0].name.empty());
	EXPECT_EQ(ReadMeta("project://Assets/plain.probe").find("\"name\""), str::npos);
}

TEST_F(SubAssetIdentityFixture, ProductlessSaveImportsNothing) {
	WriteSource("project://Assets/empty.probe", "no products here\n");
	Asset* source = AssetDatabase::LoadAsset(Path("project://Assets/empty.probe"));
	ASSERT_NE(source, nullptr);
	ASSERT_TRUE(ProductKeys("project://Assets/empty.probe").empty());

	_importer.importCount = 0;
	AssetDatabase::SaveAsset(source);
	EXPECT_EQ(_importer.importCount, 0u);

	AssetDatabase::UnloadAsset(source);
	AssetDatabase::CollectGarbage();
}

TEST_F(SubAssetIdentityFixture, SaveAssetRefusesAProduct) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	Asset* product = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(product, nullptr);
	AssetDatabase::SaveAsset(product);

	EXPECT_FALSE(AssetDatabase::LookupGuid(Path("project://Assets/rig.probe : Meshes/Body")).IsValid());
	std::error_code ec;
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "rig.probe : Meshes", ec));
	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	AssetDatabase::UnloadAsset(product);
	AssetDatabase::CollectGarbage();
}

namespace {

	class SubAssetArtifactFixture : public SubAssetIdentityFixture {
	protected:
		ScopedImportCostThreshold _cacheAll{ScopedImportCostThreshold::CacheEverything};

		Guid SourceGuid(const str& virtualPath) { return AssetDatabase::LookupGuid(Path(virtualPath)); }

		Path ArtifactPathFor(const str& virtualPath) { return ImportArtifactPath(SourceGuid(virtualPath)); }

		bool ArtifactExists(const str& virtualPath) { return FileSystem::Exists(ArtifactPathFor(virtualPath)); }

		bool ReadArtifact(const str& virtualPath, Buffer& bytes, ImportArtifactView& view) {
			Path path = ArtifactPathFor(virtualPath);
			if (!FileSystem::Exists(path)) {
				return false;
			}
			bytes = FileSystem::ReadFile(path, MemTag::Asset);
			return ImportArtifactRead(bytes.data, bytes.size, view);
		}
	};

} // namespace

TEST_F(SubAssetArtifactFixture, SourceWithProductsRoundTripsThroughCache) {
	WriteSource("project://Assets/plain.probe", "nothing-here\n");
	ImportAndDrop("project://Assets/plain.probe");
	_importer.importCount = 0;
	ImportAndDrop("project://Assets/plain.probe");
	ASSERT_EQ(_importer.importCount, 0u);

	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	Guid visor = ProductGuid("project://Assets/rig.probe", "Meshes/Visor");
	ASSERT_TRUE(body.IsValid());
	ASSERT_TRUE(visor.IsValid());

	_importer.importCount = 0;
	ImportAndDrop("project://Assets/rig.probe");
	EXPECT_EQ(_importer.importCount, 0u);

	EXPECT_EQ(ContentOf(body), "6");
	EXPECT_EQ(ContentOf(visor), "3");
	EXPECT_EQ(_importer.importCount, 0u);
}

TEST_F(SubAssetArtifactFixture, ArtifactHoldsMainRecordAndEveryProduct) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");

	Buffer bytes;
	ImportArtifactView view;
	ASSERT_TRUE(ReadArtifact("project://Assets/rig.probe", bytes, view));
	EXPECT_TRUE(view.HasMain());
	ASSERT_EQ(view.header.prodCount, 2u);

	ImportArtifactProductView product;
	ASSERT_TRUE(ImportArtifactFindProduct(view, "Meshes/Body", product));
	EXPECT_GT(product.descSize, 0u);
	EXPECT_TRUE(ImportArtifactFindProduct(view, "Meshes/Visor", product));
	EXPECT_FALSE(ImportArtifactFindProduct(view, "Meshes/Helm", product));
}

TEST_F(SubAssetArtifactFixture, ProductResolvesFromArtifactWithoutImportingSource) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	AssetDatabase::CollectGarbage();

	_importer.importCount = 0;
	Asset* product = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(product, nullptr);
	EXPECT_EQ(static_cast<TextAsset*>(product)->content, "6");
	EXPECT_EQ(_importer.importCount, 0u);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);

	AssetDatabase::UnloadAsset(product);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
}

TEST_F(SubAssetArtifactFixture, ProductArtifactAnswersIsDisplayedByName) {
	_importer.keying = ProbeImporter::Keying::ByIndex;
	_importer.nameProducts = true;
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/0");
	ASSERT_TRUE(body.IsValid());
	AssetDatabase::CollectGarbage();
	AssetDatabase::RebuildDatabase();

	_importer.importCount = 0;
	Asset* product = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(product, nullptr);
	EXPECT_EQ(_importer.importCount, 0u);
	EXPECT_EQ(product->virtualPath, "project://Assets/rig.probe : Body");
	AssetDatabase::UnloadAsset(product);
	AssetDatabase::CollectGarbage();
}

TEST_F(SubAssetArtifactFixture, ProductsCachedWithoutMain) {
	_importer.refuseMainRecord = true;
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	Buffer bytes;
	ImportArtifactView view;
	ASSERT_TRUE(ReadArtifact("project://Assets/rig.probe", bytes, view));
	EXPECT_FALSE(view.HasMain());
	EXPECT_EQ(view.header.prodCount, 2u);

	AssetDatabase::CollectGarbage();
	_importer.importCount = 0;
	EXPECT_EQ(ContentOf(body), "6");
	EXPECT_EQ(_importer.importCount, 0u);

	ImportAndDrop("project://Assets/rig.probe");
	EXPECT_EQ(_importer.importCount, 1u);
}

TEST_F(SubAssetArtifactFixture, VersionBumpInvalidatesProductsWithRecord) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	AssetDatabase::CollectGarbage();

	_importer.version = 2;
	_importer.importCount = 0;
	EXPECT_EQ(ContentOf(body), "6");
	EXPECT_EQ(_importer.importCount, 1u);
}

TEST_F(SubAssetArtifactFixture, UncookableProductNoArtifact) {
	_importer.refuseProductRecord = true;
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	EXPECT_FALSE(ArtifactExists("project://Assets/rig.probe"));

	_importer.importCount = 0;
	ImportAndDrop("project://Assets/rig.probe");
	EXPECT_EQ(_importer.importCount, 1u);
}

TEST_F(SubAssetArtifactFixture, RemovedProductFromArtifact) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid visor = ProductGuid("project://Assets/rig.probe", "Meshes/Visor");
	ASSERT_TRUE(visor.IsValid());

	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	ASSERT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Visor"), visor);
	AssetDatabase::CollectGarbage();

	_importer.importCount = 0;
	EXPECT_EQ(ContentOf(visor), "<unresolved>");
	EXPECT_EQ(_importer.importCount, 0u);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);
}

TEST_F(SubAssetArtifactFixture, WarmFillSkipsRewrite) {
	_importer.refuseMainRecord = true;
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Path artifact = ArtifactPathFor("project://Assets/rig.probe");
	ASSERT_TRUE(FileSystem::Exists(artifact));
	u64 written = FileSystem::GetLastWriteTime(artifact);

	std::this_thread::sleep_for(std::chrono::milliseconds(60));

	_importer.importCount = 0;
	ImportAndDrop("project://Assets/rig.probe");
	ASSERT_EQ(_importer.importCount, 1u);
	EXPECT_EQ(FileSystem::GetLastWriteTime(artifact), written);
}

TEST_F(SubAssetIdentityFixture, ExtractedProductFromFile) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	EXPECT_EQ(ContentOf(body), "6");
	AssetDatabase::CollectGarbage();

	WriteSource("project://Assets/extracted.txt", "edited by hand");
	WriteSource("project://Assets/extracted.txt.meta",
				std::format("{{\n\t\"assetId\": \"{}\",\n\t\"type\": \"0000000000000000\"\n}}\n", body.ToString()));
	AssetDatabase::ScanProjectDirectory();
	ASSERT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/extracted.txt")), body);

	u32 before = _importer.importCount;
	Asset* extracted = AssetDatabase::LoadAssetByGuid(body);
	ASSERT_NE(extracted, nullptr);
	EXPECT_EQ(static_cast<TextAsset*>(extracted)->content, "edited by hand");
	EXPECT_EQ(_importer.importCount, before);
	AssetDatabase::UnloadAsset(extracted);
	AssetDatabase::CollectGarbage();

	ImportAndDrop("project://Assets/rig.probe");
	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/extracted.txt")), MemTag::Core), "edited by hand");

	std::error_code ec;
	std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(Path("project://Assets/extracted.txt")).ToString()), ec);
	std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(Path("project://Assets/extracted.txt.meta")).ToString()), ec);
	AssetDatabase::RebuildDatabase();
	ImportAndDrop("project://Assets/rig.probe");

	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	EXPECT_EQ(ContentOf(body), "6");
}

TEST_F(SubAssetArtifactFixture, ExtractedProductIsNotInSourcesArtifact) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	u64 keyBefore = 0;
	{
		Buffer bytes;
		ImportArtifactView view;
		ASSERT_TRUE(ReadArtifact("project://Assets/rig.probe", bytes, view));
		ASSERT_EQ(view.header.prodCount, 2u);
		keyBefore = view.header.key;
	}

	WriteSource("project://Assets/extracted.txt", "edited by hand");
	WriteSource("project://Assets/extracted.txt.meta",
				std::format("{{\n\t\"assetId\": \"{}\",\n\t\"type\": \"0000000000000000\"\n}}\n", body.ToString()));
	AssetDatabase::ScanProjectDirectory();

	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\nSpare:1\n");
	ImportAndDrop("project://Assets/rig.probe");

	Buffer bytes;
	ImportArtifactView view;
	ASSERT_TRUE(ReadArtifact("project://Assets/rig.probe", bytes, view));
	ASSERT_NE(view.header.key, keyBefore);
	EXPECT_EQ(view.header.prodCount, 2u);
}

TEST_F(SubAssetIdentityFixture, GetProductsAnswersSidecarsTableInOrder) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");

	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	ASSERT_TRUE(source.IsValid());
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(source, products);

	ASSERT_EQ(products.size(), 2u);
	EXPECT_EQ(products[0].key, "Meshes/Body");
	EXPECT_EQ(products[1].key, "Meshes/Visor");
	EXPECT_EQ(products[0].guid, ProductGuid("project://Assets/rig.probe", "Meshes/Body"));
	EXPECT_EQ(products[1].guid, ProductGuid("project://Assets/rig.probe", "Meshes/Visor"));

	Asset* body = AssetDatabase::LoadAssetByGuid(products[0].guid);
	ASSERT_NE(body, nullptr);
	EXPECT_EQ(products[0].type, body->GetAssetTypeId());
	AssetDatabase::UnloadAsset(body);

	EXPECT_TRUE(products[0].extractedPath.ToString().empty());
	EXPECT_TRUE(products[1].extractedPath.ToString().empty());
}

TEST_F(SubAssetIdentityFixture, ProductlessAssetHasNone) {
	WriteSource("project://Assets/plain.txt", "no products here");
	AssetDatabase::ScanProjectDirectory();

	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	products.push_back(AssetProduct{"stale", StringHash{}, AssetDatabase::GenerateGuid(), Path()});

	AssetDatabase::GetProducts(AssetDatabase::LookupGuid(Path("project://Assets/plain.txt")), products);
	EXPECT_TRUE(products.empty());

	products.push_back(AssetProduct{"stale", StringHash{}, AssetDatabase::GenerateGuid(), Path()});
	AssetDatabase::GetProducts(AssetDatabase::GenerateGuid(), products);
	EXPECT_TRUE(products.empty());
}

TEST_F(SubAssetIdentityFixture, GetProductsAnswersNothingForProductsGuid) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");

	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(body, products);
	EXPECT_TRUE(products.empty());
}

TEST_F(SubAssetIdentityFixture, ProductsIncludeStaleKey) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	WriteSource("project://Assets/rig.probe", "Torso:6\n");
	ImportAndDrop("project://Assets/rig.probe");

	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(source, products);

	ASSERT_EQ(products.size(), 2u);
	EXPECT_EQ(products[0].key, "Meshes/Body");
	EXPECT_EQ(products[1].key, "Meshes/Torso");
	EXPECT_EQ(products[0].guid, body);
}

TEST_F(SubAssetIdentityFixture, ProductsReportExtractedFile) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	WriteSource("project://Assets/extracted.txt", "edited by hand");
	WriteSource("project://Assets/extracted.txt.meta",
				std::format("{{\n\t\"assetId\": \"{}\",\n\t\"type\": \"0000000000000000\"\n}}\n", body.ToString()));
	AssetDatabase::ScanProjectDirectory();

	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(source, products);
	ASSERT_EQ(products.size(), 2u);
	EXPECT_EQ(products[0].extractedPath.ToString(), "project://Assets/extracted.txt");
	EXPECT_TRUE(products[1].extractedPath.ToString().empty());

	std::error_code ec;
	std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(Path("project://Assets/extracted.txt")).ToString()), ec);
	AssetDatabase::GetProducts(source, products);
	ASSERT_EQ(products.size(), 2u);
	EXPECT_TRUE(products[0].extractedPath.ToString().empty());
}

TEST_F(SubAssetIdentityFixture, ProductSourceNamesSource) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	WriteSource("project://Assets/plain.txt", "an ordinary asset");
	ImportAndDrop("project://Assets/rig.probe");
	AssetDatabase::ScanProjectDirectory();

	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(source.IsValid());
	ASSERT_TRUE(body.IsValid());

	EXPECT_EQ(AssetDatabase::GetProductSource(body), source);
	EXPECT_EQ(AssetDatabase::LookupPath(AssetDatabase::GetProductSource(body)).ToString(),
			  "project://Assets/rig.probe");

	EXPECT_FALSE(AssetDatabase::GetProductSource(AssetDatabase::LookupGuid(Path("project://Assets/plain.txt"))).IsValid());
	EXPECT_FALSE(AssetDatabase::GetProductSource(source).IsValid());
	EXPECT_FALSE(AssetDatabase::GetProductSource(AssetDatabase::GenerateGuid()).IsValid());
}

TEST_F(SubAssetIdentityFixture, ExtractingProductWritesFileCarryingGuid) {
	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());

	ASSERT_TRUE(AssetDatabase::ExtractProduct(body, Path("project://Assets/rig.probe.meshes/Body.txt")));

	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/rig.probe.meshes/Body.txt")), body);
	EXPECT_EQ(AssetDatabase::LookupPath(body).ToString(), "project://Assets/rig.probe.meshes/Body.txt");
	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);

	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/rig.probe.meshes/Body.txt")), MemTag::Core), "6");
	EXPECT_EQ(ContentOf(body), "6");

	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(source, products);
	ASSERT_EQ(products.size(), 2u);
	EXPECT_EQ(products[0].extractedPath.ToString(), "project://Assets/rig.probe.meshes/Body.txt");
	EXPECT_TRUE(products[1].extractedPath.ToString().empty());
}

TEST_F(SubAssetIdentityFixture, ReimportDoesNotOverwriteExtractedProduct) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(AssetDatabase::ExtractProduct(body, Path("project://Assets/extracted.txt")));

	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/extracted.txt")), "edited by hand");
	AssetDatabase::CollectGarbage();

	WriteSource("project://Assets/rig.probe", "Body:6\nVisor:3\n");
	ImportAndDrop("project://Assets/rig.probe");

	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/extracted.txt")), MemTag::Core), "edited by hand");
	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	EXPECT_EQ(ContentOf(body), "edited by hand");
}

TEST_F(SubAssetIdentityFixture, ExtractedServedFromFile) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(body.IsValid());
	ASSERT_TRUE(AssetDatabase::ExtractProduct(body, Path("project://Assets/extracted.txt")));

	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Assets/extracted.txt")), "edited by hand");
	EXPECT_EQ(ContentOf(body), "edited by hand");
}

TEST_F(SubAssetIdentityFixture, DeletingExtractedFileReEmbedsProduct) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	ImportAndDrop("project://Assets/rig.probe");
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	ASSERT_TRUE(AssetDatabase::ExtractProduct(body, Path("project://Assets/extracted.txt")));
	AssetDatabase::CollectGarbage();

	std::error_code ec;
	std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(Path("project://Assets/extracted.txt")).ToString()), ec);
	std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(Path("project://Assets/extracted.txt.meta")).ToString()), ec);
	AssetDatabase::RebuildDatabase();
	ImportAndDrop("project://Assets/rig.probe");

	EXPECT_EQ(ProductGuid("project://Assets/rig.probe", "Meshes/Body"), body);
	EXPECT_EQ(ContentOf(body), "6");
}

TEST_F(SubAssetIdentityFixture, ExtractRefusesBadRequests) {
	WriteSource("project://Assets/rig.probe", "Body:6\n");
	WriteSource("project://Assets/plain.txt", "an ordinary asset");
	ImportAndDrop("project://Assets/rig.probe");
	AssetDatabase::ScanProjectDirectory();
	Guid body = ProductGuid("project://Assets/rig.probe", "Meshes/Body");
	Guid source = AssetDatabase::LookupGuid(Path("project://Assets/rig.probe"));
	ASSERT_TRUE(body.IsValid());

	EXPECT_FALSE(AssetDatabase::ExtractProduct(source, Path("project://Assets/out.txt")));
	EXPECT_FALSE(AssetDatabase::ExtractProduct(AssetDatabase::LookupGuid(Path("project://Assets/plain.txt")), Path("project://Assets/out.txt")));
	EXPECT_FALSE(AssetDatabase::ExtractProduct(AssetDatabase::GenerateGuid(), Path("project://Assets/out.txt")));
	EXPECT_FALSE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/out.txt"))));

	EXPECT_FALSE(AssetDatabase::ExtractProduct(body, Path("project://Assets/plain.txt")));
	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/plain.txt")), MemTag::Core), "an ordinary asset");

	ASSERT_TRUE(AssetDatabase::ExtractProduct(body, Path("project://Assets/first.txt")));
	EXPECT_FALSE(AssetDatabase::ExtractProduct(body, Path("project://Assets/second.txt")));
	EXPECT_FALSE(FileSystem::Exists(FileSystem::Resolve(Path("project://Assets/second.txt"))));
	EXPECT_EQ(AssetDatabase::LookupPath(body).ToString(), "project://Assets/first.txt");
}
