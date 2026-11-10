#include "TestsPCH.h"

TEST(PlatformTest, CorrectPlatformMacroIsDefined) {
#if defined(_WIN32) != defined(SLT_PLATFORM_WINDOWS)
	FAIL() << "SLT_PLATFORM_WINDOWS disagrees with the compiler";
#elif defined(__linux__) != defined(SLT_PLATFORM_LINUX)
	FAIL() << "SLT_PLATFORM_LINUX disagrees with the compiler";
#elif defined(SLT_PLATFORM_WINDOWS) == defined(SLT_PLATFORM_LINUX)
	FAIL() << "1 platform macro should be defined";
#else
	SUCCEED();
#endif
}

TEST(PlatformTest, CorrectCompilerMacroIsDefined) {
#if !defined(__clang__)
	FAIL() << "engine must compile with Clang";
#elif !defined(SLT_COMPILER_CLANG)
	FAIL() << "SLT_COMPILER_CLANG undefined under Clang";
#else
	SUCCEED();
#endif
}

TEST(PlatformTest, CPUArchitectureMacroIsDefined) {
#if defined(SLT_CPU_X64) && defined(SLT_CPU_ARM64)
	FAIL();
#elif !defined(SLT_CPU_X64) && !defined(SLT_CPU_ARM64)
	FAIL() << "no CPU architecture macro";
#else
	SUCCEED();
#endif
}

TEST(PlatformTest, DebugBreakMacroIsDefined) {
	auto invokeIf = [](bool shouldBreak) {
		if (shouldBreak) {
			SLT_DEBUG_BREAK();
		}
	};
	invokeIf(false);
	SUCCEED();
}
