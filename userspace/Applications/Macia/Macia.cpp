/* The Macia Language (MACIA)
 *
 * Copyright 2016, Philip Meulengracht
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
 * Macia - Compiler Suite
 */

/* Includes */
#include <tchar.h>
#include <cstdio>
#include <cstdlib>

#define DIAGNOSE
#define VERSION "0.0.1-dev"
#define AUTHOR	"Philip Meulengracht"

/* Suite Includes */
#include "Lexer/Scanner.h"

int _tmain(int argc, _TCHAR* argv[])
{
	/* Variables we will need
	 * for build */
	Scanner *scrambler = NULL;
	char *fileData = NULL;
	FILE *source = NULL;
	size_t size = 0;
	size_t bread = 0;

	/* So, welcome! */
#ifdef DIAGNOSE
	printf("Macia Compiler %s - 2016 August Build\n", VERSION);
	printf(" - Author: %s\n\n", AUTHOR);
#endif

	/* Step 1. Read file */
	source = fopen("test.mc", "r+b");

	/* Sanity */
	if (source == NULL) {
		printf("Failed to open %s\n", "test.mc");
		return -1;
	}

	/* Get size before allocation of
	 * data array */
	fseek(source, 0, SEEK_END);
	size = ftell(source);
	fseek(source, 0, SEEK_SET);

	/* Allocata a new array */
	fileData = (char*)malloc(size);

	/* Read entire file */
	if ((bread = fread(fileData, 1, size, source)) != size) {
		printf("Failed to read file; read %u, expected %u\n", bread, size);
		goto Cleanup;
	}

	/* Cleanup file handle */
	fclose(source);
	source = NULL;

	/* Create a new scanner */
	scrambler = new Scanner();

#ifdef DIAGNOSE
	printf(" - Scanning (flength = %u)\n", size);
#endif

	/* Parse our file */
	if (scrambler->Scan(fileData, size)) {
		printf("Failed to scramble file\n");
		goto Cleanup;
	}

Cleanup:
#ifdef DIAGNOSE
	printf(" - Cleaning up & exitting\n");
#endif

	/* Cleanup all our stuff */
	if (scrambler != NULL) {
		delete scrambler;
	}

	if (fileData != NULL) {
		free(fileData);
	}

	if (source != NULL) {
		fclose(source);
	}

	for (;;);

	return 0;
}

