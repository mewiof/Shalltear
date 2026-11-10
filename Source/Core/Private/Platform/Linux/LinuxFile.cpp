#include "Platform/NativeFile.h"

#include "Shalltear/Core/Log.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Shalltear::Platform {

	u64 GetFileCreationTimeUnix(const str& utf8Path) {
		struct statx status{};
		if (::statx(AT_FDCWD, utf8Path.c_str(), 0, STATX_BTIME | STATX_MTIME, &status) != 0) {
			return 0;
		}
		// birth time missing on some file systems
		const statx_timestamp& stamp = (status.stx_mask & STATX_BTIME) ? status.stx_btime : status.stx_mtime;
		return stamp.tv_sec > 0 ? static_cast<u64>(stamp.tv_sec) : 0;
	}

	struct DirectoryWatcher {
		static constexpr u32 WATCH_MASK = IN_CREATE | IN_DELETE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB |
										  IN_MOVED_FROM | IN_MOVED_TO;

		Path directory;
		FileWatcher::Callback onEvent;
		std::thread workerThread;
		int inotifyFd = -1;
		int stopFd = -1;
		// relative to directory, empty for directory itself
		std::unordered_map<int, str> watched;
		bool limitReported = false;

		DirectoryWatcher(const Path& dir, FileWatcher::Callback cb) : directory(dir), onEvent(std::move(cb)) {
			inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
			stopFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
			if (inotifyFd < 0 || stopFd < 0) {
				return;
			}
			WatchTree({}, false);
			if (watched.empty()) {
				return;
			}
			workerThread = std::thread(&DirectoryWatcher::WorkerLoop, this);
		}

		~DirectoryWatcher() {
			if (workerThread.joinable()) {
				const u64 one = 1;
				[[maybe_unused]] const ssize_t written = ::write(stopFd, &one, sizeof(one));
				workerThread.join();
			}
			if (inotifyFd >= 0) {
				::close(inotifyFd);
			}
			if (stopFd >= 0) {
				::close(stopFd);
			}
		}

		str PhysicalPath(const str& relative) const {
			return relative.empty() ? directory.ToString() : directory.ToString() + "/" + relative;
		}

		static str Join(const str& relative, std::string_view name) {
			return relative.empty() ? str(name) : relative + "/" + str(name);
		}

		void Emit(const str& relative, FileAction action) const {
			if (onEvent) {
				onEvent(Path(PhysicalPath(relative)), action);
			}
		}

		void Watch(const str& relative) {
			const int descriptor = ::inotify_add_watch(inotifyFd, PhysicalPath(relative).c_str(), WATCH_MASK | IN_ONLYDIR);
			if (descriptor >= 0) {
				watched[descriptor] = relative;
				return;
			}
			if (errno == ENOSPC && !limitReported) {
				limitReported = true;
				SLT_LOG_WARN("FileSystem", "FileWatcher: out of inotify watches under {}. Raise fs.inotify.max_user_watches",
							 directory.ToString());
			}
		}

		// inotify isn't recursive. Files inside a new directory reported as added
		void WatchTree(const str& relative, bool announce) {
			Watch(relative);
			std::error_code ec;
			std::filesystem::directory_iterator it(PhysicalPath(relative), ec);
			for (; !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
				const str child = Join(relative, it->path().filename().string());
				std::error_code typeEc;
				const bool isDirectory = it->is_directory(typeEc) && !it->is_symlink(typeEc);
				if (announce) {
					Emit(child, FileAction::Added);
				}
				if (isDirectory) {
					WatchTree(child, announce);
				}
			}
		}

		void WorkerLoop() {
			alignas(inotify_event) char buffer[64 * 1024];
			pollfd waits[2] = {{inotifyFd, POLLIN, 0}, {stopFd, POLLIN, 0}};
			for (;;) {
				if (::poll(waits, 2, -1) < 0) {
					if (errno == EINTR) {
						continue;
					}
					break;
				}
				if (waits[1].revents != 0) {
					break;
				}
				const ssize_t length = ::read(inotifyFd, buffer, sizeof(buffer));
				if (length <= 0) {
					continue;
				}
				for (ssize_t offset = 0; offset < length;) {
					inotify_event event;
					std::memcpy(&event, buffer + offset, sizeof(event));
					const char* name = buffer + offset + sizeof(event);
					offset += static_cast<ssize_t>(sizeof(event) + event.len);
					HandleNamed(event, name);
				}
			}
		}

		void HandleNamed(const inotify_event& header, const char* name) {
			if (header.mask & IN_Q_OVERFLOW) {
				SLT_LOG_WARN("FileSystem", "FileWatcher: notification overflow, events were dropped ({})", directory.ToString());
				return;
			}
			auto found = watched.find(header.wd);
			if (found == watched.end()) {
				return;
			}
			if (header.mask & IN_IGNORED) {
				watched.erase(found);
				return;
			}
			if (header.len == 0) {
				return;
			}
			const str relative = Join(found->second, name);
			const bool isDirectory = (header.mask & IN_ISDIR) != 0;

			if (header.mask & IN_CREATE) {
				Emit(relative, FileAction::Added);
				if (isDirectory) {
					WatchTree(relative, true);
				}
			} else if (header.mask & IN_DELETE) {
				Emit(relative, FileAction::Removed);
			} else if (header.mask & (IN_MOVED_FROM | IN_MOVED_TO)) {
				Emit(relative, FileAction::Renamed);
				if (isDirectory && (header.mask & IN_MOVED_TO)) {
					WatchTree(relative, false);
				}
			} else if (header.mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB)) {
				Emit(relative, FileAction::Modified);
			}
		}
	};

	DirectoryWatcher* StartDirectoryWatcher(const Path& directory, FileWatcher::Callback callback) {
		return new DirectoryWatcher(directory, std::move(callback));
	}

	void StopDirectoryWatcher(DirectoryWatcher* watcher) {
		delete watcher;
	}

} // namespace Shalltear::Platform
