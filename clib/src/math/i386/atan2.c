/* Math functions for i387.
Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.
This file is part of the GNU C Library.
Contributed by John C. Bowman <bowman@ipp-garching.mpg.de>, 1995.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with the GNU C Library; if not, write to the Free
Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.
*/
#include <math.h>

#ifdef _MSC_VER
#pragma function(atan2)
#pragma warning(disable:4725)
#endif


#ifdef MATH_USE_C
/*
 * @implemented
 */
double atan2 (double __y, double __x)
{
  register double __val;
#ifdef __GNUC__
  __asm __volatile__
    ("fpatan\n\t"
     "fld %%st(0)"
     : "=t" (__val) : "0" (__x), "u" (__y));
#else
  __asm
  {
    fld __y
    fld __x
    fpatan
    fstp __val
  }
#endif /*__GNUC__*/
  return __val;
}
#endif