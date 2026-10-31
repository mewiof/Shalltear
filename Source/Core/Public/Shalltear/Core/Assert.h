#pragma once

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Platform.h"

#if !defined(SHALLTEAR_CONFIG_RELEASE)
#define SLT_ENABLE_ASSERTS
#endif

#if defined(SLT_ENABLE_ASSERTS)

#define SLT_CORE_ASSERT(condition)                                        \
	do {                                                                  \
		if (!(condition)) {                                               \
			SLT_LOG_FATAL("Core", "Assertion Failed: '{0}'", #condition); \
			SLT_DEBUG_BREAK();                                            \
		}                                                                 \
	} while (0)

#define SLT_CORE_ASSERT_MSG(condition, ...)                                                              \
	do {                                                                                                 \
		if (!(condition)) {                                                                              \
			SLT_LOG_FATAL("Core", "Assertion Failed: '{0}'. {1}", #condition, std::format(__VA_ARGS__)); \
			SLT_DEBUG_BREAK();                                                                           \
		}                                                                                                \
	} while (0)

#define SLT_APP_ASSERT(condition)                                        \
	do {                                                                 \
		if (!(condition)) {                                              \
			SLT_LOG_FATAL("App", "Assertion Failed: '{0}'", #condition); \
			SLT_DEBUG_BREAK();                                           \
		}                                                                \
	} while (0)

#define SLT_APP_ASSERT_MSG(condition, ...)                                                              \
	do {                                                                                                \
		if (!(condition)) {                                                                             \
			SLT_LOG_FATAL("App", "Assertion Failed: '{0}'. {1}", #condition, std::format(__VA_ARGS__)); \
			SLT_DEBUG_BREAK();                                                                          \
		}                                                                                               \
	} while (0)

#else

#define SLT_CORE_ASSERT(...)
#define SLT_CORE_ASSERT_MSG(...)
#define SLT_APP_ASSERT(...)
#define SLT_APP_ASSERT_MSG(...)

#endif
