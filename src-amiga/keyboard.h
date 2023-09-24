#pragma once

#include <os_types.h>

extern volatile uint16_t InputBuffer[64];
extern volatile uint8_t  InputBufferTail;
extern volatile uint8_t  InputBufferHead;

extern uint8_t  KeyboardBitmap[16];

extern void OpenKeyboard();
extern int  GetModifiers();
extern void CloseKeyboard();

static inline bool IsKeyPressed(uint8_t code)
{
	return KeyboardBitmap[(code & 0x7F) >> 3] & (1 << (code & 7));
}