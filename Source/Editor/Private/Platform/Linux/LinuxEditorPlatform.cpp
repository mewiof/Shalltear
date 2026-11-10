#include "Platform/Posix/PosixSpawn.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#include <elf.h>

namespace Shalltear::EditorPlatform {

	namespace {

		str EnvironmentValue(const char* name) {
			const char* value = std::getenv(name);
			return value ? str(value) : str();
		}

		str ToFileUri(const str& path) {
			static const char HEX[] = "0123456789ABCDEF";
			str uri = "file://";
			for (const char c : path) {
				const unsigned char byte = static_cast<unsigned char>(c);
				const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
								   byte == '/' || byte == '-' || byte == '_' || byte == '.' || byte == '~';
				if (plain) {
					uri.push_back(c);
				} else {
					uri.push_back('%');
					uri.push_back(HEX[byte >> 4]);
					uri.push_back(HEX[byte & 0xF]);
				}
			}
			return uri;
		}

		bool Launch(const Vector<str>& arguments) {
			SpawnedProcess spawned;
			str error;
			if (!SpawnArguments(arguments, SpawnOptions{}, spawned, error)) {
				return false;
			}
			ReleaseProcess(spawned.process);
			return true;
		}

		struct ImageFile {
			std::ifstream stream;
			u64 size = 0;

			explicit ImageFile(const Path& path) : stream(std::filesystem::path(path.CStr()), std::ios::binary) {
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

	bool OpenWithDefaultApplication(const str& path) {
		Vector<str> arguments{GetMemoryResource(MemTag::Editor)};
		arguments.push_back("xdg-open");
		arguments.push_back(path);
		return Launch(arguments);
	}

	bool RevealInFileManager(const str& path, bool isDirectory) {
		if (isDirectory) {
			return OpenWithDefaultApplication(path);
		}
		// ShowItems is optional on the bus (xdg-open as fallback)
		Vector<str> arguments{GetMemoryResource(MemTag::Editor)};
		arguments.push_back("/bin/sh");
		arguments.push_back("-c");
		arguments.push_back("dbus-send --session --print-reply --dest=org.freedesktop.FileManager1 /org/freedesktop/FileManager1 "
							"org.freedesktop.FileManager1.ShowItems \"array:string:$1\" string: >/dev/null 2>&1 || xdg-open \"$2\"");
		arguments.push_back("sh");
		arguments.push_back(ToFileUri(path));
		arguments.push_back(std::filesystem::path(path).parent_path().string());
		return Launch(arguments);
	}

	std::string_view GetHostPlatformName() {
		return "Linux";
	}

	std::string_view GetSharedLibrarySuffix() {
		return ".so";
	}

	void AppendDefaultToolDirectories(Vector<str>& out) {
		out.push_back("/usr/local/bin");
		out.push_back("/usr/bin");
	}

	void AppendExternalEditorCandidates(Vector<ExternalEditorCandidate>& out) {
		constexpr const char* CODE_FOLDER = "\"{folder}\"";
		constexpr const char* CODE_FILE = "\"{folder}\" --goto \"{file}:{line}:{column}\"";
		constexpr const char* IDE_FOLDER = "\"{folder}\"";
		constexpr const char* IDE_FILE = "--line {line} \"{file}\"";

		Vector<str> directories{GetMemoryResource(MemTag::Editor)};
		directories.push_back("/usr/bin");
		directories.push_back("/usr/local/bin");
		directories.push_back("/snap/bin");
		const str home = EnvironmentValue("HOME");
		if (!home.empty()) {
			directories.push_back(home + "/.local/bin");
			directories.push_back(home + "/.local/share/JetBrains/Toolbox/scripts");
		}
		for (const str& directory : directories) {
			out.push_back({"VS Code", directory + "/code", CODE_FOLDER, CODE_FILE});
		}
		for (const str& directory : directories) {
			out.push_back({"CLion", directory + "/clion", IDE_FOLDER, IDE_FILE});
			out.push_back({"Rider", directory + "/rider", IDE_FOLDER, IDE_FILE});
		}
	}

	bool ReadImportedLibraries(const Path& binary, Vector<str>& out) {
		ImageFile file(binary);

		Elf64_Ehdr header;
		if (!file.Read(0, header) || std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0 ||
			header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_shentsize != sizeof(Elf64_Shdr)) {
			return false;
		}

		std::vector<Elf64_Shdr> sections(header.e_shnum);
		if (!sections.empty() && !file.Read(header.e_shoff, sections.data(), sections.size() * sizeof(Elf64_Shdr))) {
			return false;
		}

		for (const Elf64_Shdr& section : sections) {
			if (section.sh_type != SHT_DYNAMIC || section.sh_link >= sections.size()) {
				continue;
			}
			const Elf64_Shdr& strings = sections[section.sh_link];
			const u64 count = section.sh_size / sizeof(Elf64_Dyn);
			for (u64 i = 0; i < count; ++i) {
				Elf64_Dyn entry;
				if (!file.Read(section.sh_offset + i * sizeof(Elf64_Dyn), entry)) {
					return false;
				}
				if (entry.d_tag == DT_NULL) {
					break;
				}
				if (entry.d_tag != DT_NEEDED || entry.d_un.d_val >= strings.sh_size) {
					continue;
				}
				str name;
				u64 offset = strings.sh_offset + entry.d_un.d_val;
				const u64 end = strings.sh_offset + strings.sh_size;
				char c = 0;
				while (offset < end && file.Read(offset, c) && c != '\0') {
					name.push_back(c);
					++offset;
				}
				if (name.empty()) {
					return false;
				}
				out.push_back(std::move(name));
			}
		}
		return true;
	}

} // namespace Shalltear::EditorPlatform
