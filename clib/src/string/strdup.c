/*
 *     STRING
 *     Creation
 */
#include <string.h>
#include <stdlib.h>

#ifdef LIBC_KERNEL
#include <heap.h>
char *strdup(const char *str)
{
	size_t len = strlen(str);
	char * out = (char*)kmalloc(sizeof(char) * (len+1));

	memcpy(out, str, len+1);

	return out;
}
#else
char *strdup(const char *str) 
{
	size_t len = strlen(str);
	char * out = (char*)malloc(sizeof(char) * (len+1));

	memcpy(out, str, len+1);

	return out;
}
#endif