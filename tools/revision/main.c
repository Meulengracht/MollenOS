/* Revision Utility 
 * Author: Philip Meulengracht
 * Date: 03-07-17
 * Used as a utility for MollenOS to generate a revision and date */

// Includes
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Prints usage format of this program
static void ShowSyntax(void)
{
	printf("  Syntax:\n\n"
           "    Build            :  revision build <msvc, clang>\n"
           "    Minor Release    :  revision minor <msvc, clang>\n"
           "    Major Release    :  revision major <msvc, clang>\n"
           "\n");
}

// Retrieves the next text-line from a file
int GetNextLine(FILE *handle, char **tokens, int *count)
{
	// Variables
	char buffer[512];
	char character;
	int bufindex = 0;
	int result = 0;
	int tokenindex = 0;

	// Reset
	*count = 0;
	memset(buffer, 0, sizeof(buffer));

	// Iterate entire file content
	while ((character = fgetc(handle)) != EOF) {
		// Skip newlines and line-feed
		if (character == '\r'
			|| character == '\t') {
			continue;
		}

		// Detect end of line
		if (character == '\n') {
			break;
		}

		// Store in temporary buffer
		buffer[bufindex++] = character;
	}

	// Did we reach eof?
	if (character == EOF) {
		result = 1;
	}

	// Get initial token
	char *Token = strtok(buffer, " ");

	// Iterate tokens on line
	while (Token != NULL) {
		memset(tokens[tokenindex], 0, 64);
		strcpy(tokens[tokenindex], Token);

		// Increase index and get next
		tokenindex++;
		Token = strtok(NULL, " ");
	}

	// Update count and return
	*count = tokenindex;
	return result;
}

// Use this to get next revision of the file
int GetRevision(FILE *handle, int *revision, int *minor, int *major)
{
	// Variables
	long fsize = 0;
	char **tokens;
	int tokencount;

	// read file to end
	fseek(handle, 0, SEEK_END);
	fsize = ftell(handle);

	// New file?
	if (fsize == 0) {
		*revision = 0;
		return 1;
	}

	// Otherwise parse file
	rewind(handle);

	// Init token storage
	tokens = (char**)malloc(sizeof(char*) * 24);
	for (int i = 0; i < 24; i++)
		tokens[i] = (char*)malloc(64);

	// Iterate data
	while (1) {
		int result = GetNextLine(handle, tokens, &tokencount);
		if (tokencount >= 3) {
			if (!strcmp(tokens[0], "#define")
				&& !strcmp(tokens[1], "REVISION_BUILD")) {
				*revision = atoi(tokens[2]);
            }
            if (!strcmp(tokens[0], "#define")
                && !strcmp(tokens[1], "REVISION_MINOR")) {
                *minor = atoi(tokens[2]);
            }
            if (!strcmp(tokens[0], "#define")
                && !strcmp(tokens[1], "REVISION_MAJOR")) {
                *major = atoi(tokens[2]);
            }
        }
        
        if (result == 1) {
            break;
        }
	}

	// Cleanup
	for (int i = 0; i < 24; i++) {
		free(tokens[i]);
	}
	free(tokens);

	// Done
	return 1;
}

// main
int main(int argc, char *argv[])
{
	// Variables
	FILE *out = NULL;
	time_t DateTime;
	struct tm *DateTimeInfo;
	char buffer[64];
	int revision = 0, minor = 0, major = 0;

	// Print header
	printf("MollenOS Versioning Utility\n"
           "Copyright 2017 Philip Meulengracht (www.mollenos.com)\n\n");

	// Validate the number of arguments
	// format: revision $(cmd) $(arg)
	if (argc != 3) {
		ShowSyntax();
		return 1;
	}

	// Create the output file
	printf("opening existing file for parsing...\n");
	out = fopen("revision.h", "r");
	if (out == NULL) {
		goto SkipParse;
	}

	// Get the next revision for generation
	printf("extracting revision...\n");
	GetRevision(out, &revision, &minor, &major);

	// Close and cleanup
SkipParse:
	fflush(out);
	fclose(out);

	// Truncate the file
	printf("truncating file...\n");
	out = fopen("revision.h", "w");
	if (out == NULL) {
		printf("%s was an invalid output file\n", argv[2]);
		return 1;
    }

    // Increase revision?
    if (!strcmp(argv[1], "build")) {
        revision++;
    }

    // Increase revision?
    if (!strcmp(argv[1], "minor")) {
        minor++;
    }

    // Increase revision?
    if (!strcmp(argv[1], "major")) {
        major++;
    }

	// Print out header
	printf("generating revision file...\n");
	fprintf(out, "/* Automatically generated revision file, do not change contents.\n");
	fprintf(out, " * Provides a time, date and description of the current build. */\n");
	fprintf(out, "#ifndef _REVISION_H_\n");
	fprintf(out, "#define _REVISION_H_\n\n");

	// Print out time and date
	DateTime = time(NULL);
	DateTimeInfo = localtime(&DateTime);
	strftime(&buffer[0], 64, "%d %B %Y", DateTimeInfo);
	fprintf(out, "#define BUILD_DATE \"%s\"\n", &buffer[0]);
	strftime(&buffer[0], 64, "%H:%M:%S", DateTimeInfo);
	fprintf(out, "#define BUILD_TIME \"%s\"\n\n", &buffer[0]);
	fprintf(out, "#define BUILD_SYSTEM \"%s\"\n", argv[2]);

	// Print out revision
	fprintf(out, "#define REVISION_MAJOR %i\n", major);
	fprintf(out, "#define REVISION_MINOR %i\n", minor);
	fprintf(out, "#define REVISION_BUILD %i\n\n", revision);

	// End
	fprintf(out, "#endif //!_REVISION_H_\n");

	// Close and cleanup
	printf("cleaning up...\n");
	fflush(out);
	return fclose(out);
}
