#include <ddb.h>
#include <ddb_data.h>
#include <ddb_pal.h>
#include <ddb_scr.h>
#include <ddb_vid.h>
#include <ddb_xmsg.h>
#include <dmg.h>
#include <dmg_font.h>
#include <os_char.h>
#include <os_file.h>
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

static inline uint32_t PackSurfaceRGB(SDL_PixelFormat* format, uint8_t r, uint8_t g, uint8_t b)
{
	return (((uint32_t)(r >> format->Rloss)) << format->Rshift) |
		(((uint32_t)(g >> format->Gloss)) << format->Gshift) |
		(((uint32_t)(b >> format->Bloss)) << format->Bshift) |
		format->Amask;
}

static void GetDisplayAspect(int* width, int* height)
{
	*width = screenWidth;
	*height = screenHeight;

	if (screenMode == ScreenMode_HiRes)
		*height *= 2;
}

static const int InitialWindowBorderWidth = 160;
static const int InitialWindowBorderHeight = 100;

static int GetMinimumBorderSize()
{
	SDL_DisplayMode DM;
	SDL_GetCurrentDisplayMode(0, &DM);

	if (DM.h >= 2000)
		return 100;
	if (DM.h >= 1440)
		return 40;
	return 0;
}

static int GetInitialWindowScale()
{
	int displayWidth, displayHeight;
	GetDisplayAspect(&displayWidth, &displayHeight);

	SDL_DisplayMode DM;
	SDL_GetCurrentDisplayMode(0, &DM);

	int preferredScale = 3;
	if (DM.h >= 1440)
		preferredScale = 4;
	if (DM.h >= 2000)
		preferredScale = 6;

	int minBorderSize = GetMinimumBorderSize();
	int maxWindowWidth = DM.w - minBorderSize;
	int maxWindowHeight = DM.h - minBorderSize;

	int scale = 1;
	while ((scale + 1) * displayWidth + InitialWindowBorderWidth <= maxWindowWidth &&
		(scale + 1) * displayHeight + InitialWindowBorderHeight <= maxWindowHeight &&
		scale < preferredScale)
	{
		scale++;
	}

	return scale;
}

void VID_SetDisplayPlanesHint(uint8_t planes)
{
	(void)planes;
}

SDL_Window*  window;
SDL_Surface* surface;

uint8_t*   graphicsBuffer;
uint8_t*   textBuffer;

uint8_t*   frontBuffer = NULL;
uint8_t*   backBuffer = NULL;
uint8_t*   pictureData;
static uint8_t charset16[256 * 32];
static bool charset16Available = false;
#if HAS_PCX
uint8_t*   pcxPictureData = NULL;
#if HAS_SPECTRUM
uint8_t*   zxsPictureBitmap = NULL;
uint8_t*   zxsPictureAttributes = NULL;
bool       zxsPictureMirror = false;
#endif
#endif
uint8_t*   audioData;
DMG_Entry* bufferedEntry = NULL;
uint8_t    bufferedIndex;
bool       quit;
bool       exitGame = false;
bool       textInput;
bool	   charsetInitialized = false;
bool       videoInitialized = false;
uint32_t   palette[256];
uint8_t    defaultCharWidth = 6;
#if HAS_PCX
uint32_t   pcxPalette[256];

uint32_t   pcxPictureSize = 0;
int        pcxPictureWidth = 0;
int        pcxPictureHeight = 0;
#if HAS_SPECTRUM
int        zxsPictureWidth = 0;
int        zxsPictureHeight = 0;
#endif
#endif

int        xCoordMultiplier = 1;
int        yCoordMultiplier = 1;
static bool screen2XMode = false;
static DDB_Version screenVersion = DDB_VERSION_1;

static bool PaletteMatches(const uint32_t* candidate, int paletteCount, int firstColor, bool clearOutside)
{
	if (candidate == NULL)
		return false;

	for (int n = 0; n < paletteCount && firstColor + n < 256; n++)
	{
		if (palette[firstColor + n] != candidate[n])
			return false;
	}

	if (clearOutside && firstColor == 0)
	{
		for (int n = paletteCount; n < 256; n++)
		{
			if (palette[n] != 0xFF000000)
				return false;
		}
	}

	return true;
}

// Specific for Spectrum
uint8_t*   bitmap = NULL;
uint8_t*   attributes = NULL;
uint8_t    stride = 32;

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

void VID_SetCharset (const uint8_t* newCharset)
{
	MemCopy(charset, newCharset, 2048);
	charsetInitialized = true;
}

void VID_SetCharsetWidth(uint8_t w)
{
	if (screen2XMode)
		w = (uint8_t)(w * 2);
	for (int n = 0; n < 256; n++)
		charWidth[n] = w;
}

static void ApplyIBMPCTextMetrics(bool enable2X)
{
	uint8_t baseColumnWidth = (screenVersion == DDB_VERSION_PAWS || screenMachine == DDB_MACHINE_PCW) ? 8 : 6;
	lineHeight = enable2X ? 16 : 8;
	screenCellWidth = enable2X ? 16 : 8;
	columnWidth = enable2X ? (uint8_t)(baseColumnWidth * 2) : baseColumnWidth;
	defaultCharWidth = columnWidth;
	screen2XMode = enable2X;
	charset16Available = false;
	for (int n = 0; n < 256; n++)
		charWidth[n] = defaultCharWidth;
}

static bool LoadCharset (uint8_t* ptr, const char* filename)
{
	File* file = File_Open(filename, ReadOnly);
	if (file == NULL)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}
	if (File_GetSize(file) != 2176)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		File_Close(file);
		return false;
	}
	File_Seek(file, 128);
	if (File_Read(file, ptr, 2048) != 2048)
	{
		DDB_SetError(DDB_ERROR_READING_FILE);
		File_Close(file);
		return false;
	}
	File_Close(file);
	return true;
}

