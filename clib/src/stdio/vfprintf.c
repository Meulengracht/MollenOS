/* MollenOS vfprintf standard implementation
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

int vfprintf(FILE *stream, const char *format, va_list ap)
{
	//Step 1. Combine string and arguments
	char *Out = (char*)malloc(256);
	memset(Out, 0, 256);

	//Use sprintf that for this
	vsprintf(Out, format, ap);

	//If we have a valid stream, write to that.
	if(stream != NULL)
		fwrite(Out, strlen(Out), 1, stream);
	else
	{
		//Ok, writing to sys-log
		FILE *temp = fopen("c:/system/mlog.txt", "r+");

		if(temp == NULL)
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