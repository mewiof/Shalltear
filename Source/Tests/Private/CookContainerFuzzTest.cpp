#include "TestsPCH.h"

#include "Shalltear/Core/Asset/PakFormat.h"

#include "Shalltear/Editor/CookOps.h"

#include "Asset/PakReader.h"

#include "FuzzHarness.h"

using namespace Shalltear;

namespace {

	using Fuzz::GuardedInput;
	using Fuzz::Rng;

	void BuildCorpus(Vector<u8>& out) {
		Rng rng(0x5A17EDC0FFEEull);
		Vector<CookOps::PakAssetSource> sources{GetMemoryResource(MemTag::Editor)};

		auto fill = [&rng](Vector<u8>& bytes, size_t size) {
			bytes.resize(size);
			for (size_t i = 0; i < size; ++i) {
				bytes[i] = rng.Byte();
			}
		};

		auto add = [&](u64 high, u64 low, u64 typeHash, size_t descBytes, size_t payloadBytes, const char* name) {
			CookOps::PakAssetSource source;
			source.guid = Guid(high, low);
			source.typeNameHash = typeHash;
			source.recordVersion = 1;
			source.layoutHash = 0xABC000ull + typeHash;
			source.name = name;
			fill(source.descriptor, descBytes);
			fill(source.payload, payloadBytes);
			source.payloadAlignment = 16;
			source.residency = PakResidency::Hot;
			sources.push_back(std::move(source));
		};

		add(0x4444000000000000ull, 0x4ull, 0x3000, 64, 200, "project://Assets/D.bin");
		add(0x1111000000000000ull, 0x1ull, 0x1000, 40, 0, "project://Assets/A.bin");
		add(0x3333000000000000ull, 0x3ull, 0x2000, 8, 5000, "project://Assets/C.bin");
		add(0x2222000000000000ull, 0x2ull, 0x1000, 24, 100, "project://Assets/B.bin");

		sources[0].dependencies.push_back(sources[1].guid);
		sources[0].dependencies.push_back(sources[2].guid);
		sources[2].dependencies.push_back(sources[3].guid);

		CookOps::PakWriteDesc desc;
		desc.shipDebugNames = true;
		desc.pageSize = PAK_SECTOR_SIZE;
		desc.strings.push_back(str("_BaseColor"));
		desc.strings.push_back(str("_Metallic"));
		desc.strings.push_back(str(""));

		str error;
		ASSERT_TRUE(CookOps::BuildPackage(sources, desc, out, error)) << error;
		ASSERT_FALSE(out.empty());
	}

	void RepairChecksum(Vector<u8>& bytes) {
		u32 crc = PakCrc32c(bytes.data(), 4092);
		std::memcpy(bytes.data() + offsetof(PakSuperblock, crc32c), &crc, sizeof(u32));
	}

	void SetFileSize(Vector<u8>& bytes, u64 size) {
		std::memcpy(bytes.data() + offsetof(PakSuperblock, fileSize), &size, sizeof(u64));
	}

	bool Inside(const void* data, size_t size, const u8* base, size_t total) {
		if (size == 0) {
			return true;
		}
		uintptr_t at = reinterpret_cast<uintptr_t>(data);
		uintptr_t low = reinterpret_cast<uintptr_t>(base);
		return at >= low && at - low <= total && size <= total - (at - low);
	}

	void Touch(const void* data, size_t size) {
		volatile u8 sink = 0;
		const u8* bytes = static_cast<const u8*>(data);
		for (size_t i = 0; i < size; ++i) {
			sink = static_cast<u8>(sink + bytes[i]);
		}
		(void)sink;
	}

