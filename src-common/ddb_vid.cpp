#include <ddb_vid.h>
#include <os_lib.h>
#include <dmg.h>

bool		waitingForKey = false;
bool 		buffering = false;
uint8_t 	lineHeight;
uint8_t 	columnWidth;
uint16_t	screenWidth;
uint16_t	screenHeight;
uint8_t 	charWidth[256];
uint8_t 	inkMap[16] = { 0, 15 };

bool VID_PictureExists (uint8_t picno)
{
	if (dmg == 0) return false;
	
	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	return (entry != 0 && entry->type == DMGEntry_Image);
}

bool VID_SampleExists (uint8_t picno)
{
	if (dmg == 0) return false;

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	return (entry != 0 && entry->type == DMGEntry_Audio);
}

void VID_UpdateInkMap()
{
	// DAAD Ink (as used by INK, PAPER condacts) is mapped as follows:
	//
	// 0:		Left as is
	// 1:		Replaced by 15
	// 2-15:	Decreased by 1
	//
	// You can define USE_ADAPTIVE_INKMAP if you want color 1 to be
	// replaced by the highest color in the palette instead

#if USE_ADAPTIVE_INKMAP
	uint8_t white = 0;
	int whiteValue = 0;
	uint8_t black = 0;
	int blackValue = 0x1000000;
	int n;

	for (n = 0; n < 16; n++)
	{
		uint8_t r, g, b;
		VID_GetPaletteColor(n, &r, &g, &b);
		int value = r + g + b;
		if (value >= whiteValue)
		{
			whiteValue = value;
			white = n;
		}
		if (value < blackValue)
		{
			blackValue = value;
			black = n;
		}
	}
	inkMap[0] = black;
	inkMap[1] = white;
	for (n = 2; n < 16; n++)
	{
		int i = n;
		if (black >= i) i--;
		if (white >= i) i--;
		inkMap[n] = i;
	}
#else
	inkMap[0] = 0;
	inkMap[1] = 15;
	for (int n = 2; n < 16; n++)
		inkMap[n] = n-1;
#endif
}

void VID_ShowError(const char* msg)
{
	int bg = 0, cr = 0;
	for (int n = 0; n < 16; n++)
	{
		uint8_t r, g, b;
		VID_GetPaletteColor(n, &r, &g, &b);
		if (r > cr)
		{
			bg = n;
			cr = r;
		}
	}

	VID_Clear(0, 0, 320, 10, bg);
	
	uint16_t x = 160 - StrLen(msg)*3;
	for (int n = 0; msg[n]; n++, x+= 6)
		VID_DrawCharacter(x, 1, msg[n], 0, 255);

	VID_WaitForKey();
}

static bool progressBarVisible = false;

void VID_ShowProgressBar(uint16_t amount)
{
	uint16_t x = screenWidth/2 - 32;
	uint16_t y = screenHeight - 32;
	
	if (!progressBarVisible)
	{
		VID_Clear(x, y, 64, 1, 15);
		VID_Clear(x, y, 1, 12, 15);
		VID_Clear(x+63, y, 1, 12, 15);
		VID_Clear(x, y+11, 64, 1, 15);
		VID_Clear(x+1, y+1, 62, 10, 0);
		progressBarVisible = true;
	}
	VID_Clear(x+2, y+2, amount*60/255, 8, 15);
}

#ifndef HAS_DRAWTEXT

void VID_DrawText(int x, int y, const char* text, uint8_t ink, uint8_t paper)
{
	while (*text && x < screenWidth)
	{
		VID_DrawCharacter(x, y, (uint8_t)*text, ink, paper);
		x += charWidth[(uint8_t)*text];
		text++;
	}
}

#endif