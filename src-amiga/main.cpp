#include "gcc8_c_support.h"

#ifdef _AMIGA

#include <ddb.h>
#include <ddb_pal.h>
#include <os_lib.h>
#include <os_types.h>

#include "video.h"

#include <graphics/gfxbase.h>
#include <proto/intuition.h>
#include <exec/ports.h>
#include <exec/execbase.h>
#include <hardware/intbits.h>
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

UWORD SystemInts;
UWORD SystemDMA;
UWORD SystemADKCON;

static APTR  VBR = 0;
static APTR  SystemIrq;
struct View *ActiView;

static uint16_t savedColors[32];

void TakeSystem() 
{
	SystemADKCON = custom->adkconr;
	SystemInts = custom->intenar;
	SystemDMA = custom->dmaconr;

	ActiView = GfxBase->ActiView;

	LoadView(0);
	WaitTOF();
	WaitTOF();

	VID_VSync();
	VID_VSync();

	OwnBlitter();
	WaitBlit();	
	Disable();

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
		Enable();
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

	custom->intena = 0x7fff; // Disable all interrupts
	custom->intreq = 0x7fff; // Clear any interrupts that were pending
	custom->dmacon = 0x7fff; // Clear all DMA channels

	custom->cop1lc = (ULONG)GfxBase->copinit;
	custom->cop2lc = (ULONG)GfxBase->LOFlist;
	custom->copjmp1 = 0x7fff; // Start coppper

	// Restore all interrupts and DMA settings
	custom->intena = SystemInts | 0x8000;
	custom->dmacon = SystemDMA | 0x8000;
	custom->adkcon = SystemADKCON | 0x8000;

	DisownBlitter();
	Enable();
	
	LoadView(ActiView);
	WaitTOF();
	WaitTOF();

	for(int a = 0; a < 32 ; a++)
		custom->color[a] = savedColors[a];

	systemTaken = false;
}

int main ()
{
	const char* msg = "\nUniversal DAAD Interpreter " VERSION_STR "\n";
	const size_t msgLen = StrLen(msg);

	SysBase       = *((struct ExecBase**)4UL);
	custom        = (Custom*)0xdff000;			// ?? Shouldn't be dynamic?
	GfxBase       = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library",0);
	DOSBase       = (struct DosLibrary*)OpenLibrary((CONST_STRPTR)"dos.library", 0);
	IntuitionBase = (struct IntuitionBase*)OpenLibrary((CONST_STRPTR)"intuition.library", 0);

	if (!GfxBase || !DOSBase)
	{
		Write(Output(), (APTR)"Failed to open libraries\n", 25);
		Exit(0);
	}

	Write(Output(), (APTR)msg, msgLen);

	if (!DDB_RunPlayer())
	{
		const char* errorString = DDB_GetErrorString();
		Write(Output(), (APTR)errorString, StrLen(errorString));
		Write(Output(), (APTR)"\n", 1);
	}

	CloseLibrary((struct Library*)DOSBase);
	CloseLibrary((struct Library*)GfxBase);
	return 0;
}

void OSSyncFS()
{
}

#endif