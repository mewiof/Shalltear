#include "Shalltear/Core/FileSystem.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringUtil.h"

#include "FileAccessRecorder.h"
#include "Module/EngineStateTable.h"

#ifdef _WIN32
#include <windows.h>
#undef CreateDirectory
#endif

namespace Shalltear {

	Buffer::Buffer(size_t byteCount, MemTag memTag) {
		Allocate(byteCount, memTag);
	}

	Buffer::~Buffer() {
		Release();
	}

	Buffer::Buffer(Buffer&& other) noexcept
		: data(other.data), size(other.size), tag(other.tag) {
		other.data = nullptr;
		other.size = 0;
		other.tag = MemTag::Unknown;
	}

	Buffer& Buffer::operator=(Buffer&& other) noexcept {
		if (this != &other) {
			Release();
			data = other.data;
			size = other.size;
			tag = other.tag;
			other.data = nullptr;
			other.size = 0;
			other.tag = MemTag::Unknown;
		}
		return *this;
	}

	void Buffer::Allocate(size_t byteCount, MemTag memTag) {
		Release();
		if (byteCount > 0) {
			data = static_cast<u8*>(Shalltear::Allocate(byteCount, memTag));
			size = byteCount;
			tag = memTag;
		}
	}

	void Buffer::Release() {
		if (data) {
			Shalltear::Free(data);
			data = nullptr;
			size = 0;
			tag = MemTag::Unknown;
		}
	}

	Path::Path(const str& path) : _path(path) { Normalize(); }
	Path::Path(const char* path) : _path(path) { Normalize(); }

	void Path::Normalize() {
		std::replace(_path.begin(), _path.end(), '\\', '/');
		while (_path.length() > 1 && _path.back() == '/') {
			_path.pop_back();
		}
	}

	Path Path::operator/(const Path& other) const {
		return Path(_path + "/" + other._path);
	}

	Path& Path::operator/=(const Path& other) {
		_path += "/" + other._path;
		Normalize();
		return *this;
	}

	Path Path::operator/(const str& other) const {
		return Path(_path + "/" + other);
	}

	Path& Path::operator/=(const str& other) {
		_path += "/";
		_path += other;
		Normalize();
		return *this;
	}

	Path Path::operator/(const char* other) const {
		return Path(_path + "/" + other);
	}

	Path& Path::operator/=(const char* other) {
		_path += "/";
		_path += other;
		Normalize();
		return *this;
	}

	str Path::GetDirectory() const {
		size_t pos = _path.find_last_of('/');
		if (pos != str::npos) {
			return _path.substr(0, pos);
		}
		return "";
	}

	str Path::GetFilename() const {
		size_t pos = _path.find_last_of('/');
		if (pos != str::npos) {
			return _path.substr(pos + 1);
		}
		return _path;
	}

	str Path::GetExtension() const {
		str filename = GetFilename();
		size_t pos = filename.find_last_of('.');
		if (pos != str::npos && pos != 0) {
			return filename.substr(pos);
		}
		return "";
	}

	bool Path::IsAbsolute() const {
		if (_path.empty())
			return false;
		if (_path[0] == '/')
			return true;
		if (_path.length() >= 2 && _path[1] == ':')
			return true;
		return false;
	}

	namespace {

		struct VfsData {
			std::unordered_map<str, Path> mounts;
			std::shared_mutex mutex;
			std::atomic<u64> generation{0};
		};

		VfsData* _boundVfs = nullptr;

		VfsData& GetVfs() {
			if (_boundVfs)
				return *_boundVfs;
			static VfsData local;
			return local;
		}

	} // anonymous namespace

	void FileSystem::Mount(const str& scheme, const Path& physicalPath) {
		auto& vfs = GetVfs();
		std::unique_lock lock(vfs.mutex);
		vfs.mounts[scheme] = physicalPath;
		vfs.generation.fetch_add(1, std::memory_order_relaxed);
	}

	void FileSystem::Unmount(const str& scheme) {
		auto& vfs = GetVfs();
		std::unique_lock lock(vfs.mutex);
		vfs.mounts.erase(scheme);
		vfs.generation.fetch_add(1, std::memory_order_relaxed);
	}

