#ifdef _DOS

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <malloc.h>
#include <conio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <i86.h>

#include "dma.h"
#include "sb.h"
#include "mixer.h"

int      sbSoundMode = 0;
// 32-bit: rates above 32767 Hz (e.g. 44100) must not truncate in the 16-bit
// build, where plain int is 16 bits — that overflow set the wrong SB rate and
// wrecked the mixer resample at 44 kHz.
int32_t  sbMixFrequency = 30000;
// Sized per sample rate in SB_Init to hold a roughly constant amount of time
// (~185ms) so the poll-fill keeps up at every quality: ~8K at 44kHz (4K underran
// / added noise there) down to ~2K at 11kHz (keeps latency and RAM in check).
int      sbDMABufferSize = 4096;
bool     sbAvailable = false;
bool     sbStarted = false;

uint16_t sbInterrupt;     // interrupt vector that belongs to sbIrq
uint16_t sbVersion;       // DSP version number
uint16_t sbPort;          // sb base port
uint8_t  sbIrq;           // sb irq
uint8_t  sbLoDMA;         // 8 bit dma channel (1.0/2.0/pro)
uint8_t  sbHiDMA;         // 16 bit dma channel (16/16asp)
uint8_t  sbDMA;           // current dma channel

uint8_t *sbDMABuffer;
uint8_t  sbTimeConstant;
uint8_t  sbPic1MSK;
uint8_t  sbPic2MSK;

void (interrupt far *oldhandler)(void);

uint16_t sbLast = 0;
uint16_t sbCurr = 0;
static bool sbConfigured = false;
static bool sbAutodetect = false;
static int sbConfiguredMode = 0;
static int32_t sbConfiguredFrequency = 30000;

// Highest DSP version the driver is allowed to use, so a selected card family
// (plain SB / Pro) drives an over-capable card through its own path/rate limit.
// 0xFFFF = use whatever is detected (SB16). See ADPSetup_CardDSPCap.
static uint16_t sbMaxVersion = 0xFFFF;

void SB_SetMaxVersion(uint16_t maxVersion) { sbMaxVersion = maxVersion; }

void SB_Configure(uint16_t port, uint8_t irq, uint8_t dma8)
{
	// 8-bit mono output never uses the 16-bit (high) DMA channel.
	sbPort = port; sbIrq = irq; sbLoDMA = dma8; sbHiDMA = 0xff;
	sbConfigured = true;
}

// Take the card resources from the BLASTER environment variable instead
// of an explicit SB_Configure call (games running without ADPSETUP.CFG)
void SB_UseBlasterAutodetect(void)
{
	sbAutodetect = true;
	sbConfigured = false;
}

void SB_ConfigureOutput(int mode, int32_t frequency) { sbConfiguredMode = mode; sbConfiguredFrequency = frequency; }
bool SB_IsConfigured(void) { return sbConfigured; }
bool SB_InitializeConfigured(void) { return (sbConfigured || sbAutodetect) && SB_Init(sbConfiguredMode, sbConfiguredFrequency); }
void SB_GetConfiguration(uint16_t* port, uint8_t* irq, uint8_t* dma8)
{
	if (port) *port = sbPort;
	if (irq) *irq = sbIrq;
	if (dma8) *dma8 = sbLoDMA;
}

uint16_t SB_GetDetectedVersion(void) { return sbVersion; }

void SB_Disable(void) { sbConfigured = false; sbAutodetect = false; sbAvailable = false; }

#define SB_MIXER_ADDRESS 		(sbPort + 0x04)
#define SB_MIXER_DATA			(sbPort + 0x05)
#define SB_DSP_RESET			(sbPort + 0x06)
#define SB_DSP_READ_DATA		(sbPort + 0x0a)
#define SB_DSP_WRITE_DATA		(sbPort + 0x0c)
#define SB_DSP_WRITE_STATUS		(sbPort + 0x0c)
#define SB_DSP_DATA_AVAIL		(sbPort + 0x0e)

void SB_MixerStereo(void)
{
	outp(SB_MIXER_ADDRESS, 0xe);
	outp(SB_MIXER_DATA, inp(SB_MIXER_DATA)|2);
}

void SB_MixerMono(void)
{
	outp(SB_MIXER_ADDRESS, 0xe);
	outp(SB_MIXER_DATA, inp(SB_MIXER_DATA)&0xfd);
}

bool SB_WaitDSPWrite(void)
{
	uint16_t timeout = 32767;
	while (timeout--)
	{
		if (!(inp(SB_DSP_WRITE_STATUS) & 0x80))
			return 1;
	}
	return 0;
}

bool SB_WaitDSPRead(void)
{
	uint16_t timeout = 32767;
	while (timeout--)
	{
		if (inp(SB_DSP_DATA_AVAIL) & 0x80)
			return 1;
	}
	return 0;
}

bool SB_WriteDSP(uint8_t data)
{
	if (!SB_WaitDSPWrite())
		return 0;
	outp(SB_DSP_WRITE_DATA, data);
	return 1;
}

