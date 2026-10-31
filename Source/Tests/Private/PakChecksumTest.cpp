#include "TestsPCH.h"

#include "Shalltear/Core/Asset/PakFormat.h"

#include "Asset/ImportArtifact.h"

#include <string_view>
#include <vector>

using namespace Shalltear;

namespace {

	u32 Crc32cBitwiseOracle(const void* data, size_t size, u32 seed = 0) {
		const u8* bytes = static_cast<const u8*>(data);
		u32 crc = ~seed;
		for (size_t i = 0; i < size; ++i) {
			crc ^= bytes[i];
			for (int bit = 0; bit < 8; ++bit) {
				crc = (crc >> 1) ^ (0x82F63B78u & (0u - (crc & 1u)));
			}
		}
		return ~crc;
	}

	class Xorshift {
	public:
		explicit Xorshift(u64 seed) : _state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

		u64 Next() {
			_state ^= _state >> 12;
			_state ^= _state << 25;
			_state ^= _state >> 27;
			return _state * 0x2545F4914F6CDD1Dull;
		}
	private:
		u64 _state;
	};

	std::vector<u8> RandomBytes(size_t count, u64 seed) {
		Xorshift rng(seed);
		std::vector<u8> out(count);
		for (u8& b : out) {
			b = static_cast<u8>(rng.Next() >> 24);
		}
		return out;
	}

} // namespace

TEST(PakChecksumTest, MatchesPublishedCheckValues) {
	EXPECT_EQ(PakCrc32c("", 0), 0x00000000u);
	EXPECT_EQ(PakCrc32c("a", 1), 0xC1D04330u);
	EXPECT_EQ(PakCrc32c("123456789", 9), 0xE3069283u);

	constexpr std::string_view PANGRAM = "The quick brown fox jumps over the lazy dog";
	EXPECT_EQ(PakCrc32c(PANGRAM.data(), PANGRAM.size()), 0x22620404u);
}

TEST(PakChecksumTest, SeedChainsHalves) {
	const std::vector<u8> bytes = RandomBytes(1000, 0xC0FFEEull);
	for (size_t split : {size_t(0), size_t(1), size_t(7), size_t(8), size_t(9), size_t(499), size_t(999), size_t(1000)}) {
		const u32 whole = PakCrc32c(bytes.data(), bytes.size());
		const u32 chained = PakCrc32c(bytes.data() + split, bytes.size() - split, PakCrc32c(bytes.data(), split));
		EXPECT_EQ(chained, whole) << "split at " << split;
	}
}

TEST(PakChecksumTest, MatchesBitLoopUpTo64) {
	const std::vector<u8> bytes = RandomBytes(64 + 16, 0x5EED1234ull);
	for (size_t offset = 0; offset < 16; ++offset) {
		for (size_t size = 0; size <= 64; ++size) {
			const u8* at = bytes.data() + offset;
			EXPECT_EQ(PakCrc32c(at, size), Crc32cBitwiseOracle(at, size))
				<< "offset " << offset << " size " << size;
			EXPECT_EQ(PakCrc32c(at, size, 0xDEADBEEFu), Crc32cBitwiseOracle(at, size, 0xDEADBEEFu))
				<< "seeded, offset " << offset << " size " << size;
		}
	}
}

TEST(PakChecksumTest, MatchesBitLoopOnRandom) {
	Xorshift rng(0xABCDEF01ull);
	for (int trial = 0; trial < 200; ++trial) {
		const size_t size = static_cast<size_t>(rng.Next() % 8192u);
		const size_t offset = static_cast<size_t>(rng.Next() % 16u);
		const std::vector<u8> bytes = RandomBytes(size + offset, 0x1000ull + static_cast<u64>(trial));
		const u8* at = bytes.data() + offset;
		EXPECT_EQ(PakCrc32c(at, size), Crc32cBitwiseOracle(at, size)) << "trial " << trial << " size " << size;
	}
}

TEST(PakChecksumTest, SuperblockSpanAgreesAndTailIsExercised) {
	static_assert(4092 % 8 == 4, "the superblock span ends in a partial word (the tail path)");

	const std::vector<u8> bytes = RandomBytes(4096, 0x5B10Cull);
	EXPECT_EQ(PakCrc32c(bytes.data(), 4092), Crc32cBitwiseOracle(bytes.data(), 4092));

	for (size_t at : {size_t(0), size_t(60), size_t(4088), size_t(4091)}) {
		std::vector<u8> tampered = bytes;
		tampered[at] ^= 0x01u;
		EXPECT_NE(PakCrc32c(tampered.data(), 4092), PakCrc32c(bytes.data(), 4092)) << "flipped byte " << at;
	}
}

TEST(PakChecksumTest, BodySpanMatchesAnyAlignment) {
	for (size_t total : {size_t(113), size_t(119), size_t(120), size_t(4096), size_t(65537)}) {
		const std::vector<u8> file = RandomBytes(total, 0xA27FACull + total);
		for (size_t skew = 0; skew < 8; ++skew) {
			if (total <= IMPORT_ARTIFACT_HEADER_SIZE + skew) {
				continue;
			}
			const u8* body = file.data() + IMPORT_ARTIFACT_HEADER_SIZE + skew;
			const size_t span = total - IMPORT_ARTIFACT_HEADER_SIZE - skew;
			EXPECT_EQ(PakCrc32c(body, span), Crc32cBitwiseOracle(body, span))
				<< "total " << total << " skew " << skew;
		}
	}
}
