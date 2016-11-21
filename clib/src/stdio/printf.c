/* MollenOS Standard C Library
 *
 */


#ifndef LIBC_KERNEL
#include <os/MollenOS.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* Allocate a buffer, act a sprintf and send it to CP SYSTEM */
int printf(const char *format, ...)
{ 
	/* Variables */
	va_list Arguments;
	int RetVal;
	char Out[256];

	/* Reset buffer */
	memset(&Out[0], 0, sizeof(Out));

	/* Build buffer */
	va_start(Arguments, format);
	RetVal = vsprintf(&Out[0], format, Arguments);
	va_end(Arguments);

	/* Now spit out data */
	MollenOSSystemLog("%s", &Out[0]);

	/* Done! */
	return RetVal;
}

#else

#include <Arch.h>
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

	/* Do the deed */
	va_start(args, format);
	result = streamout(NULL, 0x20000, format, args);
	va_end(args);

	return result;
}

#endif // !LIBC_KERNEL


