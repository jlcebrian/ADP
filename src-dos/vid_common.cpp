#ifdef _DOS

#include "vid_common.h"

static VID_CommonState state;

void VID_CommonInit(unsigned width, unsigned height, unsigned pageSize, unsigned lineSize,
	unsigned pageCount, unsigned scratchPage, const VID_CommonOps* ops)
{
	state.width = width;
	state.height = height;
	state.pageSize = pageSize;
	state.lineSize = lineSize;
	state.pageCount = pageCount;
	state.frontPage = 0;
	state.backPage = pageCount > 1 ? 1 : 0;
	state.scratchPage = scratchPage;
	state.activePage = state.frontPage;
	state.minx = 0;
	state.miny = 0;
	state.maxx = (int)width - 1;
	state.maxy = (int)height - 1;
	state.ops = ops;
	state.offset = ops != 0 ? ops->getPagePtr(state.activePage) : 0;
}

VID_CommonState* VID_CommonGetState()
{
	return &state;
}

bool VID_CommonBeginFixedPicturePresentation()
{
	if (state.ops == 0 || state.activePage != state.frontPage || state.scratchPage == VID_INVALID_PAGE)
		return false;

	state.ops->copyPage(state.frontPage, state.scratchPage);
	state.activePage = state.scratchPage;
	state.offset = state.ops->getPagePtr(state.activePage);
	return true;
}

void VID_CommonEndFixedPicturePresentation()
{
	if (state.ops == 0 || state.scratchPage == VID_INVALID_PAGE)
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
	if (state.ops == 0)
		return;

	state.ops->copyPage(state.backPage, state.frontPage);
	if (state.activePage == state.frontPage)
		state.offset = state.ops->getPagePtr(state.activePage);
}

void VID_CommonSaveScreen()
{
	if (state.ops != 0)
		state.ops->copyPage(state.frontPage, state.backPage);
}

void VID_CommonSetActiveBuffer(bool front)
{
	if (state.ops == 0)
		return;

	state.activePage = front ? state.frontPage : state.backPage;
	state.offset = state.ops->getPagePtr(state.activePage);
}

void VID_CommonClearBuffer(bool front, uint8_t color)
{
	if (state.ops != 0 && state.ops->clearPage != 0)
		state.ops->clearPage(front ? state.frontPage : state.backPage, color);
}

void VID_CommonSwapBuffers()
{
	if (state.ops == 0)
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

#endif
