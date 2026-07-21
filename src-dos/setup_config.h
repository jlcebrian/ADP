#ifndef ADP_SETUP_CONFIG_H
#define ADP_SETUP_CONFIG_H

#include <ddb.h>
#include <stdint.h>
#include <stdbool.h>

#define ADP_SETUP_CONFIG_FILE "ADPSETUP.CFG"

struct ADPSetupConfig
{
	DDB_ScreenMode videoMode;
	bool soundEnabled;
	uint16_t sbPort;
	uint8_t sbIrq;
	uint8_t sbDMA8;
	uint8_t sbDMA16;
	uint16_t sampleRate;
	uint8_t soundMode;
};

void ADPSetup_Default(ADPSetupConfig* config);
bool ADPSetup_Load(const char* fileName, ADPSetupConfig* config);
bool ADPSetup_Save(const char* fileName, const ADPSetupConfig* config);
bool ADPSetup_IsValid(const ADPSetupConfig* config);
const char* ADPSetup_VideoName(DDB_ScreenMode mode);

#endif
