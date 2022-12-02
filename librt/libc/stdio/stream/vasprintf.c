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

#include <internal/_file.h>
#include <internal/_io.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

int vasprintf(
	_In_ char **ret, 
	_In_ const char *format,
	_In_ va_list ap)
{
	char buffer[512];
	int  result;
	FILE stream;

	if(format == NULL || ret == NULL) {
		return -1;
	}

	memset(&buffer[0], 0, 512);
    stream._base = &buffer[0];
    stream._ptr = stream._base;
    stream._charbuf = 0;
    stream._cnt = 512;
    stream._bufsiz = 0;
    stream._flag = _IOSTRG | _IOWRT;
    stream._tmpfname = 0;
    usched_mtx_init(&stream._lock, USCHED_MUTEX_RECURSIVE);

	result = streamout(&stream, format, ap);
	*ret = strdup(&buffer[0]);
	return result;
}
