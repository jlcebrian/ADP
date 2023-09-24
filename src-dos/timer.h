#ifndef __TIMER_H__
#define __TIMER_H__

#include <stdint.h>

extern void      Timer_Start           (void);
extern uint32_t  Timer_GetMilliseconds (void);
extern void      Timer_Stop            (void);

#endif