	u64 FileSystem::GetMountGeneration() {
		return GetVfs().generation.load(std::memory_order_relaxed);
	}

	Path FileSystem::Resolve(const Path& virtualPath) {
		str pathStr = virtualPath.ToString();
		size_t pos = pathStr.find("://");
		if (pos != str::npos) {
			str scheme = pathStr.substr(0, pos);
			str rest = pathStr.substr(pos + 3);

			auto& vfs = GetVfs();
			std::shared_lock lock(vfs.mutex);
			auto it = vfs.mounts.find(scheme);
			if (it != vfs.mounts.end()) {
				return it->second / rest;
			}
		}
		return virtualPath;
	}

	Path FileSystem::Virtualize(const Path& physicalPath) {
		const str& text = physicalPath.ToString();
		if (text.empty() || text.find("://") != str::npos) {
			return physicalPath;
		}
		str lowered = StringUtil::ToLower(text);

		auto& vfs = GetVfs();
		std::shared_lock lock(vfs.mutex);

		const str* bestScheme = nullptr;
		size_t bestLength = 0;
		for (const auto& [scheme, root] : vfs.mounts) {
			const str& rootText = root.ToString();
			if (rootText.empty() || rootText.size() > text.size() || rootText.size() <= bestLength) {
				continue;
			}
			if (text.size() != rootText.size() && text[rootText.size()] != '/') {
				continue;
			}
			if (lowered.compare(0, rootText.size(), StringUtil::ToLower(rootText)) != 0) {
				continue;
			}
			bestScheme = &scheme;
			bestLength = rootText.size();
		}
		if (!bestScheme) {
			return physicalPath;
		}
		str rest = text.substr(bestLength);
		if (!rest.empty() && rest.front() == '/') {
			rest.erase(0, 1);
		}
		if (rest.empty()) {
			return physicalPath;
		}
		return Path(*bestScheme + "://" + rest);
	}

	namespace {

		thread_local FileAccessRecording* _accessSink = nullptr;

		std::atomic<std::thread::id> _recordingThread{};

		bool NotSeen(const Vector<str>& seen, std::string_view text) {
			for (const str& already : seen) {
				if (already == text) {
					return false;
				}
			}
			return true;
		}

		void NoteRead(const Path& resolved, const void* data, size_t size) {
			FileAccessRecording* sink = _accessSink;
			if (!sink) {
				return;
			}
			str key = FileSystem::Virtualize(resolved).ToString();
			if (key.empty()) {
				return;
			}
			for (const FileAccessRead& already : sink->reads) {
				if (already.path == key) {
					return;
				}
			}

			FileAccessRead record;
			record.path = std::move(key);
			record.size = static_cast<u64>(size);
			record.mtime = FileSystem::GetLastWriteTime(resolved);
			record.contentHash = PakContentHash(data, size);
			sink->reads.push_back(std::move(record));
		}

		void NoteWrite(const Path& resolved) {
			FileAccessRecording* sink = _accessSink;
			if (!sink) {
				return;
			}
			str key = FileSystem::Virtualize(resolved).ToString();
			if (NotSeen(sink->writes, key)) {
				sink->writes.push_back(std::move(key));
			}
		}

	} // anonymous namespace

	ScopedFileAccessRecorder::ScopedFileAccessRecorder(FileAccessRecording& sink) noexcept
		: _sink(&sink), _previous(_accessSink) {
		if (!_previous) {
			std::thread::id self = std::this_thread::get_id();
			std::thread::id owner = _recordingThread.load(std::memory_order_relaxed);
			SLT_CORE_ASSERT_MSG(owner == std::thread::id() || owner == self,
								"file access is recorded on 1 thread. Asset operations are main thread only");
			_recordingThread.store(self, std::memory_order_relaxed);
		}
		_accessSink = &sink;
	}

