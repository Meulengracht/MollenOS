/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS Build System
*/

/* Includes */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Definitions */
#define MAX_NUM_TOKENS		24
#define MAX_TOKEN_LENGTH	256

/* Globals */
const char *GlbDefaultMake = "build.um";

/* Print Help */
void PrintHelp(void)
{
	printf("µMake V0.0.0.1 Help:\n");
	printf("Basic Usage: umake <makefile> [args]\n\n");
	printf("- Commands\n");
	printf("-- <makefile> - OPTIONAL: If not provided it will look for the default name build.um\n\n");
	printf("-- [args] - OPTIONAL, Available arguments:\n");
}


/* Helpers */
int GetNextLine(FILE *fHandle, char(*Tokens)[MAX_TOKEN_LENGTH], int *TokenCount)
{
	/* Temporary buffer */
	char Buffer[2048];
	char Character;
	int BufIndex = 0;
	int RetVal = 0;

	/* Memset */
	memset(Buffer, 0, sizeof(Buffer));

	/* Keep reading */
	while ((Character = fgetc(fHandle)) != EOF)
	{
		/* Skip newline etc etc */
		if (Character == '\n'
			|| Character == '\r'
			|| Character == '\t')
			continue;

		/* EoL? */
		if (Character == ';')
			break;

		/* Store */
		Buffer[BufIndex++] = Character;
	}

	/* Sanity */
	if (Character == EOF)
		RetVal = 1;

	/* Now tokenize it */
	*TokenCount = 0;
	char *Token = strtok(Buffer, " ");

	/* Iterate */
	while (Token != NULL)
	{
		/* Copy it */
		strcpy(Tokens[(*TokenCount)++], Token);

		/* Get next */
		Token = strtok(NULL, " ");
	}

	/* Done */
	return RetVal;
}

/* Entry point */
int main(int argc, char* argv[])
{
	/* Default to standard strings */
	char *MakeFile = GlbDefaultMake;
	char Strings[MAX_NUM_TOKENS][MAX_TOKEN_LENGTH];
	int StringCount;

	/* Parse arguments */
	if (argc != 0)
	{
		/* Iterate */
		for (int i = 0; i < argc; i++)
		{

		}
	}

	/* Parse makefile */
	FILE *mHandle = fopen(MakeFile, "r+");

	/* Iterate lines */
	while (1)
	{
		/* Get Line */
		int RetVal = GetNextLine(mHandle, Strings, &StringCount);

		/* Parse Tokens */
	}

	return 0;
}

