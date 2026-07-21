#ifdef _DOS

#include "setup_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ADPSetup_Default(ADPSetupConfig* config)
{
	config->videoMode = ScreenMode_VGA16;
	config->soundEnabled = false;
	config->sbPort = 0x220;
	config->sbIrq = 5;
	config->sbDMA8 = 1;
	config->sbDMA16 = 5;
	config->sampleRate = 22050;
	config->soundMode = 0;
}

const char* ADPSetup_VideoName(DDB_ScreenMode mode)
{
	switch (mode)
	{
		case ScreenMode_CGA: return "CGA";
		case ScreenMode_EGA: return "EGA";
		case ScreenMode_VGA16: return "VGA16";
		case ScreenMode_VGA: return "VGA";
		case ScreenMode_SHiRes: return "SVGA";
		default: return "Unknown";
	}
}

bool ADPSetup_IsValid(const ADPSetupConfig* config)
{
	if (config->videoMode != ScreenMode_CGA &&
		config->videoMode != ScreenMode_EGA &&
		config->videoMode != ScreenMode_VGA16 &&
		config->videoMode != ScreenMode_VGA &&
		config->videoMode != ScreenMode_SHiRes)
		return false;

	if (!config->soundEnabled)
		return true;

	if (config->sbPort < 0x200 || config->sbPort > 0x280)
		return false;
	if (config->sbIrq < 2 || config->sbIrq > 15)
		return false;
	if (config->sbDMA8 > 3)
		return false;
	if (config->sbDMA16 < 4 || config->sbDMA16 > 7)
		return false;
	if (config->sampleRate != 11025 &&
		config->sampleRate != 22050 &&
		config->sampleRate != 30000 &&
		config->sampleRate != 44100)
		return false;
	// The DOS mixer only produces 8-bit mono output, so no other
	// playback mode is accepted here.
	return config->soundMode == 0;
}

static void TrimLine(char* text)
{
	char* end = text + strlen(text);
	while (end > text &&
		(end[-1] == '\r' || end[-1] == '\n' ||
		 end[-1] == ' ' || end[-1] == '\t'))
		*--end = 0;
}

static void ApplyEntry(ADPSetupConfig* config, const char* key,
	const char* value, int* version)
{
	if (strcmp(key, "VERSION") == 0)
		*version = atoi(value);
	else if (strcmp(key, "VIDEO") == 0)
	{
		if (strcmp(value, "CGA") == 0)
			config->videoMode = ScreenMode_CGA;
		else if (strcmp(value, "EGA") == 0)
			config->videoMode = ScreenMode_EGA;
		else if (strcmp(value, "VGA16") == 0)
			config->videoMode = ScreenMode_VGA16;
		else if (strcmp(value, "VGA") == 0)
			config->videoMode = ScreenMode_VGA;
		else if (strcmp(value, "SVGA") == 0 || strcmp(value, "SGA") == 0)
			config->videoMode = ScreenMode_SHiRes;
		else
			config->videoMode = ScreenMode_Default;
	}
	else if (strcmp(key, "SOUND") == 0)
		config->soundEnabled = atoi(value) != 0;
	else if (strcmp(key, "SBPORT") == 0)
		config->sbPort = (uint16_t)strtol(value, 0, 16);
	else if (strcmp(key, "SBIRQ") == 0)
		config->sbIrq = (uint8_t)atoi(value);
	else if (strcmp(key, "SBDMA8") == 0)
		config->sbDMA8 = (uint8_t)atoi(value);
	else if (strcmp(key, "SBDMA16") == 0)
		config->sbDMA16 = (uint8_t)atoi(value);
	else if (strcmp(key, "RATE") == 0)
		config->sampleRate = (uint16_t)atoi(value);
	else if (strcmp(key, "MODE") == 0)
		config->soundMode = (uint8_t)atoi(value);
}

bool ADPSetup_Load(const char* fileName, ADPSetupConfig* config)
{
	FILE* file = fopen(fileName, "rt");
	if (file == 0)
		return false;

	ADPSetup_Default(config);
	char line[80];
	int version = 0;
	while (fgets(line, sizeof(line), file) != 0)
	{
		TrimLine(line);
		char* equals = strchr(line, '=');
		if (equals == 0)
			continue;
		*equals++ = 0;
		ApplyEntry(config, line, equals, &version);
	}
	fclose(file);
	return version == 1 && ADPSetup_IsValid(config);
}

bool ADPSetup_Save(const char* fileName, const ADPSetupConfig* config)
{
	if (!ADPSetup_IsValid(config))
		return false;

	FILE* file = fopen(fileName, "wt");
	if (file == 0)
		return false;

	fprintf(file,
		"VERSION=1\n"
		"VIDEO=%s\n"
		"SOUND=%d\n"
		"SBPORT=%X\n"
		"SBIRQ=%u\n"
		"SBDMA8=%u\n"
		"SBDMA16=%u\n"
		"RATE=%u\n"
		"MODE=%u\n",
		ADPSetup_VideoName(config->videoMode),
		config->soundEnabled ? 1 : 0,
		config->sbPort,
		config->sbIrq,
		config->sbDMA8,
		config->sbDMA16,
		config->sampleRate,
		config->soundMode);
	return fclose(file) == 0;
}

#endif