	ScopedFileAccessRecorder::~ScopedFileAccessRecorder() {
		_accessSink = _previous;
		if (!_previous || _previous == _sink) {
			return;
		}

		for (const FileAccessRead& read : _sink->reads) {
			bool fresh = true;
			for (const FileAccessRead& already : _previous->reads) {
				if (already.path == read.path) {
					fresh = false;
					break;
				}
			}
			if (fresh) {
				_previous->reads.push_back(read);
			}
		}
		for (const str& write : _sink->writes) {
			if (NotSeen(_previous->writes, write)) {
				_previous->writes.push_back(write);
			}
		}
	}

	Buffer FileSystem::ReadFile(const Path& path, MemTag tag) {
		Path resolved = Resolve(path);
		std::ifstream file(resolved.CStr(), std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			NoteRead(resolved, nullptr, 0);
			return Buffer();
		}

		size_t fileSize = (size_t)file.tellg();
		file.seekg(0);

		Buffer buffer(fileSize, tag);
		file.read(reinterpret_cast<char*>(buffer.data), fileSize);
		NoteRead(resolved, buffer.data, buffer.size);
		return buffer;
	}

	str FileSystem::ReadTextFile(const Path& path, MemTag tag) {
		Path resolved = Resolve(path);
		std::ifstream file(resolved.CStr(), std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			NoteRead(resolved, nullptr, 0);
			return "";
		}

		size_t fileSize = (size_t)file.tellg();
		file.seekg(0);

		str text;
		text.resize(fileSize);
		file.read(text.data(), fileSize);
		NoteRead(resolved, text.data(), text.size());
		return text;
	}

