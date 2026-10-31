#include <gtest/gtest.h>

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneAsset.h"

#include "Shalltear/Editor/CookOps.h"

#include "CookHarness.h"

#include <algorithm>
#include <atomic>
#include <filesystem>

using namespace Shalltear;

namespace {

	using CookHarness::BytesIdentical;
	using CookHarness::SuperblockOf;

	class CdProbeComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(CdProbeComponent, Component)
		SLT_REFLECT_FIELD(material)
		SLT_END_REFLECT()

		Handle<MaterialAsset> material;
	};
	SLT_REGISTER_TYPE(CdProbeComponent)

	class CookDeterminismFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			static std::atomic<u64> counter{0};
			std::filesystem::path base = std::filesystem::temp_directory_path() /
										 ("shalltear_cookdeterminism_" + std::to_string(counter.fetch_add(1)));
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
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root.parent_path(), ec);
		}

		void StageText(const char* relative, std::string_view text) const {
			std::filesystem::path at = _root / relative;
			std::error_code ec;
			std::filesystem::create_directories(at.parent_path(), ec);
			ASSERT_TRUE(FileSystem::WriteTextFile(Path(at.generic_string()), str(text)));
		}

		Guid GuidOf(const char* virtualPath) const { return AssetDatabase::LookupGuid(Path(virtualPath)); }

		void Rescan() const { AssetDatabase::ScanProjectDirectory(); }

		static Vector<Guid> Roots(std::initializer_list<Guid> guids) {
			Vector<Guid> out{GetMemoryResource(MemTag::Editor)};
			for (const Guid& guid : guids) {
				out.push_back(guid);
			}
			return out;
		}

		Guid StageAProjectWithEveryEdgeKind() {
			StageText("Assets/Leaf.txt", "a leaf");
			StageText("Assets/Other.txt", "a second leaf");
			Rescan();
			StageText("Assets/Mid.mat", std::format(R"({{
	"shader": "{}",
	"floats": {{ "specular": 0.5 }},
	"colors": {{}},
	"textures": {{ "_BaseMap": "{}" }}
}})",
													GuidOf("project://Assets/Other.txt").ToString(),
													GuidOf("project://Assets/Leaf.txt").ToString()));
			Rescan();

			Handle<Scene> scene = SceneSystem::CreateScene("Level");
			Handle<SceneObject> root = SceneSystem::CreateObject("Root", scene);
			Asset* material = AssetDatabase::LoadAssetByGuid(GuidOf("project://Assets/Mid.mat"));
			SLT_APP_ASSERT_MSG(material != nullptr, "the fixture's material didn't load");
			root->AddComponent<CdProbeComponent>()->material =
				Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));
			SceneSystem::SaveScene(scene, Path("project://Assets/Level.scene"), false);
			AssetDatabase::UnloadAsset(material);
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			Rescan();
			return GuidOf("project://Assets/Level.scene");
		}

		Vector<u8> CookFrom(const Vector<Guid>& roots, bool shipDebugNames = true,
							CookOps::CookCompression compression = CookOps::CookCompression::None) {
			CookOps::CookClosure closure;
			str error;
			Vector<u8> package{GetMemoryResource(MemTag::Editor)};
			if (!CookOps::ComputeClosure(roots, closure, error)) {
				ADD_FAILURE() << "closure failed: " << error;
				return package;
			}
			CookOps::PakWriteDesc desc;
			desc.shipDebugNames = shipDebugNames;
			desc.compression = compression;
			if (!CookOps::CookPackage(closure, desc, package, error)) {
				ADD_FAILURE() << "cook failed: " << error;
			}
			return package;
		}

		std::filesystem::path _root;
		std::filesystem::path _engine;
	};

	CookOps::PakAssetSource MakeSource(const Guid& guid, u64 typeNameHash, const char* name, u8 fill,
									   size_t descBytes, size_t payloadBytes) {
		CookOps::PakAssetSource source;
		source.guid = guid;
		source.typeNameHash = typeNameHash;
		source.name = name;
		source.descriptor.assign(descBytes, fill);
		source.payload.assign(payloadBytes, static_cast<u8>(fill ^ 0xFF));
		return source;
	}

	Vector<CookOps::PakAssetSource> FourAssets() {
		Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
		sources.push_back(MakeSource(Guid{0x1111, 0x4444}, StringHash("TextAsset").GetValue(), "project://d.txt", 0xA1,
									 40, 9000));
		sources.push_back(MakeSource(Guid{0x2222, 0x3333}, StringHash("TextAsset").GetValue(), "project://c.txt", 0xB2,
									 12, 0));
		sources.push_back(MakeSource(Guid{0x3333, 0x2222}, StringHash("MeshAsset").GetValue(), "project://b.mesh", 0xC3,
									 130, 300));
		sources.push_back(MakeSource(Guid{0x4444, 0x1111}, StringHash("MaterialAsset").GetValue(), "project://a.mat",
									 0xD4, 7, 1));
		return sources;
	}

	Vector<u8> Build(const Vector<CookOps::PakAssetSource>& sources,
					 CookOps::CookCompression compression = CookOps::CookCompression::None) {
		CookOps::PakWriteDesc desc;
		desc.compression = compression;
		Vector<u8> out{GetMemoryResource(MemTag::Editor)};
		str error;
		if (!CookOps::BuildPackage(sources, desc, out, error)) {
			ADD_FAILURE() << "writer refused: " << error;
		}
		return out;
	}

} // namespace