static bool LoadSINTACFont(const char* filename)
{
	DMG_Font font;
	if (!DMG_ReadSINTACFont(filename, &font))
		return false;

	if (screen2XMode)
	{
		MemCopy(charset16, font.bitmap16, sizeof(font.bitmap16));
		MemCopy(charWidth, font.width16, sizeof(font.width16));
		charset16Available = true;
	}
	else
	{
		MemCopy(charset, font.bitmap8, sizeof(font.bitmap8));
		MemCopy(charWidth, font.width8, sizeof(font.width8));
	}
	charsetInitialized = true;
	return true;
}

#if HAS_PCX
static bool FileExists(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
		return false;
	File_Close(file);
	return true;
}

static void BuildFileNameWithExtension(const char* fileName, const char* extension, char* output, size_t outputSize)
{
	StrCopy(output, outputSize, fileName);
	char* dot = (char*)StrRChr(output, '.');
	if (dot == 0)
		dot = output + StrLen(output);
	StrCopy(dot, output + outputSize - dot, extension);
}

static bool FindNamedPCXFile(const char* fileName, char* output, size_t outputSize)
{
	BuildFileNameWithExtension(fileName, ".VGA", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".vga", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".PCX", output, outputSize);
	if (FileExists(output))
		return true;

	BuildFileNameWithExtension(fileName, ".pcx", output, outputSize);
	return FileExists(output);
}

static void FreeBufferedPCXPicture()
{
	if (pcxPictureData != NULL)
	{
		Free(pcxPictureData);
		pcxPictureData = NULL;
	}
	#if HAS_SPECTRUM
	if (zxsPictureBitmap != NULL)
	{
		Free(zxsPictureBitmap);
		zxsPictureBitmap = NULL;
	}
	if (zxsPictureAttributes != NULL)
	{
		Free(zxsPictureAttributes);
		zxsPictureAttributes = NULL;
	}
	pcxPictureSize = 0;
	pcxPictureWidth = 0;
	pcxPictureHeight = 0;
	zxsPictureWidth = 0;
	zxsPictureHeight = 0;
	zxsPictureMirror = false;
	#endif
}

#if HAS_SPECTRUM
static uint8_t ReverseBits(uint8_t value)
{
	value = (uint8_t)(((value & 0xF0) >> 4) | ((value & 0x0F) << 4));
	value = (uint8_t)(((value & 0xCC) >> 2) | ((value & 0x33) << 2));
	value = (uint8_t)(((value & 0xAA) >> 1) | ((value & 0x55) << 1));
	return value;
}

static bool HasOnlyPaddingBytes(const uint8_t* data, size_t offset, size_t size)
{
	for (size_t i = offset; i < size; i++)
	{
		if (data[i] != 0x00 && data[i] != 0xE5)
			return false;
	}
	return true;
}

static uint32_t GetSpectrumBitmapOffset(uint32_t y)
{
	return ((y & 0xC0u) << 5) |
		((y & 0x07u) << 8) |
		((y & 0x38u) << 2);
}

static void DecodeLegacyZXSBitmap(const uint8_t* source, uint8_t numLines, uint8_t* destination)
{
	uint8_t scrBitmap[6144];
	memset(scrBitmap, 0, sizeof(scrBitmap));

	size_t sourceOffset = 0;
	uint8_t remainingLines = numLines;
	uint32_t scrOffset = 0;
	while (remainingLines >= 64)
	{
		memcpy(scrBitmap + scrOffset, source + sourceOffset, 2048);
		sourceOffset += 2048;
		scrOffset += 2048;
		remainingLines -= 64;
	}

	if (remainingLines != 0)
	{
		size_t rows = (size_t)remainingLines / 8u;
		size_t bytesToRead = rows * 32u;
		for (size_t block = 0; block < 8; block++)
		{
			memcpy(scrBitmap + scrOffset + block * 256u, source + sourceOffset, bytesToRead);
			sourceOffset += bytesToRead;
		}
	}

	for (uint32_t y = 0; y < numLines; y++)
		memcpy(destination + y * 32u, scrBitmap + GetSpectrumBitmapOffset(y), 32);
}

