#include "TestsPCH.h"

#include "CookHarness.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/CookOps.h"

#include "Asset/PakReader.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <format>

using namespace Shalltear;

namespace {

	class RpProbeComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(RpProbeComponent, Component)
		SLT_REFLECT_FIELD(material)
		SLT_END_REFLECT()

		Handle<MaterialAsset> material;
	};
	SLT_REGISTER_TYPE(RpProbeComponent)

	class ReportFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			static std::atomic<u64> counter{0};
			std::filesystem::path base = std::filesystem::temp_directory_path() /
										 ("shalltear_cookreport_" + std::to_string(counter.fetch_add(1)));
			_root = base / "project";
			_engine = base / "engine";
			std::error_code ec;
			std::filesystem::remove_all(base, ec);
			std::filesystem::create_directories(_root / "Assets", ec);
			std::filesystem::create_directories(_engine, ec);
			CookHarness::WriteSyntheticEngineRoots(_engine);

			FileSystem::Mount("project", Path(_root.generic_string()));
			FileSystem::Mount("engine", Path(_engine.generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			StageContent();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root.parent_path(), ec);
		}

		void StageContent() {
			StageText("Assets/Leaf.txt", str(4096, 'x'));
			AssetDatabase::ScanProjectDirectory();
			Guid leaf = AssetDatabase::LookupGuid(Path("project://Assets/Leaf.txt"));
			ASSERT_TRUE(leaf.IsValid());

			StageText("Assets/Mid.mat", std::format(R"({{
	"shader": "{}",
	"floats": {{}},
	"colors": {{}},
	"textures": {{}}
}})",
													Guid{}.ToString()));
			AssetDatabase::ScanProjectDirectory();
			_materialGuid = AssetDatabase::LookupGuid(Path("project://Assets/Mid.mat"));
			ASSERT_TRUE(_materialGuid.IsValid());

			Handle<Scene> scene = SceneSystem::CreateScene("Level");
			Handle<SceneObject> root = SceneSystem::CreateObject("Root", scene);
			Asset* material = AssetDatabase::LoadAssetByGuid(_materialGuid);
			ASSERT_NE(material, nullptr);
			root->AddComponent<RpProbeComponent>()->material =
				Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));
			ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Assets/Level.scene"), false));
			AssetDatabase::UnloadAsset(material);
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			AssetDatabase::ScanProjectDirectory();
			_sceneGuid = AssetDatabase::LookupGuid(Path("project://Assets/Level.scene"));
			ASSERT_TRUE(_sceneGuid.IsValid());
		}

		void StageText(const char* relative, const str& text) const {
			std::filesystem::path at = _root / relative;
			std::error_code ec;
			std::filesystem::create_directories(at.parent_path(), ec);
			ASSERT_TRUE(FileSystem::WriteTextFile(Path(at.generic_string()), text));
		}

		const Guid& SceneGuid() const { return _sceneGuid; }

		std::filesystem::path _root;
		std::filesystem::path _engine;
		Guid _sceneGuid;
		Guid _materialGuid;
	};

	u64 StoredBytesFromReader(const PakReader& reader, u32 assetIndex) {
		const PakAssetEntry* entry = reader.GetAsset(assetIndex);
		if (!entry || entry->regionIndex == PAK_NO_REGION) {
			return 0;
		}
		const PakRegion* region = reader.GetRegion(entry->regionIndex);
		u64 total = 0;
		for (u32 i = 0; i < region->pageCount; ++i) {
			total += reader.GetPage(region->firstPage + i)->sizeComp;
		}
		return total;
	}

} // anonymous namespace

