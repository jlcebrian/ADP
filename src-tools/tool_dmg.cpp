#include <dmg.h>
#include <img.h>
#include <ddb.h>
#include <os_lib.h>

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#define LOAD_BUFFER_SIZE 1024*1024*3

static bool debug = false;

static const char* imageExtensions[] = { "png" };
static const int count = sizeof(imageExtensions) / sizeof(imageExtensions[0]);

typedef enum
{
	OPTION_X,
	OPTION_Y,
	OPTION_FIRST_COLOR,
	OPTION_LAST_COLOR,
	OPTION_BUFFER,
	OPTION_FIXED,
	OPTION_CGA,
	OPTION_FREQ,
}
ColonOption;

static const char* colonOptions[] = {
	"x:",
	"y:",
	"first:",
	"last:",
	"buffer:",
	"fixed:",
	"cga:",
	"freq:",
};
static const int colonOptionCount = sizeof(colonOptions) / sizeof(colonOptions[0]);

static struct
{
	int value;
	DMG_KHZ freq;
}
frequencies[] =
{
	{ 5,     DMG_5KHZ },
	{ 7,     DMG_7KHZ },
	{ 9,     DMG_9_5KHZ },
	{ 15,    DMG_15KHZ },
	{ 20,    DMG_20KHZ },
	{ 30,    DMG_30KHZ },
    { 44,    DMG_44_1KHZ },
    { 48,    DMG_48KHZ },
	{ 5000,  DMG_5KHZ },
	{ 7000,  DMG_7KHZ },
	{ 95,    DMG_9_5KHZ },
	{ 9500,  DMG_9_5KHZ },
	{ 15000, DMG_15KHZ },
	{ 20000, DMG_20KHZ },
	{ 30000, DMG_30KHZ },
	{ 44100, DMG_44_1KHZ },
    { 48000, DMG_48KHZ },
	{ 0,     (DMG_KHZ)0 },
};

static uint8_t* buffer = NULL;
static uint32_t bufferSize = 0;
static DMG_ImageMode extractMode = ImageMode_Indexed;
static char filename[1024];
static char newfilename[1024];
static bool selected[256];
static bool verbose = false;
static bool readOnly = true;

typedef enum
{
	ACTION_LIST,
	ACTION_EXTRACT,
	ACTION_EXTRACT_PALETTES,
	ACTION_TEST,
	ACTION_ADD,
	ACTION_DELETE,
	ACTION_NEW,
	ACTION_UPDATE,
	ACTION_HELP,
}
Action;

static Action action = ACTION_LIST;

#pragma pack(push, 1)
struct WAVHeader
{
	uint8_t		riff[4];
	uint32_t 	fileSize;
	uint8_t		wave[4];
	uint8_t		fmt0[4];
	uint32_t	fmtSize;
	uint16_t	format;			// 1 - PCM
	uint16_t	channels;		// 1 - mono, 2 - stereo
	uint32_t	samplesPerSec;	// 8000, 11025, 22050, 44100
	uint32_t	avgBytesPerSec;	// samplesPerSec * channels * bitsPerSample/8
	uint16_t	blockAlign;		// bitsPerSample * channels / 8
	uint16_t	bitsPerSample;	// 8, 16
	uint8_t		data[4];
	uint32_t	dataSize;
};
#pragma pack(pop)

void TracePrintf(const char* format, ...)
{
}

static void PrintHelp()
{
	printf("DMG file utility for DAAD " VERSION_STR "\n\n");
	printf("Usage: dat [action] <file.dat> [options] [index:flags/file]\n\n");
	printf("Actions:\n\n");
	printf("    l     List contents of DAT file (default)\n");
	printf("    v     List contents of DAT file (with palettes)\n");
	printf("    x     Extract image/audio entries\n");
	printf("    t     Test image/audio entries\n");
	printf("    p     Extract image palettes\n");
	printf("    a     Add new images or audio entries\n");
	printf("    e     Edit image or audio entries\n");
	printf("    d     Delete image/audio entries\n");
	printf("    n     Create a new DAT file\n");
	printf("    u     Update/rebuild DAT file to newer version\n");
	printf("    h     Show this help\n");
	printf("\n");
	printf("Extract options:\n\n");
	printf("   #          Extract entry index (0-255)\n");
	printf("   #,#...     Extract the given list of entries\n");
	printf("   #-#        Extract entries from the given range (inclusive)\n");
	printf("   -i         Save indexed images (default)\n");
	printf("   -t         Save truecolor images\n");
	printf("   -e         Export EGA version of images/palettes\n");
	printf("   -c         Export CGA version of images/palettes\n");
	printf("\n");
	printf("Add/edit options:\n\n");
	printf("   #          Add/modify entry index (0-255)\n");
	printf("   file.png   Add/replace image contents & palette\n");
	printf("   file.wav   Add/replace audio sample contents\n");
	printf("   x:#        Set X coordinate\n");
	printf("   y:#        Set Y coordinate\n");
	printf("   first:#    Set first color\n");
	printf("   last:#     Set last color\n");
	printf("   cga:#      Set CGA mode (red or blue)\n");
	printf("   freq:#     Set frequency (5, 7, 9.5, 15, 20 or 30)\n");
	printf("   buffer:#   Set buffer flag (0 or 1)\n");
	printf("   fixed:#    Set fixed flag (0 or 1)\n");
}

