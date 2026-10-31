#include "TestsPCH.h"

TEST(PlatformTest, CorrectPlatformMacroIsDefined) {
#if defined(_WIN32)
#if !defined(SLT_PLATFORM_WINDOWS)
	FAIL() << "SLT_PLATFORM_WINDOWS undefined on Windows";
#endif
	SUCCEED();
#else
	FAIL() << "tests are configured for Windows only";
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