TEST_F(CookDeterminismFixture, SecondCookIsByteIdentical) {
	Guid scene = StageAProjectWithEveryEdgeKind();
	ASSERT_TRUE(scene.IsValid());

	Vector<u8> first = CookFrom(Roots({scene}));
	ASSERT_FALSE(first.empty());

	AssetDatabase::RebuildDatabase();
	Vector<u8> second = CookFrom(Roots({scene}));

	EXPECT_TRUE(BytesIdentical(first, second));
	EXPECT_EQ(SuperblockOf(first).contentHash, SuperblockOf(second).contentHash);
}

TEST_F(CookDeterminismFixture, TheRootORDERCannotReachTheFile) {
	Guid scene = StageAProjectWithEveryEdgeKind();
	Guid leaf = GuidOf("project://Assets/Leaf.txt");
	ASSERT_TRUE(scene.IsValid() && leaf.IsValid());

	Vector<u8> forward = CookFrom(Roots({scene, leaf}));
	Vector<u8> backward = CookFrom(Roots({leaf, scene}));

	ASSERT_FALSE(forward.empty());
	EXPECT_TRUE(BytesIdentical(forward, backward));
}

TEST(CookDeterminismWriter, PRESENTATIONOrderOfAssetsCannotReachFile) {
	Vector<CookOps::PakAssetSource> ascending = FourAssets();
	Vector<u8> reference = Build(ascending);
	ASSERT_FALSE(reference.empty());

	Vector<CookOps::PakAssetSource> descending = FourAssets();
	std::reverse(descending.begin(), descending.end());
	EXPECT_TRUE(BytesIdentical(reference, Build(descending)));

	Vector<CookOps::PakAssetSource> rotated = FourAssets();
	std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
	EXPECT_TRUE(BytesIdentical(reference, Build(rotated)));
}

TEST(CookDeterminismWriter, ORDEROfAssetsReferencesCannotReachDeps) {
	Vector<CookOps::PakAssetSource> plain = FourAssets();
	plain[0].dependencies.push_back(plain[1].guid);
	plain[0].dependencies.push_back(plain[2].guid);
	plain[0].dependencies.push_back(plain[3].guid);
	Vector<u8> reference = Build(plain);
	ASSERT_FALSE(reference.empty());

	Vector<CookOps::PakAssetSource> messy = FourAssets();
	messy[0].dependencies.push_back(messy[3].guid);
	messy[0].dependencies.push_back(messy[0].guid);
	messy[0].dependencies.push_back(messy[2].guid);
	messy[0].dependencies.push_back(messy[3].guid);
	messy[0].dependencies.push_back(messy[1].guid);
	messy[0].dependencies.push_back(messy[2].guid);

	EXPECT_TRUE(BytesIdentical(reference, Build(messy)));
}

