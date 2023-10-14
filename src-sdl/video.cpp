#include <ddb.h>
#include <ddb_data.h>
#include <ddb_pal.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include <dmg.h>
#include <os_char.h>
#include <os_mem.h>

#include <SDL.h>

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

DDB_Machine    screenMachine = DDB_MACHINE_IBMPC;
DDB_ScreenMode screenMode = ScreenMode_VGA16;

SDL_Window*  window;
SDL_Surface* surface;

uint8_t*   graphicsBuffer;
uint8_t*   textBuffer;

uint8_t*   frontBuffer = NULL;
uint8_t*   backBuffer = NULL;
uint8_t*   pictureData;
uint8_t*   audioData;
DMG_Entry* bufferedEntry = NULL;
uint8_t    bufferedIndex;
bool       quit;
bool       exitGame = false;
bool       textInput;
bool	   charsetInitialized = false;
uint32_t   palette[256];

// Specific for Spectrum
uint8_t*   bitmap = NULL;
uint8_t*   attributes = NULL;
uint8_t    curAttr = 0;

#if _WEB
bool       supportsOpenFileDialog = false;
#else
bool       supportsOpenFileDialog = true;
#endif

void (*mainLoopCallback)(int elapsed);

static struct
{
	SDL_KeyCode key;
	uint8_t     code;
	uint8_t	    ext;
	bool        special;
}
keyMapping[] =
{
	{ SDLK_KP_ENTER,		0x00, 0x1C, true },
	{ SDLK_HOME,			0x00, 0x47, true },
	{ SDLK_UP,              0x00, 0x48, true },
	{ SDLK_PAGEUP,          0x00, 0x49, true },
	{ SDLK_LEFT,			0x00, 0x4B, true },
	{ SDLK_RIGHT,			0x00, 0x4D, true },
	{ SDLK_END,				0x00, 0x4F, true },
	{ SDLK_DOWN,            0x00, 0x50, true },
	{ SDLK_PAGEDOWN,		0x00, 0x51, true },
	{ SDLK_INSERT,			0x00, 0x52, true },
	{ SDLK_DELETE,			0x00, 0x53, true },
	{ SDLK_F1,              0x00, 0x3B, true },
	{ SDLK_F2,              0x00, 0x3C, true },
	{ SDLK_F3,              0x00, 0x3D, true },
	{ SDLK_F4,              0x00, 0x3E, true },
	{ SDLK_F5,              0x00, 0x3F, true },
	{ SDLK_F6,              0x00, 0x40, true },
	{ SDLK_F7,              0x00, 0x41, true },
	{ SDLK_F8,              0x00, 0x42, true },
	{ SDLK_F9,              0x00, 0x43, true },
	{ SDLK_F10,             0x00, 0x44, true },
	{ SDLK_RETURN,          0x0D, 0x00, true },			// It could also be 0x6D, which allows ENTER to work in Espacial
	{ SDLK_BACKSPACE,       0x08, 0x00, true },
	{ SDLK_ESCAPE,          0x1B, 0x00, true },
	{ SDLK_SPACE,           0x20, 0x00, false },
	{ SDLK_0,               0x30, 0x00, false },
	{ SDLK_1,               0x31, 0x00, false },
	{ SDLK_2,               0x32, 0x00, false },
	{ SDLK_3,               0x33, 0x00, false },
	{ SDLK_4,               0x34, 0x00, false },
	{ SDLK_5,               0x35, 0x00, false },
	{ SDLK_6,               0x36, 0x00, false },
	{ SDLK_7,               0x37, 0x00, false },
	{ SDLK_8,               0x38, 0x00, false },
	{ SDLK_9,               0x39, 0x00, false },
	{ SDLK_a,               0x41, 0x00, false },
    { SDLK_b,               0x42, 0x00, false },
    { SDLK_c,               0x43, 0x00, false },
    { SDLK_d,               0x44, 0x00, false },
    { SDLK_e,               0x45, 0x00, false },
    { SDLK_f,               0x46, 0x00, false },
    { SDLK_g,               0x47, 0x00, false },
    { SDLK_h,               0x48, 0x00, false },
    { SDLK_i,               0x49, 0x00, false },
    { SDLK_j,               0x4A, 0x00, false },
    { SDLK_k,               0x4B, 0x00, false },
    { SDLK_l,               0x4C, 0x00, false },
    { SDLK_m,               0x4D, 0x00, false },
    { SDLK_n,               0x4E, 0x00, false },
    { SDLK_o,               0x4F, 0x00, false },
    { SDLK_p,               0x50, 0x00, false },
    { SDLK_q,               0x51, 0x00, false },
    { SDLK_r,               0x52, 0x00, false },
    { SDLK_s,               0x53, 0x00, false },
    { SDLK_t,               0x54, 0x00, false },
    { SDLK_u,               0x55, 0x00, false },
    { SDLK_v,               0x56, 0x00, false },
    { SDLK_w,               0x57, 0x00, false },
    { SDLK_x,               0x58, 0x00, false },
    { SDLK_y,               0x59, 0x00, false },
    { SDLK_z,               0x5A, 0x00, false },
	{ (SDL_KeyCode)0, 0 }
};

