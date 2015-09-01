/* MollenOS
	Standard C String routines

*/

#ifndef __STRING_INC__
#define __STRING_INC__

#include <crtdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * STRING PROTOTYPES, SEE STRING.C FOR IMPLEMENTATION
 */

//Creating
extern		 char* strdup(const char *str);

//Copying
extern		 void* memcpy(void *destination, const void *source, size_t count);
#pragma intrinsic(memcpy)

extern		 void* memmove(void *destination, const void* source, size_t count);
extern		 char* strcpy(char *to, const char *from);
#pragma intrinsic(strcpy)

extern		 char* strncpy(char* destination, const char* source, size_t num); 

//Concenation
extern		 char* strcat(char* destination, const char* source);
#pragma intrinsic(strcat)

extern		 char* strncat(char* destination, char* source, size_t num);

//Comparison
extern		 int memcmp(const void* ptr1, const void* ptr2, size_t num);
#pragma intrinsic(memcmp)

extern		 int strcmp(const char* str1, const char* str2);
extern		 int strcoll(const char* str1, const char* str2);
extern		 int strncmp(const char* s1, const char* s2, size_t n);
extern		 size_t strxfrm(char* destination, const char* source, size_t num); //Something about cutting up strings, transfer NUM chars from source to dest???

//Searching
extern       void* memchr(const void* ptr, int value, size_t num);
extern       char* strchr(const char* str, int character);
extern		 size_t strcspn(const char* str1, const char* str2);
extern       char* strpbrk(const char* str1, const char* str2);
extern       char* strrchr(const char* str, int character); 
extern		 size_t strspn(const char* str1, const char* str2);
extern       char* strstr(const char* haystack, const char* needle);
extern		 char* strtok_r(char* s, const char* delimiters, char** lasts);
extern		 char* strtok(char* str, const char* delimiters);

//Other
extern		void* memset(void *dest, int c, size_t count);
#pragma intrinsic(memset)

extern		size_t strlen(const char* str);
#pragma intrinsic(strlen)

//Conversion
extern		char *i64toa(__int64 value, char *string, int radix);
extern		int i64toa_s(__int64 value, char *str, size_t size, int radix);
extern		char *ui64toa(unsigned __int64 value, char *string, int radix);
extern		int ui64toa_s(unsigned __int64 value, char *str, size_t size, int radix);
extern		int itoa_s(int value, char *str, size_t size, int radix);
extern		char *itoa(int value, char *string, int radix);
extern		char *ltoa(long value, char *string, int radix);
extern		int ltoa_s(long value, char *str, size_t size, int radix);
extern		char *ultoa(unsigned long value, char *string, int radix);

extern		__int64 atoi64(const char *nptr);
extern		long atol(const char *str);

extern		double strtod(const char *s, char **sret);
extern		float strtof(const char *s, char **sret);
extern		long double strtold(const char *s, char **sret);
extern		long strtol(const char *nptr, char **endptr, int base);
extern		__int64 strtoi64(const char *nptr, char **endptr, int base);
extern		unsigned long strtoul(const char *nptr, char **endptr, int base);
extern		unsigned long long strtoull(const char *nptr, char **endptr, int base);


#ifdef __cplusplus
}
#endif

#endif
