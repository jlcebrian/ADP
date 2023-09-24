#ifndef __SB_H__
#define __SB_H__

typedef enum
{
	MODE_STEREO = 0x01,
	MODE_16BITS = 0x02
}
SoundMode;

extern int sbMixFrequency;

extern bool SB_Init(int mode, int freq);
extern void SB_Start(void);
extern void SB_Update(void);
extern void SB_Stop(void);

#endif