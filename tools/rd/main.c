/**
 * Copyright 2022, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * VaFs Builder
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <platform.h>
#include <vafs.h>

// Prints usage format of this program
static void ShowSyntax(void)
{
	printf("  Syntax:\n\n"
           "    Build    :  rd --arch <arch> --initrd <path to initrd folder> --out <output>\n\n");
}


static enum VaFsArchitecture __get_vafs_arch(
	const char* arch)
{
	if (!strcmp(arch, "x86") || !strcmp(arch, "i386"))
		return VaFsArchitecture_X86;
	else if (!strcmp(arch, "x64") || !strcmp(arch, "amd64"))
		return VaFsArchitecture_X64;
	else if (strcmp(arch, "arm") == 0)
		return VaFsArchitecture_ARM;
	else if (strcmp(arch, "arm64") == 0)
		return VaFsArchitecture_ARM64;
	else {
		printf("mkvafs: unknown architecture '%s'\n", arch);
		exit(-1);
	}
}

// main
int main(int argc, char *argv[])
{
    struct dirent *dp = NULL;
	char *filename_buffer;
	char *dataptr = NULL;
	FILE *out = NULL;
	DIR *dfd = NULL;
	long fentrypos = 0;
	long fdatapos = 0;
	char **tokens;
	int tokencount;


	void* vafsHandle;
	int   status;

    // parameters
    char* archParameter = NULL;
    char* initrdPath = NULL;
    char* outPath = NULL; 

	// Print header
	printf("MollenOS Ramdisk Builder\n"
           "Copyright 2022 Philip Meulengracht (www.mollenos.com)\n\n");

	// Validate the number of arguments
	// format: rd $(arch) $(out)
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--arch") && (i + 1) < argc) {
            archParameter = argv[++i];
        }
        else if (!strcmp(argv[i], "--initrd") && (i + 1) < argc) {
            initrdPath = argv[++i];
        }
        else if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            outPath = argv[++i];
        }
    }

	if (archParameter == NULL || initrdPath == NULL || outPath == NULL) {
		ShowSyntax();
		return 1;
	}

	status = vafs_create(outPath, __get_vafs_arch(archParameter), VaFsCompressionType_NONE, &vafsHandle);
	if (status) {
		printf("mkvafs: cannot create vafs output file: %s\n", outPath);
		return 1;
	}

    // Open directory
    printf("Counting available files for rd\n");
	if ((dfd = opendir(initrdPath)) == NULL) {
		fprintf(stderr, "Can't open initrd folder\n");
		return 1;
	}
	
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
		sprintf(filename_buffer, "%s/%s", initrdPath, dp->d_name);
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
