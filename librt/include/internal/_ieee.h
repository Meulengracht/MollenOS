#ifndef __INTERNAL_IEEE_INC__
#define __INTERNAL_IEEE_INC__

typedef struct 
{
	unsigned int mantissa : 23;
	unsigned int exponent : 8;
	unsigned int sign : 1;
} float_s;

typedef struct 
{
	unsigned int mantissal : 32;
	unsigned int mantissah : 20;
	unsigned int exponent : 11;
	unsigned int sign : 1;
} double_s;

typedef struct 
{
	unsigned int mantissal : 32;
	unsigned int mantissah : 32;
	unsigned int exponent : 15;
	unsigned int sign : 1;
	unsigned int empty : 16;
} long_double_s;

#endif