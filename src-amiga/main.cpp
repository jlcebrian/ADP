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
#include <exec/execbase.h>
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

void TakeSystem() 
{
	DebugPrintf("TakeSystem\n");
	ActiView = GfxBase->ActiView;

	LoadView(0);
	WaitTOF();
	WaitTOF();

	VID_VSync();
	VID_VSync();

	OwnBlitter();
	WaitBlit();

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

	for(int a = 0; a < 32 ; a++)
		custom->color[a] = savedColors[a];

	systemTaken = false;
}

int main ()
{
	const char* msg = "ADP " VERSION_STR "\n";
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

	WORD savedDMACON = custom->dmacon;

	ConvertSample(beepSample, beepSampleSize);
	ConvertSample(clickSample, clickSampleSize);

	Write(Output(), (APTR)msg, msgLen);
	OpenKeyboard();
	OpenAudio();

	if (!DDB_RunPlayer())
	{
		const char* errorString = DDB_GetErrorString();
		Write(Output(), (APTR)errorString, StrLen(errorString));
		Write(Output(), (APTR)"\n", 1);
	}

	custom->dmacon = (savedDMACON & 0x3FF) | DMAF_SETCLR;
	Delay(5);
	custom->dmacon = (~savedDMACON & 0x3FF);

	CloseAudio();
	CloseKeyboard();

	CloseLibrary((struct Library*)IntuitionBase);
	CloseLibrary((struct Library*)DOSBase);
	CloseLibrary((struct Library*)GfxBase);
	return 0;
}

void OSSyncFS()
{
}

#endif