#include <os_lib.h>

#include "video.h"

volatile void* newScreen = 0;
volatile long* newPalette = 0;
volatile int   swapPending = 0;
volatile int   vblSlot = -1;
volatile int   colors = 256;
volatile int   count = 0;

typedef void (*VBLFUNC)(void);

#define VBLSEM      (*(volatile short *)0x452L)
#define NVBLS       (*(volatile short *)0x454L)
#define VBLQUEUE    (*(volatile VBLFUNC **)0x456L)

#define FALCON_SCREEN_BASE_HI (*(volatile uint8_t *)0xFFFF8201L)
#define FALCON_SCREEN_BASE_MI (*(volatile uint8_t *)0xFFFF8203L)
#define FALCON_PALETTE        ((volatile uint32_t *)0xFFFF9800L)

static void _vblWriteFalconScreenBase(void* screen)
{
	uint32_t address = (uint32_t)(unsigned long)screen;
	FALCON_SCREEN_BASE_HI = (uint8_t)(address >> 16);
	FALCON_SCREEN_BASE_MI = (uint8_t)(address >> 8);
}

static void _vblWriteFalconPalette(const long* palette, int count)
{
	volatile uint32_t* hwPalette = FALCON_PALETTE;
	for (int index = 0; index < count; index++)
		hwPalette[index] = (uint32_t)palette[index];
}

/* VBL routine: MUST be tiny and MUST return RTS */
static void _vblHandler(void)
{
    count++;
    if (!swapPending)
        return;

    _vblWriteFalconScreenBase((void*)newScreen);
    _vblWriteFalconPalette((const long*)newPalette, colors);

    swapPending = 0;
}

static long _vblInstall(void)
{
    short i;
    volatile VBLFUNC *q = VBLQUEUE;

    for (i = 0; i < NVBLS; i++)
    {
        if (q[i] == 0)
        {
            q[i] = _vblHandler;
            vblSlot = i;
            return 1;
        }
    }
    return 0;
}

static long _vblRemove(void)
{
    if (vblSlot >= 0)
    {
        volatile VBLFUNC *q = VBLQUEUE;
        q[vblSlot] = 0;
        vblSlot = -1;
    }
    return 1;
}

int VBL_Install(void)
{
    DebugPrintf("Installing VBL handler\n");
    
    long r = Supexec(_vblInstall);
    if (!r)
        DebugPrintf("WARNING: VBL handler was NOT installed!\n");
    return (int)r;
}

void VBL_Remove(void)
{
    DebugPrintf("Removing VBL handler\n");
    Supexec(_vblRemove);
}

void VBL_QueueSwap(void* screen, long* palette)
{
    newScreen = screen;
    newPalette = palette;
    colors = 256;
    swapPending = 1;
}

void VBL_Wait()
{
    if (swapPending)
    {
        for (int n = 0; n < 4; n++)
        {
            Vsync();
            if (!swapPending)
                return;
        }
        DebugPrintf("WARNING: VBL interrupt is not working! Call count is %d\n", count);
    }
}