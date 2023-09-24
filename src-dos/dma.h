#ifndef __DMA_H__
#define __DMA_H__

#include <stdint.h>

#define READ_DMA                0
#define WRITE_DMA               1
#define INDEF_READ              2
#define INDEF_WRITE             3

extern void*    DMA_AllocMem  (uint16_t size);
extern void     DMA_FreeMem   (void *p);
extern int      DMA_Start     (int channel, void *pc_ptr, uint16_t size, int type);
extern void     DMA_Stop      (int channel);
extern uint16_t DMA_Todo      (int channel);

#endif