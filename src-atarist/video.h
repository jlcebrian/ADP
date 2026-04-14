#pragma once

#include <ddb_vid.h>
#include <os_types.h>

extern uint16_t* textScreen;
extern uint16_t* screen;
extern uint32_t  screenBufferSize;
extern uint16_t  screenRowBytes;
extern uint8_t   screenGroupBytes;

extern void VID_ApplyPalette (bool waitForVsync);

extern void VID_ClearST      (int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
extern void VID_ScrollST     (int x, int y, int w, int h, int lines, uint8_t paper);
extern void VID_BlitST       (uint16_t* dstBase, const uint16_t* srcBase, uint32_t srcStride, int x, int y, int w, int h);

extern void VID_ClearFalcon  (int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
extern void VID_ScrollFalcon (int x, int y, int w, int h, int lines, uint8_t paper);
extern void VID_BlitFalcon   (uint16_t* dstBase, const uint16_t* srcBase, uint32_t srcStride, int x, int y, int w, int h);

extern int  VBL_Install      (void);
extern void VBL_QueueSwap    (void* screen, long* palette);
extern void VBL_Wait         (void);
extern void VBL_Remove       (void);