static void ShowWarning(const char* message)
{
	fprintf(stderr, "Warning: %s\n", message);
}

static bool SaveWAV (const char* filename, uint8_t* data, size_t size, DMG_KHZ sampleRate)
{
	struct WAVHeader wav;
	File* file = File_Create(filename);
	uint8_t* ptr;

	if (file == NULL)
		return false;

	// Some samples have 0 padding at the end which produces nasty clicks
	ptr = data + size - 1;
	while (ptr > data && *ptr == 0)
		ptr--;
	size = ptr - data + 1;

	memcpy(wav.riff, "RIFF", 4);
	wav.fileSize = 36 + size;
	memcpy(wav.wave, "WAVE", 4);
	memcpy(wav.fmt0, "fmt ", 4);
	wav.fmtSize = 16;
	wav.format = 1;
	wav.channels = 1;
	switch (sampleRate)
	{
		case DMG_5KHZ:    wav.samplesPerSec =  5000; break;
		case DMG_7KHZ:    wav.samplesPerSec =  7000; break;
		case DMG_9_5KHZ:  wav.samplesPerSec =  9500; break;
		case DMG_15KHZ:   wav.samplesPerSec = 15000; break;
		case DMG_20KHZ:   wav.samplesPerSec = 20000; break;
		case DMG_30KHZ:   wav.samplesPerSec = 30000; break;
        case DMG_44_1KHZ: wav.samplesPerSec = 44100; break;
        case DMG_48KHZ:   wav.samplesPerSec = 48000; break;
		default:          wav.samplesPerSec = 11025; break;
	}
	wav.avgBytesPerSec = wav.samplesPerSec;
	wav.blockAlign = 1;
	wav.bitsPerSample = 8;
	memcpy(wav.data, "data", 4);
	wav.dataSize = size;

	if (File_Write(file, &wav, sizeof(wav)) != sizeof(wav))
	{
		File_Close(file);
		return false;
	}
	if (File_Write(file, data, size) != size)
	{
		File_Close(file);
		return false;
	}
	File_Close(file);
	return true;
}

const char* MakeFileName(const char* original, int index, const char* extension)
{
	char* ptr = newfilename;

	while (ptr < newfilename + sizeof(newfilename) - 16 && *original)
		*ptr++ = *original++;
	*ptr = 0;
	while (ptr > newfilename && *ptr != '\\' && *ptr != '/')
		ptr--;
	if (*ptr == '\\' || *ptr == '/')
		ptr++;
	snprintf(ptr, newfilename+sizeof(newfilename)-ptr, "%03d.%s", index, extension);
	return newfilename;
}

