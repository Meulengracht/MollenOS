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

static inline int __compare_guids(
    struct VaFsGuid* lh,
    struct VaFsGuid* rh)
{
    return memcmp(lh, rh, sizeof(struct VaFsGuid));
}

int vafs_feature_add(
    struct VaFs*              vafs,
    struct VaFsFeatureHeader* feature)
{
    int status;
    int i;

    if (vafs == NULL || feature == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < vafs->FeatureCount; i++) {
        if (!__compare_guids(&vafs->Features[i]->Guid, &feature->Guid)) {
            errno = EEXIST;
            return -1;
        }
    }

    vafs->Features[vafs->FeatureCount] = malloc(feature->Length);
    if (!vafs->Features[vafs->FeatureCount]) {
        errno = ENOMEM;
        return -1;
    }

    memcpy(vafs->Features[vafs->FeatureCount], feature, feature->Length);
    vafs->FeatureCount++;
    return 0;
}

int vafs_feature_query(
    struct VaFs*               vafs,
    struct VaFsGuid*           guid,
    struct VaFsFeatureHeader** featureOut)
{
    int status;
    int i;

    if (vafs == NULL || guid == NULL || featureOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < vafs->FeatureCount; i++) {
        if (!__compare_guids(&vafs->Features[i]->Guid, guid)) {
            *featureOut = vafs->Features[i];
            return 0;
        }
    }
    
    errno = ENOENT;
    return -1;
}

static int __initialize_root(
    struct VaFs* vafs)
{
    if (vafs->Mode == VaFsMode_Read) {
        return vafs_directory_open_root(vafs, &vafs->Header.RootDescriptor, &vafs->RootDirectory);
    }
    else {
        return vafs_directory_create_root(vafs, &vafs->RootDirectory);
    }
}

static struct VaFsFeatureHeader* __load_feature(
    struct VaFs* vafs)
{
    struct VaFsFeatureHeader  header;
    struct VaFsFeatureHeader* feature;
    int                       status;
    long                      offset;
    size_t                    read;

    offset = vafs_streamdevice_seek(vafs->ImageDevice, 0, SEEK_CUR);
    if (offset < 0) {
        VAFS_ERROR("__load_feature: failed to retrieve current offset: %i\n", offset);
        return NULL;
    }

    status = vafs_streamdevice_read(vafs->ImageDevice, &header, sizeof(struct VaFsFeatureHeader), &read);
    if (status || read != sizeof(struct VaFsFeatureHeader)) {
        VAFS_ERROR("__load_feature: failed to read feature header %i\n", status);
        return NULL;
    }

    feature = malloc(header.Length);
    if (!feature) {
        VAFS_ERROR("__load_feature: failed to allocate memory\n");
        return NULL;
    }

    status = vafs_streamdevice_seek(vafs->ImageDevice, offset, SEEK_SET);
    if (status) {
        VAFS_ERROR("__load_feature: failed to seek to offset %i\n", offset);
        free(feature);
        return NULL;
    }

    status = vafs_streamdevice_read(vafs->ImageDevice, feature, header.Length, &read);
    if (status || read != header.Length) {
        VAFS_ERROR("__load_feature: failed to read feature %i\n", status);
        free(feature);
        return NULL;
    }
    return feature;
}

static int __load_features(
    struct VaFs* vafs)
{
    int i;

    if (!vafs->Header.FeatureCount) {
        return 0;
    }
    
    for (i = 0; i < vafs->Header.FeatureCount; i++) {
        vafs->Features[i] = __load_feature(vafs);
        if (vafs->Features[i] == NULL) {
            return -1;
        }
    }
    return 0;
}

static int __verify_header(
    struct VaFs* vafs)
{
    if (vafs->Header.Magic != VA_FS_MAGIC) {
        VAFS_ERROR("__verify_header: invalid image magic 0x%x\n", vafs->Header.Magic);
        return -1;
    }
    
    if (vafs->Header.Version != VA_FS_VERSION) {
        VAFS_ERROR("__verify_header: invalid image version 0x%x\n", vafs->Header.Version);
        return -1;
    }
    
    return 0;
}

