#ifdef _DOS

#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <malloc.h>
#include <conio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdbool.h>
#include <conio.h>

#include "dma.h"

/* DMA Controler #1 (8-bit controller) */
#define DMA1_STAT   0x08  /* read status register      */
#define DMA1_WCMD   0x08  /* write command register    */
#define DMA1_WREQ   0x09  /* write request register    */
#define DMA1_SNGL   0x0A  /* write single bit register */
#define DMA1_MODE   0x0B  /* write mode register       */
#define DMA1_CLRFF  0x0C  /* clear byte ptr flip/flop  */
#define DMA1_MCLR   0x0D  /* master clear register     */
#define DMA1_CLRM   0x0E  /* clear mask register       */
#define DMA1_WRTALL 0x0F  /* write all mask register   */

/* DMA Controler #2 (16-bit controller) */
#define DMA2_STAT   0xD0  /* read status register      */
#define DMA2_WCMD   0xD0  /* write command register    */
#define DMA2_WREQ   0xD2  /* write request register    */
#define DMA2_SNGL   0xD4  /* write single bit register */
#define DMA2_MODE   0xD6  /* write mode register       */
#define DMA2_CLRFF  0xD8  /* clear byte ptr flip/flop  */
#define DMA2_MCLR   0xDA  /* master clear register     */
#define DMA2_CLRM   0xDC  /* clear mask register       */
#define DMA2_WRTALL 0xDE  /* write all mask register   */

#define DMA0_ADDR   0x00  /* chan 0 base adddress      */
#define DMA0_CNT    0x01  /* chan 0 base count         */
#define DMA1_ADDR   0x02  /* chan 1 base adddress      */
#define DMA1_CNT    0x03  /* chan 1 base count         */
#define DMA2_ADDR   0x04  /* chan 2 base adddress      */
#define DMA2_CNT    0x05  /* chan 2 base count         */
#define DMA3_ADDR   0x06  /* chan 3 base adddress      */
#define DMA3_CNT    0x07  /* chan 3 base count         */
#define DMA4_ADDR   0xC0  /* chan 4 base adddress      */
#define DMA4_CNT    0xC2  /* chan 4 base count         */
#define DMA5_ADDR   0xC4  /* chan 5 base adddress      */
#define DMA5_CNT    0xC6  /* chan 5 base count         */
#define DMA6_ADDR   0xC8  /* chan 6 base adddress      */
#define DMA6_CNT    0xCA  /* chan 6 base count         */
#define DMA7_ADDR   0xCC  /* chan 7 base adddress      */
#define DMA7_CNT    0xCE  /* chan 7 base count         */

#define DMA0_PAGE 0x87 /* chan 0 page register (refresh)   */
#define DMA1_PAGE 0x83 /* chan 1 page register             */
#define DMA2_PAGE 0x81 /* chan 2 page register             */
#define DMA3_PAGE 0x82 /* chan 3 page register             */
#define DMA4_PAGE 0x8F /* chan 4 page register (unuseable) */
#define DMA5_PAGE 0x8B /* chan 5 page register             */
#define DMA6_PAGE 0x89 /* chan 6 page register             */
#define DMA7_PAGE 0x8A /* chan 7 page register             */

#define MAX_DMA 8

#define DMA_DECREMENT 0x20 /* mask to make DMA hardware go backwards */

typedef struct
{
	uint8_t dma_disable; /* bits to disable dma channel */
	uint8_t dma_enable;	 /* bits to enable dma channel */
	uint16_t page;		 /* page port location */
	uint16_t addr;		 /* addr port location */
	uint16_t count;		 /* count port location */
	uint16_t single;	 /* single mode port location */
	uint16_t mode;		 /* mode port location */
	uint16_t clear_ff;	 /* clear flip-flop port location */
	uint8_t write;		 /* bits for write transfer */
	uint8_t read;		 /* bits for read transfer */
} DMA_ENTRY;

#define ENTER_CRITICAL IRQ_PUSH_OFF()
extern void IRQ_PUSH_OFF(void);
#pragma aux IRQ_PUSH_OFF = \
	"pushfd",              \
	"cli";

#define ENTER_CRITICAL_ON IRQ_PUSH_ON()
extern void IRQ_PUSH_ON(void);
#pragma aux IRQ_PUSH_ON = \
	"pushfd",             \
	"sti";

#define LEAVE_CRITICAL IRQ_POP()
extern void IRQ_POP(void);
#pragma aux IRQ_POP = \
	"popfd";
#define LEAVE_CRITICAL_ON LEAVE_CRITICAL

/* Variables needed ... */

