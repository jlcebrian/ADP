#ifndef __SB_H__
#define __SB_H__

#include <stdint.h>

typedef enum
{
	MODE_STEREO = 0x01,
	MODE_16BITS = 0x02
}
SoundMode;

// 32-bit: sample rates can exceed 32767 (44100), which would truncate in the
// 16-bit build if this were a plain int.
extern int32_t sbMixFrequency;

extern void SB_Configure(uint16_t port, uint8_t irq, uint8_t dma8);
extern void SB_UseBlasterAutodetect(void);
// Cap the DSP version the driver will use (0xFFFF = as detected). Lets a
// selected card family drive an over-capable card through the older path.
extern void SB_SetMaxVersion(uint16_t maxVersion);
extern void SB_ConfigureOutput(int mode, int32_t frequency);
extern bool SB_IsConfigured(void);
extern bool SB_InitializeConfigured(void);
extern void SB_GetConfiguration(uint16_t* port, uint8_t* irq, uint8_t* dma8);
// DSP version detected by the last SB_Init/SB_Detect (0 if none). High byte is
// the major version: <2 = SB 1.x (max ~22 kHz), >=2 = SB 2.0/Pro (~44 kHz),
// >=4 = SB16 (native 44.1 kHz). 0xFFFF means the probe failed.
extern uint16_t SB_GetDetectedVersion(void);
extern void SB_Disable(void);

extern bool SB_Init(int mode, int32_t freq);
extern bool SB_Start(void);
extern void SB_Update(void);
extern void SB_Stop(void);
extern void SB_Exit(void);

#endif