static void __initialize_header(
    struct VaFs*             vafs,
    enum VaFsArchitecture    architecture)
{
    vafs->Header.Magic = VA_FS_MAGIC;
    vafs->Header.Version = VA_FS_VERSION;
    vafs->Header.Architecture = architecture;
    vafs->Header.FeatureCount = 0;
    vafs->Header.Reserved = 0;
    vafs->Header.BlockSize = VA_FS_BLOCKSIZE;
    vafs->Header.Attributes = 0;
}

static int __initialize_imagestream(
    struct VaFs*             vafs,
    const char*              path,
    enum VaFsArchitecture    architecture)
{
    int status;

    VAFS_DEBUG("__initialize_imagestream: path: %s\n", path);
    // initalize the underlying stream device, if we are opening
    // an existing image, we need to read and verify the header
    if (vafs->Mode == VaFsMode_Read) {
        size_t read;

        status = vafs_streamdevice_open_file(path, &vafs->ImageDevice);
        if (status) {
            VAFS_ERROR("__initialize_imagestream: failed to open image file: %i\n", status);
            return status;
        }
        
        status = vafs_streamdevice_read(vafs->ImageDevice, &vafs->Header, sizeof(VaFsHeader_t), &read);
        if (status) {
            VAFS_ERROR("__initialize_imagestream: failed to read image header: %i\n", status);
            return status;
        }

        status = __verify_header(vafs);
        if (status) {
            VAFS_ERROR("__initialize_imagestream: failed to verify image header: %i\n", status);
            return status;
        }

        status = __load_features(vafs);
    }
    else {
        status = vafs_streamdevice_create_file(path, &vafs->ImageDevice);
        if (status) {
            VAFS_ERROR("__initialize_imagestream: failed to create image file: %i\n", status);
            return status;
        }

        __initialize_header(vafs, architecture);
    }
    return status;
}

static int __initialize_fsstreams_read(
    struct VaFs* vafs)
{
    int status;
    
    VAFS_DEBUG("__initialize_fsstreams_read: vafs: %p\n", vafs);
    // create the descriptor and data streams, when reading we do not
    // provide any compression parameter as its set on block level.
    status = vafs_stream_create(
        vafs->ImageDevice, 
        vafs->Header.DescriptorBlockOffset,
        VA_FS_BLOCKSIZE,
        &vafs->DescriptorStream
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_read: failed to create descriptor stream: %i\n", status);
        return status;
    }

    status = vafs_stream_create(
        vafs->ImageDevice, 
        vafs->Header.DataBlockOffset,
        VA_FS_BLOCKSIZE,
        &vafs->DataStream
    );
    return status;
}

static int __initialize_fsstreams_write(
    struct VaFs* vafs)
{
    int status;
    
    VAFS_DEBUG("__initialize_fsstreams_write: vafs: %p\n", vafs);
    status = vafs_streamdevice_create_memory(
        vafs->Header.BlockSize, &vafs->DescriptorDevice
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create descriptor stream device: %i\n", status);
        return status;
    }

    status = vafs_streamdevice_create_memory(
        vafs->Header.BlockSize, &vafs->DataDevice
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create data stream device: %i\n", status);
        return status;
    }

    status = vafs_stream_create(
        vafs->DescriptorDevice, 
        0,
        vafs->Header.BlockSize,
        &vafs->DescriptorStream
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create descriptor stream: %i\n", status);
        return status;
    }

    status = vafs_stream_create(
        vafs->DataDevice, 
        0,
        vafs->Header.BlockSize,
        &vafs->DataStream
    );
    return status;
}

static int __initialize_fsstreams(
    struct VaFs* vafs)
{
    if (vafs->Mode == VaFsMode_Read) {
        return __initialize_fsstreams_read(vafs);
    }
    else {
        return __initialize_fsstreams_write(vafs);
    }
}