TEST_F(ReportFixture, ReportMatchesPackage) {
	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(SceneGuid());

	CookOps::CookClosure closure;
	str error;
	ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;

	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	CookOps::CookReport report;
	CookOps::PakWriteDesc desc;
	ASSERT_TRUE(CookOps::CookPackage(closure, desc, package, error, &report)) << error;

	PakReader reader;
	ASSERT_TRUE(reader.MountFromMemory(package.data(), package.size(), "Report.slpak", error)) << error;
	const PakSuperblock& sb = reader.GetSuperblock();

	EXPECT_EQ(report.fileBytes, sb.fileSize);
	EXPECT_EQ(report.fileBytes, package.size());
	EXPECT_EQ(report.tocBytes, sb.tocSizeRaw);
	EXPECT_EQ(report.payloadBytes, sb.payloadSize);
	EXPECT_EQ(report.regionCount, sb.regionCount);
	EXPECT_EQ(report.pageCount, sb.pageCount);
	ASSERT_EQ(report.assets.size(), sb.assetCount);
	ASSERT_GT(report.assets.size(), 1u);

	for (u32 i = 0; i < sb.assetCount; ++i) {
		const CookOps::CookReportAsset& line = report.assets[i];
		const PakAssetEntry* entry = reader.GetAsset(i);
		ASSERT_NE(entry, nullptr);
		EXPECT_EQ(line.guid, entry->guid) << "row " << i << ' ' << i;
		EXPECT_EQ(line.descriptorBytes, entry->descSize);
		EXPECT_EQ(line.payloadStored, StoredBytesFromReader(reader, i)) << line.name;

		if (entry->regionIndex == PAK_NO_REGION) {
			EXPECT_EQ(line.payloadRaw, 0u) << line.name;
			EXPECT_EQ(line.pageCount, 0u);
		} else {
			const PakRegion* region = reader.GetRegion(entry->regionIndex);
			ASSERT_NE(region, nullptr);
			EXPECT_EQ(line.payloadRaw, region->sizeRaw) << line.name;
			EXPECT_EQ(line.pageCount, region->pageCount);
			EXPECT_EQ(line.codec, region->codec);
		}

		std::string_view packaged = reader.GetName(i);
		if (!packaged.empty()) {
			EXPECT_EQ(line.name, str(packaged));
		}

		const PakTypeEntry* type = reader.GetType(entry->typeIndex);
		ASSERT_NE(type, nullptr);
		const TypeInfo* info = TypeRegistry::FindType(StringHash(type->typeNameHash));
		ASSERT_NE(info, nullptr);
		EXPECT_EQ(line.typeName, str(info->displayName));

		const PakDepEdge* edges = nullptr;
		u32 edgeCount = 0;
		ASSERT_TRUE(reader.GetDependencies(i, edges, edgeCount));
		Vector<u32> fromPackage{GetMemoryResource(MemTag::Editor)};
		for (u32 e = 0; e < edgeCount; ++e) {
			fromPackage.push_back(edges[e].targetAsset);
		}
		EXPECT_EQ(line.dependsOn.size(), fromPackage.size()) << line.name;
		for (size_t e = 0; e < line.dependsOn.size() && e < fromPackage.size(); ++e) {
			EXPECT_EQ(line.dependsOn[e], fromPackage[e]) << line.name << " edge " << e;
		}
	}

	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	CookOps::CookReport compressedReport;
	CookOps::PakWriteDesc lz4 = desc;
	lz4.compression = CookOps::CookCompression::LZ4HC;
	ASSERT_TRUE(CookOps::CookPackage(closure, lz4, compressed, error, &compressedReport)) << error;

	PakReader reader2;
	ASSERT_TRUE(reader2.MountFromMemory(compressed.data(), compressed.size(), "LZ4.slpak", error)) << error;
	EXPECT_EQ(compressedReport.fileBytes, reader2.GetSuperblock().fileSize);

	bool anyCompressed = false;
	for (u32 i = 0; i < reader2.GetAssetCount(); ++i) {
		const CookOps::CookReportAsset& line = compressedReport.assets[i];
		EXPECT_EQ(line.payloadStored, StoredBytesFromReader(reader2, i)) << line.name;
		if (line.payloadStored != 0 && line.payloadStored < line.payloadRaw) {
			anyCompressed = true;
			EXPECT_EQ(line.codec, static_cast<u8>(PakCodec::LZ4)) << line.name;
		}
	}
	EXPECT_TRUE(anyCompressed);
}

TEST_F(ReportFixture, ReportExplainsEachAsset) {
	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(SceneGuid());

	CookOps::CookClosure closure;
	str error;
	ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;

	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	CookOps::CookReport report;
	ASSERT_TRUE(CookOps::CookPackage(closure, CookOps::PakWriteDesc(), package, error, &report)) << error;

	EXPECT_EQ(closure.roots.size(), roots.size() + AssetDatabase::GetEngineBuiltinRoots().size());
	u32 rootCount = 0;
	for (const CookOps::CookReportAsset& line : report.assets) {
		if (line.isRoot) {
			++rootCount;
			EXPECT_TRUE(std::find(closure.roots.begin(), closure.roots.end(), line.guid) != closure.roots.end())
				<< line.name;
			EXPECT_TRUE(line.rootPath.empty());
		}
	}
	EXPECT_EQ(rootCount, closure.roots.size());
	EXPECT_GT(rootCount, 1u);

	for (u32 i = 0; i < static_cast<u32>(report.assets.size()); ++i) {
		for (u32 target : report.assets[i].dependsOn) {
			const Vector<u32>& back = report.assets[target].pulledInBy;
			EXPECT_TRUE(std::find(back.begin(), back.end(), i) != back.end()) << report.assets[i].name << " -> " << report.assets[target].name;
		}
		for (u32 source : report.assets[i].pulledInBy) {
			const Vector<u32>& forward = report.assets[source].dependsOn;
			EXPECT_TRUE(std::find(forward.begin(), forward.end(), i) != forward.end()) << report.assets[i].name;
		}
	}

	u32 checked = 0;
	for (const CookOps::CookReportAsset& line : report.assets) {
		if (line.isRoot) {
			continue;
		}
		ASSERT_FALSE(line.rootPath.empty()) << line.name;
		EXPECT_TRUE(report.assets[line.rootPath.front()].isRoot) << line.name;
		for (size_t hop = 0; hop + 1 < line.rootPath.size(); ++hop) {
			const Vector<u32>& out = report.assets[line.rootPath[hop]].dependsOn;
			EXPECT_TRUE(std::find(out.begin(), out.end(), line.rootPath[hop + 1]) != out.end()) << line.name << ' ' << hop;
		}
		const Vector<u32>& last = report.assets[line.rootPath.back()].dependsOn;
		u32 self = static_cast<u32>(&line - report.assets.data());
		EXPECT_TRUE(std::find(last.begin(), last.end(), self) != last.end()) << line.name;
		++checked;
	}
	EXPECT_GT(checked, 0u);
}

