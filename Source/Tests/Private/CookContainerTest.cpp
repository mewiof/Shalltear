#include "TestsPCH.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/BuildInfo.h"
#include "Shalltear/Core/FileSystem.h"

#include "Shalltear/Editor/CookOps.h"

#include "Asset/PakReader.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;

namespace {

	class Xorshift {
	public:
		explicit Xorshift(u64 seed) : _state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

		u64 Next() {
			_state ^= _state >> 12;
			_state ^= _state << 25;
			_state ^= _state >> 27;
			return _state * 0x2545F4914F6CDD1Dull;
		}

		u32 Next(u32 bound) { return bound == 0 ? 0 : static_cast<u32>(Next() % bound); }
	private:
		u64 _state;
	};

	class CookContainerFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			_dir = std::filesystem::temp_directory_path() / "shalltear_pak_test";
			std::error_code ec;
			std::filesystem::create_directories(_dir, ec);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_dir, ec);
		}

		Path FilePath(const char* name) const { return Path((_dir / name).generic_string()); }

		static Vector<u8> Bytes(Xorshift& random, size_t size) {
			Vector<u8> out{GetMemoryResource(MemTag::Editor)};
			out.resize(size);
			for (size_t i = 0; i < size; ++i) {
				out[i] = static_cast<u8>(random.Next());
			}
			return out;
		}

		template <typename Fn>
		static void EditSuperblock(Vector<u8>& bytes, Fn&& edit) {
			PakSuperblock sb{};
			std::memcpy(&sb, bytes.data(), sizeof(PakSuperblock));
			edit(sb);
			sb.crc32c = PakCrc32c(&sb, 4092);
			std::memcpy(bytes.data(), &sb, sizeof(PakSuperblock));
		}

		static size_t FindSectionOffset(const Vector<u8>& bytes, u32 fourcc) {
			PakSuperblock sb{};
			std::memcpy(&sb, bytes.data(), sizeof(PakSuperblock));
			for (u16 i = 0; i < sb.sectionCount; ++i) {
				PakSection section{};
				std::memcpy(&section, bytes.data() + sb.tocOffset + i * sizeof(PakSection), sizeof(PakSection));
				if (section.fourcc == fourcc) {
					return static_cast<size_t>(sb.tocOffset + section.offset);
				}
			}
			return 0;
		}

		bool WriteBytes(const Path& path, const Vector<u8>& bytes) const {
			Buffer buffer(bytes.size(), MemTag::Editor);
			std::memcpy(buffer.data, bytes.data(), bytes.size());
			return FileSystem::WriteFile(path, buffer);
		}

		std::filesystem::path _dir;
	};

} // anonymous namespace

