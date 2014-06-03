/* MollenOS Standard C Library
 *
 */


#ifndef LIBC_KERNEL

#include <MollenOS.h>
#include <Messaging.h>
#include <MIO.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Allocate a buffer, act a sprintf and send it to CP SYSTEM */
int printf(const char *format, ...)
{
	//Decl
	va_list args;
	int result = 0;
	char *buf;

	//Sanity
	if (format == NULL)
		return -1;

	//Buffer
	buf = (char*)HeapAlloc(QKEY_TERM_MSG_MAX_LENGTH);
	memset(buf, 0, QKEY_TERM_MSG_MAX_LENGTH);

	//Use sprintf for this
	va_start(args, format);
	result = vsprintf(buf, format, args);
	va_end(args);

	//Send it
	MollenOSPrintString(buf);

	//Cleanup
	HeapFree(buf);

	return result;
}

#else

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Directly call */
extern int streamout(char **out, size_t size, const char *format, va_list argptr);

/* However in kernel mode, we just print the damned string */
int printf(const char *format, ...)
{
	//Decl
	va_list args;
	int result = 0;

	//Sanity
	if (format == NULL)
		return -1;

	//Use sprintf for this
	va_start(args, format);
	result = streamout(NULL, 0x20000, format, args);
	va_end(args);

	return result;
}

#endif // !LIBC_KERNEL


