#pragma once

#include <os_types.h>

extern uint8_t* plane[4];
extern uint16_t (*charsetWords)[256][8];

const uint32_t BITPLANES    = 4;
const uint32_t SCR_WIDTHPX  = 320;
const uint32_t SCR_HEIGHTPX = 200;

// Distance between lines 
const uint32_t SCR_STRIDEB  = 40;
const uint32_t SCR_STRIDEW  = 20;
const uint32_t SCR_STRIDEL  = 10;

// Line width 
const uint32_t SCR_WIDTHB   = 40;
const uint32_t SCR_WIDTHW   = 20;
const uint32_t SCR_WIDTHL   = 10;

// Distance between bitplanes
const uint32_t SCR_BPNEXTB  = 8000;
const uint32_t SCR_BPNEXTW  = 4000;
const uint32_t SCR_BPNEXTL  = 2000;

const uint32_t SCR_ALLOCATE = SCR_WIDTHPX * SCR_HEIGHTPX / 2;

extern void VID_SetColor (uint8_t n, uint16_t pal);
extern void VID_VSync    ();
extern void VID_ActivatePalette();

extern bool isPAL;
extern bool systemTaken;
extern bool interruptsTaken;

void CallingDOS();
void AfterCallingDOS();
void TakeSystem();
void RestoreInterrupts(bool andCopperLists = false);
void TakeInterrupts();
void FreeSystem();

void BlitterCopy (
	void* src, uint16_t srcX, uint16_t srcY, 
	void* dst, uint16_t dstX, uint16_t dstY, 
	uint16_t w, uint16_t h, bool solid);

void BlitterLine(void* dst, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void BlitterRect(void* dst, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool set);
void BlitterChar(void* dst, uint16_t x, uint16_t y, uint8_t charIndex, uint8_t ink, uint8_t paper);

