#pragma once

#include <stdint.h>
#include "vid_common.h"

enum VideoMode
{
	MODE_TEXT,
	MODE_320x200,
	MODE_320x240,
	MODE_320x400,
	MODE_360x270,
	MODE_400x300
};

void ModeX_SetVideoMode(int mode);
const VID_Adapter* ModeX_GetAdapter();