static bool LoadExternalZXSPicture(const char* fileName)
{
	File* file = File_Open(fileName, ReadOnly);
	if (file == 0)
	{
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
	}

	uint64_t size = File_GetSize(file);
	if (size < 2 || size > 65535)
	{
		DDB_SetError(DDB_ERROR_INVALID_FILE);
		File_Close(file);
		return false;
	}

	uint8_t* compressed = Allocate<uint8_t>("ZXS picture", (size_t)size);
	if (compressed == NULL)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		File_Close(file);
		return false;
	}

	if (File_Read(file, compressed, size) != size)
	{
		DDB_SetError(DDB_ERROR_READING_FILE);
		File_Close(file);
		Free(compressed);
		return false;
	}
	File_Close(file);

	if (zxsPictureBitmap == NULL)
		zxsPictureBitmap = Allocate<uint8_t>("ZXS picture bitmap", 32 * 192);
	if (zxsPictureAttributes == NULL)
		zxsPictureAttributes = Allocate<uint8_t>("ZXS picture attributes", 32 * 24);
	if (zxsPictureBitmap == NULL || zxsPictureAttributes == NULL)
	{
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		Free(compressed);
		return false;
	}

	memset(zxsPictureBitmap, 0, 32 * 192);
	memset(zxsPictureAttributes, 0, 32 * 24);

	uint8_t legacyNumLines = compressed[0];
	if ((legacyNumLines & 7) == 0 && legacyNumLines <= 192)
	{
		size_t legacyBitmapBytes = 0;
		uint8_t remainingLines = legacyNumLines;
		if (remainingLines >= 64)
		{
			legacyBitmapBytes += 2048;
			remainingLines -= 64;
		}
		if (remainingLines >= 64)
		{
			legacyBitmapBytes += 2048;
			remainingLines -= 64;
		}
		if (remainingLines >= 64)
		{
			legacyBitmapBytes += 2048;
			remainingLines -= 64;
		}
		if (remainingLines != 0)
			legacyBitmapBytes += ((size_t)remainingLines / 8u) * 32u * 8u;

		size_t legacyAttributeBytes = ((size_t)legacyNumLines / 8u) * 32u;
		size_t legacySize = 1 + legacyBitmapBytes + legacyAttributeBytes;
		if (legacySize <= size && HasOnlyPaddingBytes(compressed, legacySize, (size_t)size))
		{
			DecodeLegacyZXSBitmap(compressed + 1, legacyNumLines, zxsPictureBitmap);
			memcpy(zxsPictureAttributes, compressed + 1 + legacyBitmapBytes, legacyAttributeBytes);
			zxsPictureWidth = 256;
			zxsPictureHeight = legacyNumLines;
			zxsPictureMirror = false;
			Free(compressed);
			return true;
		}
	}

	if (size >= 6)
	{
		uint16_t bitmapSize = (uint16_t)(compressed[0] | (compressed[1] << 8));
		uint16_t attributeSize = (uint16_t)(compressed[2] | (compressed[3] << 8));
		uint8_t numLinesBitmap = compressed[4];
		uint8_t numLinesAttributes = compressed[5] & 0x7F;
		bool mirror = (compressed[5] & 0x80) != 0;

		uint32_t expectedBitmapBytes = (uint32_t)numLinesBitmap * 32;
		uint32_t expectedAttributeBytes = (uint32_t)numLinesAttributes * 32;
		if (numLinesBitmap <= 192 && numLinesAttributes <= 24 &&
			numLinesAttributes >= ((numLinesBitmap + 7) >> 3) &&
			6u + bitmapSize + attributeSize == size &&
			expectedBitmapBytes <= 6144 && expectedAttributeBytes <= 768)
		{
			uint8_t* bitmapRows = Allocate<uint8_t>("ZXS bitmap rows", expectedBitmapBytes);
			uint8_t* attributeRows = Allocate<uint8_t>("ZXS attribute rows", expectedAttributeBytes);
			if ((expectedBitmapBytes != 0 && bitmapRows == NULL) || (expectedAttributeBytes != 0 && attributeRows == NULL))
			{
				DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
				Free(bitmapRows);
				Free(attributeRows);
				Free(compressed);
				return false;
			}

			bool ok = DMG_DecompressZX0(compressed + 6, bitmapSize, bitmapRows, expectedBitmapBytes) &&
				DMG_DecompressZX0(compressed + 6 + bitmapSize, attributeSize, attributeRows, expectedAttributeBytes);
			if (!ok)
			{
				DDB_SetError(DDB_ERROR_INVALID_FILE);
				Free(bitmapRows);
				Free(attributeRows);
				Free(compressed);
				return false;
			}

			for (uint32_t y = 0; y < numLinesBitmap; y++)
			{
				uint8_t* dst = zxsPictureBitmap + y * 32;
				uint8_t* src = bitmapRows + y * 32;
				if (mirror)
				{
					memcpy(dst, src, 16);
					for (int x = 0; x < 16; x++)
						dst[16 + x] = ReverseBits(src[15 - x]);
				}
				else
				{
					memcpy(dst, src, 32);
				}
			}
			for (uint32_t y = 0; y < numLinesAttributes; y++)
			{
				uint8_t* dst = zxsPictureAttributes + y * 32;
				uint8_t* src = attributeRows + y * 32;
				if (mirror)
				{
					memcpy(dst, src, 16);
					for (int x = 0; x < 16; x++)
						dst[16 + x] = src[15 - x];
				}
				else
				{
					memcpy(dst, src, 32);
				}
			}

			Free(bitmapRows);
			Free(attributeRows);
			zxsPictureWidth = 256;
			zxsPictureHeight = numLinesBitmap;
			zxsPictureMirror = mirror;
			Free(compressed);
			return true;
		}
	}

	DDB_SetError(DDB_ERROR_INVALID_FILE);
	Free(compressed);
	return false;
}
#endif

static bool HasExternalPCXGraphics(const char* fileName)
{
	char introScreen[FILE_MAX_PATH];
	if (FindNamedPCXFile(fileName, introScreen, sizeof(introScreen)))
		return true;

	for (int picno = 0; picno < 256; picno++)
	{
		char pictureFileName[FILE_MAX_PATH];
		if (VID_GetExternalPictureFileName((uint8_t)picno, pictureFileName, sizeof(pictureFileName)))
			return true;
	}

	return false;
}

static bool IsPCXScreenFile(const char* fileName)
{
	const char* dot = StrRChr(fileName, '.');
	return dot != 0 && (StrIComp(dot, ".vga") == 0 || StrIComp(dot, ".pcx") == 0);
}
#endif

bool VID_IsBackBufferEnabled()
{
	return true;
}

void VID_EnableBackBuffer()
{
	// Always enabled
}

void VID_Clear (int x, int y, int w, int h, uint8_t color, VID_ClearMode mode)
{
	// DebugPrintf("Clear %d,%d to %d,%d %d\n", x, y, x+w, y+h, color);

	if (mode == Clear_All && attributes && x <= 0 && y <= 0 && w >= screenWidth && h >= screenHeight)
	{
		memset(bitmap, 0, stride * screenHeight);
		memset(attributes, screenMachine == DDB_MACHINE_SPECTRUM ? 0x00 : (color << 4), stride * (screenHeight >> 3));
		if (frontBuffer)
			memset(frontBuffer, color, screenWidth * screenHeight);
		if (backBuffer)
			memset(backBuffer, color, screenWidth * screenHeight);
		return;
	}

	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y >= screenHeight)
		return;
	if (x >= screenWidth)
		return;
	if (x + w > screenWidth)
		w = screenWidth - x;
	if (y + h > screenHeight)
		h = screenHeight - y;
	if (w <= 0)
		return;
	if (h <= 0)
		return;

	if (attributes)
	{
		uint8_t maskLeft = 0xFF00 >> (x & 7);
		uint8_t maskRight = (0x00FF << ((x + w) & 7)) >> 8;
		w = ((x + w - 1) >> 3) - (x >> 3);
		for (int dy = 0; dy < h; dy++)
		{
			uint8_t* ptr = bitmap + (y + dy) * stride + (x >> 3);
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

#		if HAS_DRAWSTRING
		uint8_t* attr = attributes + (y >> 3) * stride + (x >> 3);
		uint8_t  attrValue = VID_GetAttributes();
		VID_SetPaper(color);
		uint8_t  attrSet = VID_GetAttributes();
		for (int dy = 0; dy < h; dy += 8)
		{
			for (int dx = 0; dx <= w; dx++)
				attr[dx] = attrSet;
			attr += stride;
		}
		VID_SetAttributes(attrValue);
#		endif
	}
	else
	{
		for (int dy = 0 ; dy < h; dy++)
		{
			uint8_t* ptr = textBuffer + (y + dy) * screenWidth + x;
			for (int dx = 0; dx < w; dx++)
				ptr[dx] = color;
		}
	}
}

