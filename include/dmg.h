#pragma once

/* ───────────────────────────────────────────────────────────────────────── */
/*  DMG - .DAT Graphic file manager for DAAD                                 */
/* ───────────────────────────────────────────────────────────────────────── */

#include <os_file.h>

#define DMG_MAX_IMAGE_WIDTH		999
#define DMG_MAX_IMAGE_HEIGHT	999
#define DMG_CACHE_BLOCKS	    128

enum DMG_Error
{
	DMG_ERROR_NONE,
	DMG_ERROR_FILE_NOT_FOUND,
	DMG_ERROR_CREATING_FILE,
	DMG_ERROR_UNKNOWN_SIGNATURE,
	DMG_ERROR_READING_FILE,
	DMG_ERROR_SEEKING_FILE,
	DMG_ERROR_WRITING_FILE,
	DMG_ERROR_INVALID_ENTRY_COUNT,
	DMG_ERROR_FILE_TOO_SMALL,
	DMG_ERROR_FILE_TOO_BIG,
	DMG_ERROR_TRUNCATED_DATA_STREAM,
	DMG_ERROR_DATA_STREAM_TOO_LONG,
	DMG_ERROR_DATA_OFFSET_OUT_OF_BOUNDS,
	DMG_ERROR_CORRUPTED_DATA_STREAM,
	DMG_ERROR_IMAGE_TOO_BIG,
	DMG_ERROR_BUFFER_TOO_SMALL,
	DMG_ERROR_ENTRY_IS_EMPTY,
	DMG_ERROR_OUT_OF_MEMORY,
	DMG_ERROR_DECOMPRESSION_BUFFER_MISSING,
	DMG_ERROR_INVALID_IMAGE,
};

enum DMG_ImageFormat
{
	Image_ARGB32,			// True color, 4 byter per color, 0xAARRGBB (native endianess)
	Image_ARGB16,			// True color, 2 bytes per color, 0xARGB (native endianess)
	Image_Chunky256,		// Indexed, 1 color per byte
	Image_Chunky16,			// Indexed, 2 colors per byte
	Image_Chunky4,			// Indexed, 4 colors per byte (CGA)
	Image_Planar16Byte,		// 4 planes, 8 bits per pixel, successive planes
	Image_Planar16Word,		// 4 planes, 16 bits per pixel, successive planes (Atari ST)
	Image_Planar16Line,		// 4 planes, interleaved image lines
	
	Image_CompressedCGA,	// Old .CGA compression
	Image_CompressedEGA,	// Old .EGA compression
	Image_CompressedOldRLE,	// Old .DAT compression
	Image_CompressedNewRLE,	// New .DAT compression

	Image_Raw,				// As present in file (i.e. compressed bytestream)
};

enum DMG_ImageMode
{
	ImageMode_Packed	  = 0x00,		// packed 16 bits, 2 colors per byte
	ImageMode_PackedEGA   = 0x01,
	ImageMode_PackedCGA   = 0x02,
	ImageMode_RGBA32	  = 0x10,		// 4 bytes per pixel, little endian, 0xAARRGGBB format
	ImageMode_RGBA32EGA	  = 0x11,
	ImageMode_RGBA32CGA	  = 0x12,
	ImageMode_PlanarST    = 0x20,       // Atari ST format (P0P0P1P1P2P2P3P3...)
    ImageMode_Planar      = 0x24,       // One complete bitmap per plane 
	ImageMode_Indexed	  = 0x40,		// 1 byte per pixel, regardless of color depth
	ImageMode_IndexedEGA  = 0x41,
	ImageMode_IndexedCGA  = 0x42,
	ImageMode_Raw         = 0xFF,
	ImageMode_Audio       = 0xFF,
};

#define DMG_IS_INDEXED(mode) (((mode) & 0xF0) == 0x40)
#define DMG_IS_RGBA32(mode)  (((mode) & 0xF0) == 0x10)
#define DMG_IS_EGA(mode) 	 (((mode) & 0x0F) == 0x01)
#define DMG_IS_CGA(mode)     (((mode) & 0x0F) == 0x02)	

enum DMG_EntryType
{
	DMGEntry_Empty,
	DMGEntry_Image,
	DMGEntry_Audio,
};

typedef enum
{
	CGA_Blue,
	CGA_Red
}
DMG_CGAMode;

typedef enum
{
	DMG_Version1_CGA,
	DMG_Version1_EGA,
	DMG_Version1,
	DMG_Version2,
    DMG_Version5
}
DMG_Version;

typedef enum
{
	DMG_5KHZ 			= 0,
	DMG_7KHZ			= 1,
	DMG_9_5KHZ			= 2,
	DMG_15KHZ			= 3,
	DMG_20KHZ			= 4,
	DMG_30KHZ			= 5,
    DMG_44_1KHZ         = 6,        // Version 5+
    DMG_48KHZ           = 7,        // Version 5+
}
DMG_KHZ;

typedef enum
{
    DMG_FLAG_COMPRESSED    = 0x0001,
    DMG_FLAG_BUFFERED      = 0x0002,
    DMG_FLAG_FIXED         = 0x0004,
    DMG_FLAG_ZX0           = 0x0008,
    DMG_FLAG_PROCESSED     = 0x0010,
    DMG_FLAG_AMIPALHACK    = 0x0020,
    DMG_FLAG_CGAMODE       = 0x0040,
}
DMG_FLAGS;

struct DMG_Entry
{
	DMG_EntryType	type;

	uint32_t		RGB32Palette[16];
	uint8_t			CGAPalette[16];
	uint8_t			EGAPalette[16];

	uint8_t		    flags;
    uint8_t         bitDepth;       // Number of planes, only used if ImageMode_Planar
	int16_t			x;
	int16_t			y;
	uint16_t		width;
	uint16_t		height;
	uint8_t         firstColor;
	uint8_t         lastColor;

	uint32_t		fileOffset;
	uint32_t		length;
};

struct DMG_Slot
{
	uint8_t         firstEntry;
	uint8_t         usageCount;
	uint32_t        startOffset;
	uint32_t        length;
};

struct DMG_Cache
{
	DMG_Cache*  next;
	uint32_t    size;
	uint32_t    time;
	uint8_t     index;
	bool        buffer;
	bool        populated;
};

struct DMG
{
	bool		littleEndian;
	DMG_Version version;
	uint8_t		screenMode;

	DMG_Entry*  entries[256];

	DMG_Cache*  cache;
	DMG_Cache*  cacheTail;
	uint32_t    cacheSize;
	uint32_t    cacheFree;
	uint8_t     cacheBitmap[32];

	uint32_t    fileCacheBlockSize;
	uint8_t*    fileCacheBlocks[128];
	uint32_t    fileCacheOffset;

	File*		file;
	uint32_t    fileSize;
};

extern DMG* dmg;

DMG*		DMG_Open			   (const char* filename, bool readOnly);
DMG*		DMG_OpenFromFile	   (File* file);
DMG*        DMG_Create             (const char* filename);
DMG_Entry*	DMG_GetEntry		   (DMG* dmg, uint8_t index);
bool        DMG_UpdateEntry        (DMG* dmg, uint8_t index);
uint8_t*    DMG_GetEntryData	   (DMG* dmg, uint8_t index, DMG_ImageMode mode);
uint8_t*    DMG_GetEntryDataChunky (DMG* dmg, uint8_t index);
uint8_t*    DMG_GetEntryDataPlanar (DMG* dmg, uint8_t index);
uint32_t*   DMG_GetEntryPalette    (DMG* dmg, uint8_t index, DMG_ImageMode mode);
bool        DMG_RemoveEntry	 	   (DMG* dmg, uint8_t index);
bool        DMG_SetEntryPalette    (DMG* dmg, uint8_t index, uint32_t* palette);
bool		DMG_SetImageData       (DMG* dmg, uint8_t index, uint8_t* buffer, uint16_t width, uint16_t height, uint16_t size, bool compressed);
bool        DMG_SetAudioData       (DMG* dmg, uint8_t index, uint8_t* buffer, uint16_t size, DMG_KHZ freq);
bool        DMG_ReuseEntryData     (DMG* dmg, uint8_t index, uint8_t originalIndex);
int         DMG_GetEntryCount      (DMG* dmg);
uint8_t     DMG_FindFreeEntry      (DMG* dmg);
void		DMG_Close			   (DMG* dmg);

bool        DMG_SetupImageCache    (DMG* dmg, uint32_t bytes);
DMG_Cache*  DMG_GetImageCache      (DMG* dmg, uint8_t index, DMG_Entry* entry, uint32_t size);
void        DMG_FreeImageCache     (DMG* dmg);

void        DMG_SetupFileCache     (DMG* dmg, uint32_t blockSize = 0, void(*progressFunction)(uint16_t) = 0);
uint32_t    DMG_ReadFromFile       (DMG* dmg, uint32_t offset, void* buffer, uint32_t size);
void*       DMG_GetFromFileCache   (DMG* dmg, uint32_t offset, uint32_t size);
void        DMG_FreeFileImageCache (DMG* dmg);

void        DMG_Warning            (const char* format, ...);
void        DMG_SetError           (DMG_Error error);
DMG_Error   DMG_GetError           ();
const char* DMG_GetErrorString     ();
void        DMG_SetWarningHandler  (void (*handler)(const char* message));

const char* DMG_DescribeFreq       (DMG_KHZ freq);

uint32_t    DMG_GetTemporaryBufferSize ();
uint8_t*    DMG_GetTemporaryBuffer     (DMG_ImageMode mode);
void        DMG_FreeTemporaryBuffer    ();

bool        DMG_UncCGAToPacked         (const uint8_t* input, uint16_t width, uint16_t height, uint8_t* output, int pixels);
bool        DMG_UncEGAToPacked         (const uint8_t* input, uint16_t width, uint16_t height, uint8_t* output, int pixels);
bool        DMG_CopyImageData          (uint8_t* ptr, uint16_t length, uint8_t* output, int pixels);
uint8_t*    DMG_Planar8To16            (uint8_t* data, uint8_t* buffer, int length, uint32_t width);

bool        DMG_DecompressCGA          (const uint8_t* data, uint16_t dataLength, uint8_t* buffer, int width, int height);
bool        DMG_DecompressEGA          (const uint8_t* data, uint16_t dataLength, uint8_t* buffer, int width, int height);
bool        DMG_DecompressOldRLE       (const uint8_t* data, uint16_t rleMask, uint16_t dataLength, uint8_t* buffer, int pixels, bool littleEndian);
bool        DMG_DecompressNewRLE       (const uint8_t* data, uint16_t rleMask, uint16_t dataLength, uint8_t* buffer, int pixels, bool littleEndian);
bool        DMG_DecompressNewRLEPlanar (const uint8_t* data, uint16_t rleMask, uint16_t dataLength, uint8_t* buffer, uint32_t width, bool littleEndian);
bool        DMG_Planar8ToPacked        (const uint8_t* data, uint16_t length, uint8_t* output, int pixels, uint32_t width);
void        DMG_ConvertChunkyToPlanar  (uint8_t *buffer, uint32_t bufferSize, uint32_t width);

uint32_t    DMG_CalculateRequiredSize  (DMG_Entry* entry, DMG_ImageMode mode);

static inline DMG_CGAMode DMG_GetCGAMode(DMG_Entry* entry)
{
    return (entry->flags & DMG_FLAG_CGAMODE) ? CGA_Red : CGA_Blue;
}

static inline void DMG_SetCGAMode(DMG_Entry* entry, DMG_CGAMode mode)
{
    if (mode == CGA_Red)
        entry->flags |= DMG_FLAG_CGAMODE;
    else
        entry->flags &= ~DMG_FLAG_CGAMODE;
}