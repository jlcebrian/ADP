#include <ddb_pal.h>

uint32_t ZXSpectrumPalette[16] = 
{
	0xFF000000, 0xFF0000d8, 0xFFd80000, 0xFFd800d8,
	0xFF00d800, 0xFF00d8d8, 0xFFd8d800, 0xFFd8d8d8,
	0xFF000000, 0xFF0000FF, 0xFFFF0000, 0xFFFF00FF,
	0xFF00FF00, 0xFF00FFFF, 0xFFFFFF00, 0xFFFFFFFF,
};

uint32_t EGAPalette[16] = 
{
	0xFF000000, 0xFF0000A0, 0xFF00AA00, 0xFF00AAAA,
	0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
	0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
	0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};

uint32_t CGAPaletteRed[16] = 
{
	0xFF000000, 0xFF00AA00, 0xFFAA0000, 0xFFAAAAAA,
	0xFF000000, 0xFF00AA00, 0xFFAA0000, 0xFFAAAAAA,
	0xFF000000, 0xFF00AA00, 0xFFAA0000, 0xFFAAAAAA,
	0xFF000000, 0xFF00AA00, 0xFFAA0000, 0xFFAAAAAA,
};

uint32_t CGAPaletteCyan[16] = 
{
	0xFF000000, 0xFF00AAAA, 0xFFAA00AA, 0xFFAAAAAA,
	0xFF000000, 0xFF00AAAA, 0xFFAA00AA, 0xFFAAAAAA,
	0xFF000000, 0xFF00AAAA, 0xFFAA00AA, 0xFFAAAAAA,
	0xFF000000, 0xFF00AAAA, 0xFFAA00AA, 0xFFAAAAAA,
};

uint32_t DefaultPalette[16] = 
{
	0xFF000000, 0xFF0000A0, 0xFF00AA00, 0xFF00AAAA,
	0xFFAA0000, 0xFFAA00AA, 0xFFAA5500, 0xFFAAAAAA,
	0xFF555555, 0xFF5555FF, 0xFF55FF55, 0xFF55FFFF,
	0xFFFF5555, 0xFFFF55FF, 0xFFFFFF55, 0xFFFFFFFF,
};

uint16_t RGB2Pal(uint32_t c)
{
	uint8_t r = (c >> 8) & 0x0F;
	uint8_t g = (c >> 4) & 0x0F;
	uint8_t b = (c >> 0) & 0x0F;
	return 0xFF000000 + 0x110000 * r + 0x001100 * g + 0x000011 * b;
}

uint32_t Pal2RGB(uint16_t paletteEntry, bool amigaHack)
{
	uint8_t r = (paletteEntry >> 8) & 0x0F;
	uint8_t g = (paletteEntry >> 4) & 0x0F;
	uint8_t b = paletteEntry & 0x0F;

	if (amigaHack)
	{
		r = (r << 4) | r;
		g = (g << 4) | g;
		b = (b << 4) | b;
	}
	else
	{
		r = ((r << 1) | (r >> 3));
		g = ((g << 1) | (g >> 3));
		b = ((b << 1) | (b >> 3));
		r = (r << 4) | r;
		g = (g << 4) | g;
		b = (b << 4) | b;
	}

	return 0xFF000000 | (r << 16) | (g << 8) | b;
}