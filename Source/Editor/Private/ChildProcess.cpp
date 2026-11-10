#include "ChildProcess.h"

#include "Platform/EditorPlatform.h"

#include "Shalltear/Core/Log.h"

#include <format>

namespace Shalltear {

	namespace {

		constexpr size_t MAX_QUEUED_LINES = 20000;

		constexpr size_t READ_CHUNK = 4096;

	} // anonymous namespace

	str QuoteCommandArgument(const str& value) {
		return "\"" + value + "\"";
	}

	bool StartDetachedProcess(const str& commandLine, const str& workingDirectory) {
		EditorPlatform::SpawnOptions options;
		options.workingDirectory = workingDirectory;
		EditorPlatform::SpawnedProcess spawned;
		str error;
		if (!EditorPlatform::SpawnProcess(commandLine, options, spawned, error)) {
			SLT_LOG_ERROR("Editor", "couldn't launch '{}' ({})", commandLine, error);
			return false;
		}
		EditorPlatform::ReleaseProcess(spawned.process);
		return true;
	}

	ChildProcess::~ChildProcess() {
		Detach();
	}

	bool ChildProcess::Start(const str& commandLine, const str& workingDirectory) {
		if (_running) {
			return false;
		}

		EditorPlatform::SpawnOptions options;
		options.workingDirectory = workingDirectory;
		options.captureOutput = true;
		EditorPlatform::SpawnedProcess spawned;
		str error;
		if (!EditorPlatform::SpawnProcess(commandLine, options, spawned, error)) {
			SLT_LOG_ERROR("Editor", "child process: couldn't start '{}' ({})", commandLine, error);
			return false;
		}

		_process = spawned.process;
		_readEnd = spawned.output;
		_running = true;
		{
			std::lock_guard lock(_mutex);
			_lines.clear();
			_partial.clear();
			_droppedLines = 0;
		}

		_reader = std::thread([this, output = spawned.output]() {
			char chunk[READ_CHUNK];
			for (;;) {
				const size_t read = EditorPlatform::ReadProcessOutput(output, chunk, READ_CHUNK);
				if (read == 0) {
					break;
				}
				std::lock_guard lock(_mutex);
				for (size_t i = 0; i < read; ++i) {
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
		i32 exitCode = 0;
		if (!EditorPlatform::TryWaitProcess(_process, exitCode)) {
			return false;
		}
		if (_reader.joinable()) {
			_reader.join();
		}
		outExitCode = exitCode;
		CloseHandles();
		_running = false;
		return true;
	}

	void ChildProcess::Kill() {
		if (!_running || !_process) {
			return;
		}
		EditorPlatform::KillProcess(_process);
		if (_reader.joinable()) {
			_reader.join();
		}
		CloseHandles();
		_running = false;
	}

	void ChildProcess::Detach() {
		if (_reader.joinable()) {
			EditorPlatform::CancelProcessOutput(_readEnd);
			_reader.join();
		}
		CloseHandles();
		_running = false;
	}

	void ChildProcess::CloseHandles() {
		if (_readEnd) {
			EditorPlatform::CloseProcessOutput(_readEnd);
			_readEnd = nullptr;
		}
		if (_process) {
			EditorPlatform::ReleaseProcess(_process);
			_process = nullptr;
		}
	}

} // namespace Shalltear