// Ring buffer
static uint16_t inputBuffer[256];
static int inputBufferHead = 0;
static int inputBufferTail = 0;

// Audio buffering
static bool audioAvailable;
static uint8_t* audioPtr;
static uint8_t* audioEnd;
static int mixVolume = 256;
static int inputHz;
static int outputHz;
static int mixCounter = 0;

// Web specific stuff
#if _WEB
#include <emscripten.h>
EM_JS(int, GetWindowWidth, (), {
	return window.innerWidth;
});
EM_JS(int, GetWindowHeight, (), {
	return window.innerHeight - (kbHeight || 0);
});
EM_JS(void, RequestFullScreen, (), {
	document.getElementsByClassName('canvas')[0].requestFullscreen();
});

__attribute__((used))
extern "C" void KeyPressed(int key)
{				
	if (((inputBufferHead + 1) & 0xFF) == inputBufferTail)
		return;

	DebugPrintf("Key %04X received from JS\n", key);
	inputBuffer[inputBufferHead] =key;
	inputBufferHead = (inputBufferHead + 1) & 255;
}
#endif

static const char* ChangeExtension (const char* filename, const char* newExtension)
{
	static char buffer[2048];
	size_t length = strlen(filename);
	const char* ptr = strrchr(filename, '.');
	if (ptr == NULL)
	{
		strcpy(buffer, filename);
		strcat(buffer + length + 1, newExtension);
		return buffer;
	}
	strncpy(buffer, filename, ptr - filename);
	strcpy(buffer + (ptr - filename), newExtension);
	return buffer;
}

void VID_SetCharset (const uint8_t* newCharset)
{
	MemCopy(charset, newCharset, 2048);
	charsetInitialized = true;
}

static bool LoadCharset (uint8_t* ptr, const char* filename)
{
	FILE* file = fopen(filename, "rb");
	if (file == NULL)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	fseek(file, 0, SEEK_END);
	if (ftell(file) != 2176)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		fclose(file);
		return false;
	}
	fseek(file, 128, SEEK_SET);
	if (fread(ptr, 1, 2048, file) != 2048)
	{
		fclose(file);
		return false;
	}
	fclose(file);
	return true;
}

void VID_Clear (int x, int y, int w, int h, uint8_t color)
{
	//fprintf(stderr, "Clear %d,%d to %d,%d %d\n", x, y, x+w, y+h, color);

	switch (screenMachine)
	{
		case DDB_MACHINE_SPECTRUM:
		{
			uint8_t maskLeft = 0xFF00 >> (x & 7);
			uint8_t maskRight = (0x00FF << 8 - ((x + w) & 7)) >> 8;
			w = ((x + w + 7) >> 3) - (x >> 3);
			for (int dy = 0; dy < h; dy++)
			{
				uint8_t* ptr = bitmap + (y + dy) * 32 + (x >> 3);
				if (w == 0)
					*ptr &= (maskLeft | maskRight);
				else
				{
					ptr[0] &= maskLeft;
					for (int dx = 1; dx < w; dx++)
						ptr[dx] = 0;
					ptr[w] &= maskRight;
				}
			}

			color = ((color & 7) << 3) | (curAttr & 0xC7);

			uint8_t* attr = attributes + (y >> 3) * 32 + (x >> 3);
			for (int dy = 0; dy < h; dy += 8)
			{
				for (int dx = 0; dx < w; dx++)
					attr[dx] = color;
				attr += 32;
			}
			break;
		}

		default:
			for (int dy = 0 ; dy < h; dy++)
			{
				uint8_t* ptr = textBuffer + (y + dy) * screenWidth + x;
				for (int dx = 0; dx < w; dx++)
					ptr[dx] = color;
			}
			break;
	}
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	int dy = 0;

	if (lines < h)
	{
		switch (screenMachine)
		{
			case DDB_MACHINE_SPECTRUM:
				w >>= 3;
				for (; dy < h - lines; dy++)
				{
					uint8_t* ptr = bitmap + (y + dy) * 32 + (x >> 3);
					uint8_t* next = ptr + lines * 32;

					for (int dx = 0; dx < w; dx++)
						ptr[dx] = next[dx];
				}
				// TODO: Scroll attributes
				w <<= 3;
				break;

			default:
				for (; dy < h - lines; dy++)
				{
					uint8_t* ptr = textBuffer + (y + dy) * screenWidth + x;
					uint8_t* next = ptr + lines * screenWidth;

					for (int dx = 0; dx < w; dx++)
						ptr[dx] = next[dx];
				}
				break;
		}
	}

	VID_Clear(x, y + dy, w, lines, paper);
}

void VID_ClearBuffer (bool front)
{
	memset(front ? frontBuffer : backBuffer, 0, screenWidth * screenHeight);
}

