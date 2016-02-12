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
int GetNextLine(FILE *fHandle, char **Tokens, int *TokenCount)
{
	/* Temporary buffer */
	char Buffer[2048];
	char Character;
	int BufIndex = 0;
	int RetVal = 0;
	int TokenItr = 0;

	/* Set 0 */
	*TokenCount = 0;

	/* Memset */
	memset(Buffer, 0, sizeof(Buffer));

	/* Keep reading */
	while ((Character = fgetc(fHandle)) != EOF)
	{
		/* Skip newline etc etc */
		if (Character == '\r'
			|| Character == '\t')
			continue;

		/* EoL? */
		if (Character == '\n')
			break;

		/* Store */
		Buffer[BufIndex++] = Character;
	}

	/* Sanity */
	if (Character == EOF)
		RetVal = 1;

	/* Now tokenize it */
	char *Token = strtok(Buffer, " ");

	/* Iterate */
	while (Token != NULL)
	{
		/* Clear line */
		memset(Tokens[TokenItr], 0, MAX_TOKEN_LENGTH);

		/* Copy it */
		strcpy(Tokens[TokenItr], Token);

		/* Increase */
		TokenItr++;

		/* Get next */
		Token = strtok(NULL, " ");
	}

	/* Update */
	*TokenCount = TokenItr;

	/* Done */
	return RetVal;
}

/* Used for setting variables */
char *SetVariable(char *Existing, char *Operator, char *Value)
{
	/* Vars */
	char *RetVal = NULL;

	/* Simple case, if 
	 * existing is null, it's a new variable */
	if (Existing == NULL) {
		
		/* Allocate new */
		RetVal = (char*)malloc(strlen(Value) + 1);
		memset(RetVal, 0, strlen(Value) + 1);
		strcpy(RetVal, Value);
	}
	else
	{
		/* Depends on the operator */
		if (!strcmp(Operator, "=")) {
			
			/* Replace */
			free(Existing);

			/* Allocate a new */
			RetVal = (char*)malloc(strlen(Value) + 1);
			memset(RetVal, 0, strlen(Value) + 1);
			strcpy(RetVal, Value);
		}
		else if (!strcmp(Operator, "+=")) {

			/* Append */
			size_t Length = strlen(Existing) + strlen(Value) + 2;

			/* Allocate a new */
			RetVal = (char*)malloc(Length);
			memset(RetVal, 0, Length);

			/* Build */
			strcpy(RetVal, Existing);
			strcpy(RetVal, " ");
			strcpy(RetVal, Value);
		}
	}

	/* Done! */
	return RetVal;
}

/* Entry point */
int main(int argc, char* argv[])
{
	/* Default to standard strings */
	char *MakeFile = GlbDefaultMake;
	char **Tokens;
	int TokenCount;

	/* State Variables */
	char *cCompiler = NULL;
	char *cxxCompiler = NULL;
	char *sCompiler = NULL;
	char *Linker = NULL;

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

	/* Sanity */
	if (mHandle == NULL) {
		printf("Couldn't find file %s\n", MakeFile);
		return -1;
	}

	/* Setup */
	Tokens = (char**)malloc(sizeof(char*) * MAX_NUM_TOKENS);
	for (int i = 0; i < MAX_NUM_TOKENS; i++)
		Tokens[i] = (char*)malloc(MAX_TOKEN_LENGTH);

	/* Iterate lines */
	while (1)
	{
		/* Get Line */
		int RetVal = GetNextLine(mHandle, Tokens, &TokenCount);

		/* Debug */
		printf("Tokens: %i\n", TokenCount);
		for (int i = 0; i < TokenCount; i++)
			printf("Token: %s\n", Tokens[i]);
		printf("\n");

		/* Parse */
		if (TokenCount != 0)
		{
			/* Comment? */
			if (strncmp(Tokens[0], "#", 1)) 
			{
				/* Otherwise check for buzz-words */
				if (!strcmp(Tokens[0], "C_COMPILER")) {
					cCompiler = SetVariable(cCompiler, Tokens[1], Tokens[2]);
				}

				if (!strcmp(Tokens[0], "CXX_COMPILER")) {
					cxxCompiler = SetVariable(cxxCompiler, Tokens[1], Tokens[2]);
				}

				if (!strcmp(Tokens[0], "ASM_COMPILER")) {
					sCompiler = SetVariable(sCompiler, Tokens[1], Tokens[2]);
				}

				if (!strcmp(Tokens[0], "LD")) {
					Linker = SetVariable(Linker, Tokens[1], Tokens[2]);
				}
			}
		}

		/* Sanity */
		if (RetVal)
			break;
	}

	/* Print */
	if (cCompiler != NULL)
		printf("Active C-Compiler: %s\n", cCompiler);
	if (cxxCompiler != NULL)
		printf("Active CXX-Compiler: %s\n", cxxCompiler);
	if (sCompiler != NULL)
		printf("Active ASM-Compiler: %s\n", sCompiler); 
	if (Linker != NULL)
		printf("Active Linker: %s\n", Linker);

	for (;;);

	/* Done! */
	return 0;
}