	void ExpectEveryViewInside(const PakReader& reader, const u8* base, size_t size) {
		for (u32 i = 0; i < reader.GetAssetCount(); ++i) {
			const PakAssetEntry* entry = reader.GetAsset(i);
			ASSERT_NE(entry, nullptr) << "asset " << i << " of " << reader.GetAssetCount();

			ASSERT_EQ(reader.FindAsset(entry->guid), i) << "asset " << i;

			CookReader descriptor = reader.GetDescriptor(i);
			size_t descriptorSize = descriptor.Size();
			const u8* descriptorBytes = descriptor.ReadSpan(descriptorSize);
			if (descriptorSize > 0) {
				ASSERT_NE(descriptorBytes, nullptr);
				ASSERT_TRUE(Inside(descriptorBytes, descriptorSize, base, size)) << "asset " << i << ' ' << descriptorSize;
				Touch(descriptorBytes, descriptorSize);
			}

			std::string_view name = reader.GetName(i);
			ASSERT_TRUE(Inside(name.data(), name.size(), base, size)) << "asset " << i << ' ' << name.size();
			Touch(name.data(), name.size());

			const PakDepEdge* edges = nullptr;
			u32 edgeCount = 0;
			if (reader.GetDependencies(i, edges, edgeCount)) {
				ASSERT_TRUE(Inside(edges, edgeCount * sizeof(PakDepEdge), base, size)) << "asset " << i << "'s " << edgeCount;
				for (u32 e = 0; e < edgeCount; ++e) {
					ASSERT_LT(edges[e].targetAsset, reader.GetAssetCount());
				}
			}

			if (entry->regionIndex != PAK_NO_REGION) {
				const PakRegion* region = reader.GetRegion(entry->regionIndex);
				ASSERT_NE(region, nullptr) << "asset " << i << " names region " << entry->regionIndex;

				Buffer payload;
				str error;
				if (reader.ReadRegion(entry->regionIndex, payload, error)) {
					ASSERT_EQ(payload.size, region->sizeRaw);
					Touch(payload.data, payload.size);
				} else {
					ASSERT_FALSE(error.empty()) << "region " << entry->regionIndex;
				}
			}
		}

		for (u32 i = 0; i < reader.GetStringCount(); ++i) {
			std::string_view text = reader.GetString(i);
			ASSERT_TRUE(Inside(text.data(), text.size(), base, size)) << "interned string " << i << " is " << text.size();
			Touch(text.data(), text.size());
		}

		u32 missing = reader.FindAsset(Guid(0xDEADBEEFDEADBEEFull, 0xFEEDFACEFEEDFACEull));
		ASSERT_TRUE(missing == PAK_INVALID_INDEX || missing < reader.GetAssetCount());
	}

	void ExpectRefusedOrCoherent(const u8* placed, size_t size, bool& outAccepted) {
		PakReader reader;
		str error;
		outAccepted = reader.MountFromMemory(placed, size, "fuzz://package.slpak", error);
		if (!outAccepted) {
			ASSERT_FALSE(error.empty());
			ASSERT_FALSE(reader.IsMounted());
			return;
		}
		ASSERT_NO_FATAL_FAILURE(ExpectEveryViewInside(reader, placed, size));
	}

} // anonymous namespace

TEST(CookContainerFuzzTest, EveryPrefixRefused) {
	Vector<u8> valid{GetMemoryResource(MemTag::Editor)};
	ASSERT_NO_FATAL_FAILURE(BuildCorpus(valid));
	GuardedInput input(valid.size());
	ASSERT_TRUE(input.IsValid());

	for (size_t length = 0; length < valid.size(); ++length) {
		const u8* placed = input.Place(valid.data(), length, 8);

		PakReader reader;
		str error;
		ASSERT_FALSE(reader.MountFromMemory(placed, length, "fuzz://truncated.slpak", error)) << "a " << length << ' ' << valid.size();
		ASSERT_FALSE(error.empty()) << length;
	}

	const u8* whole = input.Place(valid.data(), valid.size(), 8);
	PakReader reader;
	str error;
	ASSERT_TRUE(reader.MountFromMemory(whole, valid.size(), "fuzz://package.slpak", error)) << error;
	ASSERT_NO_FATAL_FAILURE(ExpectEveryViewInside(reader, whole, valid.size()));
}

