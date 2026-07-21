#ifdef _DOS

#include "setup_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Bump when the on-disk layout changes so an older ADPSETUP.CFG is treated as
// absent (the player then asks the user to re-run SETUP). v2 replaced the
// SOUND/SBDMA16/MODE keys with a single CARD key.
#define ADP_SETUP_CONFIG_VERSION 2

void ADPSetup_Default(ADPSetupConfig* config)
{
	config->videoMode = ScreenMode_VGA16;
	config->card = SoundCard_None;
	config->sbPort = 0x220;
	config->sbIrq = 5;
	config->sbDMA = 1;
	config->sampleRate = 22050;
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

const char* ADPSetup_CardName(ADPSoundCard card)
{
	switch (card)
	{
		case SoundCard_None:  return "None";
		case SoundCard_SB:    return "Sound Blaster";
		case SoundCard_SBPro: return "Sound Blaster Pro";
		case SoundCard_SB16:  return "Sound Blaster 16";
		default:              return "Unknown";
	}
}

uint16_t ADPSetup_CardMaxRate(ADPSoundCard card)
{
	switch (card)
	{
		case SoundCard_SB:    return 22050;   // classic single-cycle / 2.0 auto-init
		case SoundCard_SBPro: return 44100;
		case SoundCard_SB16:  return 44100;
		default:              return 0;
	}
}

uint16_t ADPSetup_CardDSPCap(ADPSoundCard card)
{
	switch (card)
	{
		case SoundCard_SB:    return 0x0200;  // <=2.0: single-cycle / auto-init 8-bit
		case SoundCard_SBPro: return 0x03FF;  // high-speed 8-bit
		case SoundCard_SB16:  return 0xFFFF;  // native rate command
		default:              return 0xFFFF;
	}
}

// Written to / parsed from the CARD key.
static const char* CardToken(ADPSoundCard card)
{
	switch (card)
	{
		case SoundCard_SB:    return "SB";
		case SoundCard_SBPro: return "SBPRO";
		case SoundCard_SB16:  return "SB16";
		default:              return "NONE";
	}
}

static ADPSoundCard CardFromToken(const char* value)
{
	if (strcmp(value, "SB") == 0)    return SoundCard_SB;
	if (strcmp(value, "SBPRO") == 0) return SoundCard_SBPro;
	if (strcmp(value, "SB16") == 0)  return SoundCard_SB16;
	return SoundCard_None;
}

bool ADPSetup_IsValid(const ADPSetupConfig* config)
{
	if (config->videoMode != ScreenMode_CGA &&
		config->videoMode != ScreenMode_EGA &&
		config->videoMode != ScreenMode_VGA16 &&
		config->videoMode != ScreenMode_VGA &&
		config->videoMode != ScreenMode_SHiRes)
		return false;

	if (config->card == SoundCard_None)
		return true;

	if (config->card != SoundCard_SB &&
		config->card != SoundCard_SBPro &&
		config->card != SoundCard_SB16)
		return false;

	// SB-family resources.
	if (config->sbPort < 0x200 || config->sbPort > 0x280)
		return false;
	if (config->sbIrq < 2 || config->sbIrq > 15)
		return false;
	if (config->sbDMA > 3)
		return false;

	if (config->sampleRate != 11025 &&
		config->sampleRate != 22050 &&
		config->sampleRate != 30000 &&
		config->sampleRate != 44100)
		return false;
	if (config->sampleRate > ADPSetup_CardMaxRate(config->card))
		return false;
	return true;
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
	else if (strcmp(key, "CARD") == 0)
		config->card = CardFromToken(value);
	else if (strcmp(key, "SBPORT") == 0)
		config->sbPort = (uint16_t)strtol(value, 0, 16);
	else if (strcmp(key, "SBIRQ") == 0)
		config->sbIrq = (uint8_t)atoi(value);
	else if (strcmp(key, "SBDMA") == 0)
		config->sbDMA = (uint8_t)atoi(value);
	else if (strcmp(key, "RATE") == 0)
		config->sampleRate = (uint16_t)atoi(value);
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
	return version == ADP_SETUP_CONFIG_VERSION && ADPSetup_IsValid(config);
}

bool ADPSetup_Save(const char* fileName, const ADPSetupConfig* config)
{
	if (!ADPSetup_IsValid(config))
		return false;

	FILE* file = fopen(fileName, "wt");
	if (file == 0)
		return false;

	fprintf(file,
		"VERSION=%d\n"
		"VIDEO=%s\n"
		"CARD=%s\n",
		ADP_SETUP_CONFIG_VERSION,
		ADPSetup_VideoName(config->videoMode),
		CardToken(config->card));

	if (config->card != SoundCard_None)
	{
		fprintf(file,
			"SBPORT=%X\n"
			"SBIRQ=%u\n"
			"SBDMA=%u\n"
			"RATE=%u\n",
			config->sbPort,
			config->sbIrq,
			config->sbDMA,
			config->sampleRate);
	}
	return fclose(file) == 0;
}

#endif
