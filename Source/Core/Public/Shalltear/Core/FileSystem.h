#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Memory.h"

#include <string>
#include <functional>
#include <span>
#include <vector>

namespace Shalltear {

	enum class FileAction {
		Added,
		Modified,
		Removed,
		Renamed
	};

	struct SHALLTEAR_CORE_API Buffer {
		u8* data = nullptr;
		size_t size = 0;
		MemTag tag = MemTag::Unknown;

		Buffer() = default;
		Buffer(size_t byteCount, MemTag memTag);
		~Buffer();

		Buffer(const Buffer&) = delete;
		Buffer& operator=(const Buffer&) = delete;

		Buffer(Buffer&& other) noexcept;
		Buffer& operator=(Buffer&& other) noexcept;

		void Allocate(size_t byteCount, MemTag memTag);
		void Release();

		operator bool() const { return data != nullptr; }
	};

	class SHALLTEAR_CORE_API Path {
	public:
		Path() = default;
		Path(const str& path);
		Path(const char* path);

		Path(const Path& other) = default;
		Path& operator=(const Path& other) = default;

		Path(Path&& other) noexcept = default;
		Path& operator=(Path&& other) noexcept = default;

		Path operator/(const Path& other) const;
		Path& operator/=(const Path& other);
		Path operator/(const str& other) const;
		Path& operator/=(const str& other);
		Path operator/(const char* other) const;
		Path& operator/=(const char* other);

		bool operator==(const Path& other) const { return _path == other._path; }
		bool operator!=(const Path& other) const { return _path != other._path; }

		str GetDirectory() const;
		str GetFilename() const;
		str GetExtension() const;
		bool IsAbsolute() const;

		const str& ToString() const { return _path; }
		const char* CStr() const { return _path.c_str(); }
	private:
		void Normalize();
		str _path;
	};

	class SHALLTEAR_CORE_API FileSystem {
	public:
		static void Mount(const str& scheme, const Path& physicalPath);
		static void Unmount(const str& scheme);

		static u64 GetMountGeneration();
		static Path Resolve(const Path& virtualPath);
		static Path Virtualize(const Path& physicalPath);

		static Buffer ReadFile(const Path& path, MemTag tag);
		static str ReadTextFile(const Path& path, MemTag tag = MemTag::Unknown);
		static bool WriteFile(const Path& path, const Buffer& buffer);
		static bool WriteTextFile(const Path& path, const str& text);

		static bool Exists(const Path& path);
		static bool IsDirectory(const Path& path);
		static bool CreateDirectory(const Path& path);
		static size_t GetFileSize(const Path& path);
		static u64 GetLastWriteTime(const Path& path);

		static u64 GetLastWriteTimeUnix(const Path& path);
		static u64 GetCreationTimeUnix(const Path& path);

		static Path GetUserDataDirectory();

		static Path GetUserHomeDirectory();
	};

	class SHALLTEAR_CORE_API FileReader {
	public:
		FileReader() = default;
		~FileReader();

		FileReader(const FileReader&) = delete;
		FileReader& operator=(const FileReader&) = delete;
		FileReader(FileReader&& other) noexcept;
		FileReader& operator=(FileReader&& other) noexcept;

		bool Open(const Path& physicalPath);
		bool IsOpen() const { return _handle != nullptr; }

		u64 GetSize() const { return _size; }

		bool ReadAt(u64 offset, std::span<u8> out) const;
	private:
		void* _handle = nullptr;
		u64 _size = 0;
	};

	class SHALLTEAR_CORE_API FileWatcher {
	public:
		using Callback = std::function<void(const Path&, FileAction)>;

		FileWatcher(const Path& directory, Callback callback);
		~FileWatcher();

		FileWatcher(const FileWatcher&) = delete;
		FileWatcher& operator=(const FileWatcher&) = delete;

		FileWatcher(FileWatcher&& other) noexcept;
		FileWatcher& operator=(FileWatcher&& other) noexcept;
	private:
		struct Impl;
		Impl* _impl;
	};

} // namespace Shalltear
