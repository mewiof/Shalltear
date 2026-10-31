#include "TestsPCH.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"

#include "Shalltear/Editor/CookOps.h"

#include "Asset/LZ4Decode.h"
#include "Asset/PakReader.h"

#include "FuzzHarness.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;

namespace {

	using Fuzz::GuardedInput;
	using Fuzz::Rng;

	class CookCompressionFixture : public ::testing::Test {
	protected:
		void SetUp() override {
			_dir = std::filesystem::temp_directory_path() / "shalltear_pak_codec_test";
			std::error_code ec;
			std::filesystem::create_directories(_dir, ec);
		}

		void TearDown() override {
			std::error_code ec;
			std::filesystem::remove_all(_dir, ec);
		}

		Path FilePath(const char* name) const { return Path((_dir / name).generic_string()); }

		static Vector<u8> Compressible(size_t size, u64 seed) {
			Vector<u8> out{GetMemoryResource(MemTag::Editor)};
			out.resize(size);
			u32 word = static_cast<u32>(seed);
			for (size_t i = 0; i + 4 <= size; i += 4) {
				if ((i / 4) % 7 == 0) {
					word = word * 1103515245u + 12345u;
				}
				std::memcpy(out.data() + i, &word, 4);
			}
			return out;
		}

		static Vector<u8> Incompressible(size_t size, u64 seed) {
			Vector<u8> out{GetMemoryResource(MemTag::Editor)};
			out.resize(size);
			u64 state = seed ? seed : 1;
			for (size_t i = 0; i < size; ++i) {
				state ^= state >> 12;
				state ^= state << 25;
				state ^= state >> 27;
				out[i] = static_cast<u8>(state * 0x2545F4914F6CDD1Dull >> 56);
			}
			return out;
		}

		static CookOps::PakAssetSource OneAsset(u64 guid, Vector<u8> payload) {
			CookOps::PakAssetSource source;
			source.guid = Guid(guid, guid);
			source.typeNameHash = 0x1;
			source.payload = std::move(payload);
			return source;
		}

		void BuildBothWays(const Vector<u8>& payload, CookOps::CookCompression how, Vector<u8>& stored,
						   Vector<u8>& compressed, u32 pageSize = PAK_DEFAULT_PAGE_SIZE) const {
			Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
			sources.push_back(OneAsset(7, payload));

			CookOps::PakWriteDesc desc;
			desc.pageSize = pageSize;
			str error;
			desc.compression = CookOps::CookCompression::None;
			ASSERT_TRUE(CookOps::BuildPackage(sources, desc, stored, error)) << error;
			desc.compression = how;
			ASSERT_TRUE(CookOps::BuildPackage(sources, desc, compressed, error)) << error;
		}

		bool WriteBytes(const Path& path, const Vector<u8>& bytes) const {
			Buffer buffer(bytes.size(), MemTag::Editor);
			std::memcpy(buffer.data, bytes.data(), bytes.size());
			return FileSystem::WriteFile(path, buffer);
		}

		static size_t PageEntryOffset(const Vector<u8>& bytes, u32 pageIndex) {
			PakSuperblock sb{};
			std::memcpy(&sb, bytes.data(), sizeof(PakSuperblock));
			for (u16 i = 0; i < sb.sectionCount; ++i) {
				PakSection section{};
				std::memcpy(&section, bytes.data() + sb.tocOffset + i * sizeof(PakSection), sizeof(PakSection));
				if (section.fourcc == PAK_PAGE) {
					return static_cast<size_t>(sb.tocOffset + section.offset) + pageIndex * sizeof(PakPage);
				}
			}
			return 0;
		}

		static size_t RegionEntryOffset(const Vector<u8>& bytes, u32 regionIndex) {
			PakSuperblock sb{};
			std::memcpy(&sb, bytes.data(), sizeof(PakSuperblock));
			for (u16 i = 0; i < sb.sectionCount; ++i) {
				PakSection section{};
				std::memcpy(&section, bytes.data() + sb.tocOffset + i * sizeof(PakSection), sizeof(PakSection));
				if (section.fourcc == PAK_PLDM) {
					return static_cast<size_t>(sb.tocOffset + section.offset) + regionIndex * sizeof(PakRegion);
				}
			}
			return 0;
		}