TEST_F(CookContainerFixture, TenThousandRecordsRoundTrip) {
	constexpr u32 COUNT = 10000;
	Xorshift random(0xC00C1EDull);

	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 0; i < COUNT; ++i) {
		CookOps::PakAssetSource source;
		source.guid = Guid(random.Next(), random.Next());
		source.typeNameHash = 0x1000 + (i % 7);
		source.recordVersion = static_cast<u16>(1 + (i % 7));
		source.layoutHash = 0xABC000ull + (i % 7);
		source.name = std::format("project://Assets/Generated/asset_{}.bin", i);
		source.descriptor = Bytes(random, 1 + random.Next(96));
		if (i % 3 != 0) {
			source.payload = Bytes(random, 1 + random.Next(2000));
		}
		source.payloadAlignment = 16;
		source.residency = static_cast<PakResidency>(i % 3);
		sources.push_back(std::move(source));
	}

	for (u32 i = 2; i < COUNT; ++i) {
		sources[i].dependencies.push_back(sources[i - 1].guid);
		sources[i].dependencies.push_back(sources[i - 2].guid);
	}

	CookOps::PakWriteDesc desc;
	desc.targetId = 7;
	desc.strings.push_back(str("_BaseColor"));
	desc.strings.push_back(str("_Metallic"));
	desc.strings.push_back(str(""));
	desc.strings.push_back(str("_Roughness"));

	Path path = FilePath("Content.slpak");
	str error;
	ASSERT_TRUE(CookOps::WritePackage(path, sources, desc, error)) << error;

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	EXPECT_EQ(reader.GetAssetCount(), COUNT);
	EXPECT_EQ(reader.GetTypeCount(), 7u);
	EXPECT_EQ(reader.GetSuperblock().targetId, 7u);
	EXPECT_EQ(reader.GetSuperblock().engineHash, BuildInfo::GetEngineHash());

	for (u32 i = 0; i < COUNT; ++i) {
		const CookOps::PakAssetSource& source = sources[i];
		u32 index = reader.FindAsset(source.guid);
		ASSERT_NE(index, PAK_INVALID_INDEX) << "asset " << i;

		const PakAssetEntry* entry = reader.GetAsset(index);
		ASSERT_NE(entry, nullptr);
		EXPECT_EQ(entry->guid, source.guid);

		CookReader descriptor = reader.GetDescriptor(index);
		ASSERT_EQ(descriptor.Size(), source.descriptor.size()) << "asset " << i;
		const u8* descBytes = descriptor.ReadSpan(descriptor.Size());
		ASSERT_NE(descBytes, nullptr);
		EXPECT_EQ(std::memcmp(descBytes, source.descriptor.data(), source.descriptor.size()), 0) << "descriptor " << i;

		if (source.payload.empty()) {
			EXPECT_EQ(entry->regionIndex, PAK_NO_REGION) << "asset " << i;
			EXPECT_EQ(entry->flags & static_cast<u16>(PakAssetFlags::HasPayload), 0);
		} else {
			ASSERT_NE(entry->regionIndex, PAK_NO_REGION) << "asset " << i;
			EXPECT_NE(entry->flags & static_cast<u16>(PakAssetFlags::HasPayload), 0);
			const PakRegion* region = reader.GetRegion(entry->regionIndex);
			ASSERT_NE(region, nullptr);
			EXPECT_EQ(region->codec, static_cast<u8>(PakCodec::None));
			EXPECT_EQ(region->residency, static_cast<u8>(source.residency));

			Buffer payload;
			ASSERT_TRUE(reader.ReadRegion(entry->regionIndex, payload, error)) << error << " (asset " << i << ")";
			ASSERT_EQ(payload.size, source.payload.size());
			EXPECT_EQ(std::memcmp(payload.data, source.payload.data(), payload.size), 0) << "payload " << i;
		}

		const PakTypeEntry* type = reader.GetType(entry->typeIndex);
		ASSERT_NE(type, nullptr);
		EXPECT_EQ(type->typeNameHash, source.typeNameHash);
		EXPECT_EQ(type->recordVersion, source.recordVersion);
		EXPECT_EQ(type->layoutHash, source.layoutHash);

		EXPECT_EQ(reader.GetName(index), std::string_view(source.name));

		Vector<Guid> expected{GetMemoryResource(MemTag::Editor)};
		expected.assign(source.dependencies.begin(), source.dependencies.end());
		std::sort(expected.begin(), expected.end());
		expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
		expected.erase(std::remove(expected.begin(), expected.end(), source.guid), expected.end());

		const PakDepEdge* edges = nullptr;
		u32 edgeCount = 0;
		ASSERT_TRUE(reader.GetDependencies(index, edges, edgeCount));
		EXPECT_EQ(edgeCount, expected.size()) << "asset " << i;
		for (u32 e = 0; e < edgeCount && e < expected.size(); ++e) {
			const PakAssetEntry* target = reader.GetAsset(edges[e].targetAsset);
			ASSERT_NE(target, nullptr);
			EXPECT_EQ(target->guid, expected[e]) << "edge " << e << " of asset " << i;
			EXPECT_EQ(edges[e].flags, static_cast<u32>(PakRefKind::Hard));
		}
	}

	ASSERT_EQ(reader.GetStringCount(), 4u);
	EXPECT_EQ(reader.GetString(0), "_BaseColor");
	EXPECT_EQ(reader.GetString(1), "_Metallic");
	EXPECT_EQ(reader.GetString(2), "");
	EXPECT_EQ(reader.GetString(3), "_Roughness");
	EXPECT_EQ(reader.GetString(4), "");
}