TEST(CookDeterminismWriter, CodecDoesNotAcquireOrderDependence) {
	for (CookOps::CookCompression how : {CookOps::CookCompression::LZ4, CookOps::CookCompression::LZ4HC}) {
		Vector<CookOps::PakAssetSource> ascending = FourAssets();
		Vector<u8> reference = Build(ascending, how);
		ASSERT_FALSE(reference.empty());

		Vector<CookOps::PakAssetSource> descending = FourAssets();
		std::reverse(descending.begin(), descending.end());
		EXPECT_TRUE(BytesIdentical(reference, Build(descending, how)));

		Vector<CookOps::PakAssetSource> rotated = FourAssets();
		std::rotate(rotated.begin(), rotated.begin() + 1, rotated.end());
		EXPECT_TRUE(BytesIdentical(reference, Build(rotated, how)));
	}
}

TEST(CookDeterminismWriter, TheEncoderCarriesNothingBetweenCalls) {
	Vector<CookOps::PakAssetSource> alone = FourAssets();
	Vector<u8> reference = Build(alone, CookOps::CookCompression::LZ4HC);
	ASSERT_FALSE(reference.empty());

	Vector<CookOps::PakAssetSource> interloper = FourAssets();
	for (CookOps::PakAssetSource& source : interloper) {
		source.payload.assign(source.payload.size(), u8{0x5A});
	}
	(void)Build(interloper, CookOps::CookCompression::LZ4HC);

	EXPECT_TRUE(BytesIdentical(reference, Build(FourAssets(), CookOps::CookCompression::LZ4HC)));
}

static void CheckEveryGapIsZero(const Vector<u8>& package) {
	PakSuperblock sb = SuperblockOf(package);
	ASSERT_EQ(sb.fileSize, package.size());

	auto expectZero = [&package](u64 begin, u64 end, const char* what) {
		ASSERT_LE(begin, end) << what;
		ASSERT_LE(end, package.size()) << what;
		for (u64 at = begin; at < end; ++at) {
			ASSERT_EQ(package[static_cast<size_t>(at)], 0u) << what << ": byte " << at << ' ' << begin << ", " << end;
		}
	};

	for (size_t i = 0; i < sizeof(sb.reserved); ++i) {
		ASSERT_EQ(sb.reserved[i], 0u) << "PakSuperblock::reserved[" << i;
	}

	expectZero(sb.headerSize, sb.tocOffset, "between the superblock and the TOC");

	ASSERT_GE(sb.sectionCount, 1u);
	Vector<PakSection> sections{GetMemoryResource(MemTag::Editor)};
	sections.resize(sb.sectionCount);
	ASSERT_LE(sb.tocOffset + static_cast<u64>(sb.sectionCount) * sizeof(PakSection), package.size());
	std::memcpy(sections.data(), package.data() + sb.tocOffset, sections.size() * sizeof(PakSection));
	std::sort(sections.begin(), sections.end(),
			  [](const PakSection& a, const PakSection& b) { return a.offset < b.offset; });
	for (size_t i = 1; i < sections.size(); ++i) {
		expectZero(sb.tocOffset + sections[i - 1].offset + sections[i - 1].size, sb.tocOffset + sections[i].offset,
				   "between two TOC sections");
	}
	expectZero(sb.tocOffset + sections.back().offset + sections.back().size, sb.tocOffset + sb.tocSizeRaw,
			   "after the last TOC section");

	expectZero(sb.tocOffset + sb.tocSizeRaw, sb.payloadOffset, "between the TOC and the payload");

	const PakSection* pageSection = nullptr;
	for (const PakSection& section : sections) {
		if (section.fourcc == PAK_PAGE) {
			pageSection = &section;
		}
	}
	ASSERT_NE(pageSection, nullptr);
	ASSERT_GT(pageSection->elemCount, 0u);
	Vector<PakPage> pages{GetMemoryResource(MemTag::Editor)};
	pages.resize(pageSection->elemCount);
	std::memcpy(pages.data(), package.data() + sb.tocOffset + pageSection->offset, pages.size() * sizeof(PakPage));
	std::sort(pages.begin(), pages.end(), [](const PakPage& a, const PakPage& b) { return a.offset < b.offset; });

	expectZero(sb.payloadOffset, pages.front().offset, "before the first page");
	for (size_t i = 1; i < pages.size(); ++i) {
		expectZero(pages[i - 1].offset + pages[i - 1].sizeComp, pages[i].offset, "a page tail");
	}
	expectZero(pages.back().offset + pages.back().sizeComp, sb.fileSize, "after the last page");
}

