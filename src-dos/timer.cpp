#ifdef _DOS

#include "timer.h"
#include "sb.h"

#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <i86.h>

#define TIMER_INTERRUPT 0x08

static uint32_t timeValue = 0;
static int32_t nextOldTimer = 0;
static bool timerInitialized = false;

static void(__interrupt __far *oldDosTimerInterrupt)();
static void __interrupt __far newCustomTimerInterrupt()
{
	timeValue++;

	nextOldTimer -= 10;
	if (nextOldTimer <= 0)
	{
		nextOldTimer += 182;
		oldDosTimerInterrupt();
		SB_Update();
	}
	else
	{
		outp(0x20, 0x20);
	}
}

uint32_t Timer_GetMilliseconds(void)
{
	return timeValue;
}

void Timer_Start(void)
{
	// The clock we're dealing with here runs at 1.193182mhz, so we
	// just divide 1.193182 by the number of triggers we want per
	// second to get our divisor.
	uint32_t c = 1193181 / (uint32_t)1000;

	// Increment ref count and refuse to init if we're already
	// initialized.
	if (timerInitialized)
		return;

	// Swap out interrupt handlers.
	oldDosTimerInterrupt = _dos_getvect(TIMER_INTERRUPT);
	_dos_setvect(TIMER_INTERRUPT, newCustomTimerInterrupt);

	_disable();

	// There's a ton of options encoded into this one byte I'm going
	// to send to the PIT here so...

	// 0x34 = 0011 0100 in binary.

	// 00  = Select counter 0 (counter divisor)
	// 11  = Command to read/write counter bits (low byte, then high
	//       byte, in sequence).
	// 010 = Mode 2 - rate generator.
	// 0   = Binary counter 16 bits (instead of BCD counter).

	outp(0x43, 0x34);

	// Set divisor low byte.
	outp(0x40, (uint8_t)(c & 0xff));

	// Set divisor high byte.
	outp(0x40, (uint8_t)((c >> 8) & 0xff));

	_enable();

	timerInitialized = true;
}

void Timer_Stop(void)
{
	if (!timerInitialized)
		return;

	_disable();

	// Send the same command we sent in timer_init() just so we can
	// set the timer divisor back.
	outp(0x43, 0x34);

	// FIXME: I guess giving zero here resets it? Not sure about this.
	// Maybe we should save the timer values first.
	outp(0x40, 0);
	outp(0x40, 0);

	_enable();

	// Restore original timer interrupt handler.
	_dos_setvect(TIMER_INTERRUPT, oldDosTimerInterrupt);
}

#endif