uint16_t SB_ReadDSP(void)
{
	if (!SB_WaitDSPRead())
		return 0xffff;
	return (inp(SB_DSP_READ_DATA));
}

void SB_SpeakerOn(void)
{
	SB_WriteDSP(0xd1);
}

void SB_SpeakerOff(void)
{
	SB_WriteDSP(0xd3);
}

void SB_ResetDSP(void)
{
	int t;
	outp(SB_DSP_RESET, 1);
	for (t = 0; t < 8; t++)
		inp(SB_DSP_RESET);
	outp(SB_DSP_RESET, 0);
}

bool SB_Ping(void)
{
	SB_ResetDSP();
	return (SB_ReadDSP() == 0xaa);
}

uint16_t SB_GetDSPVersion(void)
{
	uint16_t hi, lo;

	if (!SB_WriteDSP(0xe1))
		return 0xffff;

	hi = SB_ReadDSP();
	lo = SB_ReadDSP();

	return ((hi << 8) | lo);
}

bool SB_Detect(void)
{
	char *envptr, c;
	static char *endptr;

	if (sbConfigured)
	{
		sbInterrupt = (sbIrq > 7) ? sbIrq + 104 : sbIrq + 8;
		if (!SB_Ping()) return false;
		return (sbVersion = SB_GetDSPVersion()) != 0xffff;
	}

	sbPort  = 0xffff;
	sbIrq   = 0xff;
	sbLoDMA = 0xff;
	sbHiDMA = 0xff;

	if ((envptr = getenv("BLASTER")) == NULL)
		return 0;

	while (1)
	{
		do c = *(envptr++); while (c == ' ' || c == '\t');
		if (c == 0)
			break;

		switch (c)
		{
			case 'a':
			case 'A':
				sbPort = strtol(envptr, &endptr, 16);
				break;

			case 'i':
			case 'I':
				sbIrq = strtol(envptr, &endptr, 10);
				break;

			case 'd':
			case 'D':
				sbLoDMA = strtol(envptr, &endptr, 10);
				break;

			case 'h':
			case 'H':
				sbHiDMA = strtol(envptr, &endptr, 10);
				break;

			default:
				strtol(envptr, &endptr, 16);
				break;
		}
		envptr = endptr;
	}

	if (sbPort == 0xffff || sbIrq == 0xff || sbLoDMA == 0xff)
		return 0;

	sbInterrupt = (sbIrq > 7) ? sbIrq + 104 : sbIrq + 8;

	if (!SB_Ping())
		return 0;

	if ((sbVersion = SB_GetDSPVersion()) == 0xffff)
		return 0;

	return 1;
}

bool SB_Init(int mode, int32_t freq)
{
	uint32_t t;

	if (sbStarted) 
		return true;

	sbAvailable = false;

	if (!SB_Detect())
		return false;

	sbSoundMode = mode;
	sbMixFrequency = freq;

	// Cap the detected version to the selected card family: a plain-SB or Pro
	// choice drives an SB16 through the older path (and its lower rate ceiling).
	if (sbVersion > sbMaxVersion)
		sbVersion = sbMaxVersion;

	if (sbVersion < 0x400 && sbSoundMode & MODE_16BITS)
	{
		// DSP versions below 4.00 can't do 16 bit sound.
		sbSoundMode &= ~MODE_16BITS;
	}

	if (sbVersion < 0x300 && sbSoundMode & MODE_STEREO)
	{
		// DSP versions below 3.00 can't do stereo sound.
		sbSoundMode &= ~MODE_STEREO;
	}

	// Use low dma channel for 8 bit, high dma for 16 bit
	sbDMA = (sbSoundMode & MODE_16BITS) ? sbHiDMA : sbLoDMA;

	if (sbVersion < 0x400)
	{
		t = sbMixFrequency;
		if (sbSoundMode & MODE_STEREO)
			t <<= 1;

		sbTimeConstant = 256 - (1000000L / t);

		if (sbVersion < 0x201)
		{
			if (sbTimeConstant > 210)
				sbTimeConstant = 210;
		}
		else
		{
			if (sbTimeConstant > 233)
				sbTimeConstant = 233;
		}

		sbMixFrequency = 1000000L / (256 - sbTimeConstant);
		if (sbSoundMode & MODE_STEREO)
				sbMixFrequency >>= 1;
	}

	// ~185ms of buffer at the final rate (3/16s), rounded to 512 bytes:
	// 8192@44kHz, 5632@30kHz, 4096@22kHz, 2048@11kHz. Clamped for safety
	// (DMA_AllocMem over-allocates 2x and the buffer must fit one 64K page).
	{
		int32_t want = (sbMixFrequency * 3 / 16 + 256) & ~511;
		if (want < 2048)  want = 2048;
		if (want > 16384) want = 16384;
		sbDMABufferSize = (int)want;
	}

	sbDMABuffer = (uint8_t *)DMA_AllocMem(sbDMABufferSize);
	if (sbDMABuffer == NULL)
	{
		// Couldn't allocate page-contiguous dma-buffer
		return false;
	}

	sbAvailable = true;
	return true;
}