void VID_SetOpBuffer (SCR_Operation op, bool front)
{
	switch (op)
	{
		case SCR_OP_DRAWPICTURE:
			graphicsBuffer = front ? frontBuffer : backBuffer;
			break;
		case SCR_OP_DRAWTEXT:
			textBuffer = front ? frontBuffer : backBuffer;
			break;
	}
}

void VID_SaveScreen ()
{
	uint8_t* pixels = frontBuffer;
	int y;

	for (y = 0; y < screenHeight; y++)
	{
		memcpy(backBuffer + y * screenWidth, pixels, screenWidth);
		pixels += screenWidth;
	}
}

void VID_RestoreScreen ()
{
	uint8_t* pixels = frontBuffer;
	int y;

	for (y = 0; y < screenHeight; y++)
	{
		memcpy(pixels, backBuffer + y * screenWidth, screenWidth);
		pixels += screenWidth;
	}
}

void VID_SwapScreen ()
{
	uint8_t* pixels = frontBuffer;
	uint8_t* src = backBuffer;
	int y;

	for (y = 0; y < screenHeight; y++)
	{
		uint8_t* dst = pixels;
		for (int x = 0; x < screenWidth; x++)
		{
			uint8_t c = *src++;
			*src++ = *dst;
			*dst++ = c;
		}
		pixels += screenWidth;
	}
}

void VID_DrawCharacter (int x, int y, uint8_t ch, uint8_t ink, uint8_t paper)
{
	switch (screenMachine)
	{
		case DDB_MACHINE_SPECTRUM:
		{
			uint8_t* ptr = charset + (ch << 3);
			uint8_t* out = bitmap + y * 32 + (x >> 3);
			uint8_t  rot = x & 7;
			uint8_t* attr = attributes + (y >> 3) * 32 + (x >> 3);
			uint8_t xattr = (ink & 0x30) << 2;
			uint8_t width = charWidth[ch];

			ink &= 7;

			if (paper == 255)
			{
				*attr = (*attr & 0x37) | ink | xattr;
				if (rot > 8-width)
					attr[1] = (attr[1] & 0x37) | ink | xattr;
			}
			else
			{
				paper &= 7;
				curAttr = ink | (paper << 3) | xattr;
				*attr = curAttr;
				if (rot > 8-width)
					attr[1] = *attr;
			}

			for (int line = 0; line < 8; line++)
			{
				uint8_t* sav = out;
				uint8_t mask = 0x80 >> rot;
				for (int col = 0; col < 6; col++)
				{
					if ((ptr[line] & (0x80 >> col)))
						*out |= mask;
					else if (paper != 255)
						*out &= ~mask;
					mask >>= 1;
					if (mask == 0)
					{
						mask = 0x80;
						out++;
					}
				}
				out = sav + 32;
			}
			break;
		}

		default:
		{
			uint8_t* ptr = charset + (ch << 3);
			uint8_t* pixels = textBuffer + y * screenWidth + x;
			uint8_t  width = charWidth[ch];

			if (paper == 255)
			{
				for (int line = 0; line < 8; line++, pixels += screenWidth)
				{
					for (int col = 0; col < width; col++)
						if ((ptr[line] & (0x80 >> col)))
							pixels[col] = ink;
				}
			}
			else
			{
				for (int line = 0; line < 8; line++, pixels += screenWidth)
				{
					for (int col = 0; col < width; col++)
						pixels[col] = (ptr[line] & (0x80 >> col)) ? ink : paper;
				}
			}
			break;
		}
	}
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	uint32_t c = palette[color];
	*r = (c >> 16) & 0xFF;
	*g = (c >> 8) & 0xFF;
	*b = (c >> 0) & 0xFF;
}

void VID_SetPaletteColor (uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	palette[color] = (r << 16) | (g << 8) | b;
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target)
{
	uint8_t* buffer = Allocate<uint8_t>("SCR Temporary buffer", 32768);
	bool ok = SCR_GetScreen(fileName, target, buffer, 32768,
		graphicsBuffer, screenWidth, screenHeight, palette);
	Free(buffer);
	return ok;
}

void VID_LoadPicture (uint8_t picno, DDB_ScreenMode mode)
{
	if (dmg == NULL) return;

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == NULL || entry->type != DMGEntry_Image)
		return;

	bufferedEntry = entry;
	bufferedIndex = picno;
	pictureData   = DMG_GetEntryData(dmg, picno,
			mode == ScreenMode_CGA ? ImageMode_PackedCGA :
			mode == ScreenMode_EGA ? ImageMode_PackedEGA : ImageMode_Packed);
	if (pictureData == 0)
	{
		bufferedEntry = NULL;
	}
}

