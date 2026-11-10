#include "Platform/NativeFile.h"

#include <cerrno>
#include <cstdint>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Shalltear::Platform {

	namespace {

		// descriptor 0 is valid, null handle = closed
		void* ToHandle(int descriptor) {
			return descriptor < 0 ? nullptr : reinterpret_cast<void*>(static_cast<std::intptr_t>(descriptor) + 1);
		}

		int ToDescriptor(void* file) {
			return static_cast<int>(reinterpret_cast<std::intptr_t>(file) - 1);
		}

	} // anonymous namespace

	void* OpenFileForRead(const str& utf8Path, FileSharing) {
		const int descriptor = ::open(utf8Path.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor >= 0) {
			// no share modes here, lock probed by CanReplaceFile
			::flock(descriptor, LOCK_SH | LOCK_NB);
		}
		return ToHandle(descriptor);
	}

	bool CanReplaceFile(const str& utf8Path) {
		const int descriptor = ::open(utf8Path.c_str(), O_RDONLY | O_CLOEXEC);
		if (descriptor < 0) {
			return true;
		}
		const bool unheld = ::flock(descriptor, LOCK_EX | LOCK_NB) == 0;
		::close(descriptor);
		return unheld;
	}

	void* ReopenFile(void* file, FileSharing) {
		if (!file) {
			return nullptr;
		}
		return ToHandle(::fcntl(ToDescriptor(file), F_DUPFD_CLOEXEC, 0));
	}

	void CloseFile(void* file) {
		if (file) {
			::close(ToDescriptor(file));
		}
	}

	bool GetFileSize(void* file, u64& outSize) {
		struct stat status{};
		if (!file || ::fstat(ToDescriptor(file), &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0) {
			return false;
		}
		outSize = static_cast<u64>(status.st_size);
		return true;
	}

	bool ReadFileAt(void* file, u64 offset, std::span<u8> out) {
		const int descriptor = ToDescriptor(file);
		u8* into = out.data();
		size_t left = out.size();
		u64 at = offset;
		while (left > 0) {
			const ssize_t read = ::pread(descriptor, into, left, static_cast<off_t>(at));
			if (read < 0 && errno == EINTR) {
				continue;
			}
			if (read <= 0) {
				return false;
			}
			into += read;
			left -= static_cast<size_t>(read);
			at += static_cast<u64>(read);
		}
		return true;
	}

} // namespace Shalltear::Platform
