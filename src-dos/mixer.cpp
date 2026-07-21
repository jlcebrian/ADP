#ifdef _DOS

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <i86.h>

#include "sb.h"

uint8_t* mixAudioPtr = NULL;
uint8_t* mixAudioEnd;
int32_t  mixAudioHz;
int      mixVolume = 256;
// 32-bit: the resample accumulator steps by +mixAudioHz / -sbMixFrequency, and
// sbMixFrequency can be 44100 — a 16-bit int would overflow and stretch/garble
// playback at high rates in the 16-bit build.
int32_t  mixCounter = 0;

void MIX_PlaySample (uint8_t* buffer, int32_t samples, int32_t hz, int volume)
{
	_disable();
	mixAudioHz = hz;
	mixVolume = volume;
	mixCounter = 0;
	mixAudioEnd = (uint8_t*)buffer + samples;
	mixAudioPtr = (uint8_t*)buffer;
	_enable();

	// fprintf(stderr, "Playing sample: %d samples, %d Hz, %d volume\n", samples, hz, v);
}

bool MIX_IsPlaying(void)
{
	return mixAudioPtr != NULL;
}

void MIX_Stop(void)
{
	_disable();
	mixAudioPtr = NULL;
	mixAudioEnd = NULL;
	mixCounter = 0;
	_enable();
}

void MIX_StopSampleIfOverlaps(const void* buffer, uint32_t size)
{
	size_t start = (size_t)buffer;
	size_t end = start + size;

	_disable();
	if (mixAudioPtr != NULL && (size_t)mixAudioPtr < end && (size_t)mixAudioEnd > start)
	{
		mixAudioPtr = NULL;
		mixAudioEnd = NULL;
	}
	_enable();
}

void MIX_WriteAudio (uint8_t *stream, int len)
{
	uint8_t* end = stream + len;

	memset(stream, 0x80, len);

	if (mixAudioPtr == NULL)
		return;

	// Poor man's crappy ZOH resample mix I wrote while drunk
	// TODO: do a proper resample/fiter here

	while (1)
	{
		while (mixCounter <= 0)
		{
			*stream++ = 0x80 + (((*mixAudioPtr - 0x80) * mixVolume) >> 8);
			if (stream >= end) return;
			mixCounter += mixAudioHz;
		}
		while (mixCounter > 0)
		{
			mixAudioPtr++;
			if (mixAudioPtr >= mixAudioEnd)
			{
				mixAudioPtr = NULL;
				mixAudioEnd = NULL;
				return;
			}
			mixCounter -= sbMixFrequency;
		}
	}
}

#endif