static int __new_vafs(
    enum VaFsMode            mode,
    const char*              path,
    enum VaFsArchitecture    architecture,
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

    vafs = (struct VaFs*)malloc(sizeof(struct VaFs));
    if (!vafs) {
        errno = ENOMEM;
        return -1;
    }
    memset(vafs, 0, sizeof(struct VaFs));

    vafs->Mode = mode;

    vafs->Features = malloc(sizeof(struct VaFsFeatureHeader*) * VA_FS_MAX_FEATURES);
    if (!vafs->Features) {
        vafs_destroy(vafs);
        errno = ENOMEM;
        return -1;
    }

    // try to create the output file, otherwise do not continue
    status = __initialize_imagestream(vafs, path, architecture);
    if (status) {
        VAFS_ERROR("__new_vafs: failed to initialize image stream: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    status = __initialize_fsstreams(vafs);
    if (status) {
        VAFS_ERROR("__new_vafs: failed to initialize filesystem streams: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    status = __initialize_root(vafs);
    if (status) {
        VAFS_ERROR("__new_vafs: failed to initialize root directory: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    *vafsOut = vafs;
    return 0;
}

int vafs_create(
    const char*              path,
    enum VaFsArchitecture    architecture,
    struct VaFs**            vafsOut)
{
    VAFS_INFO("vafs_create: creating new image file\n");
    return __new_vafs(VaFsMode_Write, path, architecture, vafsOut);
}

int vafs_open(
    const char*   path,
    struct VaFs** vafsOut)
{
    VAFS_INFO("vafs_open: opening existing image file\n");
    return __new_vafs(VaFsMode_Read, path, 0, vafsOut);
}

static int __write_vafs_header(
    struct VaFs* vafs)
{
    size_t written;
    long   offset;
    VAFS_INFO("__write_vafs_header: writing header\n");

    offset = vafs_streamdevice_seek(vafs->DescriptorDevice, 0, SEEK_CUR);
    if (offset < 0) {
        VAFS_ERROR("__write_vafs_header: failed to seek to current position: %i\n", offset);
        return -1;
    }

    vafs->Header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    vafs->Header.DataBlockOffset = sizeof(VaFsHeader_t) + (uint32_t)offset;

    vafs->Header.RootDescriptor.Index = vafs->RootDirectory->Descriptor.Descriptor.Index;
    vafs->Header.RootDescriptor.Offset = vafs->RootDirectory->Descriptor.Descriptor.Offset;

    return vafs_streamdevice_write(vafs->ImageDevice, &vafs->Header, sizeof(VaFsHeader_t), &written);
}

static int __create_image(
    struct VaFs* vafs)
{
    int status;

    // flush files
    VAFS_DEBUG("__create_image: flushing files\n");
    status = vafs_directory_flush(vafs->RootDirectory);
    if (status) {
        VAFS_ERROR("Failed to flush files: %i\n", status);
        return -1;
    }

    // flush streams
    VAFS_DEBUG("__create_image: flushing streams\n");
    status = vafs_stream_flush(vafs->DescriptorStream);
    if (status) {
        VAFS_ERROR("Failed to flush descriptor stream: %i\n", status);
        return -1;
    }
    
    status = vafs_stream_flush(vafs->DataStream);
    if (status) {
        VAFS_ERROR("Failed to flush data stream: %i\n", status);
        return -1;
    }

    // write the header
    VAFS_DEBUG("__create_image: writing header\n");
    status = __write_vafs_header(vafs);
    if (status) {
        return -1;
    }

    // write the descriptor stream
    VAFS_DEBUG("__create_image: writing descriptor stream\n");
    status = vafs_streamdevice_copy(vafs->ImageDevice, vafs->DescriptorDevice);
    if (status) {
        return -1;
    }

    // write the data stream
    VAFS_DEBUG("__create_image: writing data stream\n");
    return vafs_streamdevice_copy(vafs->ImageDevice, vafs->DataDevice);
}

int vafs_close(
    struct VaFs* vafs)
{
    if (vafs == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (vafs->Mode == VaFsMode_Write) {
        VAFS_INFO("vafs_close: flushing image file\n");
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
    VAFS_INFO("vafs_close: cleaning up\n");
    vafs_stream_close(vafs->DescriptorStream);
    vafs_stream_close(vafs->DataStream);

    if (vafs->Mode == VaFsMode_Write) {
        vafs_streamdevice_close(vafs->DescriptorDevice);
        vafs_streamdevice_close(vafs->DataDevice);
    }
    vafs_streamdevice_close(vafs->ImageDevice);
    free(vafs->Features);
    free(vafs);
}
