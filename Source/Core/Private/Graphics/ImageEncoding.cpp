#include "Shalltear/Core/Graphics/ImageEncoding.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace Shalltear::ImageEncoding {

	namespace {

		void Put32BigEndian(Vector<u8>& out, u32 value) {
			out.push_back(static_cast<u8>(value >> 24));
			out.push_back(static_cast<u8>(value >> 16));
			out.push_back(static_cast<u8>(value >> 8));
			out.push_back(static_cast<u8>(value));
		}

		constexpr std::array<u32, 256> MakeCRCTable() {
			std::array<u32, 256> table{};
			for (u32 n = 0; n < 256; ++n) {
				u32 c = n;
				for (u32 bit = 0; bit < 8; ++bit) {
					c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
				}
				table[n] = c;
			}
			return table;
		}

		constexpr std::array<u32, 256> CRCTable = MakeCRCTable();

		u32 CRC32(std::span<const u8> bytes) {
			u32 crc = 0xFFFFFFFFu;
			for (u8 byte : bytes) {
				crc = CRCTable[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
			}
			return crc ^ 0xFFFFFFFFu;
		}

		u32 Adler32(std::span<const u8> bytes) {
			u32 low = 1;
			u32 high = 0;
			for (u8 byte : bytes) {
				low = (low + byte) % 65521u;
				high = (high + low) % 65521u;
			}
			return (high << 16) | low;
		}

		class BitWriter {
		public:
			explicit BitWriter(Vector<u8>& out) : _out(out) {}

			void Put(u32 bits, u32 count) {
				_buffer |= bits << _count;
				_count += count;
				while (_count >= 8) {
					_out.push_back(static_cast<u8>(_buffer));
					_buffer >>= 8;
					_count -= 8;
				}
			}

			void PutCode(u32 code, u32 length) {
				u32 reversed = 0;
				for (u32 bit = 0; bit < length; ++bit) {
					reversed = (reversed << 1) | ((code >> bit) & 1u);
				}
				Put(reversed, length);
			}

			void Flush() {
				if (_count > 0) {
					_out.push_back(static_cast<u8>(_buffer));
				}
				_buffer = 0;
				_count = 0;
			}
		private:
			Vector<u8>& _out;
			u32 _buffer = 0;
			u32 _count = 0;
		};

		constexpr u16 LENGTH_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
										 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
		constexpr u8 LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
		constexpr u16 DISTANCE_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
										   193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
		constexpr u8 DISTANCE_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

		void PutLiteral(BitWriter& bits, u32 symbol) {
			if (symbol < 144) {
				bits.PutCode(0x30u + symbol, 8);
			} else if (symbol < 256) {
				bits.PutCode(0x190u + symbol - 144, 9);
			} else if (symbol < 280) {
				bits.PutCode(symbol - 256, 7);
			} else {
				bits.PutCode(0xC0u + symbol - 280, 8);
			}
		}

		void PutMatch(BitWriter& bits, u32 length, u32 distance) {
			u32 lengthCode = 28;
			while (LENGTH_BASE[lengthCode] > length) {
				--lengthCode;
			}
			PutLiteral(bits, 257 + lengthCode);
			bits.Put(length - LENGTH_BASE[lengthCode], LENGTH_EXTRA[lengthCode]);

			u32 distanceCode = 29;
			while (DISTANCE_BASE[distanceCode] > distance) {
				--distanceCode;
			}
			bits.PutCode(distanceCode, 5);
			bits.Put(distance - DISTANCE_BASE[distanceCode], DISTANCE_EXTRA[distanceCode]);
		}

		void Deflate(std::span<const u8> data, Vector<u8>& out) {
			constexpr size_t WINDOW = 32768;
			constexpr size_t MIN_MATCH = 3;
			constexpr size_t MAX_MATCH = 258;
			constexpr u32 HASH_BITS = 15;
			constexpr u32 MAX_CHAIN = 64;

			BitWriter bits(out);
			bits.Put(1, 1);
			bits.Put(1, 2);

			Vector<i64> head{GetMemoryResource(MemTag::Core)};
			head.assign(size_t{1} << HASH_BITS, -1);
			Vector<i64> previous{GetMemoryResource(MemTag::Core)};
			previous.assign(WINDOW, -1);
			auto hash = [&](size_t at) {
				return ((static_cast<u32>(data[at]) << 10) ^ (static_cast<u32>(data[at + 1]) << 5) ^ data[at + 2]) &
					   ((1u << HASH_BITS) - 1);
			};
			auto insert = [&](size_t at) {
				if (at + MIN_MATCH <= data.size()) {
					const u32 key = hash(at);
					previous[at % WINDOW] = head[key];
					head[key] = static_cast<i64>(at);
				}
			};

			size_t at = 0;
			while (at < data.size()) {
				size_t bestLength = 0;
				size_t bestDistance = 0;
				if (at + MIN_MATCH <= data.size()) {
					const size_t limit = std::min(MAX_MATCH, data.size() - at);
					i64 candidate = head[hash(at)];
					for (u32 chain = 0; candidate >= 0 && chain < MAX_CHAIN; ++chain) {
						const size_t distance = at - static_cast<size_t>(candidate);
						if (distance > WINDOW) {
							break;
						}
						size_t length = 0;
						while (length < limit && data[static_cast<size_t>(candidate) + length] == data[at + length]) {
							++length;
						}
						if (length > bestLength) {
							bestLength = length;
							bestDistance = distance;
							if (length == limit) {
								break;
							}
						}
						candidate = previous[static_cast<size_t>(candidate) % WINDOW];
					}
				}
				if (bestLength >= MIN_MATCH) {
					PutMatch(bits, static_cast<u32>(bestLength), static_cast<u32>(bestDistance));
					for (size_t i = 0; i < bestLength; ++i) {
						insert(at + i);
					}
					at += bestLength;
				} else {
					PutLiteral(bits, data[at]);
					insert(at);
					++at;
				}
			}
			PutLiteral(bits, 256);
			bits.Flush();
		}

		u8 Paeth(u8 left, u8 up, u8 upLeft) {
			const i32 estimate = static_cast<i32>(left) + up - upLeft;
			const i32 toLeft = std::abs(estimate - left);
			const i32 toUp = std::abs(estimate - up);
			const i32 toUpLeft = std::abs(estimate - upLeft);
			if (toLeft <= toUp && toLeft <= toUpLeft) {
				return left;
			}
			return toUp <= toUpLeft ? up : upLeft;
		}

		void FilterRows(std::span<const u8> rgba, u32 width, u32 height, Vector<u8>& out) {
			const size_t stride = static_cast<size_t>(width) * 4;
			Vector<u8> candidate{GetMemoryResource(MemTag::Core)};
			candidate.resize(stride + 1);
			Vector<u8> best{GetMemoryResource(MemTag::Core)};
			best.resize(stride + 1);
			for (u32 y = 0; y < height; ++y) {
				const u8* row = rgba.data() + static_cast<size_t>(y) * stride;
				const u8* above = y > 0 ? row - stride : nullptr;
				u64 bestScore = ~u64{0};
				for (u8 filter = 0; filter < 5; ++filter) {
					candidate[0] = filter;
					u64 score = 0;
					for (size_t i = 0; i < stride; ++i) {
						const u8 left = i >= 4 ? row[i - 4] : 0;
						const u8 up = above ? above[i] : 0;
						const u8 upLeft = above && i >= 4 ? above[i - 4] : 0;
						u8 predicted = 0;
						switch (filter) {
						case 1:
							predicted = left;
							break;
						case 2:
							predicted = up;
							break;
						case 3:
							predicted = static_cast<u8>((static_cast<u32>(left) + up) / 2);
							break;
						case 4:
							predicted = Paeth(left, up, upLeft);
							break;
						default:
							break;
						}
						const u8 value = static_cast<u8>(row[i] - predicted);
						candidate[i + 1] = value;
						score += value < 128 ? value : 256u - value;
					}
					if (score < bestScore) {
						bestScore = score;
						best.swap(candidate);
					}
				}
				out.insert(out.end(), best.begin(), best.end());
			}
		}

		void PutChunk(Vector<u8>& out, const char (&type)[5], std::span<const u8> data) {
			Put32BigEndian(out, static_cast<u32>(data.size()));
			const size_t start = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			Put32BigEndian(out, CRC32({out.data() + start, out.size() - start}));
		}

		void WritePNG(std::span<const u8> rgba, u32 width, u32 height, Vector<u8>& out) {
			constexpr u8 SIGNATURE[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
			out.insert(out.end(), std::begin(SIGNATURE), std::end(SIGNATURE));

			// 8 bit RGBA, deflate, adaptive filters, no interlace
			constexpr u8 FORMAT[] = {8, 6, 0, 0, 0};
			Vector<u8> header{GetMemoryResource(MemTag::Core)};
			Put32BigEndian(header, width);
			Put32BigEndian(header, height);
			header.insert(header.end(), std::begin(FORMAT), std::end(FORMAT));
			PutChunk(out, "IHDR", header);

			Vector<u8> filtered{GetMemoryResource(MemTag::Core)};
			FilterRows(rgba, width, height, filtered);
			Vector<u8> stream{GetMemoryResource(MemTag::Core)};
			stream.push_back(0x78);
			stream.push_back(0x01);
			Deflate(filtered, stream);
			Put32BigEndian(stream, Adler32(filtered));
			PutChunk(out, "IDAT", stream);
			PutChunk(out, "IEND", {});
		}

	} // anonymous namespace

	bool EncodePNG(std::span<const u8> rgba8, Vec2I size, Vector<u8>& out) {
		if (size.x <= 0 || size.y <= 0 || rgba8.size() != static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * 4) {
			return false;
		}
		WritePNG(rgba8, static_cast<u32>(size.x), static_cast<u32>(size.y), out);
		return true;
	}

} // namespace Shalltear::ImageEncoding
