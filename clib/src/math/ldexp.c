/* MollenOS Math Library (ldexp)
 *
 */
#include <internal/_ieee.h>
#include <math.h>
#include <stddef.h>
#include <errno.h>

double ldexp (double value, int exp)
{
    register double result;
#ifndef __GNUC__
    register double __dy = (double)exp;
#endif

    /* Check for value correctness
     * and set errno if required
     */
    if (_isnan(value))
    {
        errno = EDOM;
    }

#ifdef __GNUC__
#if defined(__clang__)
    asm ("fild %[exp]\n"
         "fscale\n"
         "fstp %%st(1)\n"
         : [result] "=t" (result)
         : [value] "0" (value), [exp] "m" (exp));
#else
    asm ("fscale"
         : "=t" (result)
         : "0" (value), "u" ((double)exp)
         : "1");
#endif
#else /* !__GNUC__ */
    __asm
    {
        fld __dy
        fld value
        fscale
        fstp result
    }
#endif /* !__GNUC__ */
    return result;
}

