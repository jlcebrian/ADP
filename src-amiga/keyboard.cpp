
#include <os_types.h>
#include <os_lib.h>
#include <ddb_scr.h>

#ifdef _AMIGA

#include "gcc8_c_support.h"
#include "keyboard.h"

#include <exec/devices.h>
#include <exec/interrupts.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <hardware/intbits.h>
#include <proto/exec.h>
#include <proto/dos.h>

#define EXPORT extern "C" __attribute__((used))

volatile uint16_t InputBuffer[64];
volatile uint8_t  InputBufferTail = 0;
volatile uint8_t  InputBufferHead = 0;

uint8_t  KeyboardBitmap[16];

bool     kbOpen = false;

extern "C" void KeyboardHandler();

static Interrupt handler;
static IOStdReq  req;

uint16_t Keymap[128] =
{
	// First row	00-0F
	0xB4, // ´
	'1','2','3','4','5','6','7','8','9','0','-','=','\\',
	0,
	0,

	// Second row	10-1F
	'q','w','e','r','t','y','u','i','o','p','[',']',
	0,
	0,		// KP1
	0,		// KP2,
	0,		// KP3,

	// Third row	20-2F
	'a','s','d','f','g','h','j','k','l',';','\'',0,
	0,
	0,		// KP4
	0,		// KP5
	0,		// KP6

	// Fourth row	30-3F
	0,'z','x','c','v','b','n','m',',','.','/',
	0,
	0,
	0,		// KP7
	0,		// KP8
	0,		// KP9

	// Fifth row	40-4F
	' ',	// Space
	8,		// Backspace
	9,		// Tab
	0x1C00, // KP Enter,
	13,		// Return,
	27,		// Escape
	0,		// DEL
	0,
	0,
	0,
	'-',	// KP-
	0,
	0x4800,	// Cursor up
	0x5000,	// Cursor down
	0x4D00,	// Cursor right
	0x4B00,	// Cursor left

	// Sixth row	50-5F
	0x3B00,	// F1
	0x3C00,	// F2
	0x3D00,	// F3
	0x3E00,	// F4
	0x3F00,	// F5
	0x4000,	// F6
	0x4100,	// F7
	0x4200,	// F8
	0x4300,	// F9
	0x4400,	// F10
	'(',	// KP(
	')',	// KP)
	'/',	// KP/
	'*',	// KP*
	0,
	0,		// HELP

	// Seventh row	60-6F
	0,		// Left shift
	0,		// Right shift
	0,		// Caps lock
	0,		// Ctrl
	0,		// Alt
	0,
	0,		// Amiga
};

uint16_t KeymapCaps[128] =
{
	// First row	00-0F
	0x7E, // ~
	'1','2','3','4','5','6','7','8','9','0','_','+','|',
	0,
	0,

	// Second row	10-1F
	'Q','W','E','R','T','Y','U','I','O','P','{','}',
	0,
	0,		// KP1
	0,		// KP2,
	0,		// KP3,

	// Third row	20-2F
	'A','S','D','F','G','H','J','K','L','=','"',0,
	0,
	0,		// KP4
	0,		// KP5
	0,		// KP6

	// Fourth row	30-3F
	0,'Z','X','C','V','B','N','M','<','>','?',
	0,
	0,
	0,		// KP7
	0,		// KP8
	0,		// KP9

	// Fifth row	40-4F
	' ',	// Space
	8,		// Backspace
	9,		// Tab
	0x1C00, // KP Enter,
	13,		// Return,
	27,		// Escape
	0,		// DEL
	0,
	0,
	0,
	'-',	// KP-
	0,
	0x4800,	// Cursor up
	0x5000,	// Cursor down
	0x4D00,	// Cursor right
	0x4B00,	// Cursor left

	// Sixth row	50-5F
	0x3B00,	// F1
	0x3C00,	// F2
	0x3D00,	// F3
	0x3E00,	// F4
	0x3F00,	// F5
	0x4000,	// F6
	0x4100,	// F7
	0x4200,	// F8
	0x4300,	// F9
	0x4400,	// F10
	'(',	// KP(
	')',	// KP)
	'/',	// KP/
	'*',	// KP*
	0,
	0,		// HELP

	// Seventh row	60-6F
	0,		// Left shift
	0,		// Right shift
	0,		// Caps lock
	0,		// Ctrl
	0,		// Alt
	0,
	0,		// Amiga
};

EXPORT void KeyFound (InputEvent* ptr)
{
	if (ptr == 0)
		return;

	uint16_t code = ptr->ie_Code;
	if (code & 0x80)
	{
		// Key release
		code &= 0x7F;
		KeyboardBitmap[code >> 3] &= ~(1 << (code & 7));
	}
	else
	{
		// Key pressed
		code &= 0x7F;
		KeyboardBitmap[code >> 3] |= 1 << (code & 7);

		bool caps = IsKeyPressed(0x61) || IsKeyPressed(0x60);
		uint16_t tr = caps ? KeymapCaps[code] : Keymap[code];
		if (tr != 0)
		{
			uint8_t nextTail = (InputBufferTail + 1) & 63;
			if (nextTail != InputBufferHead)
			{
				InputBuffer[InputBufferTail] = tr;
				InputBufferTail = nextTail;
			}
		}
	}
}

void Handler()
{
	asm volatile(
		"   move.l %a0,%d0    \n"
		"_check:              \n"
		"   cmp.b #1,%a0@(4)  \n"	// IECLASS_RAWKEY(1), ie_Class(4)
		"   bne	_next         \n"
		"   move.l %d0,%a7@-  \n"
		"   move.l %a0,%a7@-  \n"
		"   bsr _call         \n"
		"   move.l %a7@+,%a0  \n"
		"   move.l %a7@+,%d0  \n"
		"   move.b #0,%a0@(4) \n"	// IECLASS_NULL(0), ie_Class(4))
		"_next:               \n"
		"   move.l %a0@,%a0   \n"	// ie_Next(0)
		"   move.l %a0,%d1    \n"
		"   tst %d1           \n"
		"   bne _check        \n"
		"   rts               \n"
		"_call:               \n"
		"   jmp KeyFound      \n"
	);
}

void OpenKeyboard()
{
	if (!kbOpen)
	{
		if (OpenDevice("input.device",0,(IORequest *)&req,0) != 0)
		{
			DebugPrintf("Error opening input.device: %ld\n", IoErr());
			Delay(50);
			Exit(0);
		}

		Enable();

		handler.is_Code = Handler;
		handler.is_Data = (APTR)KeyFound;
		handler.is_Node.ln_Type = NT_USER;
		handler.is_Node.ln_Pri  = 60; /* above intuition's handler */
		req.io_Data = (APTR)&handler;
		req.io_Command = IND_ADDHANDLER;
		SendIO ((IORequest *)&req);

		kbOpen = true;
	}
}

void CloseKeyboard()
{
	if (kbOpen)
	{
		req.io_Data = (APTR)&handler;
		req.io_Command = IND_REMHANDLER;
		SendIO ((IORequest *)&req);
	}
}

int GetModifiers()
{
	int mod = 0;
	if (IsKeyPressed(0x60)) mod |= SCR_KEYMOD_SHIFT;
	if (IsKeyPressed(0x61)) mod |= SCR_KEYMOD_SHIFT;
	if (IsKeyPressed(0x62)) mod |= SCR_KEYMOD_CAPSLOCK;
	if (IsKeyPressed(0x63)) mod |= SCR_KEYMOD_CTRL;
	if (IsKeyPressed(0x64)) mod |= SCR_KEYMOD_ALT;
	return mod;
}

#endif