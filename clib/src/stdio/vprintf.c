/* MollenOS Standard C Library
*
*/

#ifndef LIBC_KERNEL

#include <stdio.h>
#include <stddef.h>
#include <string.h>

int vprintf(const char *format, va_list ap)
{
	return 0;
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