#pragma once

#include "Shalltear/Core/Types.h"

#include <cstdio>
#include <filesystem>

namespace PESubsystem {

	using Shalltear::i32;
	using Shalltear::u16;
	using Shalltear::u32;

	inline constexpr u16 GUI = 2;
	inline constexpr u16 CONSOLE = 3;

	inline u16 Read(const std::filesystem::path& file) {
		std::FILE* handle = nullptr;
		if (::fopen_s(&handle, file.string().c_str(), "rb") != 0 || !handle) {
			return 0;
		}
		auto readAt = [handle](long offset, void* into, size_t bytes) {
			return std::fseek(handle, offset, SEEK_SET) == 0 && std::fread(into, 1, bytes, handle) == bytes;
		};
		u16 subsystem = 0;
		char magic[2] = {};
		i32 peOffset = 0;
		u32 signature = 0;
		if (readAt(0, magic, sizeof(magic)) && magic[0] == 'M' && magic[1] == 'Z' &&
			readAt(0x3C, &peOffset, sizeof(peOffset)) && peOffset > 0 &&
			readAt(peOffset, &signature, sizeof(signature)) && signature == 0x00004550u) {
			readAt(peOffset + 4 + 20 + 68, &subsystem, sizeof(subsystem));
		}
		std::fclose(handle);
		return subsystem;
	}

} // namespace PESubsystem