void VID_GetPictureInfo (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{
	if (bufferedEntry == NULL)
	{
		if (fixed != NULL)
			*fixed = false;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = 0;
		if (h != NULL)
			*h = 0;
	}
	else
	{
		if (fixed != NULL)
			*fixed = bufferedEntry->fixed;
		if (x != NULL)
			*x = bufferedEntry->x;
		if (y != NULL)
			*y = bufferedEntry->y;
		if (w != NULL)
			*w = bufferedEntry->width;
		if (h != NULL)
			*h = bufferedEntry->height;
	}
}

void VID_DisplayPicture (int x, int y, int w, int h, DDB_ScreenMode mode)
{
	DMG_Entry* entry = bufferedEntry;
	if (pictureData == NULL)
		return;
	if (entry == NULL)
		return;

	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if (w <= 0 || h <= 0)
		return;

	if (h > entry->height)
		h = entry->height;
	if (w > entry->width)
		w = entry->width;
	
	uint8_t* srcPtr = (uint8_t*)pictureData;
	uint8_t* dstPtr = graphicsBuffer + y * screenWidth + x;
	uint32_t* filePalette = (uint32_t*)DMG_GetEntryPalette(dmg, bufferedIndex, ImageMode_RGBA32);
	for (int dy = 0; dy < h; dy++, srcPtr += entry->width/2, dstPtr += screenWidth)
	{
		for (int dx = 0, ix = 0; dx < w; dx += 2, ix++)
		{
			dstPtr[dx] = srcPtr[ix] >> 4;
			dstPtr[dx+1] = srcPtr[ix] & 0x0F;
		}
	}

	switch (mode)
	{
		default:
		case ScreenMode_VGA16:
			if (entry->fixed)
			{
				for (int n = 0; n < 16; n++)
					palette[n] = filePalette[n];
				// TODO: This is a hack to fix the palette for Original/Jabato
				//       We should check the ST/DOS interpreter to see what's going on
				if (dmg->version == DMG_Version1)
					palette[15] = 0xFFFFFFFF;
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			for (int n = 0; n < 16; n++) {
				palette[n] = entry->CGAMode == CGA_Red ? CGAPaletteRed[n] : CGAPaletteCyan[n];
			}
			break;
	}

	//fprintf(stderr, "\nDrawBufferedPicture %d (%s): %d %d %d %d\n", bufferedIndex,
	  	//entry->fixed ? "fixed" : "floating", x, y, w, h);
}

void VID_Quit ()
{
	quit = true;
}

void VID_Finish()
{
	DebugPrintf("Closing video subsystem\n");
	if (window != NULL)
	{
		SDL_DestroyWindow(window);
		window = NULL;
	}
	if (frontBuffer != NULL)
	{
		Free(frontBuffer);
		frontBuffer = NULL;
	}
	if (backBuffer != NULL)
	{
		Free(backBuffer);
		backBuffer = NULL;
	}
	quit = true;
	exitGame = true;
}

void VID_OpenFileDialog (bool existing, char* buffer, size_t bufferSize)
{
#if _WIN32
	OPENFILENAMEA ofn;

	*buffer = 0;

	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrTitle = existing ? "Open Game State" : "Save Game State";
	ofn.lpstrFilter = "Game State Files (*.sav)\0*.sav\0";
	ofn.lpstrFile = buffer;
	ofn.nMaxFile = bufferSize;
	ofn.lpstrDefExt = ".sav";
	if (existing)
	{
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		GetOpenFileNameA(&ofn);
	}
	else
	{
		ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;
		GetSaveFileNameA(&ofn);
	}
#endif
}

bool VID_AnyKey ()
{
	return inputBufferHead != inputBufferTail;
}

void VID_GetKey (uint8_t* key, uint8_t* ext, uint8_t* mod)
{
	if (inputBufferHead != inputBufferTail)
	{
		int modState = SDL_GetModState();
		int keyCode = inputBuffer[inputBufferTail] & 0xFF;
		int extCode = inputBuffer[inputBufferTail] >> 8;

#if HAS_FULLSCREEN
		if (keyCode == SDLK_F11)
		{
			VID_ToggleFullscreen();
			return;
		}
		if (keyCode == 13 && (modState & KMOD_ALT))
		
			VID_ToggleFullscreen();
#endif

		if (key != NULL)
			*key = keyCode;
		if (ext != NULL)
			*ext = extCode;
		if (mod != NULL)
		{
			*mod = 0;
			if (modState & KMOD_SHIFT)
				*mod |= SCR_KEYMOD_SHIFT;
			if (modState & KMOD_CTRL)
				*mod |= SCR_KEYMOD_CTRL;
			if (modState & KMOD_ALT)
				*mod |= SCR_KEYMOD_ALT;
		}
		inputBufferTail = (inputBufferTail + 1) & 255;

#if DEBUG_ALLOCS
		if (ext && *ext == 0x3C)
			DumpMemory();
#endif
	}
	else
	{
		if (key != NULL)
			*key = 0;
		if (ext != NULL)
			*ext = 0;
	}
} 

void VID_GetMilliseconds (uint32_t* ms)
{
	*ms = SDL_GetTicks();
}

void VID_SetTextInputMode (bool enabled)
{
	textInput = enabled;

	if (enabled)
	{
#if _WEB
		EM_ASM(
			document.getElementById('input').focus();
		);
#endif
		SDL_StartTextInput();
	}
	else
	{
#if _WEB
		EM_ASM(
			document.getElementById('input').blur();
		);
#endif
		SDL_StopTextInput();
	}
}

static void	RenderSpectrumScreen(uint8_t* attributes)
{
	bool flashOn = (SDL_GetTicks() / 500) & 1;

	uint8_t* attrPtr = attributes;
	for (int y = 0; y < 24; y++)
	{
		uint8_t* ptr = bitmap + 32 * (y * 8);
		uint8_t* out = frontBuffer + y * 8 * screenWidth;

		for (int x = 0; x < 32; x++, ptr++)
		{
			uint8_t attr = *attrPtr++;
			uint8_t ink = (attr & 0x07);
			uint8_t paper = ((attr >> 3) & 0x07);
			if (attr & 0x40)
			{
				// Bright On
				ink |= 0x08;
				if (paper != 0)
					paper |= 0x08;
			}
			if ((attr & 0x80) && flashOn)
			{
				// Flash On
				uint8_t tmp = ink;
				ink = paper;
				paper = tmp;
			}

			uint8_t* outPtr = out;

			for (int cy = 0; cy < 8; cy++)
			{
				uint8_t pixels = ptr[cy * 32];

				for (int cx = 0; cx < 8; cx++)
				{
					out[cx] = (pixels & 0x80) ? ink : paper;
					pixels <<= 1;
				}
				out += screenWidth;
			}

			out = outPtr + 8;
		}
	}
}

void VID_SaveDebugBitmap()
{
	static int n = 0;

	uint8_t grid[768];
	for (int n = 0; n < 768; n++)
	{
		int y = n / 32;
		int x = n % 32;
		int checkboard = ((x & 1) ^ (y & 1));
		grid[n] = checkboard ? (6 << 3) : (7 << 3);
	}

	if (screenMachine == DDB_MACHINE_SPECTRUM)
		RenderSpectrumScreen(grid);

	char name[64];
	sprintf(name, "debug%04d.bmp", n++);
	printf("\nSaving debug bitmap: %s\n", name);

	SDL_Color colors[16];
	for (int n = 0; n < 16; n++)
	{
		colors[n].r = (palette[n] >> 16) & 0xFF;
		colors[n].g = (palette[n] >> 8) & 0xFF;
		colors[n].b = (palette[n] >> 0) & 0xFF;
	}

	SDL_Surface* debugSurface = SDL_CreateRGBSurfaceFrom(frontBuffer, screenWidth, screenHeight, 8, screenWidth, 0, 0, 0, 0);
	SDL_SetPaletteColors(debugSurface->format->palette, colors, 0, 16);
	SDL_SaveBMP(debugSurface, name);
	SDL_FreeSurface(debugSurface);
}

void VID_InnerLoop()
{
	Uint32 now = SDL_GetTicks();
	SDL_Event event;
	static Uint32 ticks = 0;
	static int minBorderSize = 0;

	if (minBorderSize == 0)
	{
		SDL_DisplayMode DM;
		SDL_GetCurrentDisplayMode(0, &DM);
		if (DM.h >= 1440) minBorderSize = 40;
		if (DM.h >= 2000) minBorderSize = 100;
	}
	
	if (ticks == 0)
		ticks = SDL_GetTicks();
	int elapsed = now - ticks;
	ticks = now;

	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
			case SDL_QUIT:
				VID_Finish();
				return;

			case SDL_KEYDOWN:
				// printf("SDL_KEYDOWN: %d (%s)\n", event.key.keysym.sym, SDL_GetKeyName(event.key.keysym.sym));
				if (((inputBufferHead + 1) & 0xFF) == inputBufferTail)
					break;
				if (event.key.keysym.sym == SDLK_F10)
				{
					if (++interpreter->keyClick == 3)
						interpreter->keyClick = 0;
				}

				#if HAS_FULLSCREEN
				if (event.key.keysym.sym == SDLK_F11)
				{
					VID_ToggleFullscreen();
					break;
				}
				if (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT))
				{
					VID_ToggleFullscreen();
					break;
				}
				#endif
				
				for (int n = 0; keyMapping[n].key != 0; n++)
				{
					if (keyMapping[n].key == event.key.keysym.sym)
					{
						if (textInput == false || keyMapping[n].special || (SDL_GetModState() & KMOD_CTRL))
						{
							inputBuffer[inputBufferHead] = keyMapping[n].code;
							inputBuffer[inputBufferHead] |= keyMapping[n].ext << 8;
							inputBufferHead = (inputBufferHead + 1) & 255;
							break;
						}
					}
				}
				break;

			case SDL_TEXTINPUT:
				// printf("SDL_TEXTINPUT: %s\n", event.text.text);
				if (((inputBufferHead + 1) & 0xFF) == inputBufferTail)
					break;
				uint8_t code = event.text.text[0];
				if ((code & 0xE0) == 0xC0)
					code = ((code & 0x1F) << 6) | ((event.text.text[1] & 0x7F) << 0);
				inputBuffer[inputBufferHead] = DDB_ISO2Char[code];
				inputBufferHead = (inputBufferHead + 1) & 255;
				break;
		}
	}
	
	if (mainLoopCallback)
		mainLoopCallback(elapsed);

	int pxw, pxh;
