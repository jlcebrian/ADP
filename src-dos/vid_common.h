#pragma once

#include "video.h"

#define VID_INVALID_PAGE ((unsigned)-1)

struct VID_CommonOps
{
	dos_ptr8 (*getPagePtr)(unsigned page);
	void (*presentPage)(unsigned page);
	void (*copyPage)(unsigned srcPage, unsigned dstPage);
	void (*clearPage)(unsigned page, uint8_t color);
};

struct VID_CommonState
{
	dos_ptr8 offset;
	unsigned activePage;
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
	const VID_CommonOps* ops;
};

void VID_CommonInit(unsigned width, unsigned height, unsigned pageSize, unsigned lineSize,
	unsigned pageCount, unsigned scratchPage, const VID_CommonOps* ops);
VID_CommonState* VID_CommonGetState();
bool VID_CommonBeginFixedPicturePresentation();
void VID_CommonEndFixedPicturePresentation();
void VID_CommonRestoreScreen();
void VID_CommonSaveScreen();
void VID_CommonSetActiveBuffer(bool front);
void VID_CommonClearBuffer(bool front, uint8_t color);
void VID_CommonSwapBuffers();
