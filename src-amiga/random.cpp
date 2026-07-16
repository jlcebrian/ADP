#include <os_types.h>

#ifdef _AMIGA

static uint32_t randX = 123456789;
static uint32_t randY = 362436069;

static uint32_t XorShift64()
{
	uint32_t t = (randX^(randX<<10));
	randX = randY;
	return randY = (randY^(randY>>10))^(t^(t>>13));
}

uint32_t RandInt (uint32_t min, uint32_t max)
{
	return min + (XorShift64() % (max - min));
}

void RandSeed (uint32_t seed)
{
	randX = seed | 1;
	randY = 362436069;
}

void RandReset ()
{
	randX = 123456789;
	randY = 362436069;
}

void RandSetDefaultSeed (uint32_t seed)
{
	RandSeed(seed);
}

void RandSeedFromClock (uint32_t entropy)
{
	randX ^= entropy;
	if (randX == 0)
		randX = 1;
}

#endif