TEST_F(CookContainerFixture, SuperblockMatchesFormat) {
	Xorshift random(1);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(1, 1);
	source.typeNameHash = 0x1234;
	source.descriptor = Bytes(random, 32);
	source.payload = Bytes(random, 64);
	sources.push_back(std::move(source));

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error)) << error;
	ASSERT_GE(bytes.size(), PAK_HEADER_SIZE);

	auto readU16 = [&bytes](size_t at) {
		u16 value = 0;
		std::memcpy(&value, bytes.data() + at, sizeof(value));
		return value;
	};
	auto readU32 = [&bytes](size_t at) {
		u32 value = 0;
		std::memcpy(&value, bytes.data() + at, sizeof(value));
		return value;
	};
	auto readU64 = [&bytes](size_t at) {
		u64 value = 0;
		std::memcpy(&value, bytes.data() + at, sizeof(value));
		return value;
	};

	EXPECT_EQ(std::memcmp(bytes.data(), PAK_MAGIC, sizeof(PAK_MAGIC)), 0);
	EXPECT_EQ(readU16(8), PAK_VERSION_MAJOR);
	EXPECT_EQ(readU16(10), PAK_VERSION_MINOR);
	EXPECT_EQ(readU32(12), PAK_HEADER_SIZE);
	EXPECT_EQ(readU64(16), 0u);
	EXPECT_EQ(readU64(32), bytes.size());
	EXPECT_EQ(readU32(60), 1u);
	EXPECT_EQ(readU64(64) % PAK_SECTOR_SIZE, 0u);
	EXPECT_EQ(readU32(88), PAK_SECTOR_SIZE);
	EXPECT_EQ(readU64(104), BuildInfo::GetEngineHash());

	for (size_t at = 120; at < 4092; ++at) {
		ASSERT_EQ(bytes[at], 0) << at;
	}

	EXPECT_EQ(readU64(112), PakContentHash(bytes.data() + PAK_HEADER_SIZE, bytes.size() - PAK_HEADER_SIZE));
	EXPECT_EQ(readU32(4092), PakCrc32c(bytes.data(), 4092));
}

TEST_F(CookContainerFixture, SingleAssetPackageIsLegal) {
	Xorshift random(2);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(0xFEEDul, 0xFACEul);
	source.typeNameHash = 0x99;
	source.name = str("project://Assets/Only.bin");
	source.descriptor = Bytes(random, 17);
	sources.push_back(std::move(source));

	Path path = FilePath("Single.slpak");
	str error;
	ASSERT_TRUE(CookOps::WritePackage(path, sources, CookOps::PakWriteDesc(), error)) << error;

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	EXPECT_EQ(reader.GetAssetCount(), 1u);
	EXPECT_EQ(reader.FindAsset(Guid(0xFEEDul, 0xFACEul)), 0u);
	EXPECT_EQ(reader.FindAsset(Guid(1, 1)), PAK_INVALID_INDEX);

	EXPECT_EQ(reader.GetSuperblock().regionCount, 0u);
	EXPECT_EQ(reader.GetSuperblock().pageCount, 0u);
	EXPECT_EQ(reader.GetAsset(0)->regionIndex, PAK_NO_REGION);
}

TEST_F(CookContainerFixture, CsrSentinelMakesLastAssetsRowReadable) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 0; i < 4; ++i) {
		CookOps::PakAssetSource source;
		source.guid = Guid(i + 1, 0);
		source.typeNameHash = 0x1;
		sources.push_back(std::move(source));
	}
	sources[3].dependencies.push_back(sources[0].guid);
	sources[3].dependencies.push_back(sources[2].guid);

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error)) << error;
	Path path = FilePath("Csr.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	u32 last = reader.FindAsset(sources[3].guid);
	ASSERT_EQ(last, 3u);

	const PakDepEdge* edges = nullptr;
	u32 count = 0;
	ASSERT_TRUE(reader.GetDependencies(last, edges, count));
	ASSERT_EQ(count, 2u);
	EXPECT_EQ(reader.GetAsset(edges[0].targetAsset)->guid, sources[0].guid);
	EXPECT_EQ(reader.GetAsset(edges[1].targetAsset)->guid, sources[2].guid);

	ASSERT_TRUE(reader.GetDependencies(2, edges, count));
	EXPECT_EQ(count, 0u);
}

TEST_F(CookContainerFixture, StrippingNamesRemovesSectionNotEmptyingIt) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(5, 5);
	source.typeNameHash = 0x1;
	source.name = str("project://Assets/Named.bin");
	sources.push_back(std::move(source));

	CookOps::PakWriteDesc desc;
	desc.shipDebugNames = false;

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, desc, bytes, error)) << error;
	Path path = FilePath("Stripped.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	EXPECT_EQ(reader.GetSuperblock().featuresOptional & static_cast<u64>(PakFeatureOptional::DebugNames), 0u);
	EXPECT_TRUE(reader.GetName(0).empty());
	EXPECT_EQ(reader.FindAsset(Guid(5, 5)), 0u);
}

