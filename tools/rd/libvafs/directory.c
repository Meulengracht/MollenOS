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

#include <errno.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>

struct VaFsDirectoryEntry;

struct VaFsDirectoryReader {
    struct VaFsDirectory Base;
};

struct VaFsDirectoryWriter {
    struct VaFsDirectory       Base;
    struct VaFsDirectoryEntry* Entries;
};

struct VaFsDirectoryEntry {
    int Type;
    union {
        struct VaFsFile*      File;
        struct VaFsDirectory* Directory;
    };
    struct VaFsDirectoryEntry* Link;
};

struct VaFsDirectoryHandle {
    struct VaFsDirectory* Directory;
    int                   Index;
};

static void __initialize_file_descriptor(
    VaFsFileDescriptor_t* descriptor)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    descriptor->Base.Length = sizeof(VaFsFileDescriptor_t);
    descriptor->Data.Index = VA_FS_INVALID_BLOCK;
    descriptor->Data.Offset = VA_FS_INVALID_OFFSET;
    descriptor->FileLength = 0;
}

static void __initialize_directory_descriptor(
    VaFsDirectoryDescriptor_t* descriptor)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    descriptor->Base.Length = sizeof(VaFsDirectoryDescriptor_t);
    descriptor->Descriptor.Index = VA_FS_INVALID_BLOCK;
    descriptor->Descriptor.Offset = VA_FS_INVALID_OFFSET;
}

int vafs_directory_create_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut)
{
    struct VaFsDirectoryWriter* directory;
    int                         status;
    
    if (vafs == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    directory = (struct VaFsDirectoryWriter*)malloc(sizeof(struct VaFsDirectoryWriter));
    if (!directory) {
        errno = ENOMEM;
        return -1;
    }
    memset(directory, 0, sizeof(struct VaFsDirectoryWriter));

    directory->Base.VaFs = vafs;
    directory->Base.Name = strdup("root");
    __initialize_directory_descriptor(&directory->Base.Descriptor);

    *directoryOut = &directory->Base;
    return 0;
}

int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut)
{
    return -1;
}

static int __get_path_token(
    const char* path,
    char*       token,
    size_t      tokenSize)
{
    size_t i;
    size_t j;
    size_t remainingLength;

    if (path == NULL || token == NULL) {
        errno = EINVAL;
        return 0;
    }

    remainingLength = strlen(path);
    if (remainingLength == 0) {
        errno = ENOENT;
        return 0;
    }

    // skip leading slashes
    for (i = 0; i < remainingLength; i++) {
        if (path[i] != '/') {
            break;
        }
    }

    // copy over token untill \0 or /
    for (j = 0; i < remainingLength; i++, j++) {
        if (path[i] == '/' || path[i] == '\0') {
            break;
        }

        if (j >= tokenSize) {
            errno = ENAMETOOLONG;
            return 0;
        }
        token[j] = path[i];
    }
    token[j] = '\0';
    return i;
}

static struct VaFsDirectoryEntry* __get_first_entry(
    struct VaFsDirectory* directory)
{
    if (directory->VaFs->Mode == VaFsMode_Read) {
        return NULL;
    }
    else {
        struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)directory;
        return writer->Entries;
    }
}

static int __is_root(
    const char* path)
{
    int len = strlen(path);
    if (len == 1 && path[0] == '/') {
        return 1;
    }
    if (len == 0) {
        return 1;
    }
    return 0;
}

static const char* __get_entry_name(
    struct VaFsDirectoryEntry* entry)
{
    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
        return entry->File->Name;
    }
    else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        return entry->Directory->Name;
    }
    return NULL;
}

static struct VaFsDirectoryHandle* __create_handle(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryHandle* handle;

    handle = (struct VaFsDirectoryHandle*)malloc(sizeof(struct VaFsDirectoryHandle));
    if (!handle) {
        errno = ENOMEM;
        return NULL;
    }

    handle->Directory = directory;
    handle->Index = 0;
    return handle;
}

int vafs_directory_open(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut)
{
    struct VaFsDirectoryEntry* entries;
    const char*                remainingPath = path;
    char                       token[128];

    if (vafs == NULL || path == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__is_root(path)) {
        *handleOut = __create_handle(vafs->RootDirectory);
        return 0;
    }

    // get initial entry
    entries = __get_first_entry(vafs->RootDirectory);

    do {
        // get the next token
        int charsConsumed = __get_path_token(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        // find the name in the directory
        while (entries != NULL) {
            if (!strcmp(__get_entry_name(entries), token)) {
                if (entries->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
                    errno = ENOTDIR;
                    return -1;
                }

                if (remainingPath[0] == '\0') {
                    // we found the directory
                    *handleOut = __create_handle(entries->Directory);
                    return 0;
                }

                entries = __get_first_entry(vafs->RootDirectory);
                break;
            }
            entries = entries->Link;
        }
    } while (1);
    return -1;
}

static int __write_directory_header(
    struct VaFsDirectoryWriter* writer,
    int                         count)
{
    VaFsDirectoryHeader_t header;

    header.Count = count;

    return vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &header,
        sizeof(VaFsDirectoryHeader_t)
    );
}

