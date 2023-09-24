#ifdef _AMIGA

#include "gcc8_c_support.h"

#include <os_types.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include "printf.h"

#define HAVE_ASM_STRLEN
#define HAVE_ASM_MEMSET
#define HAVE_ASM_MEMCOPY

extern struct ExecBase* SysBase;

#ifndef HAVE_ASM_STRLEN
unsigned long strlen(const char* s) 
{
	unsigned long t=0;
	while (*s)
	{
		s++;
		t++;
	}
	return t;
}
#endif

#ifdef HAVE_ASM_MEMSET
extern void fillMemWithZero( void *dest, size_t length );
#endif

#ifdef HAVE_ASM_MEMCOPY
extern void *_memcpy( void *dest, const void *src, size_t len );
extern void *_memmove( void *dest, const void *src, size_t len );
#endif

__attribute__((optimize("no-tree-loop-distribute-patterns"))) 
void* memset(void *dest, int val, unsigned long len) {
	#ifdef HAVE_ASM_MEMSET
	if (val == 0)
	{
		fillMemWithZero(dest, len);
		return (char*)dest + len;
	}
	#endif

	unsigned char *ptr = (unsigned char *)dest;
	while(len-- > 0)
		*ptr++ = val;
	return dest;
}

__attribute__((optimize("no-tree-loop-distribute-patterns"))) 
void* memcpy(void *dest, const void *src, unsigned long len) {
	#ifdef HAVE_ASM_MEMCOPY
	return _memcpy(dest, src, len);
	#else

	if (((uint32_t)dest & 0x01) == 0 &&
	    ((uint32_t)src & 0x01) == 0 &&
		(len & 0x03) == 0)
	{
		uint32_t* d = (uint32_t*)dest;
		const uint32_t* s = (const uint32_t*)src;
		len >>= 2;
		while(len--)
			*d++ = *s++;
		return dest;
	}
	else
	{
		char *d = (char *)dest;
		const char *s = (const char *)src;
		while(len--)
			*d++ = *s++;
		return dest;
	}
	#endif
}

__attribute__((optimize("no-tree-loop-distribute-patterns"))) 
void* memmove(void *dest, const void *src, unsigned long len) {
	#ifdef HAVE_ASM_MEMCOPY
	return _memmove(dest, src, len);
	#else
	char *d = dest;
	const char *s = src;
	if (d < s) {
		while (len--)
			*d++ = *s++;
	} else {
		const char *lasts = s + (len - 1);
		char *lastd = d + (len - 1);
		while (len--)
			*lastd-- = *lasts--;
	}
	return dest;
	#endif
}

// vbcc
// typedef unsigned char *va_list;
// #define va_start(ap, lastarg) ((ap)=(va_list)(&lastarg+1)) 

void KPutCharX();
void PutChar();

	#ifdef _DEBUGPRINT
__attribute__((noinline)) __attribute__((optimize("O1")))
void KPrintF(const char* fmt, ...) {
	long(*UaeDbgLog)(long mode, const char* string) = (long(*)(long, const char*))0xf0ff60;
	if(*((UWORD *)UaeDbgLog) == 0x4eb9 || *((UWORD *)UaeDbgLog) == 0xa00e) {
		va_list vl;
		va_start(vl, fmt);
		char temp[128];
		int ln = vsnprintf_(temp, 128, fmt, vl);
		UaeDbgLog(86, temp);
	}
}
	#endif

int main();

extern void (*__preinit_array_start[])() __attribute__((weak));
extern void (*__preinit_array_end[])() __attribute__((weak));
extern void (*__init_array_start[])() __attribute__((weak));
extern void (*__init_array_end[])() __attribute__((weak));
extern void (*__fini_array_start[])() __attribute__((weak));
extern void (*__fini_array_end[])() __attribute__((weak));

__attribute__((used)) __attribute__((section(".text.unlikely"))) void _start() {
	// initialize globals, ctors etc.
	unsigned long count;
	unsigned long i;

	count = __preinit_array_end - __preinit_array_start;
	for (i = 0; i < count; i++)
		__preinit_array_start[i]();

	count = __init_array_end - __init_array_start;
	for (i = 0; i < count; i++)
		__init_array_start[i]();

	main();

	// call dtors
	count = __fini_array_end - __fini_array_start;
	for (i = count; i > 0; i--)
		__fini_array_start[i - 1]();
}

void warpmode(int on) { // bool
	long(*UaeConf)(long mode, int index, const char* param, int param_len, char* outbuf, int outbuf_len);
	UaeConf = (long(*)(long, int, const char*, int, char*, int))0xf0ff60;
	if(*((UWORD *)UaeConf) == 0x4eb9 || *((UWORD *)UaeConf) == 0xa00e) {
		char outbuf;
		UaeConf(82, -1, on ? "cpu_speed max" : "cpu_speed real", 0, &outbuf, 1);
		UaeConf(82, -1, on ? "cpu_cycle_exact false" : "cpu_cycle_exact true", 0, &outbuf, 1);
		UaeConf(82, -1, on ? "cpu_memory_cycle_exact false" : "cpu_memory_cycle_exact true", 0, &outbuf, 1);
		UaeConf(82, -1, on ? "blitter_cycle_exact false" : "blitter_cycle_exact true", 0, &outbuf, 1);
		UaeConf(82, -1, on ? "warp true" : "warp false", 0, &outbuf, 1);
	}
}