TEST_F(CookDeterminismFixture, NoByteOfAPackageIsUnassigned) {
	Guid scene = StageAProjectWithEveryEdgeKind();
	Vector<u8> package = CookFrom(Roots({scene}));
	ASSERT_FALSE(package.empty());
	CheckEveryGapIsZero(package);
}

TEST_F(CookDeterminismFixture, NoByteOfCompressedPackageIsUnassigned) {
	Guid scene = StageAProjectWithEveryEdgeKind();
	Vector<u8> package = CookFrom(Roots({scene}), true, CookOps::CookCompression::LZ4HC);
	ASSERT_FALSE(package.empty());
	CheckEveryGapIsZero(package);
}

TEST_F(CookDeterminismFixture, StripRemovesOnlyNAMS) {
	Guid scene = StageAProjectWithEveryEdgeKind();

	Vector<u8> named = CookFrom(Roots({scene}), true);
	Vector<u8> stripped = CookFrom(Roots({scene}), false);
	ASSERT_FALSE(named.empty());
	ASSERT_FALSE(stripped.empty());

	PakSuperblock withNames = SuperblockOf(named);
	PakSuperblock without = SuperblockOf(stripped);

	EXPECT_TRUE(withNames.featuresOptional & static_cast<u64>(PakFeatureOptional::DebugNames));
	EXPECT_FALSE(without.featuresOptional & static_cast<u64>(PakFeatureOptional::DebugNames));
	EXPECT_EQ(withNames.sectionCount, without.sectionCount + 1);
	EXPECT_EQ(withNames.assetCount, without.assetCount);
	EXPECT_EQ(withNames.payloadSize, without.payloadSize);

	EXPECT_LT(without.tocSizeRaw, withNames.tocSizeRaw);
	EXPECT_LE(stripped.size(), named.size());
	RecordProperty("tocBytesWithNames", static_cast<int>(withNames.tocSizeRaw));
	RecordProperty("tocBytesStripped", static_cast<int>(without.tocSizeRaw));

	EXPECT_TRUE(BytesIdentical(stripped, CookFrom(Roots({scene}), false)));
}

class CookShaderCacheFixture : public ::testing::Test {
protected:
	void SetUp() override {
		_engineAssets = CookHarness::FindRepoDir("EngineAssets");
		if (_engineAssets.empty()) {
			return;
		}
		static std::atomic<u64> counter{0};
		_root = std::filesystem::temp_directory_path() /
				("shalltear_cookshadercache_" + std::to_string(counter.fetch_add(1)));
		std::error_code ec;
		std::filesystem::remove_all(_root, ec);
		std::filesystem::create_directories(_root / "Assets", ec);

		FileSystem::Mount("project", Path(_root.generic_string()));
		FileSystem::Mount("engine", Path(_engineAssets.generic_string()));
		AssetDatabase::Initialize();
		SceneSystem::Initialize();
		_open = true;
	}

