#ifndef __SB_H__
#define __SB_H__

typedef enum
{
	MODE_STEREO = 0x01,
	MODE_16BITS = 0x02
}
SoundMode;

extern int sbMixFrequency;

extern void SB_Configure(uint16_t port, uint8_t irq, uint8_t dma8, uint8_t dma16);
extern void SB_UseBlasterAutodetect(void);
extern void SB_ConfigureOutput(int mode, int frequency);
extern bool SB_IsConfigured(void);
extern bool SB_InitializeConfigured(void);
extern void SB_GetConfiguration(uint16_t* port, uint8_t* irq, uint8_t* dma8, uint8_t* dma16);
// DSP version detected by the last SB_Init/SB_Detect (0 if none). High byte is
// the major version: <2 = SB 1.x (max ~22 kHz), >=2 = SB 2.0/Pro (~44 kHz),
// >=4 = SB16 (native 44.1 kHz). 0xFFFF means the probe failed.
extern uint16_t SB_GetDetectedVersion(void);
extern void SB_Disable(void);

extern bool SB_Init(int mode, int freq);
extern bool SB_Start(void);
extern void SB_Update(void);
extern void SB_Stop(void);
extern void SB_Exit(void);

#endif
