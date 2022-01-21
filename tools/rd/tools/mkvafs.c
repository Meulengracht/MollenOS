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
#include <vafs/vafs.h>

#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif
#include <sys/stat.h>

extern int __install_filter(struct VaFs* vafs, const char* filterName);

// Prints usage format of this program
static void __show_help(void)
{
	printf("usage: mkvafs [options] dir/files ...\n"
           "    --arch              {i386,amd64,arm,arm64,rv32,rv64}\n"
		   "    --compression       {aplib}\n"
		   "    --out               A path to where the disk image should be written to\n"
           "    --v,vv              Enables extra tracing output for debugging\n");
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
		fprintf(stderr, "mkvafs: unknown architecture '%s'\n", arch);
		exit(-1);
	}
}

static const char* __get_relative_path(
	const char* root,
	const char* path)
{
	const char* relative = path;
	if (strncmp(path, root, strlen(root)) == 0)
		relative = path + strlen(root);
	return relative;
}

static int __is_directory(
	const char* path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		fprintf(stderr, "mkvafs: stat failed for '%s'\n", path);
		return 0;
	}
	return S_ISDIR(st.st_mode);
}

static int __write_file(
	struct VaFsDirectoryHandle* directoryHandle,
	const char*                 root,
	const char*                 path)
{
	struct VaFsFileHandle* fileHandle;
	FILE*                  file;
	long                   fileSize;
	void*                  fileBuffer;
	int                    status;

	// create the VaFS file
	status = vafs_directory_open_file(directoryHandle, __get_relative_path(root, path), &fileHandle);
	if (status) {
		fprintf(stderr, "mkvafs: failed to create file '%s'\n", __get_relative_path(root, path));
		return -1;
	}

	if ((file = fopen(path, "rb")) == NULL) {
		fprintf(stderr, "mkvafs: unable to open file %s\n", path);
		return -1;
	}

	fseek(file, 0, SEEK_END);
	fileSize = ftell(file);
	fileBuffer = malloc(fileSize);
	rewind(file);
	fread(fileBuffer, 1, fileSize, file);
	fclose(file);

	// write the file to the VaFS file
	status = vafs_file_write(fileHandle, fileBuffer, fileSize);
	if (status) {
		fprintf(stderr, "mkvafs: failed to write file '%s'\n", __get_relative_path(root, path));
		return -1;
	}

	status = vafs_file_close(fileHandle);
	if (status) {
		fprintf(stderr, "mkvafs: failed to close file '%s'\n", __get_relative_path(root, path));
		return -1;
	}
	return 0;
}

static int __write_directory(
	struct VaFsDirectoryHandle* directoryHandle,
	const char*                 root,
	const char*                 path)
{
    struct dirent* dp;
	DIR*           dfd;
	int            status;
	char*          filepathBuffer;
	printf("mkvafs: writing directory '%s'\n", path);

	if ((dfd = opendir(path)) == NULL) {
		fprintf(stderr, "mkvafs: can't open initrd folder\n");
		return -1;
	}

    filepathBuffer = malloc(512);
	while ((dp = readdir(dfd)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
			continue;

		sprintf(filepathBuffer, "%s/%s", path, dp->d_name);
		printf("mkvafs: found '%s'\n", filepathBuffer);

		if (__is_directory(filepathBuffer)) {
			struct VaFsDirectoryHandle* subdirectoryHandle;
			status = vafs_directory_open_directory(directoryHandle, __get_relative_path(root, filepathBuffer), &subdirectoryHandle);
			if (status) {
				fprintf(stderr, "mkvafs: failed to create directory '%s'\n", __get_relative_path(root, filepathBuffer));
				continue;
			}

			status = __write_directory(subdirectoryHandle, root, filepathBuffer);
			if (status != 0) {
				fprintf(stderr, "mkvafs: unable to write directory %s\n", filepathBuffer);
				break;
			}

			status = vafs_directory_close(subdirectoryHandle);
			if (status) {
				fprintf(stderr, "mkvafs: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
				break;
			}
		}
		else {
			status = __write_file(directoryHandle, root, filepathBuffer);
			if (status != 0) {
				fprintf(stderr, "mkvafs: unable to write file %s\n", filepathBuffer);
				break;
			}
		}
	}

	free(filepathBuffer);
	closedir(dfd);
	return status;
}

int main(int argc, char *argv[])
{
	struct VaFsDirectoryHandle* directoryHandle;
	struct VaFs*                vafsHandle;
	int                         status;

    // parameters
	char* paths[32];
	char  pathCount = 0;
    char* arch = NULL;
    char* imagePath = "image.vafs"; 
    char* compressionName = NULL; 

	// Validate the number of arguments
	// compression
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--arch") && (i + 1) < argc) {
            arch = argv[++i];
        }
        else if (!strcmp(argv[i], "--compression") && (i + 1) < argc) {
            compressionName = argv[++i];
        }
        else if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            imagePath = argv[++i];
        }
		else if (!strcmp(argv[i], "--v")) {
			vafs_log_initalize(VaFsLogLevel_Info);
		}
		else if (!strcmp(argv[i], "--vv")) {
			vafs_log_initalize(VaFsLogLevel_Debug);
		}
		else {
			paths[pathCount++] = argv[i];
		}
    }

	if (arch == NULL || !pathCount) {
		__show_help();
		return -1;
	}

	status = vafs_create(imagePath, __get_vafs_arch(arch), &vafsHandle);
	if (status) {
		fprintf(stderr, "mkvafs: cannot create vafs output file: %s\n", imagePath);
		return -1;
	}

	// Was a compression requested?
	if (compressionName != NULL) {
		status = __install_filter(vafsHandle, compressionName);
		if (status) {
			fprintf(stderr, "mkvafs: cannot set compression: %s\n", compressionName);
			return -1;
		}
	}

	status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
	if (status) {
		fprintf(stderr, "mkvafs: cannot open root directory: /\n");
		return -1;
	}

	for (int i = 0; i < pathCount; i++) {
		if (__is_directory(paths[i])) {
			status = __write_directory(directoryHandle, paths[i], paths[i]);
			if (status != 0) {
				fprintf(stderr, "mkvafs: unable to write directory %s\n", paths[i]);
				break;
			}
		}
		else {
			status = __write_file(directoryHandle, paths[i], paths[i]);
			if (status != 0) {
				fprintf(stderr, "mkvafs: unable to write file %s\n", paths[i]);
				break;
			}
		}
	}
	return vafs_close(vafsHandle);
}