static void debug_cmd(unsigned int arg1, unsigned int arg2, unsigned int arg3, unsigned int arg4) {
	long(*UaeLib)(unsigned int arg0, unsigned int arg1, unsigned int arg2, unsigned int arg3, unsigned int arg4);
	UaeLib = (long(*)(unsigned int, unsigned int, unsigned int, unsigned int, unsigned int))0xf0ff60;
	if(*((UWORD *)UaeLib) == 0x4eb9 || *((UWORD *)UaeLib) == 0xa00e) {
		UaeLib(88, arg1, arg2, arg3, arg4);
	}
}

enum barto_cmd {
	barto_cmd_clear,
	barto_cmd_rect,
	barto_cmd_filled_rect,
	barto_cmd_text,
	barto_cmd_register_resource,
	barto_cmd_set_idle,
	barto_cmd_unregister_resource,
	barto_cmd_load,
	barto_cmd_save
};

enum debug_resource_type {
	debug_resource_type_bitmap,
	debug_resource_type_palette,
	debug_resource_type_copperlist,
};

struct debug_resource {
	unsigned int address; // can't use void* because WinUAE is 64-bit
	unsigned int size;
	char name[32];
	unsigned short /*enum debug_resource_type*/ type;
	unsigned short /*enum debug_resource_flags*/ flags;

	union {
		struct bitmap {
			short width;
			short height;
			short numPlanes;
		} bitmap;
		struct palette {
			short numEntries;
		} palette;
	};
};

// debug overlay
void debug_clear() {
	debug_cmd(barto_cmd_clear, 0, 0, 0);
}

void debug_rect(short left, short top, short right, short bottom, unsigned int color) {
	debug_cmd(barto_cmd_rect, (((unsigned int)left) << 16) | ((unsigned int)top), (((unsigned int)right) << 16) | ((unsigned int)bottom), color);
}

void debug_filled_rect(short left, short top, short right, short bottom, unsigned int color) {
	debug_cmd(barto_cmd_filled_rect, (((unsigned int)left) << 16) | ((unsigned int)top), (((unsigned int)right) << 16) | ((unsigned int)bottom), color);
}

void debug_text(short left, short top, const char* text, unsigned int color) {
	debug_cmd(barto_cmd_text, (((unsigned int)left) << 16) | ((unsigned int)top), (unsigned int)text, color);
}

// profiler
void debug_start_idle() {
	debug_cmd(barto_cmd_set_idle, 1, 0, 0);
}

void debug_stop_idle() {
	debug_cmd(barto_cmd_set_idle, 0, 0, 0);
}

// gfx debugger
static void my_strncpy(char* destination, const char* source, unsigned long num) {
	while(*source && --num > 0)
		*destination++ = *source++;
	*destination = '\0';
}

void debug_register_bitmap(const void* addr, const char* name, short width, short height, short numPlanes, unsigned short flags) {
	struct debug_resource resource = {
		.address = (unsigned int)addr,
		.size = width / 8 * height * numPlanes,
		.type = debug_resource_type_bitmap,
		.flags = flags,
		.bitmap = { width, height, numPlanes }
	};

	if (flags & debug_resource_bitmap_masked)
		resource.size *= 2;

	my_strncpy(resource.name, name, sizeof(resource.name));
	debug_cmd(barto_cmd_register_resource, (unsigned int)&resource, 0, 0);
}

void debug_register_palette(const void* addr, const char* name, short numEntries, unsigned short flags) {
	struct debug_resource resource = {
		.address = (unsigned int)addr,
		.size = numEntries * 2,
		.type = debug_resource_type_palette,
		.flags = flags,
		.palette = { numEntries }
	};
	my_strncpy(resource.name, name, sizeof(resource.name));
	debug_cmd(barto_cmd_register_resource, (unsigned int)&resource, 0, 0);
}

void debug_register_copperlist(const void* addr, const char* name, unsigned int size, unsigned short flags) {
	struct debug_resource resource = {
		.address = (unsigned int)addr,
		.size = size,
		.type = debug_resource_type_copperlist,
		.flags = flags,
	};
	my_strncpy(resource.name, name, sizeof(resource.name));
	debug_cmd(barto_cmd_register_resource, (unsigned int)&resource, 0, 0);
}

void debug_unregister(const void* addr) {
	debug_cmd(barto_cmd_unregister_resource, (unsigned int)addr, 0, 0);
}

// load/save
unsigned int debug_load(const void* addr, const char* name) {
	struct debug_resource resource = {
		.address = (unsigned int)addr,
		.size = 0,
	};
	my_strncpy(resource.name, name, sizeof(resource.name));
	debug_cmd(barto_cmd_load, (unsigned int)&resource, 0, 0);
	return resource.size;
}

void debug_save(const void* addr, unsigned int size, const char* name) {
	struct debug_resource resource = {
		.address = (unsigned int)addr,
		.size = size,
	};
	my_strncpy(resource.name, name, sizeof(resource.name));
	debug_cmd(barto_cmd_save, (unsigned int)&resource, 0, 0);
}

#endif