#include "gcc8_c_support.h"

#ifdef _AMIGA

#include <ddb.h>
#include <ddb_pal.h>
#include <ddb_data.h>
#include <os_lib.h>
#include <os_types.h>

#include "audio.h"
#include "video.h"
#include "keyboard.h"

#include <graphics/gfxbase.h>
#include <proto/intuition.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <exec/libraries.h>
#include <hardware/intbits.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

bool interruptsTaken = false;
bool systemTaken = false;

struct ExecBase      *SysBase;
struct DosLibrary    *DOSBase;
volatile Custom      *custom;
struct GfxBase       *GfxBase;
struct IntuitionBase *IntuitionBase;

static APTR  VBR = 0;
static APTR  SystemIrq;
struct View *ActiView;

static uint16_t savedColors[32];
static uint16_t savedSprpt[16];

static const uint32_t kPrivateStackBytes = 16 * 1024;

extern "C" int CallWithStack(APTR newStackPointer, int (*entry)());

static int RunApplication();

static uint32_t GetTaskStackSize()
{
	struct Task* task = FindTask(0);
	if (task == 0 || task->tc_SPLower == 0 || task->tc_SPUpper == 0)
		return 0;

	return (uint32_t)((uint32_t)task->tc_SPUpper - (uint32_t)task->tc_SPLower);
}

static APTR GetPrivateStackPointer(void* stackBase)
{
	uint32_t upper = (uint32_t)stackBase + kPrivateStackBytes;
	upper = (upper - 64) & ~3UL;
	return (APTR)upper;
}

static int RunWithExpandedStackIfNeeded()
{
	uint32_t stackBytes = GetTaskStackSize();
	if (stackBytes != 0)
	{
		DebugPrintf("Task stack size: %u bytes\n", stackBytes);
	}

	if (stackBytes >= kPrivateStackBytes)
		return RunApplication();

	void* privateStack = AllocMem(kPrivateStackBytes, MEMF_ANY);
	if (privateStack == 0)
	{
		Write(Output(), (APTR)"Unable to allocate private stack.\n", 34);
		return 0;
	}

	DebugPrintf("Switching to private stack: %u -> %u bytes\n", stackBytes, (uint32_t)kPrivateStackBytes);

	int result = 0;
	if (((struct Library*)SysBase)->lib_Version >= 36)
	{
		struct StackSwapStruct stackSwap;
		stackSwap.stk_Lower = privateStack;
		stackSwap.stk_Upper = (ULONG)privateStack + kPrivateStackBytes;
		stackSwap.stk_Pointer = GetPrivateStackPointer(privateStack);
		StackSwap(&stackSwap);
		result = RunApplication();
		StackSwap(&stackSwap);
	}
	else
	{
		result = CallWithStack(GetPrivateStackPointer(privateStack), RunApplication);
	}

	FreeMem(privateStack, kPrivateStackBytes);
	return result;
}

static int RunApplication()
{
	uint32_t freeMemory = AvailMem(0);
	DebugPrintf("Free memory on startup: %u bytes\n", freeMemory);

	#ifndef NO_SAMPLES
	ConvertSample(beepSample, beepSampleSize);
	ConvertSample(clickSample, clickSampleSize);
	#endif

	if (!DDB_RunPlayer())
	{
		const char* errorString = DDB_GetErrorString();
		Write(Output(), (APTR)errorString, StrLen(errorString));
		Write(Output(), (APTR)"\n", 1);
		return 0;
	}

	CloseAudio();
	CloseKeyboard();
	return 0;
}

void PrintToOutput(const char* msg)
{
	Write(Output(), (APTR)msg, strlen(msg));
}

void TakeSystem() 
{
	DebugPrintf("TakeSystem\n");

	LoadView(0);
	WaitTOF();
	WaitTOF();

	VID_VSync();
	VID_VSync();

	OwnBlitter();
	WaitBlit();

	custom->dmacon = DMAF_SPRITE;

	for (int n = 0; n < 8; n++)
	{
		uint32_t sprpt = (uint32_t)custom->sprpt[n];
		savedSprpt[n * 2 + 0] = (uint16_t)(sprpt >> 16);
		savedSprpt[n * 2 + 1] = (uint16_t)(sprpt & 0xFFFF);
		custom->sprpt[n] = 0;
	}
	
	for(int a = 0; a < 32 ; a++)
	{
		savedColors[a] = custom->color[a];
		custom->color[a] = 0;
	}

	VID_VSync();
	VID_VSync();
	
	systemTaken = true;
}

void CallingDOS()
{
	if (systemTaken)
	{
		WaitBlit();
		DisownBlitter();
	}
}

void AfterCallingDOS()
{
	if (systemTaken)
	{
		OwnBlitter();
	}
}

void FreeSystem() 
{ 
	VID_VSync();
	WaitBlit();

	custom->cop1lc = (ULONG)GfxBase->copinit;
	custom->cop2lc = (ULONG)GfxBase->LOFlist;
	custom->copjmp1 = 0x7fff; // Start coppper

	DisownBlitter();
	
	LoadView(ActiView);
	WaitTOF();
	WaitTOF();

	for (int n = 0; n < 8; n++)
		custom->sprpt[n] = (APTR)(((uint32_t)savedSprpt[n * 2 + 0] << 16) | savedSprpt[n * 2 + 1]);

	custom->dmacon = DMAF_SETCLR | DMAF_SPRITE;

	for(int a = 0; a < 32 ; a++)
		custom->color[a] = savedColors[a];

	systemTaken = false;
}

int main ()
{
	SysBase       = *((struct ExecBase**)4UL);
	custom        = (Custom*)0xdff000;			// ?? Shouldn't be dynamic?
	GfxBase       = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library",0);
	DOSBase       = (struct DosLibrary*)OpenLibrary((CONST_STRPTR)"dos.library", 0);
	IntuitionBase = (struct IntuitionBase*)OpenLibrary((CONST_STRPTR)"intuition.library", 0);

	// const char* msg = "ADP " VERSION_STR "\n";
	// PrintToOutput(msg);	

	RunWithExpandedStackIfNeeded();

	CloseLibrary((struct Library*)IntuitionBase);
	CloseLibrary((struct Library*)DOSBase);
	CloseLibrary((struct Library*)GfxBase);
	return 0;
}

void OSSyncFS()
{
}

#endif