TEST_F(ReportFixture, ReportRoundTripsJSON) {
	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(SceneGuid());
	CookOps::CookClosure closure;
	str error;
	ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;

	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	CookOps::CookReport written;
	CookOps::PakWriteDesc desc;
	desc.shipDebugNames = false;
	ASSERT_TRUE(CookOps::CookPackage(closure, desc, package, error, &written)) << error;
	written.profileName = "Windows";
	written.packagePath = "C:/Builds/Windows/Data/Content.slpak";
	written.candidateCount = 99;

	str json = CookOps::WriteReportJSON(written);
	ASSERT_FALSE(json.empty());

	CookOps::CookReport read;
	ASSERT_TRUE(CookOps::ReadReportJSON(json, read, error)) << error;

	EXPECT_EQ(read.profileName, written.profileName);
	EXPECT_EQ(read.packagePath, written.packagePath);
	EXPECT_EQ(read.fileBytes, written.fileBytes);
	EXPECT_EQ(read.tocBytes, written.tocBytes);
	EXPECT_EQ(read.payloadBytes, written.payloadBytes);
	EXPECT_EQ(read.regionCount, written.regionCount);
	EXPECT_EQ(read.pageCount, written.pageCount);
	EXPECT_EQ(read.candidateCount, written.candidateCount);
	EXPECT_EQ(read.compression, written.compression);
	EXPECT_EQ(read.debugNamesShipped, false);
	ASSERT_EQ(read.assets.size(), written.assets.size());
	for (size_t i = 0; i < read.assets.size(); ++i) {
		const CookOps::CookReportAsset& a = written.assets[i];
		const CookOps::CookReportAsset& b = read.assets[i];
		EXPECT_EQ(b.guid, a.guid);
		EXPECT_EQ(b.name, a.name);
		EXPECT_EQ(b.typeName, a.typeName);
		EXPECT_EQ(b.descriptorBytes, a.descriptorBytes);
		EXPECT_EQ(b.payloadRaw, a.payloadRaw);
		EXPECT_EQ(b.payloadStored, a.payloadStored);
		EXPECT_EQ(b.pageCount, a.pageCount);
		EXPECT_EQ(b.codec, a.codec);
		EXPECT_EQ(b.isRoot, a.isRoot);
		EXPECT_EQ(b.dependsOn, a.dependsOn);
		EXPECT_EQ(b.pulledInBy, a.pulledInBy);
		EXPECT_EQ(b.rootPath, a.rootPath);
	}
}

TEST(CookReportReader, IndexPastCountRefused) {
	str json = R"({
	"formatVersion": 1,
	"profile": "Windows",
	"package": "x",
	"fileBytes": 4096,
	"tocBytes": 0,
	"payloadBytes": 0,
	"regionCount": 0,
	"pageCount": 0,
	"candidateCount": 1,
	"compression": 0,
	"debugNamesShipped": true,
	"assets": [
		{ "guid": "00000000000000010000000000000002", "name": "a", "type": "MeshAsset",
		  "descriptorBytes": 0, "payloadRaw": 0, "payloadStored": 0, "pageCount": 0, "codec": 0,
		  "isRoot": false, "dependsOn": [ 7 ] }
	]
})";
	CookOps::CookReport report;
	str error;
	EXPECT_FALSE(CookOps::ReadReportJSON(json, report, error));
	EXPECT_NE(error.find("asset 7 of 1"), str::npos) << error;
}

TEST(CookReportReader, FutureReportRefused) {
	CookOps::CookReport report;
	str error;
	EXPECT_FALSE(CookOps::ReadReportJSON(R"({"formatVersion": 99, "assets": []})", report, error));
	EXPECT_NE(error.find("format 99"), str::npos) << error;
	EXPECT_FALSE(CookOps::ReadReportJSON("not json at all", report, error));
}
