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
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include "crc.h"
#include <errno.h>
#include "private.h"
#include <stdio.h>

static void vafs_destroy(struct VaFs* vafs);

static int g_initialized = 0;

static void vafs_init(void)
{
    crc_init();
    g_initialized = 1;
}

int vafs_create(
    const char*              path,
    enum VaFsArchitecture    architecture,
    enum VaFsCompressionType compressionType,
    void**                   handleOut)
{
    struct VaFs*       vafs;
    struct VaFsStream* stream;
    int                status;

    if (path == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!g_initialized) {
        vafs_init();
    }

    // try to create the output file, otherwise do not continue
    status = vafs_stream_open_file(path, VA_FS_BLOCKSIZE, compressionType, &stream);
    if (status) {
        return -1;
    }

    vafs = (struct VaFs*)malloc(sizeof(struct VaFs));
    if (!vafs) {
        errno = ENOMEM;
        return -1;
    }
    memset(vafs, 0, sizeof(struct VaFs));

    vafs->Stream = stream;
    vafs->Architecture = architecture;
    vafs->Mode = VaFsMode_Write;

    status = vafs_stream_open_memory(VA_FS_BLOCKSIZE, compressionType, &vafs->DescriptorStream);
    if (status) {
        vafs_destroy(vafs);
        return -1;
    }

    status = vafs_stream_open_memory(VA_FS_BLOCKSIZE, compressionType, &vafs->DataStream);
    if (status) {
        vafs_destroy(vafs);
        return -1;
    }

    *handleOut = vafs;
    return 0;
}

int vafs_close(
    void* handle)
{
    struct VaFs* vafs = (struct VaFs*)handle;
    if (vafs == NULL) {
        errno = EINVAL;
        return -1;
    }

    vafs_destroy(vafs);
    return 0;
}

static void vafs_destroy(
    struct VaFs* vafs)
{
    vafs_stream_close(vafs->Stream);
    vafs_stream_close(vafs->DescriptorStream);
    vafs_stream_close(vafs->DataStream);
    free(vafs);
}
