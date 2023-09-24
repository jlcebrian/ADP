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
uint32_t   palette[256];

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
	fread(ptr, 1, 2048, file);
	fclose(file);
	return true;
}

void VID_ResetInkMap()
{
	inkMap[0] = 0;
	inkMap[1] = 15;
	for (int n = 2; n < 16; n++)
		inkMap[n] = n-1;
}

void VID_Clear (int x, int y, int w, int h, uint8_t color)
{
	//fprintf(stderr, "Clear %d,%d to %d,%d %d\n", x, y, x+w, y+h, color);
	for (int dy = 0 ; dy < h; dy++)
	{
		uint8_t* ptr = textBuffer + (y + dy) * screenWidth + x;
		for (int dx = 0; dx < w; dx++)
			ptr[dx] = color;
	}
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	int dy = 0;

	if (lines < h)
	{
		for (; dy < h - lines; dy++)
		{
			uint8_t* ptr = textBuffer + (y + dy) * screenWidth + x;
			uint8_t* next = ptr + lines * screenWidth;

			for (int dx = 0; dx < w; dx++)
				ptr[dx] = next[dx];
		}
	}
	
	for (; dy < h; dy++)
	{
		uint8_t* ptr = textBuffer + (y + dy) * screenWidth + x;
		for (int dx = 0; dx < w; dx++)
			ptr[dx] = paper;
	}
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

	uint8_t* ptr = charset + (ch << 3);
	uint8_t* pixels = textBuffer + y * screenWidth + x;

	if (paper == 255)
	{
		for (int line = 0; line < 8; line++, pixels += screenWidth)
		{
			for (int col = 0; col < 8; col++)
				if ((ptr[line] & (0x80 >> col)))
					pixels[col] = ink;
		}
	}
	else
	{
		for (int line = 0; line < 8; line++, pixels += screenWidth)
		{
			for (int col = 0; col < 6; col++)
				pixels[col] = (ptr[line] & (0x80 >> col)) ? ink : paper;
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
				// TODO: This is a hack to fix the palette for the old version of the game
				if (dmg->version == DMG_Version1)
					entry->RGB32Palette[15] = 0xFFFFFFFF;
				for (int n = 0; n < 16; n++)
					palette[n] = filePalette[n];
				VID_UpdateInkMap();
				VID_ActivatePalette();
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			for (int n = 0; n < 16; n++) {
				palette[n] = entry->CGAMode == CGA_Red ? CGAPaletteRed[n] : CGAPaletteCyan[n];
			}
			VID_ResetInkMap();
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
		if (key != NULL)
			*key = inputBuffer[inputBufferTail] & 0xFF;
		if (ext != NULL)
			*ext = inputBuffer[inputBufferTail] >> 8;
		if (mod != NULL)
		{
			int modState = SDL_GetModState();
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
#if _WEB
				if (event.key.keysym.sym == SDLK_F11)
				{
					RequestFullScreen();
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
		memcpy(charset, DefaultCharset, 1024);
		memcpy(charset + 1024, DefaultCharset, 1024);
	}

	if (dmg->screenMode == ScreenMode_CGA)
		memcpy (palette, CGAPaletteCyan, sizeof(CGAPaletteCyan));
	else if (dmg->screenMode == ScreenMode_EGA)
		memcpy (palette, EGAPalette, sizeof(EGAPalette));
	VID_UpdateInkMap();
	
	//DMG_SetupFileCache(dmg);
	//DMG_SetupImageCache(dmg, 32768);

	return true;
}

bool VID_Initialize ()
{
	DebugPrintf("Initializing video subsystem\n");

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	VID_InitAudio();
	SDL_StopTextInput();
	
	screenWidth      = 320;
	screenHeight     = 200;
	lineHeight       = 8;
	columnWidth      = 6;
	screenWidth      = 320;
	screenHeight     = 200;
	for (int n = 0; n < 256; n++)
		charWidth[n] = 6;

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

	memcpy (palette, EGAPalette, sizeof(EGAPalette));
	VID_UpdateInkMap();

	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);

	int scale = 3;
	SDL_DisplayMode DM;
	SDL_GetCurrentDisplayMode(0, &DM);
	if (DM.h >= 1440) scale = 4;
	if (DM.h >= 2000) scale = 6;
#if _WEB
	window = SDL_CreateWindow("DAAD Interpreter " VERSION_STR, 
		SDL_WINDOWPOS_UNDEFINED, 
		SDL_WINDOWPOS_UNDEFINED, 
		360*scale, 224*scale, 
		SDL_WINDOW_SHOWN);
#else
	window = SDL_CreateWindow("DAAD Interpreter " VERSION_STR, 
		SDL_WINDOWPOS_UNDEFINED, 
		SDL_WINDOWPOS_UNDEFINED, 
		360*scale, 224*scale, 
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
#endif
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
