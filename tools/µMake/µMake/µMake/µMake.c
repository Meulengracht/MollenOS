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
#include <process.h>

#include "dirent.h"

/* Definitions */
#define MAX_NUM_TOKENS		24
#define MAX_TOKEN_LENGTH	512

/* Globals */
const char *GlbDefaultMake = "build.um";
const char *GlbDefaultTemp = "Obj";
const char *GlbDefaultOut = "Build";

/* Default Compiler flags */

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
		if (!strncmp(Operator, "=", 1)) {
			
			/* Replace */
			free(Existing);

			/* Allocate a new */
			RetVal = (char*)malloc(strlen(Value) + 1);
			memset(RetVal, 0, strlen(Value) + 1);
			strcpy(RetVal, Value);
		}
		else if (!strncmp(Operator, "+=", 2)) {

			/* Append */
			size_t Length = strlen(Existing) + strlen(Value) + 2;

			/* Allocate a new */
			RetVal = (char*)malloc(Length);
			memset(RetVal, 0, Length);

			/* Build */
			strcpy(RetVal, Existing);
			strcat(RetVal, " ");
			strcat(RetVal, Value);
		}
	}

	/* Done! */
	return RetVal;
}

/* Get default compiler flags */
char *GetDefaultFlags(char *Compiler)
{
	/* Return */
	char *RetStr = NULL;

	/* Clang? */
	if (!strstr(Compiler, "clang++")) 
	{
		/* Std C++ Clang flags with mos as target */
	}
	else if (!strstr(Compiler, "clang")) 
	{
		/* Std C Clang flags with mos as target */
	}
	else if (!strstr(Compiler, "nasm")) 
	{
		/* Std nasm OBJ flags */
		RetStr = (char*)malloc(256);
		strcpy(RetStr, "-f win32 $0 -o $1"); //win32 -Xvc  --- macho & macho64
	}
	
	/* Done! */
	return RetStr;
}

/* Path - Helpers */
const char *GetFileExtension(const char *fName) 
{
	/* Locate last dot */
	const char *Dotp = strrchr(fName, '.');

	/* Sanity */
	if (!Dotp || Dotp == fName)
		return "";

	/* Return pos */
	return Dotp + 1;
}

/* The build function */
int BuildFile(char *Compiler, char *Flags, char *In, char *Out, int Verbose)
{
	/* Sanity */
	if (Compiler == NULL
		|| In == NULL
		|| Out == NULL)
		return;

	/* Compiler flags */
	char *StdFlags = GetDefaultFlags(Compiler);
	int RetVal = 0;

	/* Override flags? */
	if (Flags != NULL)
		StdFlags = Flags;

	/* Verbose? */
	if (Verbose)
		printf("%s %s\n", Compiler, StdFlags == NULL ? "-noflags" : StdFlags);

	/* Build arguments for compiler */
	if (StdFlags == NULL)
		RetVal = spawnl(P_WAIT, Compiler, Compiler, NULL);
	else
		RetVal = spawnl(P_WAIT, Compiler, Compiler, StdFlags, NULL);

	/* Sanity */
	if (RetVal != 0) {
		printf("Returned code %s\n", strerror(errno));
		return RetVal;
	}

	/* Clean build */
	return 0;
}

