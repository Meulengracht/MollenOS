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

#define PACKED_TYPESTRUCT(name, body) typedef struct __attribute__((packed)) _##name body name##_t
#define POLYNOMIAL 0x04c11db7L      // Standard CRC-32 ppolynomial

/* MCoreRamDiskHeader
 * The ramdisk header, this is present in the 
 * first few bytes of the ramdisk image, members
 * do not vary in length */
PACKED_TYPESTRUCT(MCoreRamDiskHeader, {
    uint32_t    Magic;
    uint32_t    Version;
    uint32_t    Architecture;
    int32_t     FileCount;
});

/* MCoreRamDiskEntry
 * This is the ramdisk entry, which describes
 * an entry in the ramdisk. The ramdisk entry area
 * contains headers right after each other */
PACKED_TYPESTRUCT(MCoreRamDiskEntry, {
    uint8_t     Name[64]; // UTF-8 Encoded filename
    uint32_t    Type; // Check the ramdisk entry definitions
    uint32_t    DataHeaderOffset; // offset in the ramdisk
});

/* MCoreRamDiskModuleHeader
 * This is the module header, and contains basic information
 * about the module data that follow this header. */
PACKED_TYPESTRUCT(MCoreRamDiskModuleHeader, {
    uint32_t    Flags;
    uint32_t    LengthOfData; // Excluding this header
    uint32_t    Crc32OfData;
    
    uint32_t    VendorId;
    uint32_t    DeviceId;
    uint32_t    DeviceType;
    uint32_t    DeviceSubType;
});

// Statics
uint32_t CrcTable[256] = { 0 };
MCoreRamDiskHeader_t RdHeaderStatic = {
	0x3144524D,
	0x00000001,
	0, 0
};

// Prints usage format of this program
static void ShowSyntax(void)
{
	printf("  Syntax:\n\n"
           "    Build    :  rd <arch> <output>\n\n");
}

/* Crc32GenerateTable
 * Generates a dynamic crc-32 table. */
void
Crc32GenerateTable(void)
{
    // Variables
    register uint32_t CrcAccumulator;
    register int i, j;

    // Iterate and fill the table
    for (i=0;  i < 256; i++) {
        CrcAccumulator = ((uint32_t) i << 24);
        for (j = 0;  j < 8;  j++) {
            if (CrcAccumulator & 0x80000000L) {
                CrcAccumulator = (CrcAccumulator << 1) ^ POLYNOMIAL;
            }
            else {
                CrcAccumulator = (CrcAccumulator << 1);
            }
        }
        CrcTable[i] = CrcAccumulator;
    }
}

/* Crc32Generate
 * Generates an crc-32 checksum from the given accumulator and
 * the given data. */
uint32_t
Crc32Generate(
    uint32_t CrcAccumulator, 
    uint8_t *DataPointer, 
    size_t DataSize)
{
    // Variables
    register size_t i, j;

    // Iterate each byte and accumulate crc
    for (j = 0; j < DataSize; j++) {
        i = ((int) (CrcAccumulator >> 24) ^ *DataPointer++) & 0xFF;
        CrcAccumulator = (CrcAccumulator << 8) ^ CrcTable[i];
    }
    CrcAccumulator = ~CrcAccumulator;
    return CrcAccumulator;
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
char *RemoveExtension(char* mystr, char dot, char sep) 
{
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
	char *filename_buffer;
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
    
    // Initialize CRC
    printf("Generating crc-table\n");
    Crc32GenerateTable();

	// Fill in architecture
	// Arch - x86_32 = 0x08, x86_64 = 0x10
    if (!strcmp(argv[1], "i386") || !strcmp(argv[1], "__i386__")) {
	    RdHeaderStatic.Architecture = 0x08;
    }
    else if (!strcmp(argv[1], "amd64") || !strcmp(argv[1], "__amd64__")) {
	    RdHeaderStatic.Architecture = 0x10;
    }

	RdHeaderStatic.FileCount = 0;

    // Open directory
    printf("Counting available files for rd\n");
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
    printf("Generating ramdisk header\n");
	fwrite(&RdHeaderStatic, 1, sizeof(MCoreRamDiskHeader_t), out);
	fflush(out);

	// Store current position
	fentrypos = ftell(out);

	// Fill rest of entry space with 0
	dataptr = malloc(0x1000 - sizeof(MCoreRamDiskHeader_t));
	memset(dataptr, 0, 0x1000 - sizeof(MCoreRamDiskHeader_t));
	fwrite(dataptr, 1, 0x1000 - sizeof(MCoreRamDiskHeader_t), out);
	free(dataptr);
	fflush(out);
	fdatapos = ftell(out);

	// Init token storage
	tokens = (char**)malloc(sizeof(char*) * 24);
	for (int i = 0; i < 24; i++)
		tokens[i] = (char*)malloc(64);

    // Iterate entries
    printf("Generating ramdisk entries\n");
    filename_buffer = malloc(512);
	while ((dp = readdir(dfd)) != NULL) {
		struct stat stbuf;

		// Build path string
		sprintf(filename_buffer, "initrd/%s", dp->d_name);
		if (stat(filename_buffer, &stbuf) == -1) {
			printf("Unable to stat file: %s\n", filename_buffer);
			continue;
		}

		// Skip directories
		if ((stbuf.st_mode & S_IFMT) == S_IFDIR) {
			continue;
		}
		else {
            // Variables
            MCoreRamDiskEntry_t rdentry = { { 0 }, 0 };
            MCoreRamDiskModuleHeader_t rddataheader = { 0 };
            
            // Skip everything that is not dll's
            char *dot = strrchr(dp->d_name, '.');
			if (!dot || strcmp(dot, ".dll")) {
				continue;
			}

			// Is it a driver? check if file exists with .drvm extension
			FILE *drvdata = GetDriver(filename_buffer);
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
            
            // fill entry
            memcpy(&rdentry.Name[0], dp->d_name, strlen(dp->d_name));
            rdentry.Type = type;
            rdentry.DataHeaderOffset = fdatapos;

			// Write header data
			fwrite(&rdentry, sizeof(MCoreRamDiskEntry_t), 1, out);
			fflush(out);

			// Update entry
			fentrypos = ftell(out);

			// Seek to data
            fseek(out, fdatapos, SEEK_SET);
            
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
            
            // Update rest of header
            rddataheader.VendorId = vendorid;
            rddataheader.DeviceId = deviceid;
            rddataheader.DeviceType = dclass;
            rddataheader.DeviceSubType = dsubclass;
            rddataheader.Flags = dflags;

			// Load file data
			entry = fopen(filename_buffer, "rb+");
			fseek(entry, 0, SEEK_END);
			fsize = ftell(entry);
			dataptr = malloc(fsize);
			rewind(entry);
			fread(dataptr, 1, fsize, entry);
            fclose(entry);
            
			// Write data header
            rddataheader.LengthOfData = fsize;
            rddataheader.Crc32OfData = Crc32Generate(-1, (uint8_t*)dataptr, fsize);
			fwrite(&rddataheader, sizeof(MCoreRamDiskModuleHeader_t), 1, out);

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