#if _WEB
	pxw = GetWindowWidth();
	pxh = GetWindowHeight();
	SDL_SetWindowSize(window, pxw, pxh);

#else
	SDL_GetWindowSize(window, &pxw, &pxh);
#endif

	surface = SDL_GetWindowSurface(window);
	if (surface == 0)
	{
		static bool reported = false;
		if (!reported)
		{
			DebugPrintf("Unable to get window %p surface window\n", window);
			DebugPrintf("SDL_GetWindowSurface(window): %s\n", SDL_GetError());
			reported = true;
		}
		return;
	}

	SDL_FillRect(surface, NULL, palette[0]);

	if (SDL_LockSurface(surface) != 0) 
	{
		DebugPrintf("Unable to lock window %p\n", window);
		DebugPrintf("SDL_LockSurface(surface): %s\n", SDL_GetError());
	}
	else
	{
		if (screenMachine == DDB_MACHINE_SPECTRUM)
			RenderSpectrumScreen(attributes);

		int srcWidth = screenWidth;
		int srcHeight = screenHeight;
	
		int scale = 1;
		while (scale < (surface->w-minBorderSize) / srcWidth && scale < (surface->h-minBorderSize) / srcHeight)
			scale++;

		int dstX = (surface->w - srcWidth * scale) / 2;
		int dstY = (surface->h - srcHeight * scale) / 2;

		uint32_t surfacePalette[16];
		for (int c = 0; c < 16; c++)
		{
			uint8_t r = palette[c] >> 16;
			uint8_t g = palette[c] >> 8;
			uint8_t b = palette[c];
			surfacePalette[c] = SDL_MapRGB(surface->format, r, g, b);
		}
	
		uint8_t* srcPtr = frontBuffer;
		uint32_t* dstPtr = (uint32_t*)surface->pixels + dstY * surface->pitch / 4 + dstX;
		for (int dy = 0; dy < srcHeight; dy++, srcPtr += screenWidth)
		{
			uint32_t* dst = dstPtr;
			uint8_t*  src = srcPtr;
			for (int dx = 0; dx < srcWidth; dx++) 
			{
				for (int n = 0; n < scale; n++)
					*dst++ = surfacePalette[*src & 0x0F];
					src++;
			}
			dstPtr += surface->pitch / 4;
			for (int n = 1; n < scale; n++) 
			{
				memcpy(dstPtr, dstPtr - surface->pitch / 4, srcWidth * 4 * scale);
				dstPtr += surface->pitch / 4;
			}
		}

		SDL_UnlockSurface(surface);
	}

	SDL_UpdateWindowSurface(window);
	//DebugPrintf("SDL_UpdateWindowSurface: %s\n", SDL_GetError());
}