static DMA_ENTRY mydma[MAX_DMA] = {

	/* DMA channel 0 */
	{0x04, 0x00, DMA0_PAGE, DMA0_ADDR, DMA0_CNT,
	 DMA1_SNGL, DMA1_MODE, DMA1_CLRFF, 0x48, 0x44},

	/* DMA channel 1 */
	{0x05, 0x01, DMA1_PAGE, DMA1_ADDR, DMA1_CNT,
	 DMA1_SNGL, DMA1_MODE, DMA1_CLRFF, 0x49, 0x45},

	/* DMA channel 2 */
	{0x06, 0x02, DMA2_PAGE, DMA2_ADDR, DMA2_CNT,
	 DMA1_SNGL, DMA1_MODE, DMA1_CLRFF, 0x4A, 0x46},

	/* DMA channel 3 */
	{0x07, 0x03, DMA3_PAGE, DMA3_ADDR, DMA3_CNT,
	 DMA1_SNGL, DMA1_MODE, DMA1_CLRFF, 0x4B, 0x47},

	/* DMA channel 4 */
	{0x04, 0x00, DMA4_PAGE, DMA4_ADDR, DMA4_CNT,
	 DMA2_SNGL, DMA2_MODE, DMA2_CLRFF, 0x48, 0x44},

	/* DMA channel 5 */
	{0x05, 0x01, DMA5_PAGE, DMA5_ADDR, DMA5_CNT,
	 DMA2_SNGL, DMA2_MODE, DMA2_CLRFF, 0x49, 0x45},

	/* DMA channel 6 */
	{0x06, 0x02, DMA6_PAGE, DMA6_ADDR, DMA6_CNT,
	 DMA2_SNGL, DMA2_MODE, DMA2_CLRFF, 0x4A, 0x46},

	/* DMA channel 7 */
	{0x07, 0x03, DMA7_PAGE, DMA7_ADDR, DMA7_CNT,
	 DMA2_SNGL, DMA2_MODE, DMA2_CLRFF, 0x4B, 0x47},
};

#define LPTR(ptr) (((uint32_t)FP_SEG(ptr) << 4) + FP_OFF(ptr))

static uint16_t dma_selector;

/*
	Allocates a dma buffer of 'size' bytes.
	this watcom dma memory allocation doesn't check if it's
	page-continuous, and can only be used to allocate exactly 1 block

	returns NULL if failed.
*/
void *DMA_AllocMem(uint16_t size)
{
	static union REGS r;
	r.x.eax = 0x0100;			/* DPMI allocate DOS memory */
	r.x.ebx = (size + 15) >> 4; /* Number of paragraphs requested */

	int386(0x31, &r, &r);

	if (r.x.cflag) /* Failed */
		return ((uint32_t)0);

	dma_selector = r.x.edx;

	return (void *)((r.x.eax & 0xFFFF) << 4);
}

void DMA_FreeMem(void *p)
{
	static union REGS r;
	r.x.eax = 0x0101;		/* DPMI free DOS memory */
	r.x.edx = dma_selector; /* base selector */
	int386(0x31, &r, &r);
}

int DMA_Start(int channel, void *pc_ptr, uint16_t size, int type)
{
	DMA_ENTRY *tdma;
	uint32_t s_20bit, e_20bit;
	uint16_t spage, saddr, tcount;
	uint16_t epage, eaddr;
	uint8_t cur_mode;

	tdma = &mydma[channel]; /* point to this dma data */

	/* Convert the pc address to a 20 bit physical
	   address that the DMA controller needs */

	s_20bit = (uint32_t)pc_ptr;

	e_20bit = s_20bit + size - 1;
	spage = s_20bit >> 16;
	epage = e_20bit >> 16;

	if (spage != epage)
		return 0;

	if (channel >= 4)
	{
		/* if 16-bit xfer, then addr,count & size are divided by 2 */
		s_20bit = s_20bit >> 1;
		e_20bit = e_20bit >> 1;
		size = size >> 1;
	}

	saddr = s_20bit & 0xffff;

	tcount = size - 1;

	switch (type)
	{
		case READ_DMA:
			cur_mode = tdma->read;
			break;

		case WRITE_DMA:
			cur_mode = tdma->write;
			break;

		case INDEF_READ:
			cur_mode = tdma->read | 0x10; /* turn on auto init */
			break;

		case INDEF_WRITE:
			cur_mode = tdma->write | 0x10; /* turn on auto init */
			break;
	}

	// ENTER_CRITICAL;
	outp(tdma->single, tdma->dma_disable); /* disable channel */
	outp(tdma->mode, cur_mode);			   /* set mode */
	outp(tdma->clear_ff, 0);			   /* clear f/f */
	outp(tdma->addr, saddr & 0xff);		   /* LSB */
	outp(tdma->addr, saddr >> 8);		   /* MSB */
	outp(tdma->page, spage);			   /* page # */
	outp(tdma->clear_ff, 0);			   /* clear f/f */
	outp(tdma->count, tcount & 0x0ff);	   /* LSB count */
	outp(tdma->count, tcount >> 8);		   /* MSB count */
	outp(tdma->single, tdma->dma_enable);  /* enable */
	// LEAVE_CRITICAL;

	return 1;
}

void DMA_Stop(int channel)
{
	DMA_ENTRY *tdma;
	tdma = &mydma[channel];					   /* point to this dma data */
	outp(tdma->single, tdma->dma_disable); /* disable chan */
}

uint16_t DMA_Todo(int channel)
{
	uint16_t creg;
	uint16_t val1, val2;

	DMA_ENTRY *tdma = &mydma[channel];

	creg = tdma->count;

	ENTER_CRITICAL;

	outp(tdma->clear_ff, 0xff);

redo:
	val1  = inp(creg);
	val1 |= inp(creg) << 8;
	val2  = inp(creg);
	val2 |= inp(creg) << 8;

	val1 -= val2;
	if (val1 > 0x40)
		goto redo;

	LEAVE_CRITICAL;

	if (channel > 3)
		val2 <<= 1;

	return val2;
}

#endif