void SB_Exit(void)
{
	if (sbDMABuffer != NULL)
		DMA_FreeMem(sbDMABuffer);
	sbDMABuffer = NULL;
	sbAvailable = false;
}

void interrupt newhandler(void)
{
	if (sbVersion < 0x200)
	{
		SB_WriteDSP(0x14);
		SB_WriteDSP(0xff);
		SB_WriteDSP(0xfe);
	}

	if (sbSoundMode & MODE_16BITS)
		inp(sbPort + 0xf);
	else
		inp(SB_DSP_DATA_AVAIL);

	if (sbIrq > 7)
		outp(0xa0, 0x20);
	outp(0x20, 0x20);
}

void SB_Update(void)
{
	uint16_t todo;

	if (!sbAvailable || !sbStarted)
		return;

	sbCurr = (sbDMABufferSize - DMA_Todo(sbDMA)) & 0xfffc;
	if (sbCurr > sbLast)
	{
		todo = sbCurr - sbLast;
		MIX_WriteAudio(&sbDMABuffer[sbLast], todo);
		sbLast += todo;
		if (sbLast >= sbDMABufferSize)
			sbLast = 0;
	}
	else
	{
		todo = sbDMABufferSize - sbLast;
		MIX_WriteAudio(&sbDMABuffer[sbLast], todo);
		MIX_WriteAudio(sbDMABuffer, sbCurr);
		sbLast = sbCurr;
	}
}

bool SB_Start(void)
{
	if (!sbAvailable || sbStarted)
		return sbStarted;
	sbLast = 0;
	sbCurr = 0;

	_disable();

	sbPic1MSK = inp(0x21);
	sbPic2MSK = inp(0xa1);

	if (sbIrq > 7)
	{
		outp(0x21, sbPic1MSK & 0xfb);					// 1111 1011 enable irq 2
		outp(0xa1, sbPic2MSK & ~(1 << (sbIrq - 8)));    // and enable high irq
	}
	else
	{
		// En de SB interrupts toestaan
		outp(0x21, sbPic1MSK & ~(1 << sbIrq));
	}

	oldhandler = _dos_getvect(sbInterrupt);
	_dos_setvect(sbInterrupt, newhandler);

	_enable();

	if (sbVersion >= 0x300 && sbVersion < 0x400)
	{
		if (sbSoundMode & MODE_STEREO)
		{
			SB_MixerStereo();
		}
		else
		{
			SB_MixerMono();
		}
	}

	/* clear the dma buffer to zero (16 bits
	   signed ) or 0x80 (8 bits unsigned) */

	if (sbSoundMode & MODE_16BITS)
		memset(sbDMABuffer, 0, sbDMABufferSize);
	else
		memset(sbDMABuffer, 0x80, sbDMABufferSize);

	if (!DMA_Start(sbDMA, sbDMABuffer, sbDMABufferSize, INDEF_WRITE))
	{
		_disable();
		_dos_setvect(sbInterrupt, oldhandler);
		outp(0x21, sbPic1MSK);
		outp(0xa1, sbPic2MSK);
		_enable();
		return false;
	}

	if (sbVersion < 0x400)
	{
		SB_SpeakerOn();

		SB_WriteDSP(0x40);
		SB_WriteDSP(sbTimeConstant);

		if (sbVersion < 0x200)
		{
			SB_WriteDSP(0x14);
			SB_WriteDSP(0xff);
			SB_WriteDSP(0xfe);
		}
		else if (sbVersion == 0x200)
		{
			SB_WriteDSP(0x48);
			SB_WriteDSP(0xff);
			SB_WriteDSP(0xfe);
			SB_WriteDSP(0x1c);
		}
		else
		{
			SB_WriteDSP(0x48);
			SB_WriteDSP(0xff);
			SB_WriteDSP(0xfe);
			SB_WriteDSP(0x90);
		}
	}
	else
	{
		SB_WriteDSP(0x41);

		SB_WriteDSP(sbMixFrequency >> 8);
		SB_WriteDSP(sbMixFrequency & 0xff);

		if (sbSoundMode & MODE_16BITS)
		{
			SB_WriteDSP(0xb6);
			SB_WriteDSP((sbSoundMode & MODE_STEREO) ? 0x30 : 0x10);
		}
		else
		{
			SB_WriteDSP(0xc6);
			SB_WriteDSP((sbSoundMode & MODE_STEREO) ? 0x20 : 0x00);
		}

		SB_WriteDSP(0xff);
		SB_WriteDSP(0xef);
	}

	sbStarted = true;
	return true;
}

void SB_Stop(void)
{
	if (sbAvailable && sbStarted)
	{
		_disable();
		if (sbIrq > 7)
			outp(0xa1, inp(0xa1) | (1 << (sbIrq - 8)));
		else
			outp(0x21, inp(0x21) | (1 << sbIrq));
		DMA_Stop(sbDMA);
		_dos_setvect(sbInterrupt, oldhandler);
		outp(0x21, sbPic1MSK);
		outp(0xa1, sbPic2MSK);
		_enable();

		SB_SpeakerOff();
		SB_ResetDSP();
		MIX_Stop();
		sbStarted = false;
	}
}

#endif