void VID_Scroll (int x, int y, int w, int h, int lines, uint8_t paper)
{
	int dy = 0;

	if (lines < h)
	{
		if (attributes)
		{
			w >>= 3;
			for (; dy < h - lines; dy++)
			{
				uint8_t *ptr = bitmap + (y + dy) * stride + (x >> 3);
				uint8_t *next = ptr + lines * stride;

				for (int dx = 0; dx < w; dx++)
					ptr[dx] = next[dx];
			}

			if (lines >= 8 && w >= 1)
			{
				int row0 = y >> 3;
				int row1 = (y + h - lines) >> 3;
				int rows = h >> 3;
				int cols = w;
				int col0 = x >> 3;
				int inc  = stride * (lines >> 3);
				int attv = (paper << 3);
				for (int row = row0; row < row1; row++)
				{
					uint8_t* attr = attributes + row * stride + col0;
					for (int col = 0; col < cols; col++)
						attr[col] = attr[col + inc];
				}
				rows = lines >> 3;
				for (int row = row1; row < row1 + rows; row++)
				{
					uint8_t* attr = attributes + row * stride + col0;
					for (int col = 0; col < cols; col++)
						attr[col] = attv;
				}
			}
		}
		else
		{
			for (; dy < h - lines; dy++)
			{
				uint8_t *ptr = textBuffer + (y + dy) * screenWidth + x;
				uint8_t *next = ptr + lines * screenWidth;

				for (int dx = 0; dx < w; dx++)
					ptr[dx] = next[dx];
			}
		}
	}

	VID_Clear(x, y + dy, w, lines, paper);
}

void VID_ClearBuffer (bool front)
{
	uint8_t* buffer = front ? frontBuffer : backBuffer;
	if (buffer == NULL)
		return;
	memset(buffer, 0, screenWidth * screenHeight);
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
	uint8_t  width = charWidth[ch];

#if HAS_DRAWSTRING
	if (ink != 255)
		VID_SetInk(ink);
	if (paper != 255)
		VID_SetPaper(paper);
#endif

	if (attributes)
	{
		uint8_t* ptr = charset + (ch << 3);
		uint8_t* out = bitmap + y * stride + (x >> 3);
		uint8_t  rot = x & 7;
		uint8_t* attr = attributes + (y >> 3) * stride + (x >> 3);
		uint8_t xattr = 0;
		uint8_t paperShift = 4;

		// TODO: Use DRAWSTRING routines to handle attributes,
		// attributes shouldn't be supported without DRAWSTRING

		if (screenMachine == DDB_MACHINE_SPECTRUM)
		{
			xattr = (ink & 0x18) << 3;
			ink &= 7;
			if (paper != 255)
				paper &= 7;
			paperShift = 3;
		}

		if (paper == 255)
		{
			*attr = (*attr & 0x37) | ink | xattr;
			if (rot > 8-width)
				attr[1] = (attr[1] & 0x37) | ink | xattr;
		}
		else
		{
			paper &= 7;
			*attr = ink | (paper << paperShift) | xattr;
			if (rot > 8-width)
				attr[1] = *attr;
		}

		for (int line = 0; line < 8; line++)
		{
			uint8_t* sav = out;
			uint8_t mask = 0x80 >> rot;
			for (int col = 0; col < width; col++)
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
			out = sav + stride;
		}
		return;
	}

	uint8_t* ptr = charset + (ch << 3);
	uint8_t* pixels = textBuffer + y * screenWidth + x;
	if (screen2XMode)
	{
		if (charset16Available)
		{
			const uint8_t* ptr16 = charset16 + ch * 32;
			if (paper == 255)
			{
				for (int line = 0; line < 16; line++)
				{
					uint16_t bits = (uint16_t)(ptr16[line * 2] << 8) | ptr16[line * 2 + 1];
					uint8_t* row = pixels + line * screenWidth;
					for (int col = 0; col < width; col++)
						if ((bits & (0x8000 >> col)) != 0)
							row[col] = ink;
				}
			}
			else
			{
				for (int line = 0; line < 16; line++)
				{
					uint16_t bits = (uint16_t)(ptr16[line * 2] << 8) | ptr16[line * 2 + 1];
					uint8_t* row = pixels + line * screenWidth;
					for (int col = 0; col < width; col++)
						row[col] = (bits & (0x8000 >> col)) ? ink : paper;
				}
			}
			return;
		}

		uint8_t sourceWidth = (uint8_t)((width + 1) >> 1);
		if (paper == 255)
		{
			for (int line = 0; line < 8; line++)
			{
				uint8_t* row0 = pixels + (line * 2) * screenWidth;
				uint8_t* row1 = row0 + screenWidth;
				for (int col = 0; col < sourceWidth; col++)
				{
					if ((ptr[line] & (0x80 >> col)) == 0)
						continue;
					int dx = col * 2;
					row0[dx] = ink;
					row0[dx + 1] = ink;
					row1[dx] = ink;
					row1[dx + 1] = ink;
				}
			}
		}
		else
		{
			for (int line = 0; line < 8; line++)
			{
				uint8_t* row0 = pixels + (line * 2) * screenWidth;
				uint8_t* row1 = row0 + screenWidth;
				for (int col = 0; col < sourceWidth; col++)
				{
					uint8_t color = (ptr[line] & (0x80 >> col)) ? ink : paper;
					int dx = col * 2;
					row0[dx] = color;
					row0[dx + 1] = color;
					row1[dx] = color;
					row1[dx + 1] = color;
				}
			}
		}
		return;
	}

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
}