TEST(CookContainerFuzzTest, RepairedTruncationRefused) {
	Vector<u8> valid{GetMemoryResource(MemTag::Editor)};
	ASSERT_NO_FATAL_FAILURE(BuildCorpus(valid));
	GuardedInput input(valid.size());
	ASSERT_TRUE(input.IsValid());

	u32 accepted = 0;
	for (size_t length = PAK_HEADER_SIZE; length < valid.size(); ++length) {
		Vector<u8> truncated{GetMemoryResource(MemTag::Editor)};
		truncated.assign(valid.begin(), valid.begin() + length);
		SetFileSize(truncated, length);
		RepairChecksum(truncated);

		const u8* placed = input.Place(truncated.data(), length, 8);
		bool wasAccepted = false;
		ASSERT_NO_FATAL_FAILURE(ExpectRefusedOrCoherent(placed, length, wasAccepted)) << "length " << length;
		accepted += wasAccepted ? 1 : 0;
	}

	ASSERT_EQ(accepted, 0u) << accepted;
}

TEST(CookContainerFuzzTest, MutatedSuperblockStaysCoherent) {
	Vector<u8> valid{GetMemoryResource(MemTag::Editor)};
	ASSERT_NO_FATAL_FAILURE(BuildCorpus(valid));
	GuardedInput input(valid.size());
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC00C1EE1ull);
	u32 accepted = 0;

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		Vector<u8> mutant{GetMemoryResource(MemTag::Editor)};
		mutant.assign(valid.begin(), valid.end());

		u32 edits = 1 + rng.Below(4);
		for (u32 e = 0; e < edits; ++e) {
			size_t at = rng.Below(offsetof(PakSuperblock, reserved));
			switch (rng.Below(3)) {
			case 0:
				mutant[at] ^= static_cast<u8>(1u << rng.Below(8));
				break;
			case 1:
				mutant[at] = rng.Byte();
				break;
			case 2:
				mutant[at] = 0xFF;
				break;
			}
		}
		RepairChecksum(mutant);

		const u8* placed = input.Place(mutant.data(), mutant.size(), 8);
		bool wasAccepted = false;
		ASSERT_NO_FATAL_FAILURE(ExpectRefusedOrCoherent(placed, mutant.size(), wasAccepted))
			<< "iteration " << iteration;
		accepted += wasAccepted ? 1 : 0;
	}

	EXPECT_GT(accepted, 0u);
}

TEST(CookContainerFuzzTest, MutatedBodiesAreRefusedOrMountedCoherently) {
	Vector<u8> valid{GetMemoryResource(MemTag::Editor)};
	ASSERT_NO_FATAL_FAILURE(BuildCorpus(valid));
	GuardedInput input(valid.size());
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC00C1EE2ull);
	u32 accepted = 0;

	PakSuperblock header{};
	std::memcpy(&header, valid.data(), sizeof(PakSuperblock));
	u64 tocOffset = header.tocOffset;
	u32 tocSize = header.tocSizeRaw;

	for (u32 iteration = 0; iteration < 20000; ++iteration) {
		Vector<u8> mutant{GetMemoryResource(MemTag::Editor)};
		mutant.assign(valid.begin(), valid.end());

		u32 edits = 1 + rng.Below(6);
		for (u32 e = 0; e < edits; ++e) {
			size_t at = rng.OneIn(2)
							? static_cast<size_t>(tocOffset) + rng.Below(tocSize)
							: PAK_HEADER_SIZE + rng.Below(static_cast<u32>(mutant.size() - PAK_HEADER_SIZE));
			switch (rng.Below(4)) {
			case 0:
				mutant[at] ^= static_cast<u8>(1u << rng.Below(8));
				break;
			case 1:
				mutant[at] = rng.Byte();
				break;
			case 2:
				mutant[at] = 0xFF;
				break;
			case 3: {
				size_t run = std::min<size_t>(1 + rng.Below(8), mutant.size() - at);
				for (size_t i = 0; i < run; ++i) {
					mutant[at + i] = 0xFF;
				}
				break;
			}
			}
		}

		const u8* placed = input.Place(mutant.data(), mutant.size(), 8);
		bool wasAccepted = false;
		ASSERT_NO_FATAL_FAILURE(ExpectRefusedOrCoherent(placed, mutant.size(), wasAccepted))
			<< "iteration " << iteration;
		accepted += wasAccepted ? 1 : 0;
	}

	EXPECT_GT(accepted, 0u);
}

