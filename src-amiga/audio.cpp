
#include <os_types.h>
#include <os_lib.h>

#ifdef _AMIGA

#include <exec/exec.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <devices/audio.h>
#include <devices/serial.h>
#include <inline/alib.h>

#include "gcc8_c_support.h"
#include "audio.h"
#include "video.h"

static bool     soundOpen = false;
static bool     playing = false;
static IOAudio  req;

extern void PrintToOutput(const char* msg);

bool OpenAudio()
{
	if (!soundOpen)
	{
		if (SysBase->SoftVer >= 39)
		{
			port = CreateMsgPort();
			if (port == 0)
			{
				DebugPrintf("Error creating audio message port\n");
				return false;
			}

			req = (IOAudio*)CreateIORequest(port, sizeof(IOAudio));
			if (req == 0)
			{
				PrintToOutput("Error creating audio IO request\n");
				return false;
			}
		}
		else
		{
			req = (IOAudio*)AllocMem(sizeof(IOAudio), MEMF_ANY | MEMF_PUBLIC | MEMF_CLEAR);
			if (req == 0)
			{
				PrintToOutput("Error allocating audio IO request\n");
				return false;
			}
		}

		static UBYTE channels[] = {1,2,4,8};
   		req.ioa_Request.io_Command = ADCMD_ALLOCATE;
   		req.ioa_Request.io_Flags = ADIOF_NOWAIT;
   		req.ioa_AllocKey = 0;
   		req.ioa_Data = channels;
   		req.ioa_Length = sizeof(channels);

		if (OpenDevice("audio.device",0,(IORequest *)&req,0) != 0)
		{
			PrintToOutput("Error opening audio.device\n"); //, IoErr());
			return false;
		}

		DebugPrintf("Opened audio.device\n");
		soundOpen = true;
	}

	return soundOpen;
}

void ConvertSample(uint8_t* sample, uint32_t length)
{
	for (uint32_t i = 0; i < length; i++)
	{
		sample[i] ^= 0x80;
	}
}

void PlaySample(uint8_t* sample, uint32_t length, uint32_t hz, uint32_t volume)
{
	if (!soundOpen)
		return;
	StopSample();

	if (volume > 63) volume = 63;

	uint32_t period = (isPAL ? 3546895L : 3579545L) / hz;
	if (period < 124) period = 124;

	req.ioa_Request.io_Command = CMD_WRITE;
	req.ioa_Request.io_Flags = ADIOF_PERVOL;
	req.ioa_Data   = sample;
	req.ioa_Length = length;
	req.ioa_Period = period;
	req.ioa_Volume = volume;
	req.ioa_Cycles = 1;
	BeginIO((struct IORequest *)&req);
	playing = true;

	DebugPrintf("Playing sample at %p: %ld bytes, %ld hz, %ld period, %ld volume\n", sample, length, hz, period, volume);
}

void StopSample()
{
	if (!soundOpen)
		return;

	if (playing)
	{
		if (CheckIO((IORequest *)&req))
		{
			DebugPrintf("Aborting audio playback\n");
			AbortIO((IORequest *)&req);
		}
		playing = false;
	}
}

void CloseAudio()
{
	if (soundOpen)
	{
		StopSample();
		
		if (CheckIO((IORequest *)req))
		{
			AbortIO((IORequest *)req);
			WaitIO((IORequest *)req);
		}

		DebugPrintf("Closing audio.device\n");
		CloseDevice((IORequest *)req);

		if (SysBase->SoftVer < 39)
		{
			FreeMem(req, sizeof(IOAudio));
		}
		else
		{
			DeleteIORequest((IORequest *)req);
			DeleteMsgPort(port);
		}

		soundOpen = false;
	}
}

#endif