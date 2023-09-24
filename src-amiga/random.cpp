#include <os_types.h>

#ifdef _AMIGA

static uint32_t XorShift64()
{
	static uint32_t x = 123456789, y = 362436069;
	uint32_t t = (x^(x<<10)); 
	x = y; 
	return y = (y^(y>>10))^(t^(t>>13));
}

uint32_t RandInt (uint32_t min, uint32_t max)
{
	return min + (XorShift64() % (max - min));
}

#endif