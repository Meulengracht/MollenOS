/* MollenOS Standard C Library
*
*/

#ifndef LIBC_KERNEL


#include <MollenOS.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <Messaging.h>

int vprintf(const char *format, va_list ap)
{
	//Decl
	int result = 0;
	char *buf = NULL;

	//Sanity
	if(format == NULL)
		return -1;

	//Buffer
	buf = (char*)HeapAlloc(QKEY_TERM_MSG_MAX_LENGTH);
	memset(buf, 0, QKEY_TERM_MSG_MAX_LENGTH);

	//sprintf
	result = vsprintf(buf, format, ap);

	//Send it
	MollenOSPrintString(buf);

	//Cleanup
	HeapFree(buf);

	return result;
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