		std::filesystem::path _dir;
	};

} // anonymous namespace

TEST_F(CookCompressionFixture, CompressedRegionRoundTrips) {
	for (CookOps::CookCompression how : {CookOps::CookCompression::LZ4, CookOps::CookCompression::LZ4HC}) {
		Vector<u8> payload = Compressible(300 * 1024, 99);
		Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
		Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
		BuildBothWays(payload, how, stored, compressed);

		ASSERT_LT(compressed.size(), stored.size());

		Path path = FilePath("Codec.slpak");
		ASSERT_TRUE(WriteBytes(path, compressed));
		PakReader reader;
		str error;
		ASSERT_TRUE(reader.Mount(path, error)) << error;

		const PakAssetEntry* entry = reader.GetAsset(0);
		ASSERT_NE(entry, nullptr);
		const PakRegion* region = reader.GetRegion(entry->regionIndex);
		ASSERT_NE(region, nullptr);
		EXPECT_EQ(region->codec, static_cast<u8>(PakCodec::LZ4));
		EXPECT_EQ(region->sizeRaw, payload.size());
		EXPECT_EQ(region->pageCount, 3u);

		for (u32 i = 0; i < region->pageCount; ++i) {
			const PakPage* page = reader.GetPage(region->firstPage + i);
			ASSERT_NE(page, nullptr);
			EXPECT_EQ(page->offset % PAK_SECTOR_SIZE, 0u) << "page " << i;
			EXPECT_LT(page->sizeComp, page->sizeRaw) << "page " << i;
		}

		Buffer out;
		ASSERT_TRUE(reader.ReadRegion(entry->regionIndex, out, error)) << error;
		ASSERT_EQ(out.size, payload.size());
		EXPECT_EQ(std::memcmp(out.data, payload.data(), out.size), 0);
	}
}

TEST_F(CookCompressionFixture, HighCompressionNeverLarger) {
	Vector<u8> payload = Compressible(200 * 1024, 5);
	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> fast{GetMemoryResource(MemTag::Editor)};
	Vector<u8> high{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4, stored, fast);
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, high);

	EXPECT_LE(high.size(), fast.size());

	for (const Vector<u8>* which : {&fast, &high}) {
		Path path = FilePath("Effort.slpak");
		ASSERT_TRUE(WriteBytes(path, *which));
		PakReader reader;
		str error;
		ASSERT_TRUE(reader.Mount(path, error)) << error;
		Buffer out;
		ASSERT_TRUE(reader.ReadRegion(0, out, error)) << error;
		ASSERT_EQ(out.size, payload.size());
		EXPECT_EQ(std::memcmp(out.data, payload.data(), out.size), 0);
	}
}

TEST_F(CookCompressionFixture, IncompressibleContentNeverInflates) {
	Vector<u8> payload = Incompressible(300 * 1024, 1234);
	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, compressed);

	EXPECT_EQ(compressed.size(), stored.size());
	EXPECT_EQ(compressed, stored);

	Path path = FilePath("Stored.slpak");
	ASSERT_TRUE(WriteBytes(path, compressed));
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	const PakRegion* region = reader.GetRegion(0);
	ASSERT_NE(region, nullptr);
	EXPECT_EQ(region->codec, static_cast<u8>(PakCodec::None));

	Buffer out;
	ASSERT_TRUE(reader.ReadRegion(0, out, error)) << error;
	ASSERT_EQ(out.size, payload.size());
	EXPECT_EQ(std::memcmp(out.data, payload.data(), out.size), 0);
}