void VID_GetPaletteColor (uint8_t color, uint8_t* r, uint8_t* g, uint8_t* b)
{
	uint32_t c = palette[color];
	*r = (c >> 16) & 0xFF;
	*g = (c >> 8) & 0xFF;
	*b = (c >> 0) & 0xFF;
}

uint16_t VID_GetPaletteSize()
{
	return 256;
}

void VID_SetPaletteColor (uint8_t color, uint8_t r, uint8_t g, uint8_t b)
{
	palette[color] = (r << 16) | (g << 8) | b;
}

bool VID_DisplaySCRFile (const char* fileName, DDB_Machine target, bool fadeIn)
{
	#if HAS_PCX
	if (target == DDB_MACHINE_IBMPC && IsPCXScreenFile(fileName))
	{
		uint32_t bufferSize = (uint32_t)screenWidth * (uint32_t)screenHeight;
		uint8_t* output = Allocate<uint8_t>("PCX screen", bufferSize);
		if (output == 0)
		{
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			return false;
		}

		int width = 0;
		int height = 0;
		if (!DMG_DecompressPCX(fileName, output, &bufferSize, &width, &height, palette))
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			Free(output);
			return false;
		}

		memset(graphicsBuffer, 0, screenWidth * screenHeight);
		int copyWidth = width > screenWidth ? screenWidth : width;
		int copyHeight = height > screenHeight ? screenHeight : height;
		for (int y = 0; y < copyHeight; y++)
			memcpy(graphicsBuffer + y * screenWidth, output + y * width, copyWidth);

		if (fadeIn)
			VID_ActivatePalette();
		Free(output);
		return true;
	}
	#endif

	#if HAS_SPECTRUM
	if (target == DDB_MACHINE_SPECTRUM && bitmap != NULL && attributes != NULL)
	{
		File* file = File_Open(fileName, ReadOnly);
		if (file == 0)
		{
			DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
			return false;
		}

		uint64_t size = File_GetSize(file);
		if (size < 6912 || size > 7040)
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			File_Close(file);
			return false;
		}

		uint8_t* buffer = Allocate<uint8_t>("SCR Temporary buffer", 7040);
		if (buffer == 0)
		{
			DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
			File_Close(file);
			return false;
		}

		if (File_Read(file, buffer, size) != size)
		{
			DDB_SetError(DDB_ERROR_READING_FILE);
			File_Close(file);
			Free(buffer);
			return false;
		}
		File_Close(file);

		while (size > 6912 && (buffer[size - 1] == 0x00 || buffer[size - 1] == 0xE5))
			size--;
		if (size != 6912)
		{
			DDB_SetError(DDB_ERROR_INVALID_FILE);
			Free(buffer);
			return false;
		}

		memset(bitmap, 0, 32 * 192);
		for (int y = 0; y < 192; y++)
		{
			uint32_t sourceOffset = ((uint32_t)(y & 0xC0) << 5) |
			                      ((uint32_t)(y & 0x07) << 8) |
			                      ((uint32_t)(y & 0x38) << 2);
			memcpy(bitmap + y * stride, buffer + sourceOffset, 32);
		}
		memcpy(attributes, buffer + 6144, 32 * 24);
		memcpy(palette, ZXSpectrumPalette, sizeof(ZXSpectrumPalette));

		if (fadeIn)
			VID_ActivatePalette();

		Free(buffer);
		return true;
	}
	#endif

	uint8_t* buffer = Allocate<uint8_t>("SCR Temporary buffer", 32768);
	bool ok = SCR_GetScreen(fileName, target, buffer, 32768,
		graphicsBuffer, screenWidth, screenHeight, palette);
	Free(buffer);
	return ok;
}

void VID_LoadPicture (uint8_t picno, DDB_ScreenMode mode)
{
	#if HAS_PCX
	FreeBufferedPCXPicture();
	bufferedEntry = NULL;
	pictureData = NULL;
	bufferedIndex = picno;
	(void)mode;

	if (dmg == NULL)
	{
		char pictureFileName[FILE_MAX_PATH];
		if (!VID_GetExternalPictureFileName(picno, pictureFileName, sizeof(pictureFileName)))
			return;

		#if HAS_SPECTRUM
		const char* dot = StrRChr(pictureFileName, '.');
		if (dot != NULL && StrIComp(dot, ".zxs") == 0)
		{
			LoadExternalZXSPicture(pictureFileName);
			return;
		}
		#endif

		pcxPictureSize = (uint32_t)DMG_MAX_IMAGE_WIDTH * (uint32_t)DMG_MAX_IMAGE_HEIGHT;
		pcxPictureData = Allocate<uint8_t>("PCX picture", pcxPictureSize);
		if (pcxPictureData == NULL)
			return;

		if (!DMG_DecompressPCX(pictureFileName, pcxPictureData, &pcxPictureSize, &pcxPictureWidth, &pcxPictureHeight, pcxPalette))
			FreeBufferedPCXPicture();
		return;
	}
	#else
	if (dmg == NULL) return;
	#endif

	DMG_Entry* entry = DMG_GetEntry(dmg, picno);
	if (entry == NULL || entry->type != DMGEntry_Image)
		return;

	bufferedEntry = entry;
	bufferedIndex = picno;
	if (dmg->version == DMG_Version1_PCW)
		pictureData = DMG_GetEntryDataChunky(dmg, picno);
	else
		pictureData = DMG_GetEntryData(dmg, picno,
			dmg->version == DMG_Version5 ? ImageMode_Indexed : ImageMode_Packed);
	if (pictureData == 0)
	{
		bufferedEntry = NULL;
	}
}

