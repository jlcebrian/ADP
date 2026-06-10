#pragma once

#include <os_types.h>

extern bool OpenAudio();
extern void PlaySample(uint8_t* sample, uint32_t length, uint32_t hz, uint32_t volume);
extern void StopSample();
extern void StopSampleIfOverlaps(const void* buffer, uint32_t length);
extern void CloseAudio();
extern void ConvertSample(uint8_t* sample, uint32_t length);