	void TearDown() override {
		if (!_open) {
			return;
		}
		SceneSystem::Terminate();
		AssetDatabase::Terminate();
		FileSystem::Unmount("engine");
		FileSystem::Unmount("project");
		std::error_code ec;
		std::filesystem::remove_all(_root, ec);
	}

	void GoCold() const {
		std::error_code ec;
		std::filesystem::remove_all(_root / "Library", ec);
	}

	size_t CachedBlobCount() const {
		std::error_code ec;
		size_t count = 0;
		for (std::filesystem::directory_iterator it(_root / "Library" / "ShaderCache", ec), end; !ec && it != end;
			 it.increment(ec)) {
			++count;
		}
		return count;
	}

	void PoisonTheCache() const {
		std::error_code ec;
		for (std::filesystem::directory_iterator it(_root / "Library" / "ShaderCache", ec), end; !ec && it != end;
			 it.increment(ec)) {
			size_t size = FileSystem::GetFileSize(Path(it->path().generic_string()));
			Buffer junk(size, MemTag::Asset);
			std::memset(junk.data, 0x5A, size);
			FileSystem::WriteFile(Path(it->path().generic_string()), junk);
		}
	}

	std::filesystem::path _engineAssets;
	std::filesystem::path _root;
	bool _open = false;
};

TEST_F(CookShaderCacheFixture, WarmCacheSameSpirv) {
	if (_engineAssets.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	Path source("engine://Shaders/Blit.hlsl");

	GoCold();
	ShaderAsset cold;
	if (!cold.Import(source)) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ASSERT_FALSE(cold.variants.empty());
	ASSERT_FALSE(cold.variants[SHADER_BASE_VARIANT].vertexSpirv.empty());
	ASSERT_FALSE(cold.variants[SHADER_BASE_VARIANT].pixelSpirv.empty());
	ASSERT_GT(CachedBlobCount(), 0u);

	ShaderAsset warm;
	ASSERT_TRUE(warm.Import(source));
	EXPECT_EQ(cold.variants[SHADER_BASE_VARIANT].vertexSpirv, warm.variants[SHADER_BASE_VARIANT].vertexSpirv);
	EXPECT_EQ(cold.variants[SHADER_BASE_VARIANT].pixelSpirv, warm.variants[SHADER_BASE_VARIANT].pixelSpirv);

	PoisonTheCache();
	ShaderAsset poisoned;
	bool imported = poisoned.Import(source);
	EXPECT_TRUE(!imported || poisoned.variants[SHADER_BASE_VARIANT].vertexSpirv != cold.variants[SHADER_BASE_VARIANT].vertexSpirv ||
				poisoned.variants[SHADER_BASE_VARIANT].pixelSpirv != cold.variants[SHADER_BASE_VARIANT].pixelSpirv);
}

TEST_F(CookShaderCacheFixture, CookSameColdOrWarm) {
	if (_engineAssets.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Vector<Guid> noScenes{GetMemoryResource(MemTag::Editor)};
	CookOps::PakWriteDesc desc;
	str error;

	GoCold();
	CookOps::CookClosure closure;
	ASSERT_TRUE(CookOps::ComputeClosure(noScenes, closure, error)) << error;
	Vector<u8> fromCold{GetMemoryResource(MemTag::Editor)};
	if (!CookOps::CookPackage(closure, desc, fromCold, error)) {
		GTEST_SKIP() << "engine built ins didn't cook (dxcompiler.dll unavailable?): " << error;
	}
	ASSERT_GT(CachedBlobCount(), 0u);

	CookOps::CookClosure again;
	ASSERT_TRUE(CookOps::ComputeClosure(noScenes, again, error)) << error;
	Vector<u8> fromWarm{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(CookOps::CookPackage(again, desc, fromWarm, error)) << error;

	EXPECT_TRUE(BytesIdentical(fromCold, fromWarm));
}
