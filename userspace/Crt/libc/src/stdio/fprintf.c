/* MollenOS fprintf standard implementation
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* This shall not be included for the kernel c library */
#ifndef LIBC_KERNEL

int fprintf(FILE * stream, const char * format, ...)
{
	return 0;
}

#endif // !LIBC_KERNEL