TEST_F(CookCompressionFixture, OnePageStoresWhileItsNeighbourCompresses) {
	Vector<u8> payload = Compressible(128 * 1024, 3);
	Vector<u8> noise = Incompressible(128 * 1024, 77);
	payload.insert(payload.end(), noise.begin(), noise.end());

	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, compressed);

	Path path = FilePath("Mixed.slpak");
	ASSERT_TRUE(WriteBytes(path, compressed));
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	const PakRegion* region = reader.GetRegion(0);
	ASSERT_NE(region, nullptr);
	ASSERT_EQ(region->pageCount, 2u);
	EXPECT_EQ(region->codec, static_cast<u8>(PakCodec::LZ4));

	const PakPage* first = reader.GetPage(region->firstPage);
	const PakPage* second = reader.GetPage(region->firstPage + 1);
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	EXPECT_LT(first->sizeComp, first->sizeRaw);
	EXPECT_EQ(second->sizeComp, second->sizeRaw);

	Buffer out;
	ASSERT_TRUE(reader.ReadRegion(0, out, error)) << error;
	ASSERT_EQ(out.size, payload.size());
	EXPECT_EQ(std::memcmp(out.data, payload.data(), out.size), 0);
}

TEST_F(CookCompressionFixture, OversizedDecodeRefusedEarly) {
	Vector<u8> payload = Compressible(64 * 1024, 8);
	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, compressed);

	PakPage page{};
	size_t pageAt = PageEntryOffset(compressed, 0);
	ASSERT_NE(pageAt, 0u);
	std::memcpy(&page, compressed.data() + pageAt, sizeof(PakPage));
	const u32 honest = page.sizeRaw;
	page.sizeRaw = 0xF0000000u;
	std::memcpy(compressed.data() + pageAt, &page, sizeof(PakPage));

	PakRegion region{};
	size_t regionAt = RegionEntryOffset(compressed, 0);
	ASSERT_NE(regionAt, 0u);
	std::memcpy(&region, compressed.data() + regionAt, sizeof(PakRegion));
	region.sizeRaw += page.sizeRaw - honest;
	std::memcpy(compressed.data() + regionAt, &region, sizeof(PakRegion));

	Path path = FilePath("Bomb.slpak");
	ASSERT_TRUE(WriteBytes(path, compressed));
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(path, error)) << error;

	Buffer out;
	EXPECT_FALSE(reader.ReadRegion(0, out, error));
	EXPECT_NE(error.find("more than the codec can produce"), str::npos) << error;
}

TEST_F(CookCompressionFixture, PageBiggerCompressedThanRawIsRefused) {
	Vector<u8> payload = Compressible(32 * 1024, 9);
	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, compressed, 8 * 1024);

	PakPage page{};
	size_t pageAt = PageEntryOffset(compressed, 0);
	std::memcpy(&page, compressed.data() + pageAt, sizeof(PakPage));
	ASSERT_LT(page.sizeComp, page.sizeRaw);
	page.sizeComp = page.sizeRaw + 1;
	std::memcpy(compressed.data() + pageAt, &page, sizeof(PakPage));

	Path path = FilePath("Backwards.slpak");
	ASSERT_TRUE(WriteBytes(path, compressed));
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	Buffer out;
	EXPECT_FALSE(reader.ReadRegion(0, out, error));
	EXPECT_NE(error.find("compressed bytes for"), str::npos) << error;
}

TEST_F(CookCompressionFixture, UnregisteredCodecRefused) {
	Vector<u8> payload = Compressible(8 * 1024, 11);
	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, compressed);

	PakRegion region{};
	size_t regionAt = RegionEntryOffset(compressed, 0);
	std::memcpy(&region, compressed.data() + regionAt, sizeof(PakRegion));
	region.codec = 7;
	std::memcpy(compressed.data() + regionAt, &region, sizeof(PakRegion));

	Path path = FilePath("Future.slpak");
	ASSERT_TRUE(WriteBytes(path, compressed));
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	Buffer out;
	EXPECT_FALSE(reader.ReadRegion(0, out, error));
	EXPECT_NE(error.find("codec 7"), str::npos) << error;
}