TEST_F(CookContainerFixture, UnknownFeatureBitRefused) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(9, 9);
	source.typeNameHash = 0x1;
	sources.push_back(std::move(source));

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error)) << error;

	EditSuperblock(bytes, [](PakSuperblock& sb) {
		sb.featuresRequired |= static_cast<u64>(PakFeatureRequired::MphfIndex);
	});
	Path path = FilePath("Future.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	EXPECT_FALSE(reader.Mount(path, error));
	EXPECT_NE(error.find("MphfIndex"), str::npos) << error;

	EditSuperblock(bytes, [](PakSuperblock& sb) {
		sb.featuresRequired = 0;
		sb.featuresOptional |= 1ull << 40;
	});
	ASSERT_TRUE(WriteBytes(path, bytes));
	EXPECT_TRUE(reader.Mount(path, error)) << error;
}

TEST_F(CookContainerFixture, TruncatedPackageIsRefusedNotRead) {
	Xorshift random(3);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(3, 3);
	source.typeNameHash = 0x1;
	source.descriptor = Bytes(random, 40);
	source.payload = Bytes(random, 9000);
	sources.push_back(std::move(source));

	Vector<u8> good{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), good, error)) << error;

	Path path = FilePath("Truncated.slpak");

	{
		Vector<u8> bytes = good;
		EditSuperblock(bytes, [](PakSuperblock& sb) { sb.fileSize -= 1; });
		ASSERT_TRUE(WriteBytes(path, bytes));
		PakReader reader;
		EXPECT_FALSE(reader.Mount(path, error));
		EXPECT_FALSE(reader.IsMounted());
	}

	{
		Vector<u8> bytes = good;
		bytes.pop_back();
		ASSERT_TRUE(WriteBytes(path, bytes));
		PakReader reader;
		EXPECT_FALSE(reader.Mount(path, error));
	}

	ASSERT_TRUE(WriteBytes(path, good));
	PakReader reader;
	EXPECT_TRUE(reader.Mount(path, error)) << error;
}

TEST_F(CookContainerFixture, ChecksumCatchesCorruptSuperblock) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(4, 4);
	source.typeNameHash = 0x1;
	sources.push_back(std::move(source));

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error)) << error;

	bytes[40] ^= 0x01;
	Path path = FilePath("Corrupt.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	EXPECT_FALSE(reader.Mount(path, error));
	EXPECT_NE(error.find("crc32c"), str::npos) << error;
}

TEST_F(CookContainerFixture, ForeignEnginePackageRefused) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(6, 6);
	source.typeNameHash = 0x1;
	sources.push_back(std::move(source));

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error)) << error;
	EditSuperblock(bytes, [](PakSuperblock& sb) { sb.engineHash ^= 0xFFull; });

	Path path = FilePath("OtherCommit.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));
	PakReader reader;
	EXPECT_FALSE(reader.Mount(path, error));

	EXPECT_NE(error.find(std::format("{:016x}", BuildInfo::GetEngineHash())), str::npos) << error;
	EXPECT_NE(error.find(std::format("{:016x}", BuildInfo::GetEngineHash() ^ 0xFFull)), str::npos) << error;

	EXPECT_NE(error.find("CONFIGURE time"), str::npos) << error;

	EXPECT_EQ(error.find("rebuild the game"), str::npos) << error;
	EXPECT_EQ(error.find("rebuild the EDITOR"), str::npos) << error;
}

TEST_F(CookContainerFixture, IndexIsRangeChecked) {
	Xorshift random(5);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 0; i < 3; ++i) {
		CookOps::PakAssetSource source;
		source.guid = Guid(i + 1, 0);
		source.typeNameHash = 0x1;
		source.descriptor = Bytes(random, 8);
		sources.push_back(std::move(source));
	}

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error)) << error;

	size_t idx0At = FindSectionOffset(bytes, PAK_IDX0);
	ASSERT_NE(idx0At, 0u);
	ASSERT_LT(idx0At + sizeof(PakAssetEntry), bytes.size());

	u16 bogus = 0x7FFF;
	std::memcpy(bytes.data() + idx0At + offsetof(PakAssetEntry, typeIndex), &bogus, sizeof(bogus));
	Path path = FilePath("BadIndex.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	EXPECT_FALSE(reader.Mount(path, error));
	EXPECT_NE(error.find("type"), str::npos) << error;
}