bool ParseEntrySelectionList(int argc, char *argv[])
{
	int n;

	memset(selected, 0, sizeof(selected));

	while (argc > 1)
	{
		const char *ptr = argv[1];
		if (isdigit(*ptr))
		{
			while (*ptr)
			{
				if (isspace(*ptr))
				{
					ptr++;
				}
				else if (isdigit(*ptr))
				{
					int index = 0;
					while (*ptr >= '0' && *ptr <= '9')
						index = index * 10 + *ptr++ - '0';
					if (index >= 0 && index < 256)
						selected[index] = true;
					if (*ptr == '-')
					{
						ptr++;
						if (isdigit(*ptr))
						{
							int index2 = 0;
							while (*ptr >= '0' && *ptr <= '9')
								index2 = index2 * 10 + *ptr++ - '0';
							if (index2 >= 0 && index2 < 256)
							{
								while (index <= index2)
									selected[index++] = true;
							}
							else
							{
								fprintf(stderr, "Error: Invalid index list: \"%s\"\n", argv[1]);
								return false;
							}
						}
						else if (*ptr == 0)
						{
							while (index < 256)
								selected[index++] = true;
						}
					}
					if (*ptr == ',')
						ptr++;
				}
				else
				{
					fprintf(stderr, "Error: Invalid index list: \"%s\"\n", argv[1]);
					return false;
				}
			}
		}
		else if (*ptr == '-')
		{
			ptr++;
			while (*ptr != 0)
			{
				if (action == ACTION_EXTRACT || action == ACTION_EXTRACT_PALETTES)
				{
					switch(tolower(*ptr))
					{
						case 'c': extractMode = (DMG_ImageMode)((extractMode & 0xF0) | 0x01); break;
						case 'e': extractMode = (DMG_ImageMode)((extractMode & 0xF0) | 0x02); break;
						case 'i': extractMode = (DMG_ImageMode)((extractMode & 0x0F) | 0x40); break;	// Indexed
						case 't': extractMode = (DMG_ImageMode)((extractMode & 0x0F) | 0x10); break;	// RGBA32
						default:
							fprintf(stderr, "Error: Invalid option: \"%c\"\n", *ptr);
							break;
					}
				}
				else
				{
					fprintf(stderr, "Error: Invalid option: \"%c\"\n", *ptr);
				}
				ptr++;
			}
		}
		else
		{
			fprintf(stderr, "Error: Invalid argument: \"%s\"\n", argv[1]);
			return false;
		}
		argc--, argv++;
	}

	for (n = 0 ; n < 256; n++)
	{
		if (selected[n])
			break;
	}
	if (n == 256)
	{
		memset(selected, 1, sizeof(selected));
	}
	return true;
}

