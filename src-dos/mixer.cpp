#ifdef _DOS

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "sb.h"

uint8_t* mixAudioPtr = NULL;
uint8_t* mixAudioEnd;
int      mixAudioHz;
int      mixVolume = 256;
int      mixCounter = 0;

void MIX_PlaySample (uint8_t* buffer, int samples, int hz, int volume)
{
	mixAudioHz = hz;
	mixVolume = volume;
	mixAudioEnd = (uint8_t*)buffer + samples;
	mixAudioPtr = (uint8_t*)buffer;

	// fprintf(stderr, "Playing sample: %d samples, %d Hz, %d volume\n", samples, hz, v);
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
			if (mixAudioPtr >= mixAudioEnd) return;
			mixCounter -= sbMixFrequency;
		}
	}
}

#endif