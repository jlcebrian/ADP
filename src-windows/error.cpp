#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void OSInit()
{
	SetProcessDPIAware();
}

void OSSyncFS()
{
}

void OSError (const char* message)
{
	MessageBoxA (NULL, message, "Error", MB_OK | MB_ICONERROR);
	ExitProcess (1);
}