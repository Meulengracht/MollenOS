
#include <internal/_all.h>
#include <math.h>

/*
 * @implemented
 */
int _isnan(double __x)
{
	union
	{
		double*   __x;
		double_s*   x;
	} x;

    x.__x = &__x;
	
	return ( x.x->exponent == 0x7ff  && ( x.x->mantissah != 0 || x.x->mantissal != 0 ));
}

/*
 * @implemented
 */
int _finite(double __x)
{
	union
	{
		double*   __x;
		double_s*   x;
	} x;

	x.__x = &__x;

    return ((x.x->exponent & 0x7ff) != 0x7ff);
}