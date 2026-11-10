#include "Platform/NativeFile.h"

#include "Platform/Windows/WindowsHeaders.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringUtil.h"

#include <atomic>
#include <filesystem>
#include <thread>
#include <vector>

namespace Shalltear::Platform {

	namespace {

		DWORD ShareModeOf(FileSharing sharing) {
			return sharing == FileSharing::ReadDelete ? FILE_SHARE_READ | FILE_SHARE_DELETE : FILE_SHARE_READ;
		}

		void* Checked(HANDLE handle) {
			return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
		}

	} // anonymous namespace

	void* OpenFileForRead(const str& utf8Path, FileSharing sharing) {
		const std::wstring wide = StringUtil::UTF8ToWide(utf8Path);
		return Checked(::CreateFileW(wide.c_str(), GENERIC_READ, ShareModeOf(sharing), nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
	}

	// writer's open already refused by share mode
	bool CanReplaceFile(const str&) {
		return true;
	}

	void* ReopenFile(void* file, FileSharing sharing) {
		if (!file) {
			return nullptr;
		}
		return Checked(::ReOpenFile(static_cast<HANDLE>(file), GENERIC_READ, ShareModeOf(sharing), 0));
	}

	void CloseFile(void* file) {
		if (file) {
			::CloseHandle(static_cast<HANDLE>(file));
		}
	}

	bool GetFileSize(void* file, u64& outSize) {
		LARGE_INTEGER size{};
		if (!file || !::GetFileSizeEx(static_cast<HANDLE>(file), &size) || size.QuadPart < 0) {
			return false;
		}
		outSize = static_cast<u64>(size.QuadPart);
		return true;
	}

	bool ReadFileAt(void* file, u64 offset, std::span<u8> out) {
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
			if (!::ReadFile(static_cast<HANDLE>(file), into, asked, &read, &position) || read != asked) {
				return false;
			}
			into += read;
			left -= read;
			at += read;
		}
		return true;
	}

	u64 GetFileCreationTimeUnix(const str& utf8Path) {
		std::filesystem::path native{utf8Path};
		WIN32_FILE_ATTRIBUTE_DATA attributes{};
		if (!::GetFileAttributesExW(native.c_str(), GetFileExInfoStandard, &attributes)) {
			return 0;
		}
		u64 ticks = (static_cast<u64>(attributes.ftCreationTime.dwHighDateTime) << 32) |
					attributes.ftCreationTime.dwLowDateTime;
		constexpr u64 TICKS_AT_UNIX_EPOCH = 116444736000000000ull;
		constexpr u64 TICKS_PER_SECOND = 10000000ull;
		return ticks > TICKS_AT_UNIX_EPOCH ? (ticks - TICKS_AT_UNIX_EPOCH) / TICKS_PER_SECOND : 0;
	}

	struct DirectoryWatcher {
		static constexpr size_t WATCH_BUFFER_SIZE = 64 * 1024;

		Path directory;
		FileWatcher::Callback onEvent;
		std::atomic<bool> running{false};
		std::thread workerThread;
		HANDLE hDirectory = INVALID_HANDLE_VALUE;

		DirectoryWatcher(const Path& dir, FileWatcher::Callback cb) : directory(dir), onEvent(cb) {
			Start();
		}

		~DirectoryWatcher() {
			Stop();
		}

		void Start() {
			str dirStr = directory.ToString();
			std::wstring wideDir = StringUtil::UTF8ToWide(dirStr);

			hDirectory = ::CreateFileW(
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
			workerThread = std::thread(&DirectoryWatcher::WorkerLoop, this);
		}

		void Stop() {
			if (running.exchange(false)) {
				if (hDirectory != INVALID_HANDLE_VALUE) {
					::CancelIoEx(hDirectory, NULL);
				}
				if (workerThread.joinable()) {
					workerThread.join();
				}
				if (hDirectory != INVALID_HANDLE_VALUE) {
					::CloseHandle(hDirectory);
					hDirectory = INVALID_HANDLE_VALUE;
				}
			}
		}

		void WorkerLoop() {
			std::vector<u8> buffer(WATCH_BUFFER_SIZE);
			OVERLAPPED overlapped = {};
			overlapped.hEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);

			bool pending = false;

			while (running) {
				if (!pending) {
					::ResetEvent(overlapped.hEvent);
					BOOL result = ::ReadDirectoryChangesW(
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

				DWORD waitResult = ::WaitForSingleObject(overlapped.hEvent, 100);
				if (waitResult != WAIT_OBJECT_0) {
					continue;
				}

				DWORD bytesReturned = 0;
				if (!::GetOverlappedResult(hDirectory, &overlapped, &bytesReturned, FALSE)) {
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

			::CloseHandle(overlapped.hEvent);
		}
	};

	DirectoryWatcher* StartDirectoryWatcher(const Path& directory, FileWatcher::Callback callback) {
		return new DirectoryWatcher(directory, std::move(callback));
	}

	void StopDirectoryWatcher(DirectoryWatcher* watcher) {
		delete watcher;
	}

} // namespace Shalltear::Platform
