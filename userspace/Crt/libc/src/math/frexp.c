/* MollenOS Math Library (frexp)
 *
 */
#include <internal/_ieee.h>
#include <math.h>
#include <stddef.h>


double frexp (double __x, int *exptr)
{
	union
	{
		double*		__x;
		double_s*	x;
	} x;

	x.__x = &__x;

	if(exptr != NULL)
		*exptr = x.x->exponent - 0x3FE;

	x.x->exponent = 0x3FE;

	return __x;
}