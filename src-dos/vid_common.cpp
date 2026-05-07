#ifdef _DOS

#include "vid_common.h"

static VID_CommonState state;
static VID_ScreenAdapter screenAdapter;

static void VID_CommonScreenClear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	VID_CommonClear(x, y, w, h, color, mode);
}

static void VID_CommonScreenScroll(int x, int y, int w, int h, int lines, uint8_t paper)
{
	VID_CommonScroll(x, y, w, h, lines, paper);
}

static void VID_CommonScreenDrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	VID_CommonDrawTextSpan(x, y, text, length, ink, paper);
}

static void VID_CommonScreenBlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h)
{
	VID_CommonBlitNativeImage(pixels, srcW, srcH, x, y, w, h);
}

static void VID_CommonScreenBlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h)
{
	VID_CommonBlitIndexedImage(pixels, srcW, x, y, w, h);
}

static void VID_CommonScreenClearBuffer(bool front)
{
	VID_CommonClearBuffer(front, 0);
}

static void VID_CommonScreenSaveScreen()
{
	VID_CommonSaveScreen();
}

static void VID_CommonScreenRestoreScreen()
{
	VID_CommonRestoreScreen();
}

static void VID_CommonScreenSetTarget(SCR_Operation op, bool front)
{
	VID_CommonSetTarget(op, front);
}

static void VID_CommonScreenSwapScreen()
{
	VID_CommonSwapBuffers();
}

void VID_CommonInit(const VID_Adapter* adapter, unsigned pageSize, unsigned lineSize,
	unsigned pageCount, unsigned scratchPage)
{
	const VID_AdapterInfo* info = adapter != 0 ? &adapter->info : 0;
	state.width = info != 0 ? info->width : 0;
	state.height = info != 0 ? info->height : 0;
	state.pageSize = pageSize;
	state.lineSize = lineSize;
	state.pageCount = pageCount;
	state.frontPage = 0;
	state.backPage = pageCount > 1 ? 1 : 0;
	state.scratchPage = scratchPage;
	state.activePage = state.frontPage;
	state.minx = 0;
	state.miny = 0;
	state.maxx = (int)state.width - 1;
	state.maxy = (int)state.height - 1;
	state.adapter = adapter;
	state.ops = adapter != 0 ? &adapter->ops : 0;
	state.offset = state.ops != 0 && state.ops->getPagePtr != 0 ? state.ops->getPagePtr(state.activePage) : 0;

	if (adapter != 0)
	{
		screenAdapter.info.width = adapter->info.width;
		screenAdapter.info.height = adapter->info.height;
		screenAdapter.info.cellWidth = adapter->info.cellWidth;
		screenAdapter.info.cellHeight = adapter->info.cellHeight;
		screenAdapter.info.colorDepth = adapter->info.colorDepth;
		screenAdapter.info.paletteSize = adapter->info.paletteSize;
		screenAdapter.info.nativeImageMode = adapter->info.nativeImageMode;
		screenAdapter.info.alignmentPixels = adapter->info.alignmentPixels;
		screenAdapter.ops.clear = VID_CommonScreenClear;
		screenAdapter.ops.scroll = VID_CommonScreenScroll;
		screenAdapter.ops.drawTextSpan = VID_CommonScreenDrawTextSpan;
		screenAdapter.ops.blitNativeImage = VID_CommonScreenBlitNativeImage;
		screenAdapter.ops.blitIndexedImage = VID_CommonScreenBlitIndexedImage;
		screenAdapter.ops.clearBuffer = VID_CommonScreenClearBuffer;
		screenAdapter.ops.saveScreen = VID_CommonScreenSaveScreen;
		screenAdapter.ops.restoreScreen = VID_CommonScreenRestoreScreen;
		screenAdapter.ops.setTarget = VID_CommonScreenSetTarget;
		screenAdapter.ops.swapScreen = VID_CommonScreenSwapScreen;
		VID_ScreenRegisterAdapter(&screenAdapter);
	}
	else
		VID_ScreenRegisterAdapter(0);
}

VID_CommonState* VID_CommonGetState()
{
	return &state;
}

const VID_Adapter* VID_CommonGetAdapter()
{
	return state.adapter;
}

const VID_AdapterInfo* VID_CommonGetInfo()
{
	return state.adapter != 0 ? &state.adapter->info : 0;
}

