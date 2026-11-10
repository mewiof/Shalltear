#include "UI/SystemFontFallback.h"

#include "Platform/Library.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <format>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Shalltear::UI {

	namespace {

		// fontconfig ABI declared manually (no header in sysroot)
		struct FcConfig;
		struct FcPattern;
		struct FcCharSet;
		constexpr int FC_MATCH_PATTERN = 0;
		constexpr int FC_RESULT_MATCH = 0;
		constexpr int FC_SLANT_ROMAN = 0;
		constexpr int FC_SLANT_ITALIC = 100;

#define SLT_FC_FUNCS(X)                                                               \
	X(FcConfig*, FcInitLoadConfigAndFonts, ())                                        \
	X(void, FcConfigDestroy, (FcConfig*))                                             \
	X(FcPattern*, FcPatternCreate, ())                                                \
	X(void, FcPatternDestroy, (FcPattern*))                                           \
	X(int, FcPatternAddInteger, (FcPattern*, const char*, int))                       \
	X(int, FcPatternAddBool, (FcPattern*, const char*, int))                          \
	X(int, FcPatternAddCharSet, (FcPattern*, const char*, const FcCharSet*))          \
	X(FcCharSet*, FcCharSetCreate, ())                                                \
	X(int, FcCharSetAddChar, (FcCharSet*, unsigned int))                              \
	X(void, FcCharSetDestroy, (FcCharSet*))                                           \
	X(int, FcCharSetHasChar, (const FcCharSet*, unsigned int))                        \
	X(int, FcConfigSubstitute, (FcConfig*, FcPattern*, int))                          \
	X(void, FcDefaultSubstitute, (FcPattern*))                                        \
	X(FcPattern*, FcFontMatch, (FcConfig*, FcPattern*, int*))                         \
	X(int, FcPatternGetString, (const FcPattern*, const char*, int, unsigned char**)) \
	X(int, FcPatternGetInteger, (const FcPattern*, const char*, int, int*))           \
	X(int, FcPatternGetCharSet, (const FcPattern*, const char*, int, FcCharSet**))    \
	X(int, FcWeightFromOpenType, (int))

		struct MappedFile {
			void* data = nullptr;
			size_t size = 0;
		};

	} // anonymous namespace

	struct SystemFontFallback::Impl {
#define SLT_FC_DECLARE(result, name, arguments) result(*name) arguments = nullptr;
		SLT_FC_FUNCS(SLT_FC_DECLARE)
#undef SLT_FC_DECLARE

		void* library = nullptr;
		FcConfig* config = nullptr;
		HashMap<u64, const SystemFontFile*> matches{GetMemoryResource(MemTag::UI)};
		HashMap<str, MappedFile*> files{GetMemoryResource(MemTag::UI)};
		HashMap<str, SystemFontFile*> faces{GetMemoryResource(MemTag::UI)};

		~Impl() {
			for (auto& [key, face] : faces) {
				Delete(face);
			}
			for (auto& [key, mapped] : files) {
				if (mapped->data) {
					::munmap(mapped->data, mapped->size);
				}
				Delete(mapped);
			}
			if (config) {
				FcConfigDestroy(config);
			}
			if (library) {
				Platform::CloseLibrary(library);
			}
		}

		static Impl* Create() {
			void* library = Platform::OpenLibrary("libfontconfig.so.1");
			if (!library) {
				SLT_LOG_INFO("UI", "libfontconfig.so.1 not found (no system font fallback)");
				return nullptr;
			}
			Impl* impl = New<Impl>(MemTag::UI);
			impl->library = library;
			bool complete = true;
#define SLT_FC_LOAD(result, name, arguments)                                                  \
	impl->name = reinterpret_cast<result(*) arguments>(Platform::FindSymbol(library, #name)); \
	complete = complete && impl->name != nullptr;
			SLT_FC_FUNCS(SLT_FC_LOAD)
#undef SLT_FC_LOAD
			impl->config = complete ? impl->FcInitLoadConfigAndFonts() : nullptr;
			if (!impl->config) {
				SLT_LOG_WARN("UI", "fontconfig didn't initialize (no system font fallback)");
				Delete(impl);
				return nullptr;
			}
			return impl;
		}

		const MappedFile* Map(const str& path) {
			if (auto known = files.find(path); known != files.end()) {
				return known->second->data ? known->second : nullptr;
			}
			MappedFile* mapped = New<MappedFile>(MemTag::UI);
			files.emplace(path, mapped);
			const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
			if (descriptor < 0) {
				return nullptr;
			}
			struct stat status{};
			if (::fstat(descriptor, &status) == 0 && status.st_size > 0) {
				void* data = ::mmap(nullptr, static_cast<size_t>(status.st_size), PROT_READ, MAP_PRIVATE, descriptor, 0);
				if (data != MAP_FAILED) {
					mapped->data = data;
					mapped->size = static_cast<size_t>(status.st_size);
				}
			}
			::close(descriptor);
			return mapped->data ? mapped : nullptr;
		}

		const SystemFontFile* Match(u32 codepoint, u16 weight, bool italic) {
			FcPattern* pattern = FcPatternCreate();
			FcCharSet* wanted = FcCharSetCreate();
			if (!pattern || !wanted) {
				return nullptr;
			}
			FcCharSetAddChar(wanted, codepoint);
			FcPatternAddCharSet(pattern, "charset", wanted);
			FcPatternAddInteger(pattern, "weight", FcWeightFromOpenType(weight));
			FcPatternAddInteger(pattern, "slant", italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
			FcPatternAddBool(pattern, "scalable", 1);
			FcConfigSubstitute(config, pattern, FC_MATCH_PATTERN);
			FcDefaultSubstitute(pattern);

			const SystemFontFile* found = nullptr;
			int result = 0;
			if (FcPattern* match = FcFontMatch(config, pattern, &result)) {
				FcCharSet* covered = nullptr;
				unsigned char* file = nullptr;
				int index = 0;
				// closest match may still lack character
				if (result == FC_RESULT_MATCH && FcPatternGetCharSet(match, "charset", 0, &covered) == FC_RESULT_MATCH &&
					FcCharSetHasChar(covered, codepoint) && FcPatternGetString(match, "file", 0, &file) == FC_RESULT_MATCH && file) {
					FcPatternGetInteger(match, "index", 0, &index);
					found = FaceOf(reinterpret_cast<const char*>(file), static_cast<u32>(index) & 0xFFFFu);
				}
				FcPatternDestroy(match);
			}
			FcCharSetDestroy(wanted);
			FcPatternDestroy(pattern);
			return found;
		}

		const SystemFontFile* FaceOf(const str& path, u32 faceIndex) {
			const str key = std::format("{}#{}", path, faceIndex);
			if (auto known = faces.find(key); known != faces.end()) {
				return known->second;
			}
			const MappedFile* mapped = Map(path);
			if (!mapped) {
				return nullptr;
			}
			SystemFontFile* face = New<SystemFontFile>(MemTag::UI);
			face->data = static_cast<const u8*>(mapped->data);
			face->size = mapped->size;
			face->faceIndex = faceIndex;
			faces.emplace(key, face);
			return face;
		}
	};

	SystemFontFallback::~SystemFontFallback() {
		if (_impl) {
			Delete(_impl);
		}
	}

	const SystemFontFile* SystemFontFallback::Find(u32 codepoint, u16 weight, bool italic) {
		if (!_attempted) {
			_attempted = true;
			_impl = Impl::Create();
		}
		if (!_impl) {
			return nullptr;
		}
		const u64 key = static_cast<u64>(codepoint) | (static_cast<u64>(weight) << 32) | (static_cast<u64>(italic ? 1 : 0) << 48);
		if (auto known = _impl->matches.find(key); known != _impl->matches.end()) {
			return known->second;
		}
		const SystemFontFile* found = _impl->Match(codepoint, weight, italic);
		_impl->matches.emplace(key, found);
		return found;
	}

} // namespace Shalltear::UI
