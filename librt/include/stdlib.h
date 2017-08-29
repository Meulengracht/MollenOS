/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - Standard C Library (Stdlib)
 *  - Contains the implementation of the stdlib interface
 */
#ifndef __STDLIB_INC__
#define __STDLIB_INC__

/* Includes
 * Library */
#include <os/osdefs.h>
#include <wchar.h>

/* Termination codes that must be defined in 
 * stdlib.h by the ISO C standard */
#define EXIT_FAILURE	-1
#define EXIT_SUCCESS	0

/* Size_t is also defined here because of 
 * the ISO C standard, it tells us it must be
 * defined in stdlib */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#undef size_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef unsigned int size_t __attribute__((mode(DI)));
#else
	typedef unsigned long long size_t;
#endif
#define SIZET_MAX 0xffffffffffffffffULL
#else
	typedef unsigned int size_t;
#define SIZET_MAX 0xFFFFFFFF
#endif
#endif

/* NULL is also defined here because of 
 * the ISO C standard, it tells us it must be
 * defined in stdlib */
#ifndef _NULL_DEFINED
#define _NULL_DEFINED
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif
#endif

/* This is the maximum value that can be returned 
 * by the random function, but is guaranteed to be
 * higher than 32767 */
#ifndef RAND_MAX
#define RAND_MAX 65535
#endif

/* This denotes the maximum number of bytes that may
 * be allowed to be used by a single multi-byte character 
 * we have no MB support, so just 1 for now */
#ifndef MB_LEN_MAX
#define MB_LEN_MAX	5
#endif
#define MB_CUR_MAX 10 //__locale_mb_cur_max()

/* Define the division structures that are used 
 * by div, ldiv and lldiv. The mentioned functions
 * return these structures as result */
#ifndef _DIV_T_DEFINED
#define _DIV_T_DEFINED
typedef struct _div_t {
	int quot;
	int rem;
} div_t;

typedef struct _ldiv_t {
	long quot;
	long rem;
} ldiv_t;

typedef struct _lldiv_t {
	long long quot;
	long long rem;
} lldiv_t;
#endif

_CODE_BEGIN
/* String Conversion functions 
 * Allows the extraction of integer, floats
 * and doubles from strings */
_CRTIMP double atof(__CONST char*);
_CRTIMP float atoff(__CONST char*);
_CRTIMP int atoi(__CONST char*);
_CRTIMP long int atol(__CONST char*);

/* C++11 Added functions, to support 
 * 64 bit integers and 80/128 bit doubles */
_CRTIMP long long atoll(__CONST char*);
_CRTIMP long double	atold(__CONST char*);

/* Same as above, but these allow to specify an 
 * endpoint in the string, and allows the specification
 * of a decimal-base to use for the conversion */
_CRTIMP float strtof(__CONST char* __restrict, char ** __restrict end);
_CRTIMP double strtod(__CONST char* __restrict, char ** __restrict end);
_CRTIMP long int strtol(__CONST char* __restrict, char ** __restrict end, int base);
_CRTIMP unsigned long int strtoul(__CONST char* __restrict, char ** __restrict end, int base);

/* C++11 Added functions, to support 
 * 64 bit integers and 80/128 bit doubles */
_CRTIMP long long strtoll(__CONST char* __restrict, char ** __restrict end, int base);
_CRTIMP long double strtold(__CONST char* __restrict, char ** __restrict end);
_CRTIMP unsigned long long strtoull(__CONST char* __restrict, char ** __restrict end, int base);

/* Pseudo-random sequence generation 
 * The seed is thread-specific and setup by the CRT 
 * Use srand to set a custom seed */
_CRTIMP int	rand(void);
_CRTIMP void srand(unsigned int seed);

/* Memory management functions 
 * Use to allocate, deallocate and reallocate
 * memory, uses mollenos's virtual allocators */
_CRTIMP void *malloc(size_t);
_CRTIMP void *realloc(void*, size_t);
_CRTIMP void *calloc(size_t, size_t);
_CRTIMP void free(void*);

/* Environment functions, primarily functions
 * related to system env setup and exit functionality */
_CRTIMP_NORETURN(void abort(void));
_CRTIMP char *getenv(__CONST char*);
_CRTIMP int system(__CONST char*);

/* These are the different exit functions, they 
 * all do the same, but have different procedures
 * of doing it */

/* Terminate normally, no cleanup. 
 * Call all functions in atexit_quick stack */
_CRTIMP int at_quick_exit(void(__CRTDECL *function)(void));
_CRTIMP_NORETURN(void quick_exit(int));

/* Terminate normally, no cleanup. No calls to anything. */
_CRTIMP_NORETURN(void _Exit(int));

