#include "BC6HDecoder.h"

namespace Shalltear::BC6H {

	namespace {

		constexpr u8 PARTITION_TARGET = 12;

		struct Field {
			u8 target;
			u8 to;
			u8 from;
		};

		struct Mode {
			u8 number;
			bool transformed;
			bool twoSubsets;
			u8 endpointBits;
			u8 deltaBits[3];
			u8 fieldCount;
			Field fields[24];
		};

		constexpr Mode MODES[] = {
			{0, true, true, 10, {5, 5, 5}, 20, {{7, 4, 4}, {8, 4, 4}, {11, 4, 4}, {0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 4, 0}, {10, 4, 4}, {7, 3, 0}, {4, 4, 0}, {11, 0, 0}, {10, 3, 0}, {5, 4, 0}, {11, 1, 1}, {8, 3, 0}, {6, 4, 0}, {11, 2, 2}, {9, 4, 0}, {11, 3, 3}, {12, 4, 0}}},
			{1, true, true, 7, {6, 6, 6}, 24, {{7, 5, 5}, {10, 4, 4}, {10, 5, 5}, {0, 6, 0}, {11, 0, 0}, {11, 1, 1}, {8, 4, 4}, {1, 6, 0}, {8, 5, 5}, {11, 2, 2}, {7, 4, 4}, {2, 6, 0}, {11, 3, 3}, {11, 5, 5}, {11, 4, 4}, {3, 5, 0}, {7, 3, 0}, {4, 5, 0}, {10, 3, 0}, {5, 5, 0}, {8, 3, 0}, {6, 5, 0}, {9, 5, 0}, {12, 4, 0}}},
			{2, true, true, 11, {5, 4, 4}, 19, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 4, 0}, {0, 10, 10}, {7, 3, 0}, {4, 3, 0}, {1, 10, 10}, {11, 0, 0}, {10, 3, 0}, {5, 3, 0}, {2, 10, 10}, {11, 1, 1}, {8, 3, 0}, {6, 4, 0}, {11, 2, 2}, {9, 4, 0}, {11, 3, 3}, {12, 4, 0}}},
			{6, true, true, 11, {4, 5, 4}, 21, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 3, 0}, {0, 10, 10}, {10, 4, 4}, {7, 3, 0}, {4, 4, 0}, {1, 10, 10}, {10, 3, 0}, {5, 3, 0}, {2, 10, 10}, {11, 1, 1}, {8, 3, 0}, {6, 3, 0}, {11, 0, 0}, {11, 2, 2}, {9, 3, 0}, {7, 4, 4}, {11, 3, 3}, {12, 4, 0}}},
			{10, true, true, 11, {4, 4, 5}, 21, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 3, 0}, {0, 10, 10}, {8, 4, 4}, {7, 3, 0}, {4, 3, 0}, {1, 10, 10}, {11, 0, 0}, {10, 3, 0}, {5, 4, 0}, {2, 10, 10}, {8, 3, 0}, {6, 3, 0}, {11, 1, 1}, {11, 2, 2}, {9, 3, 0}, {11, 4, 4}, {11, 3, 3}, {12, 4, 0}}},
			{14, true, true, 9, {5, 5, 5}, 20, {{0, 8, 0}, {8, 4, 4}, {1, 8, 0}, {7, 4, 4}, {2, 8, 0}, {11, 4, 4}, {3, 4, 0}, {10, 4, 4}, {7, 3, 0}, {4, 4, 0}, {11, 0, 0}, {10, 3, 0}, {5, 4, 0}, {11, 1, 1}, {8, 3, 0}, {6, 4, 0}, {11, 2, 2}, {9, 4, 0}, {11, 3, 3}, {12, 4, 0}}},
			{18, true, true, 8, {6, 5, 5}, 20, {{0, 7, 0}, {10, 4, 4}, {8, 4, 4}, {1, 7, 0}, {11, 2, 2}, {7, 4, 4}, {2, 7, 0}, {11, 3, 3}, {11, 4, 4}, {3, 5, 0}, {7, 3, 0}, {4, 4, 0}, {11, 0, 0}, {10, 3, 0}, {5, 4, 0}, {11, 1, 1}, {8, 3, 0}, {6, 5, 0}, {9, 5, 0}, {12, 4, 0}}},
			{22, true, true, 8, {5, 6, 5}, 22, {{0, 7, 0}, {11, 0, 0}, {8, 4, 4}, {1, 7, 0}, {7, 5, 5}, {7, 4, 4}, {2, 7, 0}, {10, 5, 5}, {11, 4, 4}, {3, 4, 0}, {10, 4, 4}, {7, 3, 0}, {4, 5, 0}, {10, 3, 0}, {5, 4, 0}, {11, 1, 1}, {8, 3, 0}, {6, 4, 0}, {11, 2, 2}, {9, 4, 0}, {11, 3, 3}, {12, 4, 0}}},
			{26, true, true, 8, {5, 5, 6}, 22, {{0, 7, 0}, {11, 1, 1}, {8, 4, 4}, {1, 7, 0}, {8, 5, 5}, {7, 4, 4}, {2, 7, 0}, {11, 5, 5}, {11, 4, 4}, {3, 4, 0}, {10, 4, 4}, {7, 3, 0}, {4, 4, 0}, {11, 0, 0}, {10, 3, 0}, {5, 5, 0}, {8, 3, 0}, {6, 4, 0}, {11, 2, 2}, {9, 4, 0}, {11, 3, 3}, {12, 4, 0}}},
			{30, false, true, 6, {0, 0, 0}, 24, {{0, 5, 0}, {10, 4, 4}, {11, 0, 0}, {11, 1, 1}, {8, 4, 4}, {1, 5, 0}, {7, 5, 5}, {8, 5, 5}, {11, 2, 2}, {7, 4, 4}, {2, 5, 0}, {10, 5, 5}, {11, 3, 3}, {11, 5, 5}, {11, 4, 4}, {3, 5, 0}, {7, 3, 0}, {4, 5, 0}, {10, 3, 0}, {5, 5, 0}, {8, 3, 0}, {6, 5, 0}, {9, 5, 0}, {12, 4, 0}}},
			{3, false, false, 10, {0, 0, 0}, 6, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 9, 0}, {4, 9, 0}, {5, 9, 0}}},
			{7, true, false, 11, {9, 9, 9}, 9, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 8, 0}, {0, 10, 10}, {4, 8, 0}, {1, 10, 10}, {5, 8, 0}, {2, 10, 10}}},
			{11, true, false, 12, {8, 8, 8}, 9, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 7, 0}, {0, 10, 11}, {4, 7, 0}, {1, 10, 11}, {5, 7, 0}, {2, 10, 11}}},
			{15, true, false, 16, {4, 4, 4}, 9, {{0, 9, 0}, {1, 9, 0}, {2, 9, 0}, {3, 3, 0}, {0, 10, 15}, {4, 3, 0}, {1, 10, 15}, {5, 3, 0}, {2, 10, 15}}},
		};

		constexpr u16 PARTITIONS[32] = {0xCCCC, 0x8888, 0xEEEE, 0xECC8, 0xC880, 0xFEEC, 0xFEC8, 0xEC80, 0xC800, 0xFFEC, 0xFE80,
										0xE800, 0xFFE8, 0xFF00, 0xFFF0, 0xF000, 0xF710, 0x008E, 0x7100, 0x08CE, 0x008C, 0x7310,
										0x3100, 0x8CCE, 0x088C, 0x3110, 0x6666, 0x366C, 0x17E8, 0x0FF0, 0x718E, 0x399C};
		constexpr u8 ANCHORS[32] = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
									15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2};
		constexpr u8 WEIGHTS3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
		constexpr u8 WEIGHTS4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

		constexpr u16 HALF_ONE = 0x3C00;

		struct BitReader {
			const u8* block = nullptr;
			u32 position = 0;

			u32 Read(u32 count) {
				u32 value = 0;
				for (u32 i = 0; i < count; ++i, ++position) {
					value |= static_cast<u32>((block[position >> 3] >> (position & 7)) & 1) << i;
				}
				return value;
			}
		};

		i32 SignExtend(i32 value, u32 bits) {
			const u32 shift = 32 - bits;
			return static_cast<i32>(static_cast<u32>(value) << shift) >> shift;
		}

		i32 Unquantize(i32 value, u32 bits) {
			if (bits >= 15 || value == 0) {
				return value;
			}
			if (value == (1 << bits) - 1) {
				return 0xFFFF;
			}
			return ((value << 16) + 0x8000) >> bits;
		}

		const Mode* FindMode(u32 number) {
			for (const Mode& mode : MODES) {
				if (mode.number == number) {
					return &mode;
				}
			}
			return nullptr;
		}

	} // anonymous namespace

	void DecodeBlock(const u8 block[16], u16 rgba[64]) {
		BitReader bits{block};
		u32 number = bits.Read(2);
		if (number >= 2) {
			number |= bits.Read(3) << 2;
		}
		const Mode* mode = FindMode(number);
		if (!mode) {
			for (u32 texel = 0; texel < 16; ++texel) {
				rgba[texel * 4 + 0] = 0;
				rgba[texel * 4 + 1] = 0;
				rgba[texel * 4 + 2] = 0;
				rgba[texel * 4 + 3] = HALF_ONE;
			}
			return;
		}

		i32 values[PARTITION_TARGET + 1] = {};
		for (u32 f = 0; f < mode->fieldCount; ++f) {
			const Field& field = mode->fields[f];
			const i32 step = field.to >= field.from ? 1 : -1;
			const u32 count = static_cast<u32>((static_cast<i32>(field.to) - static_cast<i32>(field.from)) * step) + 1;
			i32 bit = field.from;
			for (u32 i = 0; i < count; ++i, bit += step) {
				values[field.target] |= static_cast<i32>(bits.Read(1)) << bit;
			}
		}

		const u32 endpoints = mode->twoSubsets ? 4 : 2;
		const i32 mask = (1 << mode->endpointBits) - 1;
		i32 unquantized[4][3] = {};
		for (u32 channel = 0; channel < 3; ++channel) {
			for (u32 e = 0; e < endpoints; ++e) {
				i32 value = values[e * 3 + channel];
				if (mode->transformed && e > 0) {
					value = (values[channel] + SignExtend(value, mode->deltaBits[channel])) & mask;
				}
				unquantized[e][channel] = Unquantize(value, mode->endpointBits);
			}
		}

		const u32 partition = static_cast<u32>(values[PARTITION_TARGET]);
		bits.position = mode->twoSubsets ? 82 : 65;
		const u32 indexBits = mode->twoSubsets ? 3 : 4;
		const u8* weights = mode->twoSubsets ? WEIGHTS3 : WEIGHTS4;
		for (u32 texel = 0; texel < 16; ++texel) {
			const bool anchor = texel == 0 || (mode->twoSubsets && texel == ANCHORS[partition]);
			const i32 weight = weights[bits.Read(indexBits - (anchor ? 1 : 0))];
			const u32 subset = mode->twoSubsets ? (PARTITIONS[partition] >> texel) & 1 : 0;
			for (u32 channel = 0; channel < 3; ++channel) {
				const i32 a = unquantized[subset * 2][channel];
				const i32 b = unquantized[subset * 2 + 1][channel];
				const i32 interpolated = (a * (64 - weight) + b * weight + 32) >> 6;
				rgba[texel * 4 + channel] = static_cast<u16>((interpolated * 31) >> 6);
			}
			rgba[texel * 4 + 3] = HALF_ONE;
		}
	}

} // namespace Shalltear::BC6H
