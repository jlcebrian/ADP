#include <ddb_vid.h>
#include <ddb_pal.h>
#include <os_lib.h>
#include <dmg.h>

bool		waitingForKey = false;
bool 		buffering = false;
uint8_t 	lineHeight;
uint8_t 	columnWidth;
uint16_t	screenWidth;
uint16_t	screenHeight;
uint8_t 	charWidth[256];

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

void VID_SetDefaultPalette()
{
	for (int n = 0; n < 16; n++)
	{
		uint32_t color = DefaultPalette[n];
		VID_SetPaletteColor(n, (color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
	}
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