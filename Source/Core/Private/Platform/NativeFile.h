#pragma once

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::Platform {

	// null on failure
	void* OpenFileForRead(const str& utf8Path, FileSharing sharing);
	void* ReopenFile(void* file, FileSharing sharing);
	void CloseFile(void* file);

	// false while a file from OpenFileForRead is open
	bool CanReplaceFile(const str& utf8Path);

	bool GetFileSize(void* file, u64& outSize);
	bool ReadFileAt(void* file, u64 offset, std::span<u8> out);

	u64 GetFileCreationTimeUnix(const str& utf8Path);

	struct DirectoryWatcher;

	// callback invoked on watcher thread
	DirectoryWatcher* StartDirectoryWatcher(const Path& directory, FileWatcher::Callback callback);
	void StopDirectoryWatcher(DirectoryWatcher* watcher);

} // namespace Shalltear::Platform
