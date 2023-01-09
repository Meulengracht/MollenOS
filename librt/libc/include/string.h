/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __STDC_STRING__
#define __STDC_STRING__

// list of types exposed in string.h
#define __need_size_t
#define __need_NULL
#define __need_STDDEF_H_misc

#include <crtdefs.h>
#include <stddef.h>
#include <locale.h>

_CODE_BEGIN
CRTDECL(char*, strdup(const char *str));
CRTDECL(char*, strndup(const char *str, size_t len));

/* memcpy
 * Copies count characters from the object pointed to by src to the object pointed to by dest. 
 * Both objects are interpreted as arrays of unsigned char. */
CRTDECL(void*, memcpy(
    _In_ void*       destination,
    _In_ const void* source,
    _In_ size_t      count));

/* memmove 
 * Copies count characters from the object pointed to by src to the object pointed to by dest. 
 * Both objects are interpreted as arrays of unsigned char. The objects may overlap: 
 * copying takes place as if the characters were copied to a temporary character array and 
 * then the characters were copied from the array to dest. */ 
CRTDECL(void*, memmove(
    _In_ void *destination,
    _In_ const void* source,
    _In_ size_t count));

/* memmove_s
 * Same as (1), except when detecting the following errors at runtime, it zeroes out the 
 * entire destination range [dest, dest+destsz) (if both dest and destsz are valid) and 
 * calls the currently installed constraint handler function:
 * 1. dest or src is a null pointer
 * 2. destsz or count is greater than RSIZE_MAX
 * 3. count is greater than destsz (buffer overflow would occur) */
CRTDECL_EX(errno_t, memmove_s(
    _In_ void *dest,
    _In_ rsize_t destsz,
    _In_ const void *src,
    _In_ rsize_t count))

/* strcpy
 * Copies the null-terminated byte string pointed to by src, including the null terminator, 
 * to the character array whose first element is pointed to by dest. */
CRTDECL(char*, strcpy(
    _In_ char *to,
    _In_ const char *from));

/* strcpy_s 
 * Same as (1), except that it may clobber the rest of the destination array with unspecified 
 * values and that the following errors are detected at runtime and call the currently installed 
 * constraint handler function:
 * 1. src or dest is a null pointer
 * 2. destsz is zero or greater than RSIZE_MAX
 * 3. destsz is less or equal strnlen_s(src, destsz); in other words, truncation would occur
 * 4. overlap would occur between the source and the destination strings */
CRTDECL_EX(errno_t, strcpy_s(
    _In_ char *restrict dest,
    _In_ rsize_t destsz,
    _In_ const char *restrict src))

/* strncpy 
 * Copies at most count characters of the character array pointed to by src 
 * (including the terminating null character, but not any of the characters that follow 
 * the null character) to character array pointed to by dest. */
CRTDECL(char*, strncpy(
    _In_ char*       destination,
    _In_ const char* source,
    _In_ size_t      num));

/* strncpy_s 
 * Same as (1), except that the function does not continue writing zeroes into the destination 
 * array to pad up to count, it stops after writing the terminating null character 
 * (if there was no null in the source, it writes one at dest[count] and then stops). 
 * Also, the following errors are detected at runtime and call the currently installed 
 * constraint handler function:
 * 1. src or dest is a null pointer
 * 2. destsz or count is zero or greater than RSIZE_MAX
 * 3. count is greater or equal destsz, but destsz is less or equal strnlen_s(src, count), in other words, truncation would occur
 * 4. overlap would occur between the source and the destination strings*/
CRTDECL_EX(errno_t, strncpy_s(
    _In_ char *restrict dest,
    _In_ rsize_t        destsz,
    _In_ const char*    restrict src,
    _In_ rsize_t        count))

/*******************************
 *       String Concenation    *
 *******************************/
CRTDECL(char*, strcat(char* destination, const char* source));
CRTDECL(char*, strncat(char* destination, const char* source, size_t num));

/*******************************
 *      String Comparison      *
 *******************************/
CRTDECL(int, memcmp(const void* ptr1, const void* ptr2, size_t num));
CRTDECL(int, strcmp(const char* str1, const char* str2));
CRTDECL(int, strcoll(const char* str1, const char* str2));
CRTDECL(int, strcoll_l(const char *a, const char *b, struct __locale_t *locale));
CRTDECL(int, strncmp(const char* s1, const char* s2, size_t n));
CRTDECL(size_t, strxfrm(char* destination, const char* source, size_t num));
CRTDECL(size_t, strxfrm_l(char *__restrict s1, const char *__restrict s2, size_t n, struct __locale_t *locale));

/*******************************
 *      String S&D             *
 *******************************/
CRTDECL(void*,  memchr(const void* ptr, int value, size_t num));
CRTDECL(char*,  strchr(const char* str, int character));
CRTDECL(size_t, strcspn(const char* str1, const char* str2));
CRTDECL(char*,  strpbrk(const char* str1, const char* str2));
CRTDECL(char*,  strrchr(const char* str, int character));
CRTDECL(size_t, strspn(const char* str1, const char* str2));
CRTDECL(char*,  strstr(const char* haystack, const char* needle));
CRTDECL(char*,  strtok_r(char* s, const char* delimiters, char** lasts));
CRTDECL(char*,  strtok(char* str, const char* delimiters));

/* strerror
 * Returns a pointer to the textual description of the system error code errnum, 
 * identical to the description that would be printed by perror(). */
CRTDECL(char*, strerror(int errnum));

/* strerror_s 
 * Same as (1), except that the message is copied into user-provided storage buf. 
 * No more than bufsz-1 bytes are written, the buffer is always null-terminated. 
 * If the message had to be truncated to fit the buffer and bufsz is greater than 3, 
 * then only bufsz-4 bytes are written, and the characters "..." are appended before the n
 * ull terminator. In addition, the following errors are detected at runtime and call the 
 * currently installed constraint handler function:
 * 1. buf is a null pointer
 * 2. bufsz is zero or greater than RSIZE_MAX */
CRTDECL_EX(errno_t, strerror_s(char *buf, rsize_t bufsz, errno_t errnum))

/* strerrorlen_s 
 * Computes the length of the untruncated locale-specific error message that strerror_s 
 * would write if it were called with errnum. The length does not include the null terminator. */
CRTDECL_EX(size_t, strerrorlen_s(errno_t errnum))

CRTDECL(void*,  memset(void *dest, int c, size_t count));
CRTDECL(size_t, strlen(const char* str));
CRTDECL(size_t, strnlen(const char *str, size_t max));

/*******************************
 *     String Conversions      *
 *******************************/
CRTDECL(char*, i64toa(__int64 value, char *string, int radix));
CRTDECL(int,   i64toa_s(__int64 value, char *str, size_t size, int radix));
CRTDECL(char*, ui64toa(unsigned __int64 value, char *string, int radix));
CRTDECL(int,   ui64toa_s(unsigned __int64 value, char *str, size_t size, int radix));
CRTDECL(int,   itoa_s(int value, char *str, size_t size, int radix));
CRTDECL(char*, itoa(int value, char *string, int radix));
CRTDECL(char*, ltoa(long value, char *string, int radix));
CRTDECL(int,   ltoa_s(long value, char *str, size_t size, int radix));
CRTDECL(char*, ultoa(unsigned long value, char *string, int radix));

_CODE_END
#endif //!__STDC_STRING__
