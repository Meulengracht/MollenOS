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
* MollenOS C Library - Standard Library Utilities
*/

#ifndef __STDLIB_INC__
#define __STDLIB_INC__

/* Includes */
#include <crtdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MOS_PAGE_SIZE
#define MOS_PAGE_SIZE	0x1000
#endif

#define _MAX_PATH 512

#define EXIT_FAILURE	-1
#define EXIT_SUCCESS	0

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

//------------------------------------------------------------//
//                     Structures   		                  //
//------------------------------------------------------------//
#ifndef _DIV_T_DEFINED
#define _DIV_T_DEFINED

typedef struct _div_t 
{
	int quot;
	int rem;
} div_t;

typedef struct _ldiv_t
{
	long quot;
	long rem;
} ldiv_t;

#endif

//------------------------------------------------------------//
//                     Type Conversion		                  //
//------------------------------------------------------------//
_CRT_EXTERN int atoi(const char * string);
_CRT_EXTERN double atof(const char* str);
_CRT_EXTERN long int atol(const char * str);
_CRT_EXTERN long double	atold(const char *ascii);

_CRT_EXTERN double strtod(const char* str, char** endptr);
_CRT_EXTERN long int strtol(const char* str, char** endptr, int base);
_CRT_EXTERN unsigned long int strtoul(const char* str, char** endptr, int base);

//------------------------------------------------------------//
//                  Integer Arethmetic		                  //
//------------------------------------------------------------//
#ifndef _CRT_DIV_DEFINED
#define _CRT_DIV_DEFINED
_CRT_EXTERN div_t div(int num, int denom);
_CRT_EXTERN ldiv_t ldiv(long num, long denom);
#endif

#ifndef _CRT_ABS_DEFINED
#define _CRT_ABS_DEFINED
_CRT_EXTERN int abs(int j);
_CRT_EXTERN long labs(long j);
#endif

//------------------------------------------------------------//
//              Pseudo-random number generation               //
//------------------------------------------------------------//
_CRT_EXTERN int	rand(void);
_CRT_EXTERN void srand(unsigned int seed);

//------------------------------------------------------------//
//              Memory Management (malloc, free)              //
//------------------------------------------------------------//

_CRT_EXTERN void *malloc(size_t);
_CRT_EXTERN void *realloc(void *, size_t);
_CRT_EXTERN void *calloc(size_t, size_t);
_CRT_EXTERN void free(void *);

//------------------------------------------------------------//
//                    Sorting Funcs                           //
//------------------------------------------------------------//
_CRT_EXTERN void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int(*compar)(const void *, const void *));
_CRT_EXTERN void qsort(void *base, unsigned num, unsigned width, int(*comp)(const void *, const void *));

//------------------------------------------------------------//
//                    Environmental                           //
//------------------------------------------------------------//
_CRT_EXTERN char *getenv(const char *name);
_CRT_EXTERN void abort(void);
EXTERN int atexit(void(__cdecl *func)(void));
_CRT_EXTERN int at_quick_exit(void(*func)(void));

_CRT_EXTERN void quick_exit(int status);				//Terminate normally, no cleanup. Call all functions in atexit_quick stack
_CRT_EXTERN void _Exit(int status);					//Terminate normally, no cleanup. No calls to anything.

//------------------------------------------------------------//
//                    EXIT		                              //
//------------------------------------------------------------//

_CRT_EXTERN void exit(int status);					//Terminate normally with cleanup, call all functions in atexit stack
#define _exit(s)	exit(s);

#ifdef __cplusplus
}
#endif

#endif
