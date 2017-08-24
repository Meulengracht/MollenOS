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
_CRTIMP	char* strdup(__CONST char *str);
_CRTIMP char *strndup(__CONST char *str, size_t len);

/*******************************
 *       String Copying        *
 *******************************/
_CRTIMP	void* memcpy(void *destination, __CONST void *source, size_t count);
_CRTIMP	void* memmove(void *destination, __CONST void* source, size_t count);
_CRTIMP	char* strcpy(char *to, __CONST char *from);
_CRTIMP char* strncpy(char* destination, __CONST char* source, size_t num);

/*******************************
 *       String Concenation    *
 *******************************/
_CRTIMP	char* strcat(char* destination, __CONST char* source);
_CRTIMP	char* strncat(char* destination, char* source, size_t num);

/*******************************
 *      String Comparison      *
 *******************************/
_CRTIMP	int memcmp(__CONST void* ptr1, __CONST void* ptr2, size_t num);
_CRTIMP	int strcmp(__CONST char* str1, __CONST char* str2);
_CRTIMP	int strcoll(__CONST char* str1, __CONST char* str2);
_CRTIMP	int strncmp(__CONST char* s1, __CONST char* s2, size_t n);
_CRTIMP	size_t strxfrm(char* destination, __CONST char* source, size_t num);

/*******************************
 *      String S&D             *
 *******************************/
_CRTIMP void* memchr(__CONST void* ptr, int value, size_t num);
_CRTIMP char* strchr(__CONST char* str, int character);
_CRTIMP	size_t strcspn(__CONST char* str1, __CONST char* str2);
_CRTIMP char* strpbrk(__CONST char* str1, __CONST char* str2);
_CRTIMP char* strrchr(__CONST char* str, int character);
_CRTIMP	size_t strspn(__CONST char* str1, __CONST char* str2);
_CRTIMP char* strstr(__CONST char* haystack, __CONST char* needle);
_CRTIMP	char* strtok_r(char* s, __CONST char* delimiters, char** lasts);
_CRTIMP	char* strtok(char* str, __CONST char* delimiters);

/*******************************
 *       String Utility        *
 *******************************/
_CRTIMP	void* memset(void *dest, int c, size_t count);
_CRTIMP	size_t strlen(__CONST char* str);
_CRTIMP size_t strnlen(__CONST char *str, size_t max);

/*******************************
 *     String Conversions      *
 *******************************/
_CRTIMP	char *i64toa(__int64 value, char *string, int radix);
_CRTIMP	int i64toa_s(__int64 value, char *str, size_t size, int radix);
_CRTIMP	char *ui64toa(unsigned __int64 value, char *string, int radix);
_CRTIMP	int ui64toa_s(unsigned __int64 value, char *str, size_t size, int radix);
_CRTIMP	int itoa_s(int value, char *str, size_t size, int radix);
_CRTIMP	char *itoa(int value, char *string, int radix);
_CRTIMP	char *ltoa(long value, char *string, int radix);
_CRTIMP	int ltoa_s(long value, char *str, size_t size, int radix);
_CRTIMP	char *ultoa(unsigned long value, char *string, int radix);

#ifdef __cplusplus
}
#endif

#endif
