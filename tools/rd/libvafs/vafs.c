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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void vafs_destroy(struct VaFs* vafs);

static int g_initialized = 0;

static void vafs_init(void)
{
    crc_init();
    g_initialized = 1;
}

static int __initialize_root(
    struct VaFs* vafs)
{
    if (vafs->Mode == VaFsMode_Read) {
        return vafs_directory_open_root(vafs, NULL, &vafs->RootDirectory);
    }
    else {
        return vafs_directory_create_root(vafs, &vafs->RootDirectory);
    }
}

int vafs_create(
    const char*              path,
    enum VaFsArchitecture    architecture,
    enum VaFsCompressionType compressionType,
    struct VaFs**            vafsOut)
{
    struct VaFs*       vafs;
    struct VaFsStream* stream;
    int                status;

    if (path == NULL || vafsOut == NULL) {
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

    status = __initialize_root(vafs);
    if (status) {
        vafs_destroy(vafs);
        return -1;
    }

    *vafsOut = vafs;
    return 0;
}

int vafs_open(
    const char*   path,
    struct VaFs** vafsOut)
{
    
}

static int __write_vafs_header(
    struct VaFs* vafs)
{
    VaFsHeader_t header;

    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.Architecture = vafs->Architecture;

    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + (uint32_t)vafs_stream_size(vafs->DescriptorStream);

    header.RootDescriptor.Index = vafs->RootDirectory->Descriptor.Descriptor.Index;
    header.RootDescriptor.Offset = vafs->RootDirectory->Descriptor.Descriptor.Offset;

    return vafs_stream_write(vafs->Stream, &header, sizeof(VaFsHeader_t));
}

static int __create_image(
    struct VaFs* vafs)
{
    int status;

    // flush files
    status = vafs_directory_flush(vafs->RootDirectory);
    if (status) {
        fprintf(stderr, "Failed to flush files: %i\n", status);
        return -1;
    }

    // flush streams
    status = vafs_stream_flush(vafs->DescriptorStream);
    if (status) {
        fprintf(stderr, "Failed to flush descriptor stream: %i\n", status);
        return -1;
    }

    status = vafs_stream_flush(vafs->DataStream);
    if (status) {
        fprintf(stderr, "Failed to flush data stream: %i\n", status);
        return -1;
    }

    // write the header
    status = __write_vafs_header(vafs);
    if (status) {
        return -1;
    }

    // write the descriptor stream
    status = vafs_stream_copy(vafs->Stream, vafs->DescriptorStream);
    if (status) {
        return -1;
    }

    // write the data stream
    return vafs_stream_copy(vafs->Stream, vafs->DataStream);
}

int vafs_close(
    struct VaFs* vafs)
{
    if (vafs == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (vafs->Mode == VaFsMode_Write) {
        int status = __create_image(vafs);
        if (status) {
            vafs_destroy(vafs);
            return -1;
        }
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
