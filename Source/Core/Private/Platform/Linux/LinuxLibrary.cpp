#include "Platform/Library.h"

#include "Platform/Process.h"

#include <cstdint>
#include <mutex>
#include <vector>

#include <dlfcn.h>
#include <link.h>
#include <unistd.h>

namespace Shalltear::Platform {

	namespace {

		struct Image {
			const u8* begin = nullptr;
			const u8* end = nullptr;
			str path;
		};

		struct ImageQuery {
			const void* address = nullptr;
			const void* base = nullptr;
			bool wantFirst = false;
			bool wantPath = false;
			Image found;
		};

		int VisitImage(dl_phdr_info* info, size_t, void* user) {
			auto* query = static_cast<ImageQuery*>(user);
			const std::uintptr_t page = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
			std::uintptr_t low = UINTPTR_MAX;
			std::uintptr_t high = 0;
			for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
				const ElfW(Phdr) & header = info->dlpi_phdr[i];
				if (header.p_type != PT_LOAD) {
					continue;
				}
				const std::uintptr_t start = info->dlpi_addr + header.p_vaddr;
				low = start < low ? start : low;
				high = start + header.p_memsz > high ? start + header.p_memsz : high;
			}
			if (low >= high) {
				return 0;
			}
			low &= ~(page - 1);

			const u8* begin = reinterpret_cast<const u8*>(low);
			const u8* end = reinterpret_cast<const u8*>(high);
			const u8* address = static_cast<const u8*>(query->address);
			const bool hit = query->wantFirst ||
							 (query->base && query->base == begin) ||
							 (address && address >= begin && address < end);
			if (!hit) {
				return 0;
			}
			query->found.begin = begin;
			query->found.end = end;
			if (query->wantPath && info->dlpi_name) {
				query->found.path = info->dlpi_name;
			}
			return 1;
		}

		Image FindImage(ImageQuery query) {
			::dl_iterate_phdr(VisitImage, &query);
			return query.found;
		}

		struct OpenedLibrary {
			void* base = nullptr;
			void* handle = nullptr;
			u32 references = 0;
		};

		struct LibraryTable {
			std::mutex mutex;
			std::vector<OpenedLibrary> opened;
		};

		LibraryTable& GetTable() {
			static LibraryTable table;
			return table;
		}

		void* BaseOfHandle(void* handle) {
			link_map* map = nullptr;
			if (::dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0 || !map) {
				return nullptr;
			}
			ImageQuery query;
			query.address = map->l_ld;
			return const_cast<u8*>(FindImage(query).begin);
		}

		// null unless image itself defines name (dependencies searched by dlsym too)
		void* OwnSymbol(void* handle, const void* imageBase, const char* name) {
			void* symbol = ::dlsym(handle, name);
			return symbol && ImageBaseOf(symbol) == imageBase ? symbol : nullptr;
		}

	} // anonymous namespace

	void* OpenLibrary(const str& utf8Path, str* outError) {
		void* handle = nullptr;
		if (utf8Path.find('/') == str::npos) {
			const str executable = GetExecutablePath();
			const size_t slash = executable.find_last_of('/');
			if (slash != str::npos) {
				handle = ::dlopen((executable.substr(0, slash + 1) + utf8Path).c_str(), RTLD_NOW | RTLD_LOCAL);
			}
		}
		if (!handle) {
			handle = ::dlopen(utf8Path.c_str(), RTLD_NOW | RTLD_LOCAL);
		}
		if (!handle) {
			if (outError) {
				const char* reason = ::dlerror();
				*outError = reason ? reason : "unknown error";
			}
			return nullptr;
		}

		void* base = BaseOfHandle(handle);
		if (!base) {
			::dlclose(handle);
			if (outError) {
				*outError = "loaded image has no address range";
			}
			return nullptr;
		}

		LibraryTable& table = GetTable();
		std::lock_guard lock(table.mutex);
		for (OpenedLibrary& entry : table.opened) {
			if (entry.base == base) {
				++entry.references;
				return base;
			}
		}
		table.opened.push_back({base, handle, 1});
		return base;
	}

	void CloseLibrary(void* imageBase) {
		if (!imageBase) {
			return;
		}
		LibraryTable& table = GetTable();
		void* handle = nullptr;
		{
			std::lock_guard lock(table.mutex);
			for (size_t i = 0; i < table.opened.size(); ++i) {
				OpenedLibrary& entry = table.opened[i];
				if (entry.base != imageBase) {
					continue;
				}
				handle = entry.handle;
				if (--entry.references == 0) {
					table.opened.erase(table.opened.begin() + static_cast<std::ptrdiff_t>(i));
				}
				break;
			}
		}
		if (handle) {
			::dlclose(handle);
		}
	}

	void* FindSymbol(const void* imageBase, const char* name) {
		if (!imageBase || !name) {
			return nullptr;
		}
		{
			LibraryTable& table = GetTable();
			std::lock_guard lock(table.mutex);
			for (const OpenedLibrary& entry : table.opened) {
				if (entry.base == imageBase) {
					return OwnSymbol(entry.handle, imageBase, name);
				}
			}
		}

		ImageQuery query;
		query.base = imageBase;
		query.wantPath = true;
		const Image image = FindImage(query);
		if (!image.begin) {
			return nullptr;
		}
		// empty path = executable
		void* handle = ::dlopen(image.path.empty() ? nullptr : image.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
		if (!handle) {
			return nullptr;
		}
		void* symbol = OwnSymbol(handle, imageBase, name);
		::dlclose(handle);
		return symbol;
	}

	void* GetExecutableImage() {
		ImageQuery query;
		query.wantFirst = true;
		return const_cast<u8*>(FindImage(query).begin);
	}

	void* ImageBaseOf(const void* address) {
		if (!address) {
			return nullptr;
		}
		ImageQuery query;
		query.address = address;
		return const_cast<u8*>(FindImage(query).begin);
	}

	ImageSpan GetImageSpan(const void* imageBase) {
		if (!imageBase) {
			return {};
		}
		ImageQuery query;
		query.base = imageBase;
		const Image image = FindImage(query);
		return {image.begin, image.end};
	}

	std::string_view GetSharedLibrarySuffix() {
		return ".so";
	}

	str GetSystemLibraryFileName(std::string_view stem) {
		return "lib" + str(stem) + ".so";
	}

	std::string_view GetDebugSymbolsSuffix() {
		return {};
	}

} // namespace Shalltear::Platform
