#pragma once

#include <ddb.h>

bool CGA_SetVideoMode(DDB_ScreenMode mode);
bool CGA_IsAvailable();
void CGA_SetPaletteRed(bool red);
