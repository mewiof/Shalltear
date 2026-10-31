#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <deque>
#include <mutex>
#include <thread>

namespace Shalltear {

	str QuoteCommandArgument(const str& value);

	bool StartDetachedProcess(const str& commandLine, const str& workingDirectory);

	class ChildProcess {
	public:
		ChildProcess() = default;
		~ChildProcess();

		ChildProcess(const ChildProcess&) = delete;
		ChildProcess& operator=(const ChildProcess&) = delete;

		bool Start(const str& commandLine, const str& workingDirectory);

		bool IsRunning() const { return _running; }

		u32 DrainLines(Vector<str>& out);

		bool TryFinish(i32& outExitCode);

		void Kill();

		void Detach();
	private:
		void CloseHandles();

		void* _process = nullptr;
		void* _readEnd = nullptr;
		std::thread _reader;
		bool _running = false;

		std::mutex _mutex;
		std::deque<str> _lines;
		str _partial;
		u64 _droppedLines = 0;
	};

} // namespace Shalltear
