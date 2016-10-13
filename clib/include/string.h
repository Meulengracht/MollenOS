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
* MollenOS C Library - String Library
*/

#ifndef __STRING_INC__
#define __STRING_INC__

/* Includes */
#include <crtdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*******************************
 *       String Creation       *
 *******************************/
_CRT_EXTERN	char* strdup(const char *str);
_CRT_EXTERN char *strndup(const char *str, size_t len);

/*******************************
 *       String Copying        *
 *******************************/
_CRT_EXTERN	void* memcpy(void *destination, const void *source, size_t count);
_CRT_EXTERN	void* memmove(void *destination, const void* source, size_t count);
_CRT_EXTERN	char* strcpy(char *to, const char *from);
_CRT_EXTERN char* strncpy(char* destination, const char* source, size_t num);

/*******************************
 *       String Concenation    *
 *******************************/
_CRT_EXTERN	char* strcat(char* destination, const char* source);
_CRT_EXTERN	char* strncat(char* destination, char* source, size_t num);

/*******************************
 *      String Comparison      *
 *******************************/
_CRT_EXTERN	int memcmp(const void* ptr1, const void* ptr2, size_t num);
_CRT_EXTERN	int strcmp(const char* str1, const char* str2);
_CRT_EXTERN	int strcoll(const char* str1, const char* str2);
_CRT_EXTERN	int strncmp(const char* s1, const char* s2, size_t n);
_CRT_EXTERN	size_t strxfrm(char* destination, const char* source, size_t num);

/*******************************
 *      String S&D             *
 *******************************/
_CRT_EXTERN void* memchr(const void* ptr, int value, size_t num);
_CRT_EXTERN char* strchr(const char* str, int character);
_CRT_EXTERN	size_t strcspn(const char* str1, const char* str2);
_CRT_EXTERN char* strpbrk(const char* str1, const char* str2);
_CRT_EXTERN char* strrchr(const char* str, int character);
_CRT_EXTERN	size_t strspn(const char* str1, const char* str2);
_CRT_EXTERN char* strstr(const char* haystack, const char* needle);
_CRT_EXTERN	char* strtok_r(char* s, const char* delimiters, char** lasts);
_CRT_EXTERN	char* strtok(char* str, const char* delimiters);

/*******************************
 *       String Utility        *
 *******************************/
_CRT_EXTERN	void* memset(void *dest, int c, size_t count);
_CRT_EXTERN	size_t strlen(const char* str);
_CRT_EXTERN size_t strnlen(const char *str, size_t max);

/*******************************
 *     String Conversions      *
 *******************************/
_CRT_EXTERN	char *i64toa(__int64 value, char *string, int radix);
_CRT_EXTERN	int i64toa_s(__int64 value, char *str, size_t size, int radix);
_CRT_EXTERN	char *ui64toa(unsigned __int64 value, char *string, int radix);
_CRT_EXTERN	int ui64toa_s(unsigned __int64 value, char *str, size_t size, int radix);
_CRT_EXTERN	int itoa_s(int value, char *str, size_t size, int radix);
_CRT_EXTERN	char *itoa(int value, char *string, int radix);
_CRT_EXTERN	char *ltoa(long value, char *string, int radix);
_CRT_EXTERN	int ltoa_s(long value, char *str, size_t size, int radix);
_CRT_EXTERN	char *ultoa(unsigned long value, char *string, int radix);

_CRT_EXTERN	__int64 atoi64(const char *nptr);
_CRT_EXTERN	long atol(const char *str);

_CRT_EXTERN	double strtod(const char *s, char **sret);
_CRT_EXTERN	float strtof(const char *s, char **sret);
_CRT_EXTERN	long double strtold(const char *s, char **sret);
_CRT_EXTERN	__int64 strtoi64(const char *nptr, char **endptr, int base);

_CRT_EXTERN long strtol(const char *nptr, char **endptr, int base);
_CRT_EXTERN	unsigned long strtoul(const char *nptr, char **endptr, int base);
_CRT_EXTERN	long long strtoll(const char *nptr, char **endptr, int base);
_CRT_EXTERN	unsigned long long strtoull(const char *nptr, char **endptr, int base);


/*******************************
 *       WString Utility       *
 *******************************/
_CRT_EXTERN size_t wcsnlen(const wchar_t * str, size_t count);


#ifdef __cplusplus
}
#endif

#endif