/* Entry point */
int main(int argc, char* argv[])
{
	/* Default to standard strings */
	char *MakeFile = GlbDefaultMake;
	char **Tokens;
	int TokenCount;

	/* Compiler Variables */
	char *cCompiler = NULL;
	char *cxxCompiler = NULL;
	char *sCompiler = NULL;
	char *Linker = NULL;
	
	/* Flag Variables */
	char *cFlags = NULL;
	char *cxxFlags = NULL;
	char *sFlags = NULL;
	char *lFlags = NULL;

	/* Directory Variables */
	char *TempDir = NULL;
	char *BuildDir = NULL;
	char *Files = NULL;

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

		/* Parse */
		if (TokenCount >= 3)
		{
			/* Comment? */
			if (strncmp(Tokens[0], "#", 1)) 
			{
				/* Otherwise check for buzz-words */
				if (!strcmp(Tokens[0], "C_COMPILER")) {
					cCompiler = SetVariable(cCompiler, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "CXX_COMPILER")) {
					cxxCompiler = SetVariable(cxxCompiler, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "ASM_COMPILER")) {
					sCompiler = SetVariable(sCompiler, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "LD")) {
					Linker = SetVariable(Linker, Tokens[1], Tokens[2]);
				}

				/* Compiler Flags */
				else if (!strcmp(Tokens[0], "C_FLAGS")) {
					cFlags = SetVariable(cFlags, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "CXX_FLAGS")) {
					cxxFlags = SetVariable(cxxFlags, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "ASM_FLAGS")) {
					sFlags = SetVariable(sFlags, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "LD_FLAGS")) {
					lFlags = SetVariable(lFlags, Tokens[1], Tokens[2]);
				}

				/* Directories */
				else if (!strcmp(Tokens[0], "TEMP_OUT")) {
					TempDir = SetVariable(TempDir, Tokens[1], Tokens[2]);
				}

				else if (!strcmp(Tokens[0], "BUILD_DIR")) {
					BuildDir = SetVariable(BuildDir, Tokens[1], Tokens[2]);
				}

				/* Handle "multi" variables */
				else if (!strcmp(Tokens[0], "FILES")) {
					for (int i = 2; i < TokenCount; i++) {
						Files = SetVariable(Files, Tokens[1], Tokens[i]);
					}
				}
			}
		}

		/* Sanity */
		if (RetVal)
			break;
	}

	/* Close */
	fclose(mHandle);

	/* Print */
	if (cCompiler != NULL)
		printf("Active C-Compiler: %s\n", cCompiler);
	if (cxxCompiler != NULL)
		printf("Active CXX-Compiler: %s\n", cxxCompiler);
	if (sCompiler != NULL)
		printf("Active ASM-Compiler: %s\n", sCompiler); 
	if (Linker != NULL)
		printf("Active Linker: %s\n", Linker);
	if (Files != NULL)
		printf("File Paths: %s\n", Files);
	printf("\n");

	/* Sanitize the dirs */
	if (TempDir == NULL)
		TempDir = GlbDefaultTemp;
	if (BuildDir == NULL)
		BuildDir = GlbDefaultOut;

	/* Iterate all files that we can use */
	char fNameBuffer[1024];
	char *Token = strtok(Files, " ");

	/* Iterate */
	while (Token != NULL)
	{
		/* Vars */
		DIR *dHandle;
		dirent *dEntry;

		/* Open dir */
		printf("Accessing path %s\n", Token);
		dHandle = opendir(Token);

		/* Sanity */
		if (dHandle != NULL)
		{
			/* Iterate files in folder */
			while ((dEntry = readdir(dHandle)) != NULL)
			{
				/* Needed so we can get file info */
				struct stat fStats;

				/* Reset buffer */
				memset(fNameBuffer, 0, sizeof(fNameBuffer));

				/* Build Path */
				sprintf(fNameBuffer, "%s/%s", Token, dEntry->d_name);

				/* Get file stats */
				if (stat(fNameBuffer, &fStats) == -1)
					continue;

				/* Skip directories */
				if ((fStats.st_mode & S_IFMT) == S_IFDIR)
					continue;
				
				/* Is the file extension supported? */
				const char *pExtension = GetFileExtension(fNameBuffer);

				/* Select compiler & flags */
				char *pCompiler = NULL;
				char *pFlags = NULL;

				if (!strcmpi(pExtension, "c")) {
					pCompiler = cCompiler;
					pFlags = cFlags;
				}
				else if (!strcmpi(pExtension, "cpp")) {
					pCompiler = cxxCompiler;
					pFlags = cxxFlags;
				}
				else if (!strcmpi(pExtension, "asm")
					|| !strcmpi(pExtension, "s")) {
					pCompiler = sCompiler;
					pFlags = sFlags;
				}

				/* Build */
				if (pCompiler != NULL) {
					if (BuildFile(pCompiler, pFlags, fNameBuffer, TempDir, 1)) {
						printf("Build failed, bailing out");
						goto Cleanup;
					}
				}
					
				printf("File: %s\n", fNameBuffer);
			}

			/* Close */
			closedir(dHandle);
		}
		else
			printf("Failed to access dir %s\n", Token);

		/* Get next */
		Token = strtok(NULL, " ");
	}

	/* Exit */
Cleanup:

	for (;;);

	/* Done! */
	return 0;
}

