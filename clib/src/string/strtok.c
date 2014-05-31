/* MollenOS
	Implementation of String routines

*/

#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>

/*
 *     STRING
 *     Searching
 */
char* strtok_r(char* s, const char* delimiters, char** lasts)
{
	char *sbegin, *send;
     sbegin = s ? s : *lasts;
     sbegin += strspn(sbegin, delimiters);
     if (*sbegin == '\0') {
         *lasts = "";
         return NULL;
     }
     send = sbegin + strcspn(sbegin, delimiters);
     if (*send != '\0')
         *send++ = '\0';
     *lasts = send;
     return sbegin;
}

char* strtok(char* str, const char* delimiters)
{
	static char *ssave = "";
    return strtok_r(str, delimiters, &ssave);
}