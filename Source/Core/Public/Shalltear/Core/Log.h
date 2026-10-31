#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <thread>
#include <utility>

namespace Shalltear {

	enum class LogLevel {
		Trace = 0,
		Debug,
		Info,
		Warn,
		Error,
		Fatal,

		None
	};

	struct LogEvent {
		LogLevel level;
		str category;
		str message;
		std::chrono::system_clock::time_point timestamp;
		std::thread::id threadId;
		const char* file;
		u32 line;
		const char* function;
	};

	class SHALLTEAR_CORE_API ILogSink {
	public:
		virtual ~ILogSink() = default;
		virtual void ReceiveLog(const LogEvent& event) = 0;
		virtual void Flush() = 0;
	};

	class SHALLTEAR_CORE_API ConsoleSink : public ILogSink {
	public:
		ConsoleSink();
		void ReceiveLog(const LogEvent& event) override;
		void Flush() override;
	};
	class SHALLTEAR_CORE_API FileSink : public ILogSink {
	public:
		FileSink(const str& filePath);
		~FileSink() override;
		void ReceiveLog(const LogEvent& event) override;
		void Flush() override;
	private:
		std::ofstream _fileStream;
	};

	struct LogInitConfig {
		bool enableConsoleSink = true;
		bool enableFileSink = true;
		str fileSinkPath = "Logs/Engine.log";
		bool createConsole = false;
	};

	class SHALLTEAR_CORE_API LogManager {
	public:
		static void Initialize();
		static void Initialize(const LogInitConfig& config);
		static void Terminate();

		static void AddSink(std::shared_ptr<ILogSink> sink);
		static bool RemoveSink(const std::shared_ptr<ILogSink>& sink);
		static void RemoveAllSinks();

		// events borrow string literals
		static void DrainBeforeImageUnload();
		static void SetGlobalLevel(LogLevel level);
		static LogLevel GetGlobalLevel();

		static void VDispatch(
			LogLevel level,
			const char* category,
			const char* file,
			u32 line,
			const char* function,
			std::string_view format,
			std::format_args args);

		template <typename... Args>
		static void Dispatch(
			LogLevel level,
			const char* category,
			const char* file,
			u32 line,
			const char* function,
			std::format_string<Args...> format,
			Args&&... args) {

			VDispatch(level, category, file, line, function, format.get(), std::make_format_args(args...));
		}
	};
} //namespace Shalltear

#if !defined(SHALLTEAR_LOG_STRIP_LEVEL)
#if defined(NDEBUG) || defined(SHALLTEAR_CONFIG_RELEASE)
#define SHALLTEAR_LOG_STRIP_LEVEL 2
#else
#define SHALLTEAR_LOG_STRIP_LEVEL 0
#endif
#endif

#define SLT_LOG_IMPL(level, category, ...)                                                                 \
	do {                                                                                                   \
		if ((level) >= ::Shalltear::LogManager::GetGlobalLevel()) {                                        \
			::Shalltear::LogManager::Dispatch(level, category, __FILE__, __LINE__, __func__, __VA_ARGS__); \
		}                                                                                                  \
	} while (0)

#if SHALLTEAR_LOG_STRIP_LEVEL <= 0
#define SLT_LOG_TRACE(category, ...) SLT_LOG_IMPL(::Shalltear::LogLevel::Trace, category, __VA_ARGS__)
#else
#define SLT_LOG_TRACE(...) \
	do {                   \
	} while (0)
#endif

#if SHALLTEAR_LOG_STRIP_LEVEL <= 1
#define SLT_LOG_DEBUG(category, ...) SLT_LOG_IMPL(::Shalltear::LogLevel::Debug, category, __VA_ARGS__)
#else
#define SLT_LOG_DEBUG(...) \
	do {                   \
	} while (0)
#endif

#if SHALLTEAR_LOG_STRIP_LEVEL <= 2
#define SLT_LOG_INFO(category, ...) SLT_LOG_IMPL(::Shalltear::LogLevel::Info, category, __VA_ARGS__)
#else
#define SLT_LOG_INFO(...) \
	do {                  \
	} while (0)
#endif

#define SLT_LOG_WARN(category, ...) SLT_LOG_IMPL(::Shalltear::LogLevel::Warn, category, __VA_ARGS__)
#define SLT_LOG_ERROR(category, ...) SLT_LOG_IMPL(::Shalltear::LogLevel::Error, category, __VA_ARGS__)
#define SLT_LOG_FATAL(category, ...) SLT_LOG_IMPL(::Shalltear::LogLevel::Fatal, category, __VA_ARGS__)
