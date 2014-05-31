#include <stdlib.h>

/*
	Pseudo Random Number generator
*/

static unsigned long int next = 1;

int __cdecl rand(void)
{
	int nseed = (int)((next = next * 214013L + 2531011L) >> 16) & 0x7fff;
	return nseed;
}

void __cdecl srand(unsigned int seed)
{
	next = seed;
}