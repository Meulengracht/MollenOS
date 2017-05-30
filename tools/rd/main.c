/* Ramdisk Builder Utility 
 * Author: Philip Meulengracht
 * Date: 30-05-17
 * Used as a utility for MollenOS to build the initial ramdisk */

// Includes
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>

// Header
#pragma pack(push, 1)
struct {
	uint32_t Magic;
	uint32_t Version;
	uint32_t Architecture;
	int32_t FileCount;
} RdHeaderStatic = {
	0x3144524D,
	0x00000001,
	0, 0
};
#pragma pack(pop)

// Prints usage format of this program
static void ShowSyntax(void)
{
	printf("  Syntax:\n\n"
           "    Build    :  rd <arch> <output>\n\n");
}

// Determines if a file has a corresponding driver descriptor
static FILE *GetDriver(const char *path)
{
	FILE *drv = NULL;
	char *copy = malloc(strlen(path) + 4);
	memset(copy, 0, strlen(path) + 4);
	memcpy(copy, path, strlen(path));
	char *dot = strrchr(copy, '.');
	if (dot != NULL) {
		memcpy(dot, ".mdrv", 5);
		drv = fopen(copy, "r");
		free(copy);
		return drv;
	}
	else {
		return NULL;
	}
}

// Removes the file-extension from a file-name
char *RemoveExtension(char* mystr, char dot, char sep) {
	char *retstr, *lastdot, *lastsep;

	// Error checks and allocate string.

	if (mystr == NULL)
		return NULL;
	if ((retstr = malloc(strlen(mystr) + 1)) == NULL)
		return NULL;

	// Make a copy and find the relevant characters.

	strcpy(retstr, mystr);
	lastdot = strrchr(retstr, dot);
	lastsep = (sep == 0) ? NULL : strrchr(retstr, sep);

	// If it has an extension separator.

	if (lastdot != NULL) {
		// and it's before the extenstion separator.

		if (lastsep != NULL) {
			if (lastsep < lastdot) {
				// then remove it.

				*lastdot = '\0';
			}
		}
		else {
			// Has extension separator with no path separator.

			*lastdot = '\0';
		}
	}

	// Return the modified string.

	return retstr;
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

// main
int main(int argc, char *argv[])
{
	// Variables
	struct dirent *dp = NULL;
	char filename_qfd[100];
	char *dataptr = NULL;
	FILE *out = NULL;
	DIR *dfd = NULL;
	long fentrypos = 0;
	long fdatapos = 0;
	char **tokens;
	int tokencount;

	// Print header
	printf("MollenOS Ramdisk Builder\n"
           "Copyright 2017 Philip Meulengracht (www.mollenos.com)\n\n");

	// Validate the number of arguments
	// format: rd $(arch) $(out)
	if (argc != 3) {
		ShowSyntax();
		return 1;
	}

	// Create the output file
	out = fopen(argv[2], "wb+");
	if (out == NULL) {
		printf("%s was an invalid output file\n", argv[2]);
		return 1;
	}

	// Fill in architecture
	// Arch - x86_32 = 0x08, x86_64 = 0x10
	RdHeaderStatic.Architecture = 0x08;
	RdHeaderStatic.FileCount = 0;

	// Open directory
	if ((dfd = opendir("initrd")) == NULL) {
		fprintf(stderr, "Can't open initrd folder\n");
		return 1;
	}

	// Count files of type .dll
	while ((dp = readdir(dfd)) != NULL) {
		char *dot = strrchr(dp->d_name, '.');
		if (dot && !strcmp(dot, ".dll")) {
			RdHeaderStatic.FileCount++;
		}
	}

	// Rewind
	rewinddir(dfd);

	// Write header
	fwrite(&RdHeaderStatic, 1, sizeof(RdHeaderStatic), out);
	fflush(out);

	// Store current position
	fentrypos = ftell(out);

	// Fill rest of entry space with 0
	dataptr = malloc(0x1000 - sizeof(RdHeaderStatic));
	memset(dataptr, 0, 0x1000 - sizeof(RdHeaderStatic));
	fwrite(dataptr, 1, 0x1000 - sizeof(RdHeaderStatic), out);
	free(dataptr);
	fflush(out);
	fdatapos = ftell(out);

	// Init token storage
	tokens = (char**)malloc(sizeof(char*) * 24);
	for (int i = 0; i < 24; i++)
		tokens[i] = (char*)malloc(64);

	// Iterate entries
	while ((dp = readdir(dfd)) != NULL) {
		struct stat stbuf;

		// Build path string
		sprintf(filename_qfd, "initrd/%s", dp->d_name);
		if (stat(filename_qfd, &stbuf) == -1)
		{
			printf("Unable to stat file: %s\n", filename_qfd);
			continue;
		}

		// Skip directories
		if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
			continue;
		}
		else {
			char *dot = strrchr(dp->d_name, '.');
			if (!dot || strcmp(dot, ".dll")) {
				continue;
			}

			// Is it a driver? check if file exists with .drvm extension
			FILE *drvdata = GetDriver(&filename_qfd[0]);
			FILE *entry = NULL;
			char *name = NULL;
			uint32_t type = drvdata == NULL ? 0x1 : 0x4;
			uint32_t vendorid = 0;
			uint32_t deviceid = 0;
			uint32_t dclass = 0;
			uint32_t dsubclass = 0;
			uint32_t dflags = 0;
			long fsize = 0;

			// Debug print
			if (drvdata != NULL) {
				printf("writing %s to rd (driver)\n", dp->d_name);
			}
			else {
				printf("writing %s to rd (file)\n", dp->d_name);
			}

			// Seek to entry pos
			fseek(out, fentrypos, SEEK_SET);

			// Write header data
			fwrite(dp->d_name, 1, strlen(dp->d_name), out);
			fwrite(&type, 4, 1, out);
			fwrite(&fdatapos, 4, 1, out);
			fflush(out);

			// Update entry
			fentrypos = ftell(out);

			// Seek to data
			fseek(out, fdatapos, SEEK_SET);

			// Write header, then file data
			name = RemoveExtension(dp->d_name, '.', '/');
			
			// Write name
			fwrite(name, 1, strlen(name), out);
			free(name);

			// Load driver data?
			if (drvdata != NULL) {
				while (1) {
					int result = GetNextLine(drvdata, tokens, &tokencount);
					if (tokencount >= 3) {
						// Skip comments
						if (strncmp(tokens[0], "#", 1)) {
							if (!strcmp(tokens[0], "VendorId")) {
								vendorid = (uint32_t)strtol(tokens[2], NULL, 16);
							}
							if (!strcmp(tokens[0], "DeviceId")) {
								deviceid = (uint32_t)strtol(tokens[2], NULL, 16);
							}
							if (!strcmp(tokens[0], "Class")) {
								dclass = (uint32_t)strtol(tokens[2], NULL, 16);
							}
							if (!strcmp(tokens[0], "SubClass")) {
								dsubclass = (uint32_t)strtol(tokens[2], NULL, 16);
							}
							if (!strcmp(tokens[0], "Flags")) {
								dflags = (uint32_t)strtol(tokens[2], NULL, 16);
							}
						}
					}

					// Break on end of file
					if (result) {
						break;
					}
				}
				
				// Cleanup
				fclose(drvdata);
			}

			// Write rest of header
			fwrite(&vendorid, 4, 1, out);
			fwrite(&deviceid, 4, 1, out);
			fwrite(&dclass, 4, 1, out);
			fwrite(&dsubclass, 4, 1, out);
			fwrite(&dflags, 4, 1, out);

			// Load file data
			entry = fopen(&filename_qfd[0], "rb+");
			fseek(entry, 0, SEEK_END);
			fsize = ftell(entry);
			dataptr = malloc(fsize);
			rewind(entry);
			fread(dataptr, 1, fsize, entry);
			fclose(entry);

			// Write file length
			fwrite(&fsize, 4, 1, out);

			// Write file data
			fwrite(dataptr, 1, fsize, out);
			fflush(out);
			free(dataptr);

			// Update data
			fdatapos = ftell(out);
		}
	}

	// Close directory
	closedir(dfd);

	// Close and cleanup
	fflush(out);
	return fclose(out);
}
