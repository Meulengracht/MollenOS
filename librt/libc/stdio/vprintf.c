/* MollenOS Standard C Library
*
*/

#ifndef LIBC_KERNEL
#include <os/utils.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

int vprintf(const char *format, va_list ap)
{
	/* Do a static debug */
	int ReturnVal = 0;
	char Out[256];

	/* Reset buffer */
	memset(&Out[0], 0, sizeof(Out));

	/* Build buffer */
	ReturnVal = vsprintf(&Out[0], format, ap);

	/* Now print it out */
	TRACE("%s", &Out[0]);

	/* Done! */
	return ReturnVal;
}

#else

#include <stdio.h>
#include <stddef.h>
#include <string.h>

int vprintf(const char *format, va_list ap)
{
	//Decl
	int result = 0;

	//Sanity
	if (format == NULL)
		return -1;

	//sprintf
	result = vsprintf(NULL, format, ap);

	return result;
}

#endif // !LIBC_KERNEL