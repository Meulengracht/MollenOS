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
 */

#include <vfs/storage.h>
#include <vfs/vfs.h>
#include <stdlib.h>

struct __FileContext {
    uuid_t file_handle;
    // size_t sector_size;
};

static void    __DestroyFile(void*);
static oserr_t __ReadFile(void*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
static oserr_t __WriteFile(void*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);

static struct VFSStorageOperations g_operations = {
        .Destroy = __DestroyFile,
        .Read = __ReadFile,
        .Write = __WriteFile
};

static struct __FileContext* __FileContextNew(
        _In_ uuid_t fileHandleID)
{
    struct __FileContext* context = malloc(sizeof(struct __FileContext));
    if (context == NULL) {
        return NULL;
    }
    context->file_handle = fileHandleID;
    return context;
}

struct VFSStorage*
VFSStorageCreateFileBacked(
        _In_ uuid_t fileHandleID)
{
    struct VFSStorage* storage = VFSStorageNew(&g_operations);
    if (storage == NULL) {
        return NULL;
    }

    storage->Data = __FileContextNew(fileHandleID);
    if (storage->Data == NULL) {
        free(storage);
        return NULL;
    }
    return storage;
}

static void __DestroyFile(
        _In_ void* context)
{
    struct __FileContext* file = context;
    free(file);
}

static oserr_t __ReadFile(
        _In_ void*         context,
        _In_ uuid_t        buffer,
        _In_ size_t        offset,
        _In_ UInteger64_t* sector,
        _In_ size_t        count,
        _In_ size_t*       read)
{
    struct __FileContext* file = context;
    size_t                bytesRead;
    oserr_t               oserr = VFSNodeReadAt(
            file->file_handle,
            &(UInteger64_t) { .QuadPart = sector->QuadPart * 512 },
            buffer,
            offset,
            count * 512,
            &bytesRead
    );
    if (oserr != OsOK) {
        return oserr;
    }
    *read = bytesRead / 512;
    return OsOK;
}

static oserr_t __WriteFile(
        _In_ void*         context,
        _In_ uuid_t        buffer,
        _In_ size_t        offset,
        _In_ UInteger64_t* sector,
        _In_ size_t        count,
        _In_ size_t*       written)
{
    struct __FileContext* file = context;
    size_t                bytesWritten;
    oserr_t               oserr = VFSNodeWriteAt(
            file->file_handle,
            &(UInteger64_t) { .QuadPart = sector->QuadPart * 512 },
            buffer,
            offset,
            count * 512,
            &bytesWritten
    );
    if (oserr != OsOK) {
        return oserr;
    }
    *written = bytesWritten / 512;
    return OsOK;
}