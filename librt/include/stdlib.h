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
#include <locale.h>
#include <wchar.h>

/* Termination codes that must be defined in 
 * stdlib.h by the ISO C standard */
#define EXIT_FAILURE    -1
#define EXIT_SUCCESS    0

/* Size_t is also defined here because of 
 * the ISO C standard, it tells us it must be
 * defined in stdlib */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#undef size_t
#if defined(_WIN64) || defined(__x86_64__)
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
#define MB_LEN_MAX    5
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

#ifndef _IMAX_DEFINED
#define _IMAX_DEFINED
typedef struct _imaxdiv_t {
  intmax_t quot;
  intmax_t rem;
} imaxdiv_t;
#endif

_CODE_BEGIN
/* String Conversion functions 
 * Allows the extraction of integer, floats
 * and doubles from strings */
CRTDECL(double,             atof(const char*));
CRTDECL(float,              atoff(const char*));
CRTDECL(int,                atoi(const char*));
CRTDECL(long int,           atol(const char*));

/* C++11 Added functions, to support 
 * 64 bit integers and 80/128 bit doubles */
CRTDECL(long long,          atoll(const char*));
CRTDECL(long double,        atold(const char*));

/* Same as above, but these allow to specify an 
 * endpoint in the string, and allows the specification
 * of a decimal-base to use for the conversion */
CRTDECL(float,              strtof(const char* __restrict, char ** __restrict));
CRTDECL(float,              strtof_l(const char *__restrict, char **__restrict, locale_t));
CRTDECL(double,             strtod(const char* __restrict, char ** __restrict));
CRTDECL(double,             strtod_l(const char *__restrict, char **__restrict, locale_t));
CRTDECL(long int,           strtol(const char* __restrict, char ** __restrict, int));
CRTDECL(long,               strtol_l(const char *__restrict, char **__restrict, int, locale_t));
CRTDECL(unsigned long int,  strtoul(const char* __restrict, char ** __restrict, int));
CRTDECL(unsigned long,      strtoul_l(const char *__restrict, char **__restrict, int, locale_t));

/* C++11 Added functions, to support 64 bit integers and 80/128 bit doubles */
CRTDECL(long long,          strtoll(const char* __restrict, char ** __restrict, int));
CRTDECL(long long,          strtoll_l(const char *__restrict, char **__restrict, int, locale_t));
CRTDECL(long double,        strtold(const char* __restrict, char ** __restrict));
CRTDECL(long double,        strtold_l(const char *__restrict, char **__restrict, locale_t));
CRTDECL(unsigned long long, strtoull(const char* __restrict, char ** __restrict, int));
CRTDECL(unsigned long long, strtoull_l(const char *__restrict, char **__restrict, int, locale_t));
CRTDECL(intmax_t,           strtoimax(const char *__restrict, char **__restrict, int));
CRTDECL(uintmax_t,          strtoumax(const char *__restrict, char **__restrict, int));

/* Pseudo-random sequence generation 
 * The seed is thread-specific and setup by the CRT */
CRTDECL(int,                rand(void));
CRTDECL(void,               srand(unsigned int));

/* Memory management functions 
 * Use to allocate, deallocate and reallocate
 * memory, uses mollenos's virtual allocators */
CRTDECL(void*,              malloc(size_t));
CRTDECL(void*,              realloc(void*, size_t));
CRTDECL(void*,              calloc(size_t, size_t));

/* aligned_alloc
 * Allocate size bytes of uninitialized storage whose alignment is specified by alignment. 
 * The size parameter must be an integral multiple of alignment. */
CRTDECL(void*,              aligned_alloc(size_t alignment, size_t size));
CRTDECL(void,               free(void *ptr));

/* Environment functions, primarily functions
 * related to system env setup and exit functionality */
_CRTIMP_NORETURN(void abort(void));
_CRTIMP char *getenv(__CONST char*);
_CRTIMP int system(__CONST char*);

/* These are the different exit functions, they 
 * all do the same, but have different procedures
 * of doing it */
CRTDECL(int,                at_quick_exit(void(*Function)(void))); // Register quick termination handler
_CRTIMP_NORETURN(void quick_exit(int));             // Quick termination, No cleanup
_CRTIMP_NORETURN(void _Exit(int));                  // No cleanup
CRTDECL(int,                atexit(void(*Function)(void)));        // Register termination handler
_CRTIMP_NORETURN(void exit(int));                   // Normal termination, cleanup
#define _exit(s)    exit(s);

/* Search and sort functions, a custom sorting 
 * comparator function can be provided for the sort */
_CRTIMP void *bsearch(__CONST void *key, __CONST void *base, size_t nmemb, 
    size_t size, int(*compar)(__CONST void *, __CONST void *));
CRTDECL(void,       qsort(void *base, size_t num, size_t width, int(*comp)(const void*, const void*)));

/* Integer Arethmetic functions 
 * Used to do integer divisions and to calculate
 * the absolute integer value of a signed integer */
#ifndef _CRT_DIV_DEFINED
#define _CRT_DIV_DEFINED
CRTDECL(div_t,     div(int n, int denom));
CRTDECL(ldiv_t,    ldiv(long n, long denom));
CRTDECL(lldiv_t,   lldiv(long long n, long long denom));
#endif

/* Abs functions are defined in math header aswell
 * and thus we protect it by a guard */
#ifndef _CRT_ABS_DEFINED
#define _CRT_ABS_DEFINED
CRTDECL(int,       abs(int));
CRTDECL(long,      labs(long));
CRTDECL(long long, llabs(long long));
#endif

/* C++11 Added functions, to support 128 bit integers */
#ifndef _CRT_IMAX_DEFINED
#define _CRT_IMAX_DEFINED
CRTDECL(imaxdiv_t, imaxdiv(intmax_t numer, intmax_t denomer));
CRTDECL(intmax_t,  imaxabs(intmax_t j));
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
CRTDECL(wint_t, btowc (int c));
CRTDECL(int,    wctob (wint_t wc));
CRTDECL(size_t, wcrtomb(char *__restrict, wchar_t, mbstate_t *__restrict));
CRTDECL(size_t, wcsnrtombs(char *__restrict, const wchar_t **__restrict, size_t, size_t, mbstate_t *__restrict));
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
CRTDECL(intmax_t,  wcstoimax(const wchar_t *__restrict, wchar_t **__restrict, int));
CRTDECL(uintmax_t, wcstoumax(const wchar_t *__restrict, wchar_t **__restrict, int));
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


#if __BSD_VISIBLE
CRTDECL(intmax_t,  strtoimax_l(const char *__restrict, char **_restrict, int, locale_t));
CRTDECL(uintmax_t, strtoumax_l(const char *__restrict, char **_restrict, int, locale_t));
CRTDECL(intmax_t,  wcstoimax_l(const wchar_t *__restrict, wchar_t **_restrict, int, locale_t));
CRTDECL(uintmax_t, wcstoumax_l(const wchar_t *__restrict, wchar_t **_restrict, int, locale_t));
#endif

#endif
