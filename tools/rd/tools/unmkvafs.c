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
 * VaFs Extractor
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
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

extern int __handle_filter(struct VaFs* vafs);

// Prints usage format of this program
static void __show_help(void)
{
    printf("usage: unmkvafs [options] image\n"
           "    --out               A path to where the disk image should be extracted to\n"
           "    --v,vv              Enables extra tracing output for debugging\n");
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

static int __directory_exists(
    const char* path)
{
    struct stat st;
    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        fprintf(stderr, "unmkvafs: stat failed for '%s'\n", path);
        return -1;
    }
    return S_ISDIR(st.st_mode);
}

static int __extract_file(
    struct VaFsFileHandle* fileHandle,
    const char*            path)
{
    FILE* file;
    long  fileSize;
    void* fileBuffer;
    int   status;

    if ((file = fopen(path, "wb+")) == NULL) {
        fprintf(stderr, "unmkvafs: unable to open file %s\n", path);
        return -1;
    }

    fileSize = vafs_file_length(fileHandle);
    fileBuffer = malloc(fileSize);
    if (fileBuffer == NULL) {
        fprintf(stderr, "unmkvafs: unable to allocate memory for file %s\n", path);
        return -1;
    }

    vafs_file_read(fileHandle, fileBuffer, fileSize);
    fwrite(fileBuffer, 1, fileSize, file);
    
    free(fileBuffer);
    fclose(file);
    return 0;
}

static int __extract_directory(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 root,
    const char*                 path)
{
    struct VaFsEntry dp;
    int              status;
    char*            filepathBuffer;

    // ensure the directory exists
    status = __directory_exists(path);
    if (status == -1) {
        return status;
    }

    if (!status && mkdir(path, 0777)) {
        fprintf(stderr, "unmkvafs: unable to create directory %s\n", path);
        return -1;
    }

    do {
        status = vafs_directory_read(directoryHandle, &dp);
        if (status) {
            if (errno != ENOENT) {
                fprintf(stderr, "unmkvafs: failed to read directory '%s' - %i\n",
                    __get_relative_path(root, path), status);
                return -1;
            }
            break;
        }

        filepathBuffer = malloc(strlen(path) + strlen(dp.Name) + 2);
        sprintf(filepathBuffer, "%s/%s", path, dp.Name);

        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to open directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }

            status = __extract_directory(subdirectoryHandle, root, filepathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: unable to extract directory '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
        }
        else {
            struct VaFsFileHandle* fileHandle;
            status = vafs_directory_open_file(directoryHandle, dp.Name, &fileHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to open file '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                return -1;
            }

            status = __extract_file(fileHandle, filepathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: unable to extract file '%s'\n", __get_relative_path(root, path));
                return -1;
            }

            status = vafs_file_close(fileHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to close file '%s'\n", __get_relative_path(root, filepathBuffer));
                return -1;
            }
        }
        free(filepathBuffer);
    } while(1);

    return 0;
}

int main(int argc, char *argv[])
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFs*                vafsHandle;
    int                         status;

    char* imagePath = NULL;
    char* destinationPath = "";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            destinationPath = argv[++i];
        }
		else if (!strcmp(argv[i], "--v")) {
			vafs_log_initalize(VaFsLogLevel_Info);
		}
		else if (!strcmp(argv[i], "--vv")) {
			vafs_log_initalize(VaFsLogLevel_Debug);
		}
        else {
            imagePath = argv[i];
        }
    }

    if (imagePath == NULL) {
        __show_help();
        return -1;
    }
    
    
    status = vafs_open_file(imagePath, &vafsHandle);
    if (status) {
        fprintf(stderr, "unmkvafs: cannot open vafs image: %s\n", imagePath);
        return -1;
    }

    status = __handle_filter(vafsHandle);
    if (status) {
        vafs_close(vafsHandle);
        fprintf(stderr, "unmkvafs: failed to handle image filter\n");
        return -1;
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        vafs_close(vafsHandle);
        fprintf(stderr, "unmkvafs: cannot open root directory: /\n");
        return -1;
    }

    status = __extract_directory(directoryHandle, destinationPath, destinationPath);
    if (status != 0) {
        vafs_close(vafsHandle);
        fprintf(stderr, "unmkvafs: unable to extract to directory %s\n", destinationPath);
        return -1;
    }

    return vafs_close(vafsHandle);
}