	bool FileSystem::WriteFile(const Path& path, const Buffer& buffer) {
		Path resolved = Resolve(path);
		std::ofstream file(resolved.CStr(), std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		file.write(reinterpret_cast<const char*>(buffer.data), buffer.size);
		NoteWrite(resolved);
		return true;
	}

	bool FileSystem::WriteTextFile(const Path& path, const str& text) {
		Path resolved = Resolve(path);
		std::ofstream file(resolved.CStr(), std::ios::binary);
		if (!file.is_open()) {
			return false;
		}

		file.write(text.c_str(), text.size());
		NoteWrite(resolved);
		return true;
	}

	bool FileSystem::Exists(const Path& path) {
		return std::filesystem::exists(Resolve(path).ToString());
	}

	bool FileSystem::IsDirectory(const Path& path) {
		return std::filesystem::is_directory(Resolve(path).ToString());
	}

	bool FileSystem::CreateDirectory(const Path& path) {
		std::error_code ec;
		return std::filesystem::create_directories(Resolve(path).ToString(), ec);
	}

	size_t FileSystem::GetFileSize(const Path& path) {
		std::error_code ec;
		size_t size = std::filesystem::file_size(Resolve(path).ToString(), ec);
		if (ec)
			return 0;
		return size;
	}

	u64 FileSystem::GetLastWriteTime(const Path& path) {
		std::error_code ec;
		auto ftime = std::filesystem::last_write_time(Resolve(path).ToString(), ec);
		if (ec)
			return 0;
		return ftime.time_since_epoch().count();
	}

	u64 FileSystem::GetLastWriteTimeUnix(const Path& path) {
		std::error_code ec;
		auto ftime = std::filesystem::last_write_time(Resolve(path).ToString(), ec);
		if (ec)
			return 0;
		auto fileDelta = ftime.time_since_epoch() - std::filesystem::file_time_type::clock::now().time_since_epoch();
		auto sys = std::chrono::system_clock::now() +
				   std::chrono::duration_cast<std::chrono::system_clock::duration>(fileDelta);
		auto seconds = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
		return seconds > 0 ? static_cast<u64>(seconds) : 0;
	}

	u64 FileSystem::GetCreationTimeUnix(const Path& path) {
#ifdef _WIN32
		std::filesystem::path native{Resolve(path).ToString()};
		WIN32_FILE_ATTRIBUTE_DATA attributes{};
		if (!GetFileAttributesExW(native.c_str(), GetFileExInfoStandard, &attributes)) {
			return 0;
		}
		u64 ticks = (static_cast<u64>(attributes.ftCreationTime.dwHighDateTime) << 32) |
					attributes.ftCreationTime.dwLowDateTime;
		constexpr u64 TICKS_AT_UNIX_EPOCH = 116444736000000000ull;
		constexpr u64 TICKS_PER_SECOND = 10000000ull;
		return ticks > TICKS_AT_UNIX_EPOCH ? (ticks - TICKS_AT_UNIX_EPOCH) / TICKS_PER_SECOND : 0;
#else
		return GetLastWriteTimeUnix(path);
#endif
	}

#ifdef _WIN32
	namespace {
		str ReadEnvironmentPath(const wchar_t* name) {
			wchar_t buffer[MAX_PATH];
			DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
			if (length == 0 || length >= MAX_PATH) {
				return {};
			}
			return StringUtil::WideToUTF8(std::wstring_view(buffer, length));
		}
	} // anonymous namespace
#endif

	Path FileSystem::GetUserDataDirectory() {
#ifdef _WIN32
		str local = ReadEnvironmentPath(L"LOCALAPPDATA");
		if (local.empty()) {
			str profile = ReadEnvironmentPath(L"USERPROFILE");
			if (profile.empty()) {
				return {};
			}
			local = profile + "/AppData/Local";
		}
		return Path(local);
#else
		return {};
#endif
	}

	Path FileSystem::GetUserHomeDirectory() {
#ifdef _WIN32
		return Path(ReadEnvironmentPath(L"USERPROFILE"));
#else
		return {};
#endif
	}

	FileReader::~FileReader() {
#ifdef _WIN32
		if (_handle) {
			CloseHandle(static_cast<HANDLE>(_handle));
		}
#endif
	}

	FileReader::FileReader(FileReader&& other) noexcept : _handle(other._handle), _size(other._size) {
		other._handle = nullptr;
		other._size = 0;
	}

	FileReader& FileReader::operator=(FileReader&& other) noexcept {
		if (this != &other) {
			FileReader closing(std::move(*this));
			_handle = other._handle;
			_size = other._size;
			other._handle = nullptr;
			other._size = 0;
		}
		return *this;
	}

	bool FileReader::Open(const Path& physicalPath) {
		*this = FileReader();
#ifdef _WIN32
		const std::wstring wide = StringUtil::UTF8ToWide(physicalPath.ToString());
		HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			return false;
		}
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0) {
			CloseHandle(handle);
			return false;
		}
		_handle = handle;
		_size = static_cast<u64>(size.QuadPart);
		return true;
#else
		(void)physicalPath;
		return false;
#endif
	}

	bool FileReader::ReadAt(u64 offset, std::span<u8> out) const {
		if (!_handle || offset > _size || out.size() > _size - offset) {
			return false;
		}
#ifdef _WIN32
		constexpr u64 LARGEST_READ = 1ull << 30;
		u8* into = out.data();
		u64 left = out.size();
		u64 at = offset;
		while (left > 0) {
			const DWORD asked = static_cast<DWORD>(left < LARGEST_READ ? left : LARGEST_READ);
			OVERLAPPED position{};
			position.Offset = static_cast<DWORD>(at & 0xFFFFFFFFull);
			position.OffsetHigh = static_cast<DWORD>(at >> 32);
			DWORD read = 0;
			if (!::ReadFile(static_cast<HANDLE>(_handle), into, asked, &read, &position) || read != asked) {
				return false;
			}
			into += read;
			left -= read;
			at += read;
		}
		return true;
#else
		return false;
#endif
	}

