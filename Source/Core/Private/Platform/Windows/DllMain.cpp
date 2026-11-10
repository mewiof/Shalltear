#ifdef SHALLTEAR_SHARED

#include "Platform/Windows/WindowsHeaders.h"

// lld links XInput's DllMain from SDK import libs without this. fucking linker
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
	return TRUE;
}

#endif
