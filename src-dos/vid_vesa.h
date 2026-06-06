#pragma once

#include "vid_common.h"

bool VESA_IsAvailable();
bool VESA_SetVideoMode();
const VID_Adapter* VESA_GetAdapter();