void VID_GetPictureInfo (bool* fixed, int16_t* x, int16_t* y, int16_t* w, int16_t* h)
{
	#if HAS_PCX
	#if HAS_SPECTRUM
	if (zxsPictureBitmap != NULL && zxsPictureAttributes != NULL)
	{
		if (fixed != NULL)
			*fixed = true;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = zxsPictureWidth;
		if (h != NULL)
			*h = zxsPictureHeight;
		return;
	}
	#endif

	if (pcxPictureData != NULL)
	{
		if (fixed != NULL)
			*fixed = true;
		if (x != NULL)
			*x = 0;
		if (y != NULL)
			*y = 0;
		if (w != NULL)
			*w = pcxPictureWidth;
		if (h != NULL)
			*h = pcxPictureHeight;
		return;
	}
	#endif

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
			*fixed = (bufferedEntry->flags & DMG_FLAG_FIXED) != 0;
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
	#if HAS_PCX
	#if HAS_SPECTRUM
	if (zxsPictureBitmap != NULL && zxsPictureAttributes != NULL && bitmap != NULL && attributes != NULL)
	{
		(void)x;
		(void)y;
		(void)w;
		(void)h;
		(void)mode;
		VID_Clear(0, 0, screenWidth, screenHeight, 0, Clear_All);
		memcpy(bitmap, zxsPictureBitmap, 32 * 192);
		memcpy(attributes, zxsPictureAttributes, 32 * 24);
		memcpy(palette, ZXSpectrumPalette, sizeof(ZXSpectrumPalette));
		VID_ActivatePalette();
		return;
	}
	#endif

	if (pcxPictureData != NULL)
	{
		int srcX = 0;
		int srcY = 0;

		if (x < 0)
		{
			srcX = -x;
			w += x;
			x = 0;
		}
		if (y < 0)
		{
			srcY = -y;
			h += y;
			y = 0;
		}
		if (x >= screenWidth || y >= screenHeight || w <= 0 || h <= 0)
			return;

		if (w > pcxPictureWidth - srcX)
			w = pcxPictureWidth - srcX;
		if (h > pcxPictureHeight - srcY)
			h = pcxPictureHeight - srcY;
		if (w > screenWidth - x)
			w = screenWidth - x;
		if (h > screenHeight - y)
			h = screenHeight - y;
		if (w <= 0 || h <= 0)
			return;

		uint8_t* srcPtr = pcxPictureData + srcY * pcxPictureWidth + srcX;
		uint8_t* dstPtr = graphicsBuffer + y * screenWidth + x;
		for (int dy = 0; dy < h; dy++, srcPtr += pcxPictureWidth, dstPtr += screenWidth)
			memcpy(dstPtr, srcPtr, w);

		memcpy(palette, pcxPalette, sizeof(pcxPalette));
		VID_ActivatePalette();
		return;
	}
	#endif

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
	uint32_t* filePalette = (uint32_t*)DMG_GetEntryPalette(dmg, bufferedIndex);
    if (dmg->version == DMG_Version5)
    {
    	for (int dy = 0; dy < h; dy++, srcPtr += entry->width, dstPtr += screenWidth)
            memcpy(dstPtr, srcPtr, w);
    }
    else
    {
    	for (int dy = 0; dy < h; dy++, srcPtr += entry->width/2, dstPtr += screenWidth)
    	{
    		for (int dx = 0, ix = 0; dx < w; dx += 2, ix++)
    		{
    			dstPtr[dx] = srcPtr[ix] >> 4;
    			dstPtr[dx+1] = srcPtr[ix] & 0x0F;
    		}
    	}
    }

	switch (mode)
	{
		default:
		case ScreenMode_VGA16:
			if (entry->flags & DMG_FLAG_FIXED)
			{
				if (dmg->version == DMG_Version1)
					filePalette[15] = 0xFFFFFFFF;
                int paletteCount = DMG_GetEntryPaletteSize(dmg, bufferedIndex);
                int firstColor = DMG_GetEntryFirstColor(dmg, bufferedIndex);
				if (!PaletteMatches(filePalette, paletteCount, firstColor, firstColor == 0))
                {
					if (firstColor == 0)
					{
						for (int n = 0; n < 256; n++)
							palette[n] = 0xFF000000;
					}
					for (int n = 0; n < paletteCount && firstColor + n < 256; n++)
						palette[firstColor + n] = filePalette[n];
                }
			}
			break;

		case ScreenMode_EGA:
			break;

		case ScreenMode_CGA:
			if (entry->flags & DMG_FLAG_FIXED)
			{
				uint32_t* cgaPalette = DMG_GetCGAMode(entry) == CGA_Red ? CGAPaletteRed : CGAPaletteCyan;
				if (!PaletteMatches(cgaPalette, 16, 0, false))
				{
					for (int n = 0; n < 16; n++)
						palette[n] = cgaPalette[n];
				}
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
	if (videoInitialized)
	{
		DebugPrintf("Closing video subsystem\n");
		videoInitialized = false;
	}
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
	#if HAS_PCX
	FreeBufferedPCXPicture();
	#endif
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
			DumpMemory(OSGetFree(), 0);
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
	uint8_t cols = screenWidth / 8;
	uint8_t rows = screenHeight / 8;
	bool spectrum = (screenMachine == DDB_MACHINE_SPECTRUM);
	for (int y = 0; y < rows; y++)
	{
		uint8_t* ptr = bitmap + stride * (y * 8);
		uint8_t* out = frontBuffer + y * 8 * screenWidth;

		for (int x = 0; x < cols; x++, ptr++)
		{
			uint8_t attr = *attrPtr++;
			uint8_t ink, paper;
			if (spectrum)
			{
				ink = (attr & 0x07);
				paper = ((attr >> 3) & 0x07);
				if (attr & 0x40)
				{
					// Bright On
					ink |= 0x8;
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
			}
			else
			{
				ink = (attr & 0x0F);
				paper = ((attr >> 4) & 0x0F);
			}

			uint8_t* outPtr = out;

			for (int cy = 0; cy < 8; cy++)
			{
				uint8_t pixels = ptr[cy * stride];

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

	if (bitmap)
	{
		uint16_t length = screenWidth * screenHeight / 64;
		uint8_t* grid = Allocate<uint8_t>("Temp Debug Grid", length);
		uint8_t  color0 = 6 << 3;
		uint8_t  color1 = 7 << 3;
		if (screenMachine == DDB_MACHINE_C64)
		{
			color0 = 1;
			color1 = 3;
		}
		for (int n = 0; n < length; n++)
		{
			int y = n / stride;
			int x = n % stride;
			int checkboard = ((x & 1) ^ (y & 1));
			grid[n] = checkboard ? color1: color0;
		}
		RenderSpectrumScreen(attributes);
		Free(grid);
	}

	char name[64];
	snprintf(name, 63, "debug%04d.bmp", n++);
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
		minBorderSize = GetMinimumBorderSize();

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

		uint32_t clearColor = PackSurfaceRGB(surface->format, (palette[0] >> 16) & 0xFF, (palette[0] >> 8) & 0xFF, palette[0] & 0xFF);
	SDL_FillRect(surface, NULL, clearColor);

	if (SDL_LockSurface(surface) != 0)
	{
		DebugPrintf("Unable to lock window %p\n", window);
		DebugPrintf("SDL_LockSurface(surface): %s\n", SDL_GetError());
	}
	else
	{
		if (attributes)
			RenderSpectrumScreen(attributes);

		int srcWidth = screenWidth;
		int srcHeight = screenHeight;
		int displayWidth, displayHeight;
		GetDisplayAspect(&displayWidth, &displayHeight);

		int scale = 1;
		int exactScaleX = surface->w / displayWidth;
		int exactScaleY = surface->h / displayHeight;
		if (surface->w % displayWidth == 0 && surface->h % displayHeight == 0 && exactScaleX == exactScaleY)
		{
			scale = exactScaleX;
		}
		else
		{
			while (scale < (surface->w-minBorderSize) / displayWidth && scale < (surface->h-minBorderSize) / displayHeight)
				scale++;
		}

		int scaleX = scale;
		int scaleY = displayHeight * scale / srcHeight;

		int dstX = (surface->w - srcWidth * scaleX) / 2;
		int dstY = (surface->h - srcHeight * scaleY) / 2;
		int dstW = srcWidth * scaleX;
		int dstH = srcHeight * scaleY;

		static int lastSurfaceW = -1;
		static int lastSurfaceH = -1;
		static int lastDstX = -1;
		static int lastDstY = -1;
		static int lastDstW = -1;
		static int lastDstH = -1;
		if (surface->w != lastSurfaceW || surface->h != lastSurfaceH ||
			dstX != lastDstX || dstY != lastDstY ||
			dstW != lastDstW || dstH != lastDstH)
		{
			lastSurfaceW = surface->w;
			lastSurfaceH = surface->h;
			lastDstX = dstX;
			lastDstY = dstY;
			lastDstW = dstW;
			lastDstH = dstH;
		}

		uint32_t surfacePalette[256];
		for (int c = 0; c < 256; c++)
		{
			uint8_t r = palette[c] >> 16;
			uint8_t g = palette[c] >> 8;
			uint8_t b = palette[c];
			surfacePalette[c] = PackSurfaceRGB(surface->format, r, g, b);
		}

		uint8_t* srcPtr = frontBuffer;
		uint32_t* dstPtr = (uint32_t*)surface->pixels + dstY * surface->pitch / 4 + dstX;
		for (int dy = 0; dy < srcHeight; dy++, srcPtr += screenWidth)
		{
			uint32_t* dst = dstPtr;
			for (int dx = 0; dx < srcWidth; dx++)
			{
				uint32_t pixel = surfacePalette[srcPtr[dx]];
				for (int n = 0; n < scaleX; n++)
					*dst++ = pixel;
			}
			dstPtr += surface->pitch / 4;
			for (int n = 1; n < scaleY; n++)
			{
				memcpy(dstPtr, dstPtr - surface->pitch / 4, srcWidth * 4 * scaleX);
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
		case DMG_5KHZ:     inputHz =  5000; break;
		case DMG_7KHZ:     inputHz =  7000; break;
		case DMG_9_5KHZ:   inputHz =  9500; break;
		case DMG_15KHZ:    inputHz = 15000; break;
		case DMG_20KHZ:    inputHz = 20000; break;
		case DMG_30KHZ:    inputHz = 30000; break;
        case DMG_44_1KHZ:  inputHz = 44100; break;
        case DMG_48KHZ:    inputHz = 48000; break;
		default:           inputHz = 11025; break;
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
	#if HAS_XMSG
	DDB_InitializeXMessageCache(65536);
	#endif

	#if HAS_PCX
	FreeBufferedPCXPicture();
	VID_SetExternalPictureBase(0);
	#endif
	charset16Available = false;
	for (int n = 0; n < 256; n++)
		charWidth[n] = defaultCharWidth;
	memcpy(charset, DefaultCharset, 1024);
	memcpy(charset + 1024, DefaultCharset, 1024);
	charsetInitialized = true;

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
		#if HAS_PCX
		VID_SetExternalPictureBase(fileName);
		if (!HasExternalPCXGraphics(fileName))
		{
			VID_SetExternalPictureBase(0);
			DebugPrintf("Data file for %s not found!\n", fileName);
			DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
			return false;
		}

		if (!screen2XMode)
		{
			screenMode = ScreenMode_VGA;
			lineHeight = 8;
			columnWidth = 8;
			screenCellWidth = 8;
		}
		#else
		DebugPrintf("Data file for %s not found!\n", fileName);
		DDB_SetError(DDB_ERROR_FILE_NOT_FOUND);
		return false;
		#endif
	}
	else if (screenMachine == DDB_MACHINE_IBMPC)
	{
		ApplyIBMPCTextMetrics(
			dmg->version == DMG_Version5 &&
			dmg->targetWidth == 640 &&
			dmg->targetHeight == 400 &&
			(dmg->dat5Flags & DMG_DAT5_FLAG_2X) != 0);
	}

	bool fontLoaded = false;
	fontLoaded = LoadSINTACFont(ChangeExtension(fileName, ".FNT")) ||
		LoadSINTACFont(ChangeExtension(fileName, ".fnt"));

	if (!fontLoaded &&
		!LoadCharset(charset, ChangeExtension(fileName, ".CH0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".ch0")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".CHR")) &&
		!LoadCharset(charset, ChangeExtension(fileName, ".chr")))
	{
		// Keep the default fixed-width charset restored above.
	}
	charsetInitialized = true;

	if (dmg != NULL && screenMachine == DDB_MACHINE_IBMPC)
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

	#if HAS_PSG
	if ((screenMachine == DDB_MACHINE_ATARIST || screenMachine == DDB_MACHINE_AMIGA) && !DDB_InitializePSGPlayback())
	{
		if (dmg != NULL)
		{
			DMG_Close(dmg);
			dmg = NULL;
		}
		DDB_SetError(DDB_ERROR_OUT_OF_MEMORY);
		return false;
	}
	#endif

	// Uncomment the following lines to test Atari/Amiga caches in desktop/web:
	// DMG_SetupFileCache(dmg);
	// DMG_SetupImageCache(dmg, 32768);

	return true;
}

bool VID_Initialize (DDB_Machine machine, DDB_Version version, DDB_ScreenMode mode)
{
	if (!videoInitialized)
		DebugPrintf("Initializing video subsystem for %s\n", DDB_DescribeMachine(machine));
	else
		DebugPrintf("Reinitializing video subsystem for %s\n", DDB_DescribeMachine(machine));
	videoInitialized = true;

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	VID_InitAudio();
	SDL_StopTextInput();

	lineHeight  = 8;
	columnWidth = (version == DDB_VERSION_PAWS || machine == DDB_MACHINE_PCW) ? 8 : 6;
	defaultCharWidth = columnWidth;
	screenCellWidth = 8;
	screenMode = mode;
	screenVersion = version;
	xCoordMultiplier = 1;
	yCoordMultiplier = 1;
	screen2XMode = false;
	charset16Available = false;
	for (int n = 0; n < 256; n++)
		charWidth[n] = columnWidth;

	switch (machine)
	{
		case DDB_MACHINE_MSX:
			memcpy(DefaultPalette, MSXPalette, sizeof(MSXPalette));
			screenMachine = machine;
			screenWidth   = 256;
			screenHeight  = 192;
			bitmap        = Allocate<uint8_t>("MSX Screen Data", 256 * 192 / 8);
			attributes    = Allocate<uint8_t>("MSX Attributes", 32 * 24);
			stride        = 32;
			break;
		case DDB_MACHINE_SPECTRUM:
			memcpy(DefaultPalette, ZXSpectrumPalette, sizeof(ZXSpectrumPalette));
			screenMachine = machine;
			screenWidth   = 256;
			screenHeight  = 192;
			bitmap        = Allocate<uint8_t>("Spectrum Screen Data", 256 * 192 / 8);
			attributes    = Allocate<uint8_t>("Spectrum Attributes", 32 * 24);
			stride        = 32;
			break;
		case DDB_MACHINE_CPC:
			memcpy(DefaultPalette, CPCPalette, sizeof(CPCPalette));
			screenMachine = machine;
			screenWidth   = 320;
			screenHeight  = 200;
			columnWidth   = 8;
			defaultCharWidth = 8;
			for (int n = 0; n < 256; n++)
				charWidth[n] = 8;
			break;
		case DDB_MACHINE_C64:
			memcpy(DefaultPalette, Commodore64Palette, sizeof(Commodore64Palette));
			screenMachine = machine;
			screenWidth   = 320;
			screenHeight  = 200;
			bitmap        = Allocate<uint8_t>("C64 Screen Data", 320 * 200 / 8);
			attributes    = Allocate<uint8_t>("C64 Attributes", 40 * 25);
			stride        = 40;
			columnWidth   = 8;
			defaultCharWidth = 8;
			for (int n = 0; n < 256; n++)
				charWidth[n] = 8;
			break;
		case DDB_MACHINE_PCW:
			screenMachine = machine;
			screenWidth   = 720;
			screenHeight  = 256;
			stride        = 40;
			columnWidth   = 8;
			defaultCharWidth = 8;
			memcpy(DefaultPalette, PCWDefaultPalette, sizeof(PCWDefaultPalette));
			for (int n = 0; n < 256; n++)
				charWidth[n] = 8;
			break;
		default:
			memcpy(DefaultPalette, EGAPalette, sizeof(EGAPalette));
			screenMachine = DDB_MACHINE_IBMPC;
			switch (mode)
            {
                case ScreenMode_HiRes:
                    screenWidth  = 640;
                    screenHeight = 200;
                    xCoordMultiplier = 2;
                    break;
                case ScreenMode_SHiRes:
                    screenWidth  = 640;
                    screenHeight = 400;
                    xCoordMultiplier = 2;
                    yCoordMultiplier = 2;
                    break;
                default:
    	    		screenWidth  = 320;
	    	    	screenHeight = 200;
                    break;
            }
			break;
	}

	for (int n = 0; n < 256; n++)
		charWidth[n] = defaultCharWidth;

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

	int displayWidth, displayHeight;
	GetDisplayAspect(&displayWidth, &displayHeight);
	int scale = GetInitialWindowScale();
#if _WEB
	int options = SDL_WINDOW_SHOWN;
#else
	int options = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#endif
	window = SDL_CreateWindow("ADP " VERSION_STR,
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		displayWidth * scale + InitialWindowBorderWidth,
		displayHeight * scale + InitialWindowBorderHeight,
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