TEST_F(CookContainerFixture, SharedGuidFailsWrite) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 0; i < 2; ++i) {
		CookOps::PakAssetSource source;
		source.guid = Guid(42, 42);
		source.typeNameHash = 0x1;
		source.name = std::format("project://Assets/Copy{}.bin", i);
		sources.push_back(std::move(source));
	}

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	EXPECT_FALSE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error));
	EXPECT_NE(error.find("Copy0.bin"), str::npos) << error;
	EXPECT_NE(error.find("Copy1.bin"), str::npos) << error;
}

TEST_F(CookContainerFixture, EditorOnlyAssetRefused) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(8, 8);
	source.typeNameHash = 0x1;
	source.name = str("project://Assets/EditorOnlyThing.bin");
	source.flags = static_cast<u16>(PakAssetFlags::EditorOnly);
	sources.push_back(std::move(source));

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	EXPECT_FALSE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error));
	EXPECT_NE(error.find("EditorOnlyThing"), str::npos) << error;
}

TEST_F(CookContainerFixture, DependencyOnSomethingOutsidePackageFailsWrite) {
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(10, 10);
	source.typeNameHash = 0x1;
	source.name = str("project://Assets/Dangling.bin");
	source.dependencies.push_back(Guid(999, 999));
	sources.push_back(std::move(source));

	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	EXPECT_FALSE(CookOps::BuildPackage(sources, CookOps::PakWriteDesc(), bytes, error));
	EXPECT_NE(error.find("Dangling.bin"), str::npos) << error;
}

TEST_F(CookContainerFixture, RegionPagesAreSectorAligned) {
	Xorshift random(6);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource source;
	source.guid = Guid(11, 11);
	source.typeNameHash = 0x1;
	source.payload = Bytes(random, 300 * 1024);
	sources.push_back(std::move(source));

	CookOps::PakWriteDesc desc;
	desc.pageSize = 128 * 1024;

	Path path = FilePath("Paged.slpak");
	str error;
	ASSERT_TRUE(CookOps::WritePackage(path, sources, desc, error)) << error;

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	const PakAssetEntry* entry = reader.GetAsset(0);
	ASSERT_NE(entry, nullptr);
	const PakRegion* region = reader.GetRegion(entry->regionIndex);
	ASSERT_NE(region, nullptr);
	EXPECT_EQ(region->pageCount, 3u);
	EXPECT_EQ(region->sizeRaw, sources[0].payload.size());

	for (u32 i = 0; i < region->pageCount; ++i) {
		const PakPage* page = reader.GetPage(region->firstPage + i);
		ASSERT_NE(page, nullptr);
		EXPECT_EQ(page->offset % PAK_SECTOR_SIZE, 0u) << "page " << i;
		EXPECT_EQ(page->sizeComp, page->sizeRaw);
	}

	Buffer payload;
	ASSERT_TRUE(reader.ReadRegion(entry->regionIndex, payload, error)) << error;
	ASSERT_EQ(payload.size, sources[0].payload.size());
	EXPECT_EQ(std::memcmp(payload.data, sources[0].payload.data(), payload.size), 0);
}

