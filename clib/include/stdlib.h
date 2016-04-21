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
extern int			atoi(const char * string);
extern long double	atold(const char *ascii);

//------------------------------------------------------------//
//                  Integer Arethmetic		                  //
//------------------------------------------------------------//
extern div_t		div(int num, int denom);
extern ldiv_t		ldiv(long num, long denom);
extern int			abs(int j);
extern long			labs(long j);

//------------------------------------------------------------//
//              Pseudo-random number generation               //
//------------------------------------------------------------//
extern int			rand(void);
extern void			srand(unsigned int seed);

//------------------------------------------------------------//
//              Memory Management (malloc, free)              //
//------------------------------------------------------------//

extern void			*malloc(size_t);
extern void			*realloc(void *, size_t);
extern void			*calloc(size_t, size_t);
extern void			free(void *);

//------------------------------------------------------------//
//                    Sorting Funcs                           //
//------------------------------------------------------------//
extern void			*bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
extern void			qsort(void *base, unsigned num, unsigned width, int (*comp)(const void *, const void *));

//------------------------------------------------------------//
//                    Environmental                           //
//------------------------------------------------------------//
extern void			abort(void);
extern int			atexit(void (*func)(void));
extern int			at_quick_exit(void (*func)(void));

extern void			quick_exit(int status);				//Terminate normally, no cleanup. Call all functions in atexit_quick stack
extern void			_Exit(int status);					//Terminate normally, no cleanup. No calls to anything.

//------------------------------------------------------------//
//                    EXIT		                              //
//------------------------------------------------------------//

extern void			exit (int status);					//Terminate normally with cleanup, call all functions in atexit stack
#define _exit(s)	exit(s);

#ifdef __cplusplus
}
#endif

#endif