/* Terminate normally with cleanup, call all functions in atexit stack */
__EXTERN int atexit(void(__CRTDECL *func)(void));
_CRTIMP_NORETURN(void exit(int));
#define _exit(s)	exit(s);

/* Search and sort functions, a custom sorting 
 * comparator function can be provided for the sort */
_CRTIMP void *bsearch(__CONST void *key, __CONST void *base, size_t nmemb, 
	size_t size, int(*compar)(__CONST void *, __CONST void *));
_CRTIMP void qsort(void *base, unsigned num, unsigned width, 
	int(*comp)(__CONST void *, __CONST void *));

/* Integer Arethmetic functions 
 * Used to do integer divisions and to calculate
 * the absolute integer value of a signed integer */
#ifndef _CRT_DIV_DEFINED
#define _CRT_DIV_DEFINED
_CRTIMP div_t div(int n, int denom);
_CRTIMP ldiv_t ldiv(long n, long denom);
_CRTIMP lldiv_t lldiv(long long n, long long denom);
#endif

/* Abs functions are defined in math header aswell
 * and thus we protect it by a guard */
#ifndef _CRT_ABS_DEFINED
#define _CRT_ABS_DEFINED
_CRTIMP int abs(int);
_CRTIMP long labs(long);
_CRTIMP long long llabs(long long);
#endif

/* Multibyte functions
 * Used for multibyte string support */
_CRTIMP int mblen( 
    __CONST char *s,
	size_t n);
_CRTIMP size_t mbrlen(
	__CONST char *__restrict s, 
	size_t n, 
	mbstate_t *__restrict ps);
_CRTIMP size_t mbrtowc(
	wchar_t *__restrict pwc,
	const char *__restrict s,
	size_t n,
	mbstate_t *__restrict ps);
_CRTIMP size_t mbstowcs(
	wchar_t *__restrict pwcs,
	__CONST char *__restrict s,
	size_t n);
_CRTIMP size_t mbsnrtowcs(
	wchar_t *__restrict dst,
	__CONST char **__restrict src,
	size_t nms,
	size_t len,
	mbstate_t *__restrict ps);
_CRTIMP size_t mbsrtowcs(
	wchar_t *__restrict dst,
	__CONST char **__restrict src,
	size_t len,
	mbstate_t *__restrict ps);

/* Wide functions
 * Used for wide string support */
_CRTIMP wint_t btowc (int c);
_CRTIMP int wctob (wint_t wc);
_CRTIMP size_t wcrtomb(
	char *__restrict s,
	wchar_t wc,
	mbstate_t *__restrict ps);
_CRTIMP size_t wcsnrtombs(
	char *__restrict dst,
	__CONST wchar_t **__restrict src,
	size_t nwc,
	size_t len,
	mbstate_t *__restrict ps);
_CRTIMP size_t wcsrtombs(
	char *__restrict dst,
	__CONST wchar_t **__restrict src,
	size_t len,
	mbstate_t *__restrict ps);
_CRTIMP size_t wcstombs(
    char *__restrict s,
    __CONST wchar_t *__restrict pwcs,
	size_t n);
_CRTIMP int mbtowc(
	wchar_t *__restrict pwc,
	__CONST char *__restrict s,
	size_t n);
_CRTIMP int wctomb(
	char *s,
	wchar_t wchar);

_CRTIMP float wcstof(
	__CONST wchar_t *__restrict nptr,
	wchar_t **__restrict endptr);
_CRTIMP double wcstod(
	__CONST wchar_t *__restrict nptr,
	wchar_t **__restrict endptr);
_CRTIMP long double wcstold(
	__CONST wchar_t *__restrict nptr, 
	wchar_t **__restrict endptr);
_CRTIMP intmax_t wcstoimax(
	__CONST wchar_t* __restrict nptr, 
	wchar_t** __restrict endptr, 
	int base);
_CRTIMP uintmax_t wcstoumax(
	__CONST wchar_t* __restrict nptr, 
	wchar_t** __restrict endptr, 
	int base);
_CRTIMP long wcstol(
	__CONST wchar_t *__restrict s,
	wchar_t **__restrict ptr,
	int base);
_CRTIMP long long wcstoll(
	__CONST wchar_t *__restrict s,
	wchar_t **__restrict ptr,
	int base);
_CRTIMP unsigned long wcstoul(
	__CONST wchar_t *__restrict s,
	wchar_t **__restrict ptr,
	int base);
_CRTIMP unsigned long long wcstoull(
	__CONST wchar_t *__restrict s,
	wchar_t **__restrict ptr,
	int base);
_CODE_END

#endif
