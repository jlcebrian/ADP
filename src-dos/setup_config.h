#ifndef ADP_SETUP_CONFIG_H
#define ADP_SETUP_CONFIG_H

#include <ddb.h>
#include <stdint.h>
#include <stdbool.h>

#define ADP_SETUP_CONFIG_FILE "ADPSETUP.CFG"

// Sound card the player should drive. The engine only produces 8-bit mono
// samples, so the SB family differ here only by DMA path and top sample rate
// (see sb.cpp): a plain SB tops out around 22 kHz, Pro/16 reach 44 kHz.
// Values are reserved past SB16 for future non-SB backends (LPT DACs, ...).
enum ADPSoundCard
{
	SoundCard_None  = 0,   // no sound
	SoundCard_SB    = 1,   // Sound Blaster 1.x / 2.0
	SoundCard_SBPro = 2,   // Sound Blaster Pro
	SoundCard_SB16  = 3,   // Sound Blaster 16
};

struct ADPSetupConfig
{
	DDB_ScreenMode videoMode;
	ADPSoundCard   card;      // SoundCard_None disables sound
	uint16_t       sbPort;    // SB base port (used by the SB family)
	uint8_t        sbIrq;
	uint8_t        sbDMA;     // 8-bit DMA channel (8-bit mono needs no 16-bit DMA)
	uint16_t       sampleRate;
};

void ADPSetup_Default(ADPSetupConfig* config);
bool ADPSetup_Load(const char* fileName, ADPSetupConfig* config);
bool ADPSetup_Save(const char* fileName, const ADPSetupConfig* config);
bool ADPSetup_IsValid(const ADPSetupConfig* config);
const char* ADPSetup_VideoName(DDB_ScreenMode mode);
const char* ADPSetup_CardName(ADPSoundCard card);
// Highest sample rate the given card supports (Hz).
uint16_t    ADPSetup_CardMaxRate(ADPSoundCard card);
// Highest SB DSP version the driver should use for this card (see SB_SetMaxVersion):
// caps an over-capable card down to the selected family's behaviour.
uint16_t    ADPSetup_CardDSPCap(ADPSoundCard card);

#endif