#ifdef _WIN32

	struct FileWatcher::Impl {
		static constexpr size_t WATCH_BUFFER_SIZE = 64 * 1024;

		Path directory;
		Callback onEvent;
		std::atomic<bool> running{false};
		std::thread workerThread;
		HANDLE hDirectory = INVALID_HANDLE_VALUE;

		Impl(const Path& dir, Callback cb) : directory(dir), onEvent(cb) {
			Start();
		}

		~Impl() {
			Stop();
		}

		void Start() {
			str dirStr = directory.ToString();
			std::wstring wideDir = StringUtil::UTF8ToWide(dirStr);

			hDirectory = CreateFileW(
				wideDir.c_str(),
				FILE_LIST_DIRECTORY,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				NULL,
				OPEN_EXISTING,
				FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
				NULL);

			if (hDirectory == INVALID_HANDLE_VALUE) {
				return;
			}

			running = true;
			workerThread = std::thread(&Impl::WorkerLoop, this);
		}

		void Stop() {
			if (running.exchange(false)) {
				if (hDirectory != INVALID_HANDLE_VALUE) {
					CancelIoEx(hDirectory, NULL);
				}
				if (workerThread.joinable()) {
					workerThread.join();
				}
				if (hDirectory != INVALID_HANDLE_VALUE) {
					CloseHandle(hDirectory);
					hDirectory = INVALID_HANDLE_VALUE;
				}
			}
		}

		void WorkerLoop() {
			std::vector<u8> buffer(WATCH_BUFFER_SIZE);
			OVERLAPPED overlapped = {};
			overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

			bool pending = false;

			while (running) {
				if (!pending) {
					ResetEvent(overlapped.hEvent);
					BOOL result = ReadDirectoryChangesW(
						hDirectory,
						buffer.data(),
						static_cast<DWORD>(buffer.size()),
						TRUE,
						FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION,
						NULL,
						&overlapped,
						NULL);

					if (!result) {
						break;
					}
					pending = true;
				}

				DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 100);
				if (waitResult != WAIT_OBJECT_0) {
					continue;
				}

				DWORD bytesReturned = 0;
				if (!GetOverlappedResult(hDirectory, &overlapped, &bytesReturned, FALSE)) {
					break;
				}
				pending = false;

				if (bytesReturned == 0) {
					SLT_LOG_WARN("FileSystem", "FileWatcher: notification overflow, events were dropped ({})", directory.ToString());
					continue;
				}

				FILE_NOTIFY_INFORMATION* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer.data());
				do {
					str fileName = StringUtil::WideToUTF8(
						std::wstring_view(fni->FileName, fni->FileNameLength / sizeof(WCHAR)));

					Path fullPath = directory / fileName;
					FileAction action;

					switch (fni->Action) {
					case FILE_ACTION_ADDED:
						action = FileAction::Added;
						break;
					case FILE_ACTION_REMOVED:
						action = FileAction::Removed;
						break;
					case FILE_ACTION_MODIFIED:
						action = FileAction::Modified;
						break;
					case FILE_ACTION_RENAMED_OLD_NAME:
					case FILE_ACTION_RENAMED_NEW_NAME:
						action = FileAction::Renamed;
						break;
					default:
						action = FileAction::Modified;
						break;
					}

					if (onEvent) {
						onEvent(fullPath, action);
					}

					if (fni->NextEntryOffset == 0) {
						break;
					}
					fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<u8*>(fni) + fni->NextEntryOffset);
				} while (true);
			}

			CloseHandle(overlapped.hEvent);
		}
	};

#else
#pragma message("FileWatcher not implemented on this platform")
	struct FileWatcher::Impl {
		Impl(const Path& dir, Callback cb) {}
		~Impl() {}
	};
#endif

	FileWatcher::FileWatcher(const Path& directory, Callback callback) {
		_impl = new Impl(FileSystem::Resolve(directory), callback);
	}

	FileWatcher::~FileWatcher() {
		delete _impl;
	}

	FileWatcher::FileWatcher(FileWatcher&& other) noexcept : _impl(other._impl) {
		other._impl = nullptr;
	}

	FileWatcher& FileWatcher::operator=(FileWatcher&& other) noexcept {
		if (this != &other) {
			delete _impl;
			_impl = other._impl;
			other._impl = nullptr;
		}
		return *this;
	}

	namespace ModuleInternal {

		void CaptureFileSystemEngineState(EngineStateTable& table) {
			table.fileSystem = &GetVfs();
		}

		void BindFileSystemEngineState(const EngineStateTable& table) {
			_boundVfs = static_cast<VfsData*>(table.fileSystem);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
