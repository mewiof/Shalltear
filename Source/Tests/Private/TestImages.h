#pragma once

#include "Shalltear/Core/Types.h"

#include <cstring>
#include <filesystem>
#include <fstream>

namespace TestImages {

	inline void WriteTga(const std::filesystem::path& path, Shalltear::u16 width, Shalltear::u16 height, const Shalltear::u8* rgba) {
		using Shalltear::u8;
		const u8 header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<u8>(width & 0xFF), static_cast<u8>(width >> 8),
							   static_cast<u8>(height & 0xFF), static_cast<u8>(height >> 8), 32, 0x28};
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char*>(header), sizeof(header));
		for (Shalltear::u32 i = 0; i < static_cast<Shalltear::u32>(width) * height; ++i) {
			const u8 bgra[4] = {rgba[i * 4 + 2], rgba[i * 4 + 1], rgba[i * 4], rgba[i * 4 + 3]};
			file.write(reinterpret_cast<const char*>(bgra), sizeof(bgra));
		}
	}
	inline void WriteTga(const std::filesystem::path& path, const Shalltear::u8 rgba[4]) {
		WriteTga(path, 1, 1, rgba);
	}

	inline constexpr Shalltear::u8 FLIPBOOK_CELLS[4][4] = {{250, 10, 10, 255}, {10, 250, 10, 255}, {40, 200, 120, 255}, {10, 10, 250, 255}};
	inline void WriteFlipbook(const std::filesystem::path& path) {
		Shalltear::u8 image[4 * 4 * 4];
		for (Shalltear::u32 y = 0; y < 4; ++y) {
			for (Shalltear::u32 x = 0; x < 4; ++x) {
				std::memcpy(&image[(y * 4 + x) * 4], FLIPBOOK_CELLS[(y / 2) * 2 + x / 2], 4);
			}
		}
		WriteTga(path, 4, 4, image);
	}

} // namespace TestImages
