#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS) && defined(SHALLTEAR_SHARED)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// lld links XInput's DllMain from SDK import libs without this. fucking linker
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
	return TRUE;
}

#endif
