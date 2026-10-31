#pragma once

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <cstring>

namespace Shalltear::LZ4 {

	constexpr u32 HASH_BITS = 16;
	constexpr u32 HASH_SIZE = 1u << HASH_BITS;

	enum class Effort : u32 {
		Fast = 1,
		High = 64
	};

	inline u32 HashAt(const u8* p) {
		u32 sequence = 0;
		std::memcpy(&sequence, p, 4);
		return (sequence * 2654435761u) >> (32 - HASH_BITS);
	}

	inline void WriteLength(Vector<u8>& out, size_t length) {
		while (length >= 255) {
			out.push_back(255);
			length -= 255;
		}
		out.push_back(static_cast<u8>(length));
	}

	inline size_t Compress(const u8* src, size_t size, Effort effort, Vector<u8>& out) {
		const size_t startedAt = out.size();
		if (size == 0) {
			return 0;
		}

		Vector<i32> head{GetMemoryResource(MemTag::Editor)};
		head.resize(HASH_SIZE, -1);
		Vector<i32> chain{GetMemoryResource(MemTag::Editor)};
		chain.resize(size, -1);

		const u32 depth = static_cast<u32>(effort);
		const size_t matchLimit = size > LZ4_MATCH_FLOOR ? size - LZ4_MATCH_FLOOR : 0;
		const size_t literalFloor = size - LZ4_LAST_LITERALS;

		size_t anchor = 0;
		size_t ip = 0;
		while (ip < matchLimit) {
			const u32 h = HashAt(src + ip);

			size_t bestLength = 0;
			size_t bestMatch = 0;
			i32 probe = head[h];
			for (u32 tried = 0; tried < depth && probe >= 0; ++tried) {
				const size_t candidate = static_cast<size_t>(probe);
				if (ip - candidate > LZ4_MAX_OFFSET) {
					break;
				}
				if (std::memcmp(src + candidate, src + ip, LZ4_MIN_MATCH) == 0) {
					size_t length = LZ4_MIN_MATCH;
					while (ip + length < literalFloor && src[candidate + length] == src[ip + length]) {
						++length;
					}
					if (length > bestLength) {
						bestLength = length;
						bestMatch = candidate;
					}
				}
				probe = chain[candidate];
			}

			chain[ip] = head[h];
			head[h] = static_cast<i32>(ip);

			if (bestLength < LZ4_MIN_MATCH) {
				++ip;
				continue;
			}

			const size_t literalCount = ip - anchor;
			const size_t matchCode = bestLength - LZ4_MIN_MATCH;
			out.push_back(static_cast<u8>((literalCount >= 15 ? 15u : static_cast<u32>(literalCount)) << 4 |
										  (matchCode >= 15 ? 15u : static_cast<u32>(matchCode))));
			if (literalCount >= 15) {
				WriteLength(out, literalCount - 15);
			}
			out.insert(out.end(), src + anchor, src + ip);
			const u16 offset = static_cast<u16>(ip - bestMatch);
			out.push_back(static_cast<u8>(offset & 0xFF));
			out.push_back(static_cast<u8>(offset >> 8));
			if (matchCode >= 15) {
				WriteLength(out, matchCode - 15);
			}

			for (size_t k = ip + 1; k < ip + bestLength && k < matchLimit; ++k) {
				const u32 hk = HashAt(src + k);
				chain[k] = head[hk];
				head[hk] = static_cast<i32>(k);
			}
			ip += bestLength;
			anchor = ip;
		}

		const size_t literalCount = size - anchor;
		out.push_back(static_cast<u8>((literalCount >= 15 ? 15u : static_cast<u32>(literalCount)) << 4));
		if (literalCount >= 15) {
			WriteLength(out, literalCount - 15);
		}
		out.insert(out.end(), src + anchor, src + size);
		return out.size() - startedAt;
	}

} // namespace Shalltear::LZ4
