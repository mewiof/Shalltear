#pragma once

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Types.h"

#include <cstring>

namespace Shalltear::LZ4 {

	// uses must check op + WILD_COPY_WIDTH <= dstEnd
	constexpr size_t WILD_COPY_WIDTH = 8;

	inline void WildCopy(u8* dst, const u8* src, size_t count) {
		size_t done = 0;
		do {
			std::memcpy(dst + done, src + done, WILD_COPY_WIDTH);
			done += WILD_COPY_WIDTH;
		} while (done < count);
	}

	// never writes outside [dst, dst + rawSize)
	inline bool Decompress(const u8* src, size_t compressedSize, u8* dst, size_t rawSize) {
		if (compressedSize == 0) {
			return rawSize == 0;
		}
		const u8* ip = src;
		const u8* const ipEnd = src + compressedSize;
		u8* op = dst;
		u8* const opEnd = dst + rawSize;

		for (;;) {
			if (ip >= ipEnd) {
				return false;
			}
			const u8 token = *ip++;

			size_t literalCount = token >> 4;
			if (literalCount == 15) {
				u8 add = 255;
				while (add == 255) {
					if (ip >= ipEnd) {
						return false;
					}
					add = *ip++;
					if (literalCount > SIZE_MAX - add) {
						return false;
					}
					literalCount += add;
				}
			}
			if (literalCount > static_cast<size_t>(ipEnd - ip) || literalCount > static_cast<size_t>(opEnd - op)) {
				return false;
			}
			if (literalCount >= WILD_COPY_WIDTH && static_cast<size_t>(ipEnd - ip) >= literalCount + WILD_COPY_WIDTH &&
				static_cast<size_t>(opEnd - op) >= literalCount + WILD_COPY_WIDTH) {
				WildCopy(op, ip, literalCount);
			} else {
				std::memcpy(op, ip, literalCount);
			}
			ip += literalCount;
			op += literalCount;

			if (ip == ipEnd) {
				break;
			}

			if (static_cast<size_t>(ipEnd - ip) < 2) {
				return false;
			}
			const u32 offset = static_cast<u32>(ip[0]) | (static_cast<u32>(ip[1]) << 8);
			ip += 2;
			if (offset == 0 || offset > static_cast<size_t>(op - dst)) {
				return false;
			}

			size_t matchLength = token & 0x0F;
			if (matchLength == 15) {
				u8 add = 255;
				while (add == 255) {
					if (ip >= ipEnd) {
						return false;
					}
					add = *ip++;
					if (matchLength > SIZE_MAX - add) {
						return false;
					}
					matchLength += add;
				}
			}
			matchLength += LZ4_MIN_MATCH;
			if (matchLength > static_cast<size_t>(opEnd - op)) {
				return false;
			}

			const u8* match = op - offset;
			if (offset >= WILD_COPY_WIDTH && static_cast<size_t>(opEnd - op) >= matchLength + WILD_COPY_WIDTH) {
				WildCopy(op, match, matchLength);
			} else {
				for (size_t k = 0; k < matchLength; ++k) {
					op[k] = match[k];
				}
			}
			op += matchLength;
		}

		return op == opEnd;
	}

} // namespace Shalltear::LZ4
