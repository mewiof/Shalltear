#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct FileAccessRead {
		str path;
		u64 size = 0;
		u64 mtime = 0;
		u64 contentHash = 0;
	};

	struct FileAccessRecording {
		Vector<FileAccessRead> reads{GetMemoryResource(MemTag::Asset)};
		Vector<str> writes{GetMemoryResource(MemTag::Asset)};
	};

	class ScopedFileAccessRecorder {
	public:
		explicit ScopedFileAccessRecorder(FileAccessRecording& sink) noexcept;
		~ScopedFileAccessRecorder();

		ScopedFileAccessRecorder(const ScopedFileAccessRecorder&) = delete;
		ScopedFileAccessRecorder& operator=(const ScopedFileAccessRecorder&) = delete;
	private:
		FileAccessRecording* _sink;
		FileAccessRecording* _previous;
	};

} // namespace Shalltear