TEST(CookContainerFuzzTest, RandomBodiesStayCoherent) {
	Vector<u8> valid{GetMemoryResource(MemTag::Editor)};
	ASSERT_NO_FATAL_FAILURE(BuildCorpus(valid));
	GuardedInput input(valid.size());
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC00C1EE3ull);
	u32 accepted = 0;

	for (u32 iteration = 0; iteration < 5000; ++iteration) {
		Vector<u8> mutant{GetMemoryResource(MemTag::Editor)};
		mutant.assign(valid.begin(), valid.end());

		for (size_t i = PAK_HEADER_SIZE; i < mutant.size(); ++i) {
			mutant[i] = rng.OneIn(3) ? 0xFF : rng.Byte();
		}

		const u8* placed = input.Place(mutant.data(), mutant.size(), 8);
		bool wasAccepted = false;
		ASSERT_NO_FATAL_FAILURE(ExpectRefusedOrCoherent(placed, mutant.size(), wasAccepted))
			<< "iteration " << iteration;
		accepted += wasAccepted ? 1 : 0;
	}

	ASSERT_EQ(accepted, 0u) << accepted;
}

TEST(CookContainerFuzzTest, SaturatedTocIsRefusedNotTrusted) {
	Vector<u8> valid{GetMemoryResource(MemTag::Editor)};
	ASSERT_NO_FATAL_FAILURE(BuildCorpus(valid));
	GuardedInput input(valid.size());
	ASSERT_TRUE(input.IsValid());

	Rng rng(0xC00C1EE4ull);

	const u32 SECTIONS[] = {PAK_SECT, PAK_TYPE, PAK_IDX0, PAK_DESC, PAK_PLDM,
							PAK_PAGE, PAK_DEPS, PAK_NAMS, PAK_STRS};

	for (u32 which = 0; which < static_cast<u32>(std::size(SECTIONS)); ++which) {
		PakSuperblock sb{};
		std::memcpy(&sb, valid.data(), sizeof(PakSuperblock));
		size_t offset = 0;
		size_t length = 0;
		for (u16 i = 0; i < sb.sectionCount; ++i) {
			PakSection section{};
			std::memcpy(&section, valid.data() + sb.tocOffset + i * sizeof(PakSection), sizeof(PakSection));
			if (section.fourcc == SECTIONS[which]) {
				offset = static_cast<size_t>(sb.tocOffset + section.offset);
				length = static_cast<size_t>(section.size);
				break;
			}
		}
		ASSERT_GT(length, 0u) << "section " << which;

		for (u32 iteration = 0; iteration < 400; ++iteration) {
			Vector<u8> mutant{GetMemoryResource(MemTag::Editor)};
			mutant.assign(valid.begin(), valid.end());

			size_t at = offset + rng.Below(static_cast<u32>(length));
			size_t run = std::min<size_t>(1 + rng.Below(64), mutant.size() - at);
			for (size_t i = 0; i < run; ++i) {
				mutant[at + i] = 0xFF;
			}

			const u8* placed = input.Place(mutant.data(), mutant.size(), 8);
			bool wasAccepted = false;
			ASSERT_NO_FATAL_FAILURE(ExpectRefusedOrCoherent(placed, mutant.size(), wasAccepted))
				<< "section " << which << ", iteration " << iteration;
		}
	}
}