void VID_MainLoopAsync (DDB_Interpreter* interpreter, void (*callback)(int elapsed))
{
	interpreter = interpreter;
	mainLoopCallback = callback;
	quit = false;
}

void VID_MainLoop (DDB_Interpreter* interpreter, void (*callback)(int elapsed))
{
	interpreter = interpreter;
	mainLoopCallback = callback;
	quit = false;

#ifndef _WEB
	Uint32 ticks = SDL_GetTicks();
	while (!quit)
	{
		// Delay for smooth scrolling
		Uint32 now = SDL_GetTicks();
		if (buffering || (interpreter != NULL && interpreter->state == DDB_VSYNC)) 
		{
			if (now - ticks < 20)
				SDL_Delay(now + 20 - ticks);
		}
		ticks = now;

		VID_InnerLoop();
	}
	quit = false;
#endif
	
}

void SDLCALL VID_FillAudio (void *udata, Uint8 *stream, int len)
{
	Uint8* end = stream + len;
	memset(stream, 128, len);

	if (audioPtr == NULL)
		return;

	// Poor man's crappy ZOH resample mix I wrote while drunk
	// TODO: do a proper resample/fiter here

	while (1)
	{
		while (mixCounter <= 0)
		{
			*stream++ = 0x80 + (((*audioPtr - 0x80) * mixVolume) >> 8);
			if (stream >= end) return;
			mixCounter += inputHz;
		}
		while (mixCounter > 0)
		{
			audioPtr++;
			if (audioPtr >= audioEnd) return;
			mixCounter -= outputHz;
		}
	}
}

void VID_PlaySampleBuffer (void* buffer, int samples, int hz, int v)
{
	SDL_LockAudio();
	audioPtr = (uint8_t*)buffer;
	audioEnd = (uint8_t*)buffer + samples;
	inputHz = hz;
	mixVolume = v;
	SDL_UnlockAudio();

	// fprintf(stderr, "PlaySampleBuffer: %d samples, %d Hz, %d volume\n", samples, hz, v);
}