TEST_F(CookCompressionFixture, UncompressedPageMismatchRefused) {
	Vector<u8> payload = Compressible(8 * 1024, 12);
	Vector<u8> stored{GetMemoryResource(MemTag::Editor)};
	Vector<u8> compressed{GetMemoryResource(MemTag::Editor)};
	BuildBothWays(payload, CookOps::CookCompression::LZ4HC, stored, compressed);

	PakRegion region{};
	size_t regionAt = RegionEntryOffset(compressed, 0);
	std::memcpy(&region, compressed.data() + regionAt, sizeof(PakRegion));
	region.codec = static_cast<u8>(PakCodec::None);
	std::memcpy(compressed.data() + regionAt, &region, sizeof(PakRegion));

	Path path = FilePath("Lying.slpak");
	ASSERT_TRUE(WriteBytes(path, compressed));
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.Mount(path, error)) << error;
	Buffer out;
	EXPECT_FALSE(reader.ReadRegion(0, out, error));
	EXPECT_NE(error.find("codec None"), str::npos) << error;
}

TEST(CookCompressionFuzz, DecoderRefusesMutatedStreams) {
	GuardedInput input(64 * 1024);
	ASSERT_TRUE(input.IsValid());

	Vector<u8> source{GetMemoryResource(MemTag::Editor)};
	source.resize(16 * 1024);
	for (size_t i = 0; i < source.size(); ++i) {
		source[i] = static_cast<u8>((i / 4) % 251);
	}
	Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};
	CookOps::PakAssetSource one;
	one.guid = Guid(1, 1);
	one.typeNameHash = 0x1;
	one.payload = source;
	sources.push_back(std::move(one));
	CookOps::PakWriteDesc desc;
	desc.compression = CookOps::CookCompression::LZ4HC;
	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(CookOps::BuildPackage(sources, desc, package, error)) << error;

	PakSuperblock sb{};
	std::memcpy(&sb, package.data(), sizeof(PakSuperblock));
	size_t pageAt = 0;
	for (u16 i = 0; i < sb.sectionCount; ++i) {
		PakSection section{};
		std::memcpy(&section, package.data() + sb.tocOffset + i * sizeof(PakSection), sizeof(PakSection));
		if (section.fourcc == PAK_PAGE) {
			pageAt = static_cast<size_t>(sb.tocOffset + section.offset);
		}
	}
	ASSERT_NE(pageAt, 0u);
	PakPage page{};
	std::memcpy(&page, package.data() + pageAt, sizeof(PakPage));
	ASSERT_LT(page.sizeComp, page.sizeRaw);

	Vector<u8> block{GetMemoryResource(MemTag::Editor)};
	block.assign(package.begin() + static_cast<std::ptrdiff_t>(page.offset),
				 package.begin() + static_cast<std::ptrdiff_t>(page.offset) + page.sizeComp);

	Vector<u8> out{GetMemoryResource(MemTag::Editor)};
	out.resize(page.sizeRaw);

	const u8* placed = input.Place(block.data(), block.size());
	ASSERT_TRUE(LZ4::Decompress(placed, block.size(), out.data(), out.size()));
	EXPECT_EQ(std::memcmp(out.data(), source.data(), out.size()), 0);

	Rng random(0xC0DEC);
	for (int round = 0; round < 3000; ++round) {
		Vector<u8> mutated = block;
		u32 edits = 1 + random.Below(4);
		for (u32 e = 0; e < edits; ++e) {
			mutated[random.Below(static_cast<u32>(mutated.size()))] = random.Byte();
		}
		if ((round % 3) == 0) {
			mutated.resize(1 + random.Below(static_cast<u32>(mutated.size())));
		}
		const u8* at = input.Place(mutated.data(), mutated.size());

		size_t room = (round % 3 == 1) ? out.size() / 2 : out.size();
		bool ok = LZ4::Decompress(at, mutated.size(), out.data(), room);
		if (ok && room == out.size() && mutated == block) {
			EXPECT_EQ(std::memcmp(out.data(), source.data(), out.size()), 0);
		}
		(void)ok;
	}
}
