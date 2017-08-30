/*
 Description
 The functions asprintf() and vasprintf() are analogs of 
 sprintf(3) and vsprintf(3), except that they allocate a 
 string large enough to hold the output including the 
 terminating null byte, and return a pointer to it via the 
 first argument. This pointer should be passed to free(3) 
 to release the allocated storage when it is no longer needed.
 
 Return Value
 When successful, these functions return the number of 
 bytes printed, just like sprintf(3). If memory allocation 
 wasn't possible, or some other error occurs, these functions 
 will return -1, and the contents of strp is undefined.
*/
#include <stdio.h>
#include <stddef.h>
#include <string.h>

int asprintf(
	_In_ char **ret, 
	_In_ __CONST char *format, 
	...)
{
	// Hold a temporary buffer
	char Buffer[512];
	int Result;
	FILE Stream;
	va_list argptr;

	// Sanity check parameters
	if(format == NULL || ret == NULL) {
		return -1;
	}

	// Reset stream-buffer
	memset(&Buffer[0], 0, 512);

	// Setup intermediate stream
    Stream._base = &Buffer[0];
    Stream._ptr = Stream._base;
    Stream._charbuf = 0;
    Stream._cnt = 512;
    Stream._bufsiz = 0;
    Stream._flag = _IOSTRG | _IOWRT;
    Stream._tmpfname = 0;

	// Store result
	va_start(argptr, format);
	Result = streamout(&Stream, format, argptr);
	va_end(argptr);

	// Allocate a new copy of the string
	*ret = strdup(&Buffer[0]);
	return Result;
}