uint32_t VID_CommonGetNativeImageSize(int width, int height)
{
	return VID_ScreenGetNativeImageSize(width, height);
}

bool VID_CommonBeginFixedPicturePresentation()
{
	if (state.ops == 0 || state.ops->copyPage == 0 || state.ops->getPagePtr == 0 ||
		state.activePage != state.frontPage || state.scratchPage == VID_INVALID_PAGE)
		return false;

	state.ops->copyPage(state.frontPage, state.scratchPage);
	state.activePage = state.scratchPage;
	state.offset = state.ops->getPagePtr(state.activePage);
	return true;
}

void VID_CommonEndFixedPicturePresentation()
{
	if (state.ops == 0 || state.ops->presentPage == 0 || state.ops->copyPage == 0 ||
		state.ops->getPagePtr == 0 || state.scratchPage == VID_INVALID_PAGE)
		return;

	unsigned oldFront = state.frontPage;
	state.frontPage = state.scratchPage;
	state.scratchPage = oldFront;

	state.ops->presentPage(state.frontPage);
	state.ops->copyPage(state.frontPage, state.backPage);

	if (state.activePage == oldFront)
		state.activePage = state.frontPage;
	state.offset = state.ops->getPagePtr(state.activePage);
}

void VID_CommonRestoreScreen()
{
	if (state.ops == 0 || state.ops->copyPage == 0)
		return;

	state.ops->copyPage(state.backPage, state.frontPage);
	if (state.activePage == state.frontPage)
		state.offset = state.ops->getPagePtr(state.activePage);
}

void VID_CommonSaveScreen()
{
	if (state.ops != 0 && state.ops->copyPage != 0)
		state.ops->copyPage(state.frontPage, state.backPage);
}

void VID_CommonSetActiveBuffer(bool front)
{
	if (state.ops == 0 || state.ops->getPagePtr == 0)
		return;

	state.activePage = front ? state.frontPage : state.backPage;
	state.offset = state.ops->getPagePtr(state.activePage);
}

void VID_CommonSetTarget(SCR_Operation op, bool front)
{
	if (state.ops != 0 && state.ops->setTarget != 0)
		state.ops->setTarget(op, front);
	else
		VID_CommonSetActiveBuffer(front);
}

void VID_CommonClearBuffer(bool front, uint8_t color)
{
	if (state.ops != 0 && state.ops->clearPage != 0)
		state.ops->clearPage(front ? state.frontPage : state.backPage, color);
}

void VID_CommonSwapBuffers()
{
	if (state.ops == 0 || state.ops->getPagePtr == 0 || state.ops->presentPage == 0)
		return;

	unsigned oldFront = state.frontPage;
	unsigned oldBack = state.backPage;

	state.frontPage = oldBack;
	state.backPage = oldFront;

	if (state.activePage == oldFront)
		state.activePage = state.frontPage;
	else if (state.activePage == oldBack)
		state.activePage = state.backPage;

	state.offset = state.ops->getPagePtr(state.activePage);
	state.ops->presentPage(state.frontPage);
}

void VID_CommonClear(int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	if (state.ops != 0 && state.ops->clear != 0)
		state.ops->clear(x, y, w, h, color, mode);
}

void VID_CommonScroll(int x, int y, int w, int h, int lines, uint8_t paper)
{
	if (state.ops != 0 && state.ops->scroll != 0)
		state.ops->scroll(x, y, w, h, lines, paper);
}

void VID_CommonDrawTextSpan(int x, int y, const uint8_t* text, uint16_t length, uint8_t ink, uint8_t paper)
{
	if (state.ops != 0 && state.ops->drawTextSpan != 0)
		state.ops->drawTextSpan(x, y, text, length, ink, paper);
}

void VID_CommonBlitNativeImage(const uint8_t* pixels, int srcW, int srcH, int x, int y, int w, int h)
{
	if (state.ops != 0 && state.ops->blitNativeImage != 0)
		state.ops->blitNativeImage(pixels, srcW, srcH, x, y, w, h);
}

void VID_CommonBlitIndexedImage(const uint8_t* pixels, int srcW, int x, int y, int w, int h)
{
	if (state.ops != 0 && state.ops->blitIndexedImage != 0)
		state.ops->blitIndexedImage(pixels, srcW, x, y, w, h);
}

#endif
