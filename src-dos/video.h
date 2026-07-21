#pragma once

#include <stdint.h>
#include <string.h>
#include <vid_font.h>

#if HAS_HIRES_FONT
// Native 16-bit (16x16) glyphs for double-resolution (SVGA/2x) text, loaded from
// a V4 SINTAC font by VID_StoreFont2X and rendered by the VESA text path. 32
// bytes/char = 16 rows of 2 bytes. See src-common/vid_font.cpp.
extern uint8_t charset16[256 * 32];
extern bool    charset16Available;
#endif

#if !defined(__386__)
#include <dos.h>
#endif

#if defined(__386__)
typedef uint8_t* dos_ptr8;
typedef const uint8_t* dos_cptr8;

static void memset32(void* addr, uint32_t value, int32_t count);
#pragma aux memset32 = \
	"cld" \
	"rep stosd" \
	parm [edi] [eax] [ecx];

static void memset8(void* addr, uint8_t value, int32_t count);
#pragma aux memset8 = \
	"cld" \
	"rep stosb" \
	parm [edi] [al] [ecx];

static void memcpy_bytes(void* dst, const void* src, int32_t count);
#pragma aux memcpy_bytes = \
	"cld" \
	"rep movsb" \
	parm [edi] [esi] [ecx] \
	modify exact [edi esi ecx];
#else
typedef uint8_t __far* dos_ptr8;
typedef const uint8_t __far* dos_cptr8;

static void memset32_raw(uint16_t dstSeg, uint16_t dstOff, uint16_t value, uint16_t count);
#pragma aux memset32_raw = \
	"push es" \
	"mov es, ax" \
	"mov ax, bx" \
	"shl cx, 1" \
	"cld" \
	"rep stosw" \
	"pop es" \
	parm [ax] [di] [bx] [cx] \
	modify exact [ax bx cx di];

static inline void memset32(void __far* addr, uint32_t value, int32_t count)
{
	memset32_raw(FP_SEG(addr), FP_OFF(addr), (uint16_t)value, (uint16_t)count);
}

static void memset8_raw(uint16_t dstSeg, uint16_t dstOff, uint16_t value, uint16_t count);
#pragma aux memset8_raw = \
	"push es" \
	"mov es, ax" \
	"mov al, dl" \
	"cld" \
	"rep stosb" \
	"pop es" \
	parm [ax] [di] [dx] [cx] \
	modify exact [ax cx di dx];

static inline void memset8(void __far* addr, uint8_t value, int32_t count)
{
	memset8_raw(FP_SEG(addr), FP_OFF(addr), value, (uint16_t)count);
}

static void memcpy_bytes_raw(uint16_t dstSeg, uint16_t dstOff, uint16_t srcSeg, uint16_t srcOff, uint16_t count);
#pragma aux memcpy_bytes_raw = \
	"push ds" \
	"push es" \
	"mov es, ax" \
	"mov ds, bx" \
	"cld" \
	"rep movsb" \
	"pop es" \
	"pop ds" \
	parm [ax] [di] [bx] [si] [cx] \
	modify exact [ax bx cx si di];

static inline void memcpy_bytes(void __far* dst, const void __far* src, uint16_t count)
{
	memcpy_bytes_raw(FP_SEG(dst), FP_OFF(dst), FP_SEG(src), FP_OFF(src), count);
}

static uint16_t dos_get_ds(void);
#pragma aux dos_get_ds = \
	"mov ax, ds" \
	value [ax] \
	modify exact [ax];

static uint16_t dos_get_es(void);
#pragma aux dos_get_es = \
	"mov ax, es" \
	value [ax] \
	modify exact [ax];

static void dos_set_ds(uint16_t value);
#pragma aux dos_set_ds = \
	"mov ds, ax" \
	parm [ax] \
	modify exact [ax];

static void dos_set_es(uint16_t value);
#pragma aux dos_set_es = \
	"mov es, ax" \
	parm [ax] \
	modify exact [ax];
#endif