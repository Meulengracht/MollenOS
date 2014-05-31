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
	//Step 1. Combine string and arguments
	va_list args;
	char *Out = (char*)malloc(256);
	memset(Out, 0, 256);

	//Use sprintf that for this
	va_start(args, format);
	vsprintf(Out, format, args);
	va_end(args);

	//If we have a valid stream, write to that.
	if (stream != NULL)
		fwrite(Out, strlen(Out), 1, stream);
	else
	{
		//Ok, writing to sys-log
		FILE *temp = fopen("c:/system/mlog.txt", "r+");

		if (temp == NULL)
			return 1;

		//Write data
		fwrite(Out, strlen(Out), 1, temp);

		//Cleanup
		fclose(temp);
	}

	//Cleanup
	free(Out);
	return 0;
}

#endif // !LIBC_KERNEL