static void ExtractSelectedEntries(DMG* dmg, bool saveToFile, bool paletteOnly)
{
	const char* outputFileName;
	int n;
	bool success;

	for (n = 0; n < 256; n++)
	{
		if (selected[n])
		{
			DMG_Entry* entry = DMG_GetEntry(dmg, n);
			size_t size;
			uint32_t* palette;

			if (entry == NULL)
			{
				if (DMG_GetError() != DMG_ERROR_NONE)
					fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
			switch (entry->type)
			{
				case DMGEntry_Image:
				{
					size = entry->width * entry->height * 4;
					if (size == 0)
						continue;
					uint8_t* buffer = DMG_GetEntryData(dmg, n, extractMode);
					if (buffer == 0)
					{
						fprintf(stderr, "%03d: Error: Unable to read image entry: %s\n", n, DMG_GetErrorString());
						continue;
					}
					if (saveToFile && paletteOnly)
					{
						outputFileName = MakeFileName(filename, n, "col");
						palette = (uint32_t*)DMG_GetEntryPalette(dmg, n, extractMode);
						success = SaveCOLPalette16(outputFileName, palette);
						if (!success)
						{
							fprintf(stderr, "%03d: Error: Unable to write palette to %s\n", n, outputFileName);
							continue;
						}
						printf("%s saved\n", outputFileName);
					}
					else if (saveToFile)
					{
						outputFileName = MakeFileName(filename, n, "png");
						if (DMG_IS_INDEXED(extractMode))
						{
							palette = DMG_GetEntryPalette(dmg, n, extractMode);
							success = SavePNGIndexed16(outputFileName, buffer, entry->width, entry->height, palette);
						}
						else if (DMG_IS_RGBA32(extractMode))
						{
							success = SavePNGRGB32(outputFileName, (uint32_t*)buffer, entry->width, entry->height);
						}
						if (!success)
						{
							fprintf(stderr, "%03d: Error: Unable to write image to %s\n", n, outputFileName);
							continue;
						}
						printf("%s saved\n", outputFileName);
					}
					else
					{
						printf("%03d: (%dx%d image, %s) %d bytes ok.\n", n, entry->width, entry->height,
						       (entry->flags & DMG_FLAG_COMPRESSED) ? "compressed" : "uncompressed", entry->length);
					}
					break;
				}

				case DMGEntry_Audio:
				{
					uint8_t* buffer = DMG_GetEntryData(dmg, n, extractMode);
					if (buffer == 0)
					{
						fprintf(stderr, "%03d: Error: Unable to read audio entry: %s\n", n, DMG_GetErrorString());
						continue;
					}
					if (saveToFile)
					{
						outputFileName = MakeFileName(filename, n, "wav");
						if (!SaveWAV(outputFileName, buffer, entry->length, (DMG_KHZ)entry->x))
						{
							fprintf(stderr, "%03d: Error: Unable to write audio entry to %s\n", n, outputFileName);
							continue;
						}
						printf("%s saved\n", outputFileName);
					}
					else
					{
						printf("%03d: %s (audio sample) %d bytes ok.\n", n, filename, entry->length);
					}
					break;
				}

				case DMGEntry_Empty:
					break;
			}
		}
	}
}

static void DeleteSelectedEntries(DMG* dmg)
{
	int n;
	int count = 0;

	for (n = 0; n < 256; n++)
	{
		if (selected[n])
		{
			if (!DMG_RemoveEntry(dmg, n))
			{
				fprintf(stderr, "%03d: Error: Unable to delete entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
			count++;
			printf("%03d: deleted\n", n);
		}
	}
	if (count == 0)
		printf("No entries deleted\n");
}

static const char *DescribeVersion(DMG_Version v)
{
	switch (v)
	{
		case DMG_Version1:      return "Version1";
		case DMG_Version1_CGA:  return "CGA";
		case DMG_Version1_EGA:  return "EGA";
		case DMG_Version2:      return "Version2";
		default:                return "unknown DAT";
	}
}

static void ListSelectedEntries(DMG* dmg, bool verbose)
{
	int n, i;

	printf ("Mode %d, %s, %s\n", dmg->screenMode, DescribeVersion(dmg->version),
		dmg->littleEndian ? "little endian" : "big endian");

	for (n = 0; n < 256; n++)
	{
		if (selected[n])
		{
			DMG_Entry* entry = DMG_GetEntry(dmg, n);
			if (entry == NULL)
			{
				if (DMG_GetError() != DMG_ERROR_NONE)
					fprintf(stderr, "Error: Unable to read entry %d: %s\n", n, DMG_GetErrorString());
				continue;
			}
			switch (entry->type)
			{
				case DMGEntry_Image:
					if (entry->width * entry->height == 0 || entry->length == 0)
						continue;
					printf("%03d: Image %3dx%-3d %s %s at X:%-4d Y:%-4d %5d bytes %s\n",
						n, entry->width, entry->height,
						(entry->flags & DMG_FLAG_BUFFERED)   ? "buffer ":"       ",
						(entry->flags & DMG_FLAG_FIXED)      ? "fixed":"float",
						entry->x, entry->y, entry->length,
						(entry->flags & DMG_FLAG_COMPRESSED) ? " (compressed)":"");
					if (verbose)
					{
                        printf("     File offset: %08X\n", entry->fileOffset);
						printf("     Color range:  %d-%d\n", entry->firstColor, entry->lastColor);
						printf("     Palette:      ");
						for (i = 0; i < 16; i++)
						{
							uint32_t c = entry->RGB32Palette[i];
							printf("%03X ", ((c >> 4) & 0xF) | ((c >> 8) & 0xF0) | ((c >> 12) & 0xF00));
						}
						printf("\n");
						printf("     EGA Palette:  ");
						for (i = 0; i < 16; i++)
							printf(" %02d ", entry->EGAPalette[i]);
						printf("\n");
						printf("     CGA Palette:  ");
						for (i = 0; i < 4; i++)
							printf(" %02d ", entry->CGAPalette[i]);
						printf (" (%s)", DMG_GetCGAMode(entry) == CGA_Blue ? "blue" : "red");
						printf("\n");
					}
					break;

				case DMGEntry_Audio:
					printf("%03d: Audio sample   %-16s               %5d bytes\n",
						n, DMG_DescribeFreq((DMG_KHZ)entry->x), entry->length);
					break;

				case DMGEntry_Empty:
					break;
			}
		}
	}
}

static void RGBA32ToIndexed16 (uint32_t* pixels, int count, uint32_t* palette, uint8_t* out)
{
	static uint8_t tables[64][256];
	int n, tableCount = 1;

	memset(tables, 0, 64*256);
	for (n = 0; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		if (tables[0][r] == 0)
			tables[0][r] = tableCount++;
	}
	for (n = 0; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		uint8_t g = (palette[n] >> 8) & 0xFF;
		uint8_t t = tables[0][r];
		if (tables[t][g] == 0)
			tables[t][g] = tableCount++;
	}
	for (n = 0 ; n < 16; n++)
	{
		uint8_t r = (palette[n] >> 16) & 0xFF;
		uint8_t g = (palette[n] >> 8) & 0xFF;
		uint8_t b = (palette[n] >> 0) & 0xFF;
		uint8_t t = tables[0][r];
		uint8_t u = tables[t][g];
		tables[u][b] = n;
	}

	while (count-- > 0)
	{
		uint32_t* color = pixels++;
		uint8_t r = (*color >> 16) & 0xFF;
		uint8_t g = (*color >> 8) & 0xFF;
		uint8_t b = (*color >> 0) & 0xFF;
		*out++ = tables[tables[tables[0][r] & 0x1F][g] & 0x1F][b];
	}
}

static bool IsImageFileExtension(const char* extension)
{
	int n;

	for (n = 0; n < count; n++)
	{
		if (stricmp(extension, imageExtensions[n]) == 0)
			return true;
	}
	return false;
}

static bool ParseEntryChanges(DMG* dmg, int argc, char *argv[])
{
	int currentIndex = -1;
	bool currentFileSaved = false;
	bool indexSpecified = false;

	while (argc > 1)
	{
		const char* ptr = argv[1];
		const char* colon = strchr(ptr, ':');
		const char* dot = strrchr(ptr, '.');
		const char* slash = strrchr(ptr, '/');
		const char* backslash = strrchr(ptr, '\\');
		const char* filepart = slash ? slash+1 : backslash ? backslash+1 : ptr;

		if (dot != NULL && IsImageFileExtension(dot+1))
		{
			uint16_t width, height;
			uint32_t palette[16];
			uint8_t* outBuffer;
			uint32_t size;
			uint16_t compressedSize;
			bool compressed;

			// Extract or calculate entry index

			if (colon && isdigit(*ptr))
			{
				currentIndex = atoi(ptr);
				indexSpecified = true;
				ptr = colon + 1;
				if (currentIndex > 255)
				{
					fprintf(stderr, "Error: Invalid index: %d\n", currentIndex);
					return false;
				}
			}
			else if (!indexSpecified && isdigit(*filepart) && atoi(filepart) < 256)
			{
				currentIndex = atoi(filepart);
			}
			else if (!indexSpecified && !currentFileSaved)
			{
				currentIndex = DMG_FindFreeEntry(dmg);
			}
			else if (currentFileSaved && currentIndex < 256)
				currentIndex++;

			// Allocate a suitable. TODO: Dynamically allocate
			// the image data, to support old platforms.

			if (bufferSize < LOAD_BUFFER_SIZE)
			{
				bufferSize = LOAD_BUFFER_SIZE;
				buffer = (uint8_t*)realloc(buffer, bufferSize);
				if (buffer == NULL)
				{
					fprintf(stderr, "Error: Out of memory: unable to allocate %d bytes\n", bufferSize);
					return false;
				}
			}

			// Load the image and store it in the DMG

			if (!LoadPNGIndexed16(ptr, buffer, bufferSize, &width, &height, &palette[0]))
			{
				fprintf(stderr, "Error: Unable to load image \"%s\"\n", ptr);
				return false;
			}
			size = width * height;
			outBuffer = buffer + size;
			if (!CompressImage(buffer, size, outBuffer, LOAD_BUFFER_SIZE - size, &compressed, &compressedSize, debug))
			{
				fprintf(stderr, "Error: Unable to compress image \"%s\": %s\n", outBuffer, DMG_GetErrorString());
				return false;
			}
			if (!DMG_SetEntryPalette(dmg, currentIndex, palette))
			{
				fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
				return false;
			}
			if (!DMG_SetImageData(dmg, currentIndex, outBuffer, width, height, compressedSize, compressed))
			{
				fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
				return false;
			}
			printf("%03d: Added image %s (%d bytes%s)\n", currentIndex, ptr, compressedSize, compressed ? ", compressed" : "");

			currentFileSaved = true;
		}
		else if (colon)
		{
			int n, i;
			bool success = true;

			for (n = 0; n < colonOptionCount && success; n++)
			{
				if (strnicmp(ptr, colonOptions[n], strlen(colonOptions[n])) == 0)
				{
					int value;
					DMG_Entry* entry;

					if (currentIndex < 0)
					{
						fprintf(stderr, "Error: Entry index required\n");
						continue;
					}
					entry = DMG_GetEntry(dmg, currentIndex);
					if (entry == NULL)
					{
						if (DMG_GetError() != DMG_ERROR_NONE)
							fprintf(stderr, "Error: Unable to read entry: %s\n", DMG_GetErrorString());
						continue;
					}

					ptr += strlen(colonOptions[n]);
					if (isdigit(*ptr))
						value = atoi(ptr);
					else if (stricmp(ptr, "true"))
						value = 1;
					else if (stricmp(ptr, "false"))
						value = 0;
					else if (stricmp(ptr, "red"))
						value = 1;
					else if (stricmp(ptr, "blue"))
						value = 0;
					else
					{
						fprintf(stderr, "Error: Invalid value: \"%s\"\n", ptr);
						success = false;
						break;
					}

					switch (n)
					{
						case OPTION_X:
							if (entry->type != DMGEntry_Image)
							{
								fprintf(stderr, "Error: X coordinate only valid for images\n");
								success = false;
								break;
							}
							if (value < 0 || value > DMG_MAX_IMAGE_WIDTH)
							{
								fprintf(stderr, "Error: Invalid X coordinate: %d\n", value);
								success = false;
								break;
							}
							entry->x = value;
							printf("%03d: X coordinate set to %d\n", currentIndex, value);
							break;
						case OPTION_Y:
							if (entry->type != DMGEntry_Image)
							{
								fprintf(stderr, "Error: X coordinate only valid for images\n");
								success = false;
								break;
							}
							if (value < 0 || value > DMG_MAX_IMAGE_HEIGHT)
							{
								fprintf(stderr, "Error: Invalid Y coordinate: %d\n", value);
								success = false;
								break;
							}
							entry->y = value;
							printf("%03d: Y coordinate set to %d\n", currentIndex, value);
							break;
						case OPTION_FIRST_COLOR:
							if (value < 0 || value > 15)
							{
								fprintf(stderr, "Error: Invalid first color: %d\n", value);
								success = false;
								break;
							}
							entry->firstColor = value;
							printf("%03d: First color set to %d\n", currentIndex, value);
							break;
						case OPTION_LAST_COLOR:
							if (value < 0 || value > 15)
							{
								fprintf(stderr, "Error: Invalid last color: %d\n", value);
								success = false;
								break;
							}
							entry->lastColor = value;
							printf("%03d: Last color set to %d\n", currentIndex, value);
							break;
						case OPTION_BUFFER:
							if (value < 0 || value > 1)
							{
								fprintf(stderr, "Error: Invalid buffer flag: %d\n", value);
								success = false;
								break;
							}
							if (value)
                                entry->flags |= DMG_FLAG_BUFFERED;
                            else
                                entry->flags &= ~DMG_FLAG_BUFFERED;
							printf("%03d: Buffer flag set to %s\n", currentIndex, value ? "true" : "false");
							break;
						case OPTION_FIXED:
							if (value < 0 || value > 1)
							{
								fprintf(stderr, "Error: Invalid fixed flag: %d\n", value);
								success = false;
								break;
							}
							if (value)
                                entry->flags |= DMG_FLAG_FIXED;
                            else
                                entry->flags &= ~DMG_FLAG_FIXED;
							printf("%03d: Fixed flag set to %s\n", currentIndex, value ? "true" : "false");
							break;
						case OPTION_CGA:
							if (value < 0 || value > 1)
							{
								fprintf(stderr, "Error: Invalid CGA mode: %d\n", value);
								success = false;
								break;
							}
							DMG_SetCGAMode(entry, (DMG_CGAMode)value);
							printf("%03d: CGA mode set to %s\n", currentIndex, value ? "blue" : "red");
							break;
						case OPTION_FREQ:
							if (entry->type != DMGEntry_Audio)
							{
								fprintf(stderr, "Error: Frequency only valid for audio\n");
								success = false;
								break;
							}
							for (i = 0; frequencies[i].value != 0; i++)
							{
								if (frequencies[i].value == value) {
									entry->x = frequencies[i].freq;
									break;
								}
							}
							if (frequencies[i].value == 0)
							{
								fprintf(stderr, "Error: Invalid frequency: %d\n", value);
								success = false;
								break;
							}
							printf("%03d: Frequency set to %s\n", currentIndex, DMG_DescribeFreq((DMG_KHZ)entry->x));
							break;
					}
					if (success)
						DMG_UpdateEntry(dmg, currentIndex);
					break;
				}
			}
			if (n == colonOptionCount)
			{
				fprintf(stderr, "Error: Unknown option: \"%s\"\n", argv[1]);
				continue;
			}
		}
		else if (isdigit(*ptr))
		{
			currentIndex = atoi(ptr);
			if (currentIndex > 255)
			{
				fprintf(stderr, "Error: Invalid index: %d\n", currentIndex);
				return false;
			}
			currentFileSaved = false;
			indexSpecified = true;
		}

		argc--, argv++;
	}
	return true;
}

bool RebuildDAT(DMG* dmg, const char* outputFileName)
{
	int n, i;
	DMG* out = DMG_Create(outputFileName);
	if (out == NULL)
	{
		fprintf(stderr, "Error: Failed to create \"%s\": %s\n", outputFileName, DMG_GetErrorString());
		return false;
	}

	for (n = 0; n < 256; n++)
	{
		DMG_Entry* entry = DMG_GetEntry(dmg, n);
		if (entry == NULL)
		{
			if (DMG_GetError() != DMG_ERROR_NONE)
				fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", n, DMG_GetErrorString());
			continue;
		}
		DMG_Entry* outEntry = DMG_GetEntry(out, n);
		if (outEntry == NULL)
		{
			fprintf(stderr, "%03d: Error: Unable to read entry: %s\n", n, DMG_GetErrorString());
			continue;
		}

		memcpy (outEntry->RGB32Palette, entry->RGB32Palette, sizeof(entry->RGB32Palette));
		memcpy (outEntry->CGAPalette, entry->CGAPalette, sizeof(entry->CGAPalette));
		memcpy (outEntry->EGAPalette, entry->EGAPalette, sizeof(entry->EGAPalette));
        outEntry->flags = entry->flags;
		outEntry->x = entry->x;
		outEntry->y = entry->y;
		outEntry->firstColor = entry->firstColor;
		outEntry->lastColor = entry->lastColor;

		if (entry->type == DMGEntry_Empty)
			continue;

		for (i = 0; i < n; i++)
		{
			if (dmg->entries[i] == 0)
				continue;
			if (dmg->entries[i]->fileOffset == entry->fileOffset)
				break;
		}
		if (i != n)
		{
			outEntry->fileOffset = out->entries[i]->fileOffset;
			printf("%03d: Reusing data from %03d\n", n, i);
			DMG_UpdateEntry(out, n);
			continue;
		}

		if (entry->type == DMGEntry_Audio)
		{
			uint16_t size = entry->length;
			uint8_t* data = DMG_GetEntryData(dmg, n, ImageMode_Audio);
			if (data == 0)
			{
				fprintf(stderr, "%03d: Error: Unable to read audio entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
			if (!DMG_SetAudioData(out, n, data, size, (DMG_KHZ)entry->x))
			{
				fprintf(stderr, "Error: Unable to set audio data: %s\n", DMG_GetErrorString());
				DMG_Close(out);
				return false;
			}
			printf("%03d: Added audio (%5d bytes, %s)\n", n, size, DMG_DescribeFreq((DMG_KHZ)entry->x));
		}

		if (entry->type == DMGEntry_Image)
		{
			uint16_t width = entry->width;
			uint16_t height = entry->height;
			uint32_t size = width * height;
			bool compressed;
			uint16_t compressedSize;
			uint8_t* outPtr;

			if (bufferSize < size + 64)
			{
				bufferSize = size + 64;
				buffer = (uint8_t*)realloc(buffer, bufferSize);
				if (buffer == NULL)
				{
					fprintf(stderr, "Error: Out of memory: unable to allocate %d bytes\n", bufferSize);
					DMG_Close(out);
					return false;
				}
			}
			outPtr = buffer;

			uint8_t* inPtr = DMG_GetEntryData(dmg, n, ImageMode_Indexed);
			if (inPtr == 0)
			{
				fprintf(stderr, "%03d: Error: Unable to read image entry: %s\n", n, DMG_GetErrorString());
				continue;
			}
			if (!CompressImage(inPtr, size, outPtr, size, &compressed, &compressedSize, debug))
			{
				fprintf(stderr, "Error: Unable to compress image \"%s\": %s\n", outPtr, DMG_GetErrorString());
				DMG_Close(out);
				return false;
			}
			if (!DMG_SetImageData(out, n, outPtr, width, height, compressedSize, compressed))
			{
				fprintf(stderr, "Error: Unable to set image data: %s\n", DMG_GetErrorString());
				DMG_Close(out);
				return false;
			}
			printf("%03d: Added image (%5d bytes%s)\n", n, compressedSize, compressed ? ", compressed" : "");
		}
		else
		{
			DMG_UpdateEntry(out, n);
		}
	}
	DMG_Close(out);
	return true;
}

int main (int argc, char *argv[])
{
	DMG* dmg = NULL;
	const char* outputFileName;
	int n;

	if (argc < 2)
	{
		PrintHelp();
		return 0;
	}

	if (strlen(argv[1]) == 1)
	{
		switch (tolower(argv[1][0]))
		{
			case 'l': action = ACTION_LIST; break;
			case 'v': action = ACTION_LIST; verbose = true; break;
			case 'x': action = ACTION_EXTRACT; break;
			case 'p': action = ACTION_EXTRACT_PALETTES; break;
			case 't': action = ACTION_TEST; break;
			case 'a': action = ACTION_ADD; readOnly = false; break;
			case 'm': action = ACTION_ADD; readOnly = false; break;
			case 'e': action = ACTION_ADD; readOnly = false; break;
			case 'd': action = ACTION_DELETE; readOnly = false; break;
			case 'c': action = ACTION_NEW; readOnly = false; break;
			case 'n': action = ACTION_NEW; readOnly = false; break;
			case 'o': action = ACTION_UPDATE; readOnly = false; break;
			case 'u': action = ACTION_UPDATE; readOnly = false; break;
			case 'h': PrintHelp(); return 0;

			default:
				fprintf(stderr, "Error: Unknown action: \"%s\"\n", argv[1]);
				return 1;
		}
		argc--, argv++;
		if (argc < 2)
		{
			fprintf(stderr, "Error: Missing filename\n");
			return 1;
		}
	}

    if (argc > 1 && argv[1][0] == '-')
    {
        if (stricmp(argv[1], "--debug") == 0)
        {
            debug = true;
            argc--, argv++;
        }
        else
        {
            fprintf(stderr, "Error: Unknown option: \"%s\"\n", argv[1]);
            return 1;
        }
    }

	DMG_SetWarningHandler(ShowWarning);

	if (strlen(argv[1]) > 1000)
	{
		fprintf(stderr, "Error: Invalid filename: \"%s\"\n", argv[1]);
		return 1;
	}
	strcpy(filename, argv[1]);

	if (action == ACTION_NEW)
	{
		dmg = DMG_Create(filename);
		if (dmg == NULL)
		{
			fprintf(stderr, "Error: Failed to create \"%s\": %s\n", argv[1], DMG_GetErrorString());
			return 1;
		}
		printf("Created new DAT file \"%s\"\n", argv[1]);
	}
	else
	{
		dmg = DMG_Open(filename, readOnly);
		if (dmg == NULL)
		{
			fprintf(stderr, "Error: Failed to open \"%s\": %s\n", filename, DMG_GetErrorString());
			return 1;
		}
	}
	argc--, argv++;

    if (argc > 0 && stricmp(argv[0], "-debug") == 0)
    {
        debug = true;
        argc--, argv++;
    }

	switch (action)
	{
		case ACTION_HELP:
			break;
		case ACTION_LIST:
			if (ParseEntrySelectionList(argc, argv))
				ListSelectedEntries(dmg, verbose);
			break;
		case ACTION_DELETE:
			if (ParseEntrySelectionList(argc, argv))
				DeleteSelectedEntries(dmg);
			break;
		case ACTION_EXTRACT:
		case ACTION_EXTRACT_PALETTES:
		case ACTION_TEST:
			if (ParseEntrySelectionList(argc, argv))
				ExtractSelectedEntries(dmg, action != ACTION_TEST, action == ACTION_EXTRACT_PALETTES);
			break;
		case ACTION_ADD:
		case ACTION_NEW:
			ParseEntryChanges(dmg, argc, argv);
			break;
		case ACTION_UPDATE:
			if (argc < 2)
				outputFileName = ChangeExtension(filename, "new");
			else
				outputFileName = argv[1];
			if (RebuildDAT(dmg, outputFileName))
			{
				if (argc < 2)
				{
					DMG_Close(dmg);
					dmg = NULL;
					if (remove(filename) != 0)
						fprintf(stderr, "Warning: Unable to delete old file \"%s\"\n", filename);
					else if (rename(outputFileName, filename) != 0)
						fprintf(stderr, "Warning: Unable to rename \"%s\" to \"%s\"\n", outputFileName, filename);
					else
						printf("%s written.\n", filename);
				}
				else
					printf("%s written.\n", outputFileName);
			}

			break;
	}
	if (dmg != NULL)
		DMG_Close(dmg);

	return 0;
}