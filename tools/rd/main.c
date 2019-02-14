/* Ramdisk Builder Utility 
 * Author: Philip Meulengracht
 * Date: 30-05-17
 * Used as a utility for MollenOS to build the initial ramdisk */

// Includes
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include "dirent.h"
#define PACKED_TYPESTRUCT(name, body) __pragma(pack(push, 1)) typedef struct _##name body name##_t __pragma(pack(pop))
#else
#include <dirent.h>
#define PACKED_TYPESTRUCT(name, body) typedef struct __attribute__((packed)) _##name body name##_t
#endif
#include <sys/stat.h>

#define POLYNOMIAL 0x04c11db7L      // Standard CRC-32 ppolynomial

PACKED_TYPESTRUCT(BitmapFileHeader, {
    uint16_t bfType;  //specifies the file type
    uint32_t bfSize;  //specifies the size in bytes of the bitmap file
    uint16_t bfReserved1;  //reserved; must be 0
    uint16_t bfReserved2;  //reserved; must be 0
    uint32_t bfOffBits;  //species the offset in bytes from the bitmapfileheader to the bitmap bits
});

PACKED_TYPESTRUCT(BitmapInfoHeader, {
    uint32_t biSize;  //specifies the number of bytes required by the struct
    uint32_t biWidth;  //specifies width in pixels
    uint32_t biHeight;  //species height in pixels
    uint16_t biPlanes; //specifies the number of color planes, must be 1
    uint16_t biBitCount; //specifies the number of bit per pixel
    uint32_t biCompression;//spcifies the type of compression
    uint32_t biSizeImage;  //size of image in bytes
    long biXPelsPerMeter;  //number of pixels per meter in x axis
    long biYPelsPerMeter;  //number of pixels per meter in y axis
    uint32_t biClrUsed;  //number of colors used by th ebitmap
    uint32_t biClrImportant;  //number of colors that are important
});

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
		free(copy);
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

int GetBitmapData(char *in, char **out, long *pixelcount)
{
    BitmapFileHeader_t *FileHeader = (BitmapFileHeader_t*)in;
    BitmapInfoHeader_t *InfoHeader;
    uint8_t *pointer = (uint8_t*)in;
    uint32_t i;

    // Verify header
    if (FileHeader->bfType != 0x4D42) {
        printf("invalid bmp header value 0x%x\n", FileHeader->bfType);
        return 1;
    }

    pointer    += sizeof(BitmapFileHeader_t);
    InfoHeader  = (BitmapInfoHeader_t*)pointer;
    pointer     = (uint8_t*)in + FileHeader->bfOffBits;

    // Pointer is now pointing at bitmap data
    (*out)          = (char*)malloc(InfoHeader->biSizeImage);
    (*pixelcount)   = 0;
    
    //swap the r and b values to get RGB (bitmap is BGR)
    for (i = 0; i < InfoHeader->biSizeImage; i += 3) // fixed semicolon
    {
        uint8_t temp = pointer[i];
        pointer[i] = pointer[i + 2];
        pointer[i + 2] = temp;
        (*pixelcount)++;
    }
    return 0;
}

// Performs run length compression on the data buffer, only really
// useful on bmp images
char *PerformRLE(char *data, long length, long *new_length)
{
    long data_length_needed = sizeof(BitmapFileHeader_t) + sizeof(BitmapInfoHeader_t);
    char *pixeldata;
    char *rledata;
    uint32_t *rlepointer;
    long pixelcount;
    long i, counter;

    if (GetBitmapData(data, &pixeldata, &pixelcount)) {
        printf("failed to parse the bmp image\n");
        return NULL;
    }

    // Bitmaps are RGB, we extend this to ARGB
    uint32_t last_value = 0xdeadbeef;
    for (i = 0; i < pixelcount; i++) {
        uint32_t value = ((uint32_t)pixeldata[(i * 3) + 2] << 16) | ((uint32_t)pixeldata[(i * 3) + 1] << 8) | pixeldata[(i * 3)];
        if (last_value != value || (i + 1) == pixelcount) {
            data_length_needed += 8; // 4 bytes count, 4 bytes value
            last_value          = value;
        }
    }

    // Allocate the data now that we know the required length
    rledata     = (char*)malloc(data_length_needed);
    rlepointer  = (uint32_t*)rledata;
    counter     = 1;
    last_value  = 0xFF000000 | ((long)pixeldata[2] << 16) | ((long)pixeldata[1] << 8) | pixeldata[0];
    for (i = 1; i < pixelcount; i++) {
        long value = 0xFF000000 | ((long)pixeldata[(i * 3) + 2] << 16) | ((long)pixeldata[(i * 3) + 1] << 8) | pixeldata[(i * 3)];
        if (last_value != value || (i + 1) == pixelcount) {
            *(rlepointer++) = (uint32_t)counter;
            *(rlepointer++) = last_value;
            counter         = 0;
            last_value      = value;
        }
    }
    *new_length = data_length_needed;
    return rledata;
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
            
            // Skip everything that is not dll's or .bmp's
            char *dot = strrchr(dp->d_name, '.');
			if (!dot || (strcmp(dot, ".dll") && strcmp(dot, ".bmp"))) {
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
            rddataheader.VendorId       = vendorid;
            rddataheader.DeviceId       = deviceid;
            rddataheader.DeviceType     = dclass;
            rddataheader.DeviceSubType  = dsubclass;
            rddataheader.Flags          = dflags;

			// Load file data
            entry = fopen(filename_buffer, "rb+");
            fseek(entry, 0, SEEK_END);
            fsize = ftell(entry);
            dataptr = malloc(fsize);
            rewind(entry);
            fread(dataptr, 1, fsize, entry);
            fclose(entry);

            // Bmp is the only supported image form, which we automatically run RLE on
            if (!strcmp(dot, ".bmp")) {

            }
            
			// Write data header
            rddataheader.LengthOfData   = fsize;
            rddataheader.Crc32OfData    = Crc32Generate(-1, (uint8_t*)dataptr, fsize);
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
    free(tokens);
	return fclose(out);
}
