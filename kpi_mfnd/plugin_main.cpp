//=============================================================================
// Dll Entry Point.
//                                                     Copyright (c) 2015 MAYO.
//=============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

//-----------------------------------------------------------------------------
// Dll Entry Point
//-----------------------------------------------------------------------------
BOOL WINAPI DllMain(HINSTANCE instance, DWORD call_reason, void* /*reserved*/)
{
	if (call_reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(instance);
	}

	return TRUE;
}