TEST_F(CookContainerFixture, StreamedRegionIsOneRun) {
	Xorshift random(8);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource streamed;
	streamed.guid = Guid(31, 1);
	streamed.typeNameHash = 0x1;
	streamed.residency = PakResidency::Streamed;
	streamed.payload.resize(300 * 1024 + 77);
	for (size_t i = 0; i < streamed.payload.size(); ++i) {
		streamed.payload[i] = static_cast<u8>((i / 61) & 0x1F);
	}
	sources.push_back(std::move(streamed));
	CookOps::PakAssetSource hot;
	hot.guid = Guid(31, 2);
	hot.typeNameHash = 0x1;
	hot.payload.resize(200 * 1024);
	for (size_t i = 0; i < hot.payload.size(); ++i) {
		hot.payload[i] = static_cast<u8>((i / 61) & 0x1F);
	}
	sources.push_back(std::move(hot));

	CookOps::PakWriteDesc desc;
	desc.compression = CookOps::CookCompression::LZ4;
	desc.pageSize = 64 * 1024;
	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, desc, bytes, error)) << error;
	const Path path = FilePath("Streamed.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	const PakAssetEntry* entry = reader.GetAsset(reader.FindAsset(sources[0].guid));
	ASSERT_NE(entry, nullptr);
	const PakRegion* region = reader.GetRegion(entry->regionIndex);
	ASSERT_NE(region, nullptr);
	EXPECT_EQ(region->residency, static_cast<u8>(PakResidency::Streamed));
	EXPECT_EQ(region->codec, static_cast<u8>(PakCodec::None));
	EXPECT_GT(region->pageCount, 3u);
	EXPECT_EQ(reader.GetPage(region->firstPage)->offset % PAK_SECTOR_SIZE, 0u);

	u64 offset = 0;
	u64 size = 0;
	ASSERT_TRUE(reader.LocateRegion(entry->regionIndex, offset, size, error)) << error;
	EXPECT_EQ(size, sources[0].payload.size());
	EXPECT_EQ(offset, reader.GetPage(region->firstPage)->offset);
	ASSERT_LE(offset + size, bytes.size());
	EXPECT_EQ(std::memcmp(bytes.data() + offset, sources[0].payload.data(), static_cast<size_t>(size)), 0);

	const PakAssetEntry* hotEntry = reader.GetAsset(reader.FindAsset(sources[1].guid));
	ASSERT_NE(hotEntry, nullptr);
	EXPECT_EQ(reader.GetRegion(hotEntry->regionIndex)->codec, static_cast<u8>(PakCodec::LZ4));
	EXPECT_FALSE(reader.LocateRegion(hotEntry->regionIndex, offset, size, error));

	const size_t pageTable = FindSectionOffset(bytes, PAK_PAGE);
	const size_t second = pageTable + static_cast<size_t>(region->firstPage + 1) * sizeof(PakPage);
	for (u32 edit = 0; edit < 2; ++edit) {
		Vector<u8> hostile = bytes;
		PakPage page{};
		std::memcpy(&page, hostile.data() + second, sizeof(PakPage));
		if (edit == 0) {
			page.offset += 8;
		} else {
			page.sizeComp -= 8;
		}
		std::memcpy(hostile.data() + second, &page, sizeof(PakPage));
		PakReader memory;
		ASSERT_TRUE(memory.MountFromMemory(hostile.data(), hostile.size(), "Hostile.slpak", error)) << error;
		EXPECT_FALSE(memory.LocateRegion(entry->regionIndex, offset, size, error)) << (edit == 0 ? "a split region" : "a compressed page") << " was located";
	}
}

TEST_F(CookContainerFixture, MountHoldsTocReadsRegions) {
	Xorshift random(7);
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource noise;
	noise.guid = Guid(21, 1);
	noise.typeNameHash = 0x1;
	noise.payload = Bytes(random, 700 * 1024);
	sources.push_back(std::move(noise));
	CookOps::PakAssetSource runs;
	runs.guid = Guid(21, 2);
	runs.typeNameHash = 0x1;
	runs.payload.resize(500 * 1024);
	for (size_t i = 0; i < runs.payload.size(); ++i) {
		runs.payload[i] = static_cast<u8>((i / 97) & 0x3F);
	}
	sources.push_back(std::move(runs));

	for (CookOps::CookCompression compression : {CookOps::CookCompression::None, CookOps::CookCompression::LZ4}) {
		CookOps::PakWriteDesc desc;
		desc.compression = compression;
		Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
		str error;
		ASSERT_TRUE(CookOps::BuildPackage(sources, desc, bytes, error)) << error;
		const Path path = FilePath("Resident.slpak");
		ASSERT_TRUE(WriteBytes(path, bytes));

		PakSuperblock sb{};
		std::memcpy(&sb, bytes.data(), sizeof(PakSuperblock));
		{
			PakReader reader;
			ASSERT_TRUE(reader.Mount(path, error)) << error;
			EXPECT_EQ(reader.GetResidentSize(), sb.tocOffset + sb.tocSizeRaw);
			EXPECT_LE(reader.GetResidentSize(), sb.payloadOffset);

			Buffer replacement(64, MemTag::Editor);
			EXPECT_FALSE(FileSystem::WriteFile(path, replacement));

			for (u32 index = 0; index < 2; ++index) {
				const u32 asset = reader.FindAsset(sources[index].guid);
				ASSERT_NE(asset, PAK_INVALID_INDEX);
				const PakAssetEntry* entry = reader.GetAsset(asset);
				const PakRegion* region = reader.GetRegion(entry->regionIndex);
				ASSERT_NE(region, nullptr);
				if (compression == CookOps::CookCompression::LZ4) {
					EXPECT_EQ(region->codec, index == 0 ? static_cast<u8>(PakCodec::None) : static_cast<u8>(PakCodec::LZ4));
				}
				Buffer payload;
				ASSERT_TRUE(reader.ReadRegion(entry->regionIndex, payload, error)) << error;
				ASSERT_EQ(payload.size, sources[index].payload.size());
				EXPECT_EQ(std::memcmp(payload.data, sources[index].payload.data(), payload.size), 0) << "asset " << index;
			}
		}

		PakReader memory;
		ASSERT_TRUE(memory.MountFromMemory(bytes.data(), bytes.size(), "Resident.slpak", error)) << error;
		EXPECT_EQ(memory.GetResidentSize(), bytes.size());
		for (u32 index = 0; index < 2; ++index) {
			const PakAssetEntry* entry = memory.GetAsset(memory.FindAsset(sources[index].guid));
			ASSERT_NE(entry, nullptr);
			Buffer payload;
			ASSERT_TRUE(memory.ReadRegion(entry->regionIndex, payload, error)) << error;
			ASSERT_EQ(payload.size, sources[index].payload.size());
			EXPECT_EQ(std::memcmp(payload.data, sources[index].payload.data(), payload.size), 0);
		}
	}
}

