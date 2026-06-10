#ifndef __MIXER_H__
#define __MIXER_H__

#include <stdint.h>

extern void MIX_PlaySample (uint8_t* buffer, int samples, int hz, int volume);
extern void MIX_StopSampleIfOverlaps(const void* buffer, uint32_t size);
extern void MIX_WriteAudio (uint8_t *stream, int len);

#endif
