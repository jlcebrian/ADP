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
static uint16_t savedSprpt[16];

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
	
	uint32_t freeMemory = AvailMem(0);
	DebugPrintf("Free memory on startup: %u bytes\n", freeMemory);

	// WORD savedDMACON = custom->dmacon;

	ConvertSample(beepSample, beepSampleSize);
	ConvertSample(clickSample, clickSampleSize);

	OpenKeyboard();
	OpenAudio();

	if (!DDB_RunPlayer())
	{
		const char* errorString = DDB_GetErrorString();
		Write(Output(), (APTR)errorString, StrLen(errorString));
		Write(Output(), (APTR)"\n", 1);
	}

	// custom->dmacon = (savedDMACON & 0x3FF) | DMAF_SETCLR;
	// while (custom->dmaconr & DMAF_BLTDONE);
	// custom->dmacon = (~savedDMACON & 0x3FF);

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
