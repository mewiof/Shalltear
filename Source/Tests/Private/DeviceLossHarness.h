#pragma once

#include "TestsPCH.h"

#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace DeviceLossTest {

	using namespace Shalltear;

	class LogSink : public ILogSink {
	public:
		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard lock(_mutex);
			_messages.push_back(str(event.message));
			_levels.push_back(event.level);
		}
		void Flush() override {}

		void Forget() {
			std::lock_guard lock(_mutex);
			_messages.clear();
			_levels.clear();
		}
		u32 Count(const char* text) const {
			std::lock_guard lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				count += message.find(text) != str::npos ? 1u : 0u;
			}
			return count;
		}
		u32 CountAtLeast(LogLevel level) const {
			std::lock_guard lock(_mutex);
			u32 count = 0;
			for (LogLevel seen : _levels) {
				count += seen >= level ? 1u : 0u;
			}
			return count;
		}
		str Joined() const {
			std::lock_guard lock(_mutex);
			str out;
			for (const str& message : _messages) {
				out += message;
				out += '\n';
			}
			return out;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		std::vector<LogLevel> _levels;
	};

	inline constexpr const char* LOG_SENTINEL = "device-loss-sentinel";

	class ScopedLog {
	public:
		ScopedLog() {
			_ownsJobSystem = JobSystem::GetWorkerCount() == 0;
			if (_ownsJobSystem) {
				JobSystem::Initialize();
			}
			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);
			_sink = std::make_shared<LogSink>();
			LogManager::AddSink(_sink);
			Delivered();
			_sink->Forget();
		}
		~ScopedLog() {
			LogManager::RemoveSink(_sink);
			LogManager::Terminate();
			if (_ownsJobSystem) {
				JobSystem::Terminate();
			}
		}

		const LogSink& Delivered() {
			const u32 before = _sink->Count(LOG_SENTINEL);
			SLT_LOG_INFO("Graphics", "{}", LOG_SENTINEL);
			for (int attempt = 0; attempt < 400 && _sink->Count(LOG_SENTINEL) == before; ++attempt) {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return *_sink;
		}
	private:
		std::shared_ptr<LogSink> _sink;
		bool _ownsJobSystem = false;
	};

	class ScopedEnvironment {
	public:
		ScopedEnvironment(const char* name, const char* value)
			: _name(name) {
			char previous[1024];
			const DWORD length = ::GetEnvironmentVariableA(name, previous, sizeof(previous));
			_had = length > 0 && length < sizeof(previous);
			if (_had) {
				_previous = previous;
			}
			::SetEnvironmentVariableA(name, value);
		}
		~ScopedEnvironment() { Restore(); }
		void Restore() {
			if (!_restored) {
				::SetEnvironmentVariableA(_name, _had ? _previous.c_str() : nullptr);
				_restored = true;
			}
		}
	private:
		const char* _name;
		std::string _previous;
		bool _had = false;
		bool _restored = false;
	};

	class ScopedMissingDriver {
	public:
		void Restore() {
			_drivers.Restore();
			_legacy.Restore();
		}
	private:
		ScopedEnvironment _drivers{"VK_DRIVER_FILES", "Z:\\shalltear-no-such-driver.json"};
		ScopedEnvironment _legacy{"VK_ICD_FILENAMES", "Z:\\shalltear-no-such-driver.json"};
	};

	inline std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

} // namespace DeviceLossTest
