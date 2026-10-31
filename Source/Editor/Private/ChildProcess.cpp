#include "ChildProcess.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringUtil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Shalltear {

	namespace {

		constexpr size_t MAX_QUEUED_LINES = 20000;

		constexpr DWORD READ_CHUNK = 4096;

	} // anonymous namespace

	str QuoteCommandArgument(const str& value) {
		return "\"" + value + "\"";
	}

	bool StartDetachedProcess(const str& commandLine, const str& workingDirectory) {
		std::wstring wideCommand = StringUtil::UTF8ToWide(commandLine);
		std::wstring wideDirectory = StringUtil::UTF8ToWide(workingDirectory);

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		if (!::CreateProcessW(nullptr, wideCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
							  wideDirectory.empty() ? nullptr : wideDirectory.c_str(), &startup, &process)) {
			SLT_LOG_ERROR("Editor", "couldn't launch '{}' (error {})", commandLine, ::GetLastError());
			return false;
		}
		::CloseHandle(process.hThread);
		::CloseHandle(process.hProcess);
		return true;
	}

	ChildProcess::~ChildProcess() {
		Detach();
	}

	bool ChildProcess::Start(const str& commandLine, const str& workingDirectory) {
		if (_running) {
			return false;
		}

		SECURITY_ATTRIBUTES security{};
		security.nLength = sizeof(security);
		security.bInheritHandle = TRUE;

		HANDLE readEnd = nullptr;
		HANDLE writeEnd = nullptr;
		if (!::CreatePipe(&readEnd, &writeEnd, &security, 0)) {
			SLT_LOG_ERROR("Editor", "child process: couldn't create the output pipe (error {})", ::GetLastError());
			return false;
		}
		::SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdOutput = writeEnd;
		startup.hStdError = writeEnd;
		startup.hStdInput = nullptr;

		std::wstring wideCommand = StringUtil::UTF8ToWide(commandLine);
		std::wstring wideDirectory = StringUtil::UTF8ToWide(workingDirectory);

		PROCESS_INFORMATION process{};
		BOOL created = ::CreateProcessW(nullptr, wideCommand.data(), nullptr, nullptr,
										TRUE, CREATE_NO_WINDOW, nullptr,
										wideDirectory.empty() ? nullptr : wideDirectory.c_str(),
										&startup, &process);
		::CloseHandle(writeEnd);
		if (!created) {
			SLT_LOG_ERROR("Editor", "child process: couldn't start '{}' (error {})", commandLine, ::GetLastError());
			::CloseHandle(readEnd);
			return false;
		}
		::CloseHandle(process.hThread);

		_process = process.hProcess;
		_readEnd = readEnd;
		_running = true;
		{
			std::lock_guard lock(_mutex);
			_lines.clear();
			_partial.clear();
			_droppedLines = 0;
		}

		_reader = std::thread([this]() {
			char chunk[READ_CHUNK];
			for (;;) {
				DWORD read = 0;
				if (!::ReadFile(static_cast<HANDLE>(_readEnd), chunk, READ_CHUNK, &read, nullptr) || read == 0) {
					break;
				}
				std::lock_guard lock(_mutex);
				for (DWORD i = 0; i < read; ++i) {
					char c = chunk[i];
					if (c == '\n') {
						if (!_partial.empty() && _partial.back() == '\r') {
							_partial.pop_back();
						}
						if (_lines.size() >= MAX_QUEUED_LINES) {
							_lines.pop_front();
							++_droppedLines;
						}
						_lines.push_back(_partial);
						_partial.clear();
					} else {
						_partial.push_back(c);
					}
				}
			}
			std::lock_guard lock(_mutex);
			if (!_partial.empty()) {
				_lines.push_back(_partial);
				_partial.clear();
			}
		});
		return true;
	}

	u32 ChildProcess::DrainLines(Vector<str>& out) {
		std::lock_guard lock(_mutex);
		u32 drained = 0;
		while (!_lines.empty()) {
			out.push_back(std::move(_lines.front()));
			_lines.pop_front();
			++drained;
		}
		if (_droppedLines > 0) {
			out.push_back(std::format("... {} output line(s) dropped (queue cap)", _droppedLines));
			_droppedLines = 0;
			++drained;
		}
		return drained;
	}

	bool ChildProcess::TryFinish(i32& outExitCode) {
		if (!_running || !_process) {
			return false;
		}
		if (::WaitForSingleObject(static_cast<HANDLE>(_process), 0) != WAIT_OBJECT_0) {
			return false;
		}
		if (_reader.joinable()) {
			_reader.join();
		}
		DWORD exitCode = 0;
		::GetExitCodeProcess(static_cast<HANDLE>(_process), &exitCode);
		outExitCode = static_cast<i32>(exitCode);
		CloseHandles();
		_running = false;
		return true;
	}

	void ChildProcess::Kill() {
		if (!_running || !_process) {
			return;
		}
		::TerminateProcess(static_cast<HANDLE>(_process), 1);
		if (_reader.joinable()) {
			_reader.join();
		}
		CloseHandles();
		_running = false;
	}

	void ChildProcess::Detach() {
		if (_reader.joinable()) {
			if (_readEnd) {
				::CloseHandle(static_cast<HANDLE>(_readEnd));
				_readEnd = nullptr;
			}
			_reader.join();
		}
		CloseHandles();
		_running = false;
	}

	void ChildProcess::CloseHandles() {
		if (_readEnd) {
			::CloseHandle(static_cast<HANDLE>(_readEnd));
			_readEnd = nullptr;
		}
		if (_process) {
			::CloseHandle(static_cast<HANDLE>(_process));
			_process = nullptr;
		}
	}

} // namespace Shalltear
