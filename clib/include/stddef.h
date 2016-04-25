/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS C Library - Standard Definitions - C
*/

#ifndef __STDDEF_INC__
#define __STDDEF_INC__

/* Includes */
#include <stdint.h>
#include <crtdefs.h>
#include <vadefs.h>

/* Define Lib-C standard */
#define __STDC_VERSION__ 199901L

/* PTRDIFFT_T Definition */
typedef signed int ptrdiff_t;

/* Undefine NULL */
#ifdef NULL
#  undef NULL
#endif

/* A null pointer constant.  */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

/* SIZE_T definitions */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#undef size_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef unsigned int size_t __attribute__ ((mode (DI)));
#else
	typedef unsigned long long size_t;
#endif
#else
	typedef unsigned int size_t;
#endif
#endif

#ifndef offsetof
/* Offset of member MEMBER in a struct of type TYPE. */
#if defined(__GNUC__)
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))
#endif

#endif /* !offsetof */

//Structures
typedef struct Value64Bit
{
	uint32_t LowPart;
	uint32_t HighPart;

} val64_t;

typedef struct Value128Bit
{
	uint32_t Part32;
	uint32_t Part64;
	uint32_t Part96;
	uint32_t Part128;

} val128_t;


#endif
