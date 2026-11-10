#include "UI/SystemFontFallback.h"

#include "Platform/Windows/WindowsHeaders.h"
#include <dwrite_2.h>

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringUtil.h"

#include <format>
#include <string>

namespace Shalltear::UI {

	namespace {

		using CreateFactoryFn = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

		template <typename T>
		void ReleaseCom(T*& object) {
			if (object) {
				object->Release();
				object = nullptr;
			}
		}

		class CharacterSource final : public IDWriteTextAnalysisSource {
		public:
			WCHAR text[2] = {};
			UINT32 length = 0;
			const WCHAR* locale = L"";

			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
				if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDWriteTextAnalysisSource))) {
					*out = static_cast<IDWriteTextAnalysisSource*>(this);
					return S_OK;
				}
				*out = nullptr;
				return E_NOINTERFACE;
			}
			// stack object, count never read
			ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
			ULONG STDMETHODCALLTYPE Release() override { return 1; }

			HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 position, const WCHAR** out, UINT32* count) override {
				*out = position < length ? text + position : nullptr;
				*count = position < length ? length - position : 0;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 position, const WCHAR** out, UINT32* count) override {
				const bool inside = position > 0 && position <= length;
				*out = inside ? text : nullptr;
				*count = inside ? position : 0;
				return S_OK;
			}
			DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override {
				return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
			}
			HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 position, UINT32* count, const WCHAR** out) override {
				*out = locale;
				*count = position < length ? length - position : 0;
				return S_OK;
			}
			HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 position, UINT32* count, IDWriteNumberSubstitution** out) override {
				*out = nullptr;
				*count = position < length ? length - position : 0;
				return S_OK;
			}
		};

		struct MappedFile {
			HANDLE file = INVALID_HANDLE_VALUE;
			HANDLE mapping = nullptr;
			SystemFontFile view;
		};

		std::wstring LocalPathOf(IDWriteFontFile& file) {
			const void* key = nullptr;
			UINT32 keySize = 0;
			IDWriteFontFileLoader* loader = nullptr;
			IDWriteLocalFontFileLoader* local = nullptr;
			std::wstring path;
			if (SUCCEEDED(file.GetReferenceKey(&key, &keySize)) && SUCCEEDED(file.GetLoader(&loader)) &&
				SUCCEEDED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader), reinterpret_cast<void**>(&local)))) {
				UINT32 length = 0;
				if (SUCCEEDED(local->GetFilePathLengthFromKey(key, keySize, &length)) && length > 0) {
					path.resize(length + 1);
					if (SUCCEEDED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1))) {
						path.resize(length);
					} else {
						path.clear();
					}
				}
			}
			ReleaseCom(local);
			ReleaseCom(loader);
			return path;
		}

	} // anonymous namespace

	struct SystemFontFallback::Impl {
		IDWriteFactory2* factory = nullptr;
		IDWriteFontFallback* fallback = nullptr;
		IDWriteFontCollection* collection = nullptr;
		std::wstring locale;
		HashMap<u64, const SystemFontFile*> matches{GetMemoryResource(MemTag::UI)};
		HashMap<str, MappedFile*> files{GetMemoryResource(MemTag::UI)};

		~Impl() {
			for (auto& [key, mapped] : files) {
				if (mapped->view.data) {
					::UnmapViewOfFile(mapped->view.data);
				}
				if (mapped->mapping) {
					::CloseHandle(mapped->mapping);
				}
				if (mapped->file != INVALID_HANDLE_VALUE) {
					::CloseHandle(mapped->file);
				}
				Delete(mapped);
			}
			ReleaseCom(collection);
			ReleaseCom(fallback);
			ReleaseCom(factory);
		}

		static Impl* Create() {
			HMODULE module = ::LoadLibraryW(L"dwrite.dll");
			auto create = module ? reinterpret_cast<CreateFactoryFn>(reinterpret_cast<void*>(::GetProcAddress(module, "DWriteCreateFactory")))
								 : nullptr;
			IUnknown* unknown = nullptr;
			if (!create || FAILED(create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), &unknown)) || !unknown) {
				SLT_LOG_WARN("UI", "DirectWrite 2 unavailable (no installed font fallback)");
				return nullptr;
			}
			Impl* impl = New<Impl>(MemTag::UI);
			impl->factory = reinterpret_cast<IDWriteFactory2*>(unknown);
			if (FAILED(impl->factory->GetSystemFontFallback(&impl->fallback)) ||
				FAILED(impl->factory->GetSystemFontCollection(&impl->collection, FALSE))) {
				SLT_LOG_WARN("UI", "DirectWrite system font fallback unavailable");
				Delete(impl);
				return nullptr;
			}
			WCHAR name[LOCALE_NAME_MAX_LENGTH] = {};
			impl->locale = ::GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0 ? std::wstring(name) : std::wstring(L"en-US");
			return impl;
		}

		const SystemFontFile* Map(const std::wstring& path, u32 faceIndex) {
			str key = std::format("{}#{}", StringUtil::WideToUTF8(path), faceIndex);
			if (auto known = files.find(key); known != files.end()) {
				return known->second->view.data ? &known->second->view : nullptr;
			}
			MappedFile* mapped = New<MappedFile>(MemTag::UI);
			files.emplace(std::move(key), mapped);
			mapped->file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			LARGE_INTEGER size{};
			if (mapped->file == INVALID_HANDLE_VALUE || !::GetFileSizeEx(mapped->file, &size) || size.QuadPart <= 0) {
				return nullptr;
			}
			mapped->mapping = ::CreateFileMappingW(mapped->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
			void* view = mapped->mapping ? ::MapViewOfFile(mapped->mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
			if (!view) {
				return nullptr;
			}
			mapped->view.data = static_cast<const u8*>(view);
			mapped->view.size = static_cast<size_t>(size.QuadPart);
			mapped->view.faceIndex = faceIndex;
			return &mapped->view;
		}

		const SystemFontFile* FileOf(IDWriteFont& font) {
			IDWriteFontFace* face = nullptr;
			if (FAILED(font.CreateFontFace(&face))) {
				return nullptr;
			}
			UINT32 count = 0;
			IDWriteFontFile* file = nullptr;
			if (FAILED(face->GetFiles(&count, nullptr)) || count != 1 || FAILED(face->GetFiles(&count, &file))) {
				ReleaseCom(face);
				return nullptr;
			}
			const u32 faceIndex = face->GetIndex();
			ReleaseCom(face);
			const std::wstring path = LocalPathOf(*file);
			ReleaseCom(file);
			return path.empty() ? nullptr : Map(path, faceIndex);
		}

		const SystemFontFile* Match(u32 codepoint, u16 weight, bool italic) {
			CharacterSource source;
			if (codepoint > 0xFFFF) {
				const u32 offset = codepoint - 0x10000;
				source.text[0] = static_cast<WCHAR>(0xD800 + (offset >> 10));
				source.text[1] = static_cast<WCHAR>(0xDC00 + (offset & 0x3FF));
				source.length = 2;
			} else {
				source.text[0] = static_cast<WCHAR>(codepoint);
				source.length = 1;
			}
			source.locale = locale.c_str();
			UINT32 mappedLength = 0;
			IDWriteFont* font = nullptr;
			FLOAT scale = 1.0f;
			const HRESULT mapped = fallback->MapCharacters(&source, 0, source.length, collection, nullptr,
														   static_cast<DWRITE_FONT_WEIGHT>(weight),
														   italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
														   DWRITE_FONT_STRETCH_NORMAL, &mappedLength, &font, &scale);
			if (FAILED(mapped) || !font) {
				return nullptr;
			}
			const SystemFontFile* file = FileOf(*font);
			ReleaseCom(font);
			return file;
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