namespace {

	Vector<CookOps::PakAssetSource> OneAsset() {
		Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
		CookOps::PakAssetSource source;
		source.guid = Guid(12, 12);
		source.typeNameHash = 0x1;
		sources.push_back(std::move(source));
		return sources;
	}

	constexpr std::string_view MANIFEST = "{\"formatVersion\": 2, \"productName\": \"Docs\"}";
	constexpr std::string_view SETTINGS = "[Graphics]\nvsync = Off\n";

} // anonymous namespace

TEST_F(CookContainerFixture, DocumentsReadBackByKind) {
	CookOps::PakWriteDesc desc;
	desc.documents.push_back({PakDocumentKind::ProjectSettings, str(SETTINGS)});
	desc.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});
	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(OneAsset(), desc, bytes, error)) << error;
	Path path = FilePath("Documents.slpak");
	ASSERT_TRUE(WriteBytes(path, bytes));

	PakReader reader;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	EXPECT_NE(reader.GetSuperblock().featuresOptional & static_cast<u64>(PakFeatureOptional::Documents), 0u);
	EXPECT_EQ(reader.GetDocument(PakDocumentKind::BootManifest), MANIFEST);
	EXPECT_EQ(reader.GetDocument(PakDocumentKind::ProjectSettings), SETTINGS);
	EXPECT_EQ(reader.FindAsset(Guid(12, 12)), 0u);

	CookOps::PakWriteDesc manifestOnly;
	manifestOnly.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});
	ASSERT_TRUE(CookOps::BuildPackage(OneAsset(), manifestOnly, bytes, error)) << error;
	PakReader second;
	ASSERT_TRUE(second.MountFromMemory(bytes.data(), bytes.size(), "ManifestOnly.slpak", error)) << error;
	EXPECT_EQ(second.GetDocument(PakDocumentKind::BootManifest), MANIFEST);
	EXPECT_TRUE(second.GetDocument(PakDocumentKind::ProjectSettings).empty());

	ASSERT_TRUE(CookOps::BuildPackage(OneAsset(), CookOps::PakWriteDesc(), bytes, error)) << error;
	EXPECT_EQ(FindSectionOffset(bytes, PAK_DOCS), 0u);
	PakReader none;
	ASSERT_TRUE(none.MountFromMemory(bytes.data(), bytes.size(), "None.slpak", error)) << error;
	EXPECT_EQ(none.GetSuperblock().featuresOptional & static_cast<u64>(PakFeatureOptional::Documents), 0u);
	EXPECT_TRUE(none.GetDocument(PakDocumentKind::BootManifest).empty());
}