void VID_PlaySample (uint8_t picno, int* duration)
{
	if (!audioAvailable) return;

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == NULL || entry->type != DMGEntry_Audio)
		return;

	audioData = DMG_GetEntryData(dmg, picno, ImageMode_Audio);
	if (audioData == 0)
		return;

	SDL_LockAudio();
	audioPtr = audioData;
	audioEnd = audioPtr + entry->length;
	mixVolume = 256;
	switch (entry->x)
	{
		case DMG_5KHZ:   inputHz =  5000; break;
		case DMG_7KHZ:   inputHz =  7000; break;
		case DMG_9_5KHZ: inputHz =  9500; break;
		case DMG_15KHZ:  inputHz = 15000; break;
		case DMG_20KHZ:  inputHz = 20000; break;
		case DMG_30KHZ:  inputHz = 30000; break;
		default:         inputHz = 11025; break;
	}
	SDL_UnlockAudio();

	if (duration != NULL)
		*duration = entry->length * 1000 / inputHz;
}

void VID_InitAudio ()
{
	SDL_AudioSpec wanted;
	memset(&wanted, 0, sizeof(wanted));
    wanted.freq = 30000;
    wanted.format = AUDIO_U8;
    wanted.channels = 1;    /* 1 = mono, 2 = stereo */
    wanted.samples = 1024;  /* Good low-latency value for callback */
    wanted.callback = VID_FillAudio;
    wanted.userdata = NULL;

	audioAvailable = false;
    if (SDL_OpenAudio(&wanted, NULL) < 0) 
	{
        fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
        return;
    }

	audioAvailable = true;
	outputHz = wanted.freq;
	mixCounter = -outputHz;
	SDL_PauseAudio(0);
}

bool VID_LoadDataFile(const char* fileName)
{
	if (dmg != NULL)
	{
		DMG_Close(dmg);
		dmg = NULL;
	}

	dmg = DMG_Open(ChangeExtension(fileName, ".dat"), true);
	if (dmg == NULL)
		dmg = DMG_Open(ChangeExtension(fileName, ".DAT"), true);
	if (dmg == NULL)
		dmg = DMG_Open(ChangeExtension(fileName, ".ega"), true);
	if (dmg == NULL)
		dmg = DMG_Open(ChangeExtension(fileName, ".cga"), true);
	if (dmg == NULL)
	{
		DebugPrintf("Data file for %s not found!\n", fileName);
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}

	if (!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		if (!charsetInitialized)
		{
			memcpy(charset, DefaultCharset, 1024);
			memcpy(charset + 1024, DefaultCharset, 1024);
		}
	}
	charsetInitialized = true;

	if (screenMachine == DDB_MACHINE_SPECTRUM)
	{
		screenMode = (DDB_ScreenMode)dmg->screenMode;
		if (dmg->screenMode == ScreenMode_CGA)
		{
			memcpy (palette, CGAPaletteCyan, sizeof(CGAPaletteCyan));
			memcpy (DefaultPalette, palette, sizeof(DefaultPalette));
		}
		else if (dmg->screenMode == ScreenMode_EGA)
		{
			memcpy (palette, EGAPalette, sizeof(EGAPalette));
			memcpy (DefaultPalette, palette, sizeof(DefaultPalette));
		}
	}
	
	// Uncomment the following lines to test Atari/Amiga caches in desktop/web:
	// DMG_SetupFileCache(dmg);
	// DMG_SetupImageCache(dmg, 32768);

	return true;
}