static int __write_file_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;

    // increase descriptor length by name, do not account
    // for the null terminator
    entry->File->Descriptor.Base.Length += strlen(entry->File->Name);

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->File->Descriptor,
        sizeof(VaFsFileDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->File->Name,
        strlen(entry->File->Name)
    );
    return status;
}

static int __write_directory_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    
    // increase descriptor length by name, do not account
    // for the null terminator
    entry->Directory->Descriptor.Base.Length += strlen(entry->Directory->Name);

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Directory->Descriptor,
        sizeof(VaFsDirectoryDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Directory->Name,
        strlen(entry->Directory->Name)
    );
    return status;
}

int vafs_directory_flush(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)directory;
    struct VaFsDirectoryEntry*  entry;
    int                         status;
    int                         entryCount = 0;
    uint16_t                    block;
    uint32_t                    offset;

    // We must flush all subdirectories first to initalize their
    // index and offset. Otherwise we will be writing empty descriptors
    // for subdirectories.
    entry = writer->Entries;
    while (entry != NULL) {
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            // flush the directory
            vafs_directory_flush(entry->Directory);
        }
        entryCount++;
        entry = entry->Link;
    }

    // get current stream position;
    status = vafs_stream_position(
        writer->Base.VaFs->DescriptorStream,
        &block, &offset
    );
    if (status) {
        fprintf(stderr, "vafs_directory_flush: failed to get stream position\n");
        return status;
    }

    directory->Descriptor.Descriptor.Index  = block;
    directory->Descriptor.Descriptor.Offset = offset;

    status = __write_directory_header(writer, entryCount);
    if (status) {
        fprintf(stderr, "vafs_directory_flush: failed to write directory header\n");
        return status;
    }

    // now we actually write all the descriptors
    entry = writer->Entries;
    while (entry != NULL) {
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
            status = __write_file_descriptor(writer, entry);
        }
        else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            status = __write_directory_descriptor(writer, entry);
        }
        else {
            fprintf(stderr, "vafs_directory_flush: unknown descriptor type\n");
            return -1;
        }

        if (status) {
            fprintf(stderr, "vafs_directory_flush: failed to write descriptor: %i\n", status);
            return status;
        }
        entry = entry->Link;
    }
    return 0;
}

int vafs_directory_close(
    struct VaFsDirectoryHandle* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // free handle
    free(handle);
    return 0;
}

static int __add_file_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsFile*       entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    newEntry->File = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    return 0;
}

static int __create_file_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name)
{
    struct VaFsFile* entry;
    int              status;

    entry = (struct VaFsFile*)malloc(sizeof(struct VaFsFile));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_file_descriptor(&entry->Descriptor);
    status = __add_file_entry(writer, entry);
    if (status) {
        free((void*)entry->Name);
        free(entry);
        return status;
    }
    return 0;
}

static int __add_directory_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectory*       entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    newEntry->Directory = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    return 0;
}

static int __create_directory_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name)
{
    struct VaFsDirectoryWriter* entry;
    int                         status;

    entry = (struct VaFsDirectoryWriter*)malloc(sizeof(struct VaFsDirectoryWriter));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->Entries = NULL;
    entry->Base.VaFs = writer->Base.VaFs;
    entry->Base.Name = strdup(name);
    if (!entry->Base.Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_directory_descriptor(&entry->Base.Descriptor);
    status = __add_directory_entry(writer, &entry->Base);
    if (status) {
        free((void*)entry->Base.Name);
        free(entry);
        return status;
    }
    return 0;
}

static struct VaFsDirectoryEntry* __find_entry(
    struct VaFsDirectory* directory,
    const char*           token)
{
    struct VaFsDirectoryEntry* entries;

    // find the name in the directory
    entries = __get_first_entry(directory);
    while (entries != NULL) {
        if (!strcmp(__get_entry_name(entries), token)) {
            return entries;
        }
        entries = entries->Link;
    }
    return NULL;
}

int vafs_directory_open_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    struct VaFsDirectoryHandle** handleOut)
{
    struct VaFsDirectoryEntry* entry;
    const char*                remainingPath = name;
    char                       token[128];

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __find_entry(handle->Directory, token);
    if (entry == NULL) {
        if (handle->Directory->VaFs->Mode == VaFsMode_Read) {
            errno = ENOENT;
            return -1;
        }
        else {
            struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)handle->Directory;
            int                         status;

            status = __create_directory_entry(writer, token);
            if (status != 0) {
                return status;
            }
            entry = __find_entry(handle->Directory, token);
        }
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }

    *handleOut = __create_handle(entry->Directory);
    return 0;
}

int vafs_directory_open_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryEntry* entry;
    const char*                remainingPath = name;
    char                       token[128];

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __find_entry(handle->Directory, token);
    if (entry == NULL) {
        if (handle->Directory->VaFs->Mode == VaFsMode_Read) {
            errno = ENOENT;
            return -1;
        }
        else {
            struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)handle->Directory;
            int                         status;

            status = __create_file_entry(writer, token);
            if (status != 0) {
                return status;
            }
            entry = __find_entry(handle->Directory, token);
        }
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_FILE) {
        errno = ENFILE;
        return -1;
    }

    *handleOut = vafs_file_create_handle(entry->File);
    return 0;
}