TEST_F(CookContainerFixture, DocumentOrderDoesNotReachFile) {
	CookOps::PakWriteDesc forward;
	forward.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});
	forward.documents.push_back({PakDocumentKind::ProjectSettings, str(SETTINGS)});
	CookOps::PakWriteDesc backward;
	backward.documents.push_back({PakDocumentKind::ProjectSettings, str(SETTINGS)});
	backward.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});

	Vector<u8> a{GetMemoryResource(MemTag::Editor)};
	Vector<u8> b{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(OneAsset(), forward, a, error)) << error;
	ASSERT_TRUE(CookOps::BuildPackage(OneAsset(), backward, b, error)) << error;
	EXPECT_TRUE(a == b);
}

TEST_F(CookContainerFixture, WriterRefusesBadDocuments) {
	Vector<u8> bytes{GetMemoryResource(MemTag::Editor)};
	str error;

	CookOps::PakWriteDesc twice;
	twice.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});
	twice.documents.push_back({PakDocumentKind::BootManifest, str("{}")});
	EXPECT_FALSE(CookOps::BuildPackage(OneAsset(), twice, bytes, error));
	EXPECT_NE(error.find("two documents"), str::npos) << error;

	CookOps::PakWriteDesc empty;
	empty.documents.push_back({PakDocumentKind::ProjectSettings, str()});
	EXPECT_FALSE(CookOps::BuildPackage(OneAsset(), empty, bytes, error));

	CookOps::PakWriteDesc unknown;
	unknown.documents.push_back({static_cast<PakDocumentKind>(99), str("?")});
	EXPECT_FALSE(CookOps::BuildPackage(OneAsset(), unknown, bytes, error));
	EXPECT_NE(error.find("99"), str::npos) << error;

	CookOps::PakWriteDesc fine;
	fine.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});
	EXPECT_TRUE(CookOps::BuildPackage(OneAsset(), fine, bytes, error)) << error;
}

namespace {

	template <typename Fn>
	void EditDocumentEntry(Vector<u8>& bytes, size_t docsAt, u32 index, Fn&& edit) {
		PakDocumentEntry entry{};
		std::memcpy(&entry, bytes.data() + docsAt + index * sizeof(PakDocumentEntry), sizeof(entry));
		edit(entry);
		std::memcpy(bytes.data() + docsAt + index * sizeof(PakDocumentEntry), &entry, sizeof(entry));
	}

} // anonymous namespace

TEST_F(CookContainerFixture, MountRefusesBadDocument) {
	CookOps::PakWriteDesc desc;
	desc.documents.push_back({PakDocumentKind::BootManifest, str(MANIFEST)});
	desc.documents.push_back({PakDocumentKind::ProjectSettings, str(SETTINGS)});
	Vector<u8> good{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(OneAsset(), desc, good, error)) << error;
	const size_t docsAt = FindSectionOffset(good, PAK_DOCS);
	ASSERT_NE(docsAt, 0u);

	auto mounts = [&error](const Vector<u8>& bytes) {
		PakReader reader;
		return reader.MountFromMemory(bytes.data(), bytes.size(), "Hostile.slpak", error);
	};

	{
		Vector<u8> bytes = good;
		EditDocumentEntry(bytes, docsAt, 1, [](PakDocumentEntry& entry) { entry.size = 0x7FFFFFFF; });
		EXPECT_FALSE(mounts(bytes));
		EXPECT_NE(error.find("DOCS"), str::npos) << error;
	}
	{
		Vector<u8> bytes = good;
		EditDocumentEntry(bytes, docsAt, 0, [](PakDocumentEntry& entry) { entry.offset = 0; });
		EXPECT_FALSE(mounts(bytes));
	}
	{
		Vector<u8> bytes = good;
		EditDocumentEntry(bytes, docsAt, 1, [](PakDocumentEntry& entry) {
			entry.kind = static_cast<u32>(PakDocumentKind::BootManifest);
		});
		EXPECT_FALSE(mounts(bytes));
		EXPECT_NE(error.find("ascending"), str::npos) << error;
	}
	{
		Vector<u8> bytes = good;
		EditDocumentEntry(bytes, docsAt, 1, [](PakDocumentEntry& entry) { entry.kind = 77; });
		PakReader reader;
		ASSERT_TRUE(reader.MountFromMemory(bytes.data(), bytes.size(), "Newer.slpak", error)) << error;
		EXPECT_EQ(reader.GetDocument(PakDocumentKind::BootManifest), MANIFEST);
		EXPECT_TRUE(reader.GetDocument(PakDocumentKind::ProjectSettings).empty());
	}

	EXPECT_TRUE(mounts(good)) << error;
}
