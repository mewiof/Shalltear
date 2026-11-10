#include "Platform/EditorPlatform.h"

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Shalltear::EditorPlatform {

	namespace {

		struct ImageFile {
			std::ifstream stream;
			u64 size = 0;

			explicit ImageFile(const Path& path)
				: stream(std::filesystem::path(path.CStr()), std::ios::binary) {
				if (stream && stream.seekg(0, std::ios::end)) {
					const std::streamoff end = stream.tellg();
					size = end > 0 ? static_cast<u64>(end) : 0;
				}
			}

			bool Read(u64 offset, void* out, size_t count) {
				if (offset > size || count > size - offset) {
					return false;
				}
				stream.clear();
				stream.seekg(static_cast<std::streamoff>(offset));
				stream.read(static_cast<char*>(out), static_cast<std::streamsize>(count));
				return static_cast<size_t>(stream.gcount()) == count;
			}

			template <typename T>
			bool Read(u64 offset, T& out) {
				return Read(offset, &out, sizeof(T));
			}
		};

	} // anonymous namespace

	bool ReadImportedLibraries(const Path& binary, Vector<str>& out) {
		ImageFile file(binary);

		IMAGE_DOS_HEADER dos;
		if (!file.Read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
			return false;
		}
		const u64 ntOffset = static_cast<u32>(dos.e_lfanew);
		DWORD signature = 0;
		IMAGE_FILE_HEADER fileHeader;
		if (!file.Read(ntOffset, signature) || signature != IMAGE_NT_SIGNATURE ||
			!file.Read(ntOffset + sizeof(DWORD), fileHeader)) {
			return false;
		}

		const u64 optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
		const u64 sectionsOffset = optionalOffset + fileHeader.SizeOfOptionalHeader;
		WORD magic = 0;
		DWORD directoryCount = 0;
		if (!file.Read(optionalOffset, magic) || magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
			!file.Read(optionalOffset + offsetof(IMAGE_OPTIONAL_HEADER64, NumberOfRvaAndSizes), directoryCount)) {
			return false;
		}
		IMAGE_DATA_DIRECTORY imports{};
		if (IMAGE_DIRECTORY_ENTRY_IMPORT < directoryCount &&
			!file.Read(optionalOffset + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
						   IMAGE_DIRECTORY_ENTRY_IMPORT * sizeof(IMAGE_DATA_DIRECTORY),
					   imports)) {
			return false;
		}

		Vector<IMAGE_SECTION_HEADER> sections(fileHeader.NumberOfSections);
		if (!sections.empty() &&
			!file.Read(sectionsOffset, sections.data(), sections.size() * sizeof(IMAGE_SECTION_HEADER))) {
			return false;
		}
		auto rvaToOffset = [&sections](DWORD rva, u64& offset) {
			for (const IMAGE_SECTION_HEADER& section : sections) {
				const DWORD extent = section.Misc.VirtualSize != 0 ? section.Misc.VirtualSize : section.SizeOfRawData;
				if (rva >= section.VirtualAddress && rva - section.VirtualAddress < extent) {
					offset = static_cast<u64>(rva - section.VirtualAddress) + section.PointerToRawData;
					return true;
				}
			}
			return false;
		};
		auto readName = [&](DWORD rva, str& name) {
			u64 offset = 0;
			if (!rvaToOffset(rva, offset)) {
				return false;
			}
			name.clear();
			char chunk[64];
			while (name.size() < MAX_PATH && offset < file.size) {
				const size_t count = file.size - offset < sizeof(chunk) ? static_cast<size_t>(file.size - offset) : sizeof(chunk);
				if (!file.Read(offset, chunk, count)) {
					return false;
				}
				if (const void* end = std::memchr(chunk, '\0', count)) {
					name.append(chunk, static_cast<const char*>(end) - chunk);
					return !name.empty();
				}
				name.append(chunk, count);
				offset += count;
			}
			return false;
		};

		if (imports.VirtualAddress != 0) {
			u64 offset = 0;
			if (!rvaToOffset(imports.VirtualAddress, offset)) {
				return false;
			}
			for (;; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
				IMAGE_IMPORT_DESCRIPTOR descriptor;
				if (!file.Read(offset, descriptor)) {
					return false;
				}
				if (descriptor.Name == 0) {
					break;
				}
				str name;
				if (!readName(descriptor.Name, name)) {
					return false;
				}
				out.push_back(std::move(name));
			}
		}
		return true;
	}

} // namespace Shalltear::EditorPlatform