bool VID_Initialize (DDB_Machine machine)
{
	DebugPrintf("Initializing video subsystem\n");

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	VID_InitAudio();
	SDL_StopTextInput();
	
	switch (machine)
	{
		case DDB_MACHINE_SPECTRUM:
			memcpy(DefaultPalette, ZXSpectrumPalette, sizeof(ZXSpectrumPalette));
			screenMachine = machine;
			screenWidth   = 256;
			screenHeight  = 192;
			bitmap        = Allocate<uint8_t>("Spectrum Screen Data", 256 * 192 / 8);
			attributes    = Allocate<uint8_t>("Spectrum Attributes", 32 * 24);
			break;
		default:
			memcpy(DefaultPalette, EGAPalette, sizeof(EGAPalette));
			screenMachine = DDB_MACHINE_IBMPC;
			screenWidth   = 320;
			screenHeight  = 200;
			break;
	}
	lineHeight       = 8;
	columnWidth      = 6;
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

	memcpy (palette, DefaultPalette, sizeof(DefaultPalette));

	frontBuffer   = Allocate<uint8_t>("Graphics front buffer", screenWidth * screenHeight);
	backBuffer    = Allocate<uint8_t>("Graphics back buffer", screenWidth * screenHeight);
	if (frontBuffer == NULL || backBuffer == NULL) {
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	memset(frontBuffer, 0, screenWidth * screenHeight);
	memset(backBuffer, 0, screenWidth * screenHeight);

	textBuffer = frontBuffer;
	graphicsBuffer = frontBuffer;

	if (charsetInitialized == false)
	{
		memcpy(charset, DefaultCharset, 1024);
		memcpy(charset + 1024, DefaultCharset, 1024);
		charsetInitialized = true;
	}

	int scale = 3;
	SDL_DisplayMode DM;
	SDL_GetCurrentDisplayMode(0, &DM);
	if (DM.h >= 1440) scale = 4;
	if (DM.h >= 2000) scale = 6;
#if _WEB
	int options = SDL_WINDOW_SHOWN;
#else
	int options = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
	window = SDL_CreateWindow("ADP " VERSION_STR, 
		SDL_WINDOWPOS_UNDEFINED, 
		SDL_WINDOWPOS_UNDEFINED, 
		360*scale, 224*scale, 
		options);
	if (window == NULL)
	{
		DDB_SetError(DDB_ERROR_SDL);
		return false;
	}

	surface = SDL_GetWindowSurface(window);
	if (surface == NULL)
	{
		DebugPrintf("Error creating window surface: %s\n", SDL_GetError());
		return false;
	}

	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	DebugPrintf("Window Created: %p (%dx%d at scale %d)\n", window, w, h, scale);

	return true;
}

void VID_WaitForKey()
{
	// Unsupported
}

void VID_ActivatePalette()
{
	// Not needed
}

void VID_VSync()
{
	VID_ActivatePalette();
	VID_InnerLoop();
	SDL_Delay(16);
}

static char* clipboard = 0;

static uint32_t ConvertUTF8 (char* text)
{
	uint8_t* org = (uint8_t*)text;
	uint8_t* src = (uint8_t*)text;
	uint8_t* dst = (uint8_t*)text;
	while (*src)
	{
		uint32_t c = *src++;
		if ((c & 0xE0) == 0xC0)
			c = ((c & 0x1F) << 6) | (*src++ & 0x7F);
		else if ((c & 0xF0) == 0xE0) {
			c = ((c & 0x0F) << 12) | ((src[0] & 0x7F) << 6) | (src[1] & 0x7F);
			src += 2;
		} else if ((c & 0xF8) == 0xF0) {
			c = ((c & 0x07) << 18) | ((src[0] & 0x7F) << 12) | ((src[1] & 0x7F) << 6) | (src[2] & 0x7F);
			src += 3;
		}
		if (c >= 32 && c <= 255)
			*dst++ = DDB_ISO2Char[c];
	}
	*dst = 0;
	return dst - org;
}

static uint32_t MeasureUTF8Conversion (const uint8_t* text, uint32_t length)
{
	uint32_t size = 0;
	const uint8_t* end = text + length;
	while (*text && text < end)
	{
		uint8_t c = *text++;
		c = DDB_Char2ISO[c];
		if (c <= 127)
			size += 1;
		else 
			size += 2;
	}
	return size + 1;
}

static void ConvertToUTF8 (const uint8_t* text, uint32_t size, uint8_t* buffer, uint32_t bufferSize)
{
	const uint8_t* src = text;
	const uint8_t* end = text + size;
	uint8_t* dst = buffer;
	uint8_t* dstEnd = buffer + bufferSize;
	while (*src && src < end && dst < dstEnd)
	{
		uint32_t c = *src++;
		c = DDB_Char2ISO[c];
		if (c <= 127)
		{
			*dst++ = c;
		}
		else 
		{
			*dst++ = 0xC0 | ((c >> 6) & 0x1F);
			if (dst < dstEnd)
				*dst++ = 0x80 | ((c >> 0) & 0x3F);
		}
	}
	if (dst < dstEnd)
		*dst = 0;
}

bool VID_HasClipboardText(uint32_t *size)
{
	if (clipboard != 0)
		SDL_free(clipboard);
	
	clipboard = SDL_GetClipboardText();
	if (clipboard != 0)
	{
		uint32_t length = ConvertUTF8(clipboard);
		if (size)
			*size = length;
		return true;
	}
	return false;
}

void VID_GetClipboardText(uint8_t *buffer, uint32_t bufferSize)
{
	if (buffer == 0 || bufferSize == 0)
		return;
	if (clipboard == 0)
	{
		*buffer = 0;
		return;
	}

	uint32_t length = ConvertUTF8(clipboard);
	if (length > bufferSize)
		length = bufferSize;
		
	MemCopy(buffer, clipboard, length);
	if (length < bufferSize)
		buffer[length] = 0;
}

void VID_SetClipboardText(uint8_t *buffer, uint32_t bufferSize)
{
	if (buffer == 0 || bufferSize == 0)
		return;
	if (clipboard != 0)
	{
		Free(clipboard);
		clipboard = 0;
	}
	
	uint32_t size = MeasureUTF8Conversion(buffer, bufferSize);
	uint8_t* text = Allocate<uint8_t>("Clipboard text", size);
	ConvertToUTF8(buffer, bufferSize, text, size);
	SDL_SetClipboardText((char *)text);
	Free(text);
}

void VID_ToggleFullscreen()
{
	uint32_t flags = SDL_GetWindowFlags(window);
	SDL_SetWindowFullscreen(window, flags & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}
