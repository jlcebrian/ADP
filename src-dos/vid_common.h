#pragma once

#include <ddb.h>
#include <ddb_vid.h>
#include <vid_screen.h>
#include <dmg.h>
#include "video.h"

#define VID_INVALID_PAGE ((unsigned)-1)

struct VID_AdapterInfo
{
	uint16_t width;
	uint16_t height;
	uint8_t cellWidth;
	uint8_t cellHeight;
	uint8_t colorDepth;
	uint16_t paletteSize;
	DMG_ImageMode nativeImageMode;
	uint8_t alignmentPixels;
};

struct VID_AdapterOps
{
	dos_ptr8 (*getPagePtr)(unsigned page);
	void (*presentPage)(unsigned page);
	void (*copyPage)(unsigned srcPage, unsigned dstPage);
	void (*clearPage)(unsigned page, uint8_t color);
	void (*setTarget)(SCR_Operation op, bool front);
	void (*clear)(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
	void (*scroll)(int x, int y, int w, int h, int lines, uint8_t paper);
	void (*drawTextSpan)(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper);
	void (*blitNativeImage)(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h);
	void (*blitIndexedImage)(const uint8_t* pixels, int srcW, int x, int y, int w, int h);
};

struct VID_Adapter
{
	VID_AdapterInfo info;
	VID_AdapterOps ops;
};

struct VID_CommonState
{
	dos_ptr8 offset;
	unsigned activePage;
	bool opFront[2];
	unsigned frontPage;
	unsigned backPage;
	unsigned scratchPage;
	unsigned pageSize;
	unsigned lineSize;
	int minx;
	int maxx;
	int miny;
	int maxy;
	unsigned pageCount;
	unsigned width;
	unsigned height;
	const VID_Adapter* adapter;
	const VID_AdapterOps* ops;
};

void VID_CommonInit(const VID_Adapter* adapter, unsigned pageSize, unsigned lineSize,
	unsigned pageCount, unsigned scratchPage);
VID_CommonState* VID_CommonGetState();
const VID_Adapter* VID_CommonGetAdapter();
const VID_AdapterInfo* VID_CommonGetInfo();
uint32_t VID_CommonGetNativeImageSize(int width, int height);
bool VID_CommonBeginFixedPicturePresentation();
void VID_CommonEndFixedPicturePresentation();
void VID_CommonRestoreScreen();
void VID_CommonSaveScreen();
void VID_CommonSetActiveBuffer(bool front);
void VID_CommonSetTarget(SCR_Operation op, bool front);
void VID_CommonClearBuffer(bool front, uint8_t color);
void VID_CommonSwapBuffers();
void VID_CommonClear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode);
void VID_CommonScroll(int x, int y, int w, int h, int lines, uint8_t paper);
void VID_CommonDrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper);
void VID_CommonBlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h);
void VID_CommonBlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h);
