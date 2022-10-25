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

#include <ddk/utils.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>
#include <stdlib.h>
#include <stdio.h>

static oserr_t __ReadBuffer(struct VFSStorage*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
static oserr_t __WriteBuffer(struct VFSStorage*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);

static struct VFSStorageOperations g_operations = {
        .Read = __ReadBuffer,
        .Write = __WriteBuffer
};

struct VFSStorage*
VFSStorageCreateMemoryBacked(
        _In_ uuid_t bufferHandle,
        _In_ size_t bufferOffset,
        _In_ void*  buffer,
        _In_ size_t size)
{
    struct VFSStorage* storage;

    storage = VFSStorageNew(&g_operations, 0);
    if (storage == NULL) {
        return NULL;
    }

    // setup protocol stuff
    storage->Protocol.StorageType = VFSSTORAGE_TYPE_EPHEMERAL;
    storage->Protocol.Storage.Memory.BufferHandle = bufferHandle;
    storage->Protocol.Storage.Memory.BufferOffset = bufferOffset;
    storage->Protocol.Storage.Memory.Buffer = buffer;
    storage->Protocol.Storage.Memory.Size = size;

    // hardcode some storage information
    storage->Stats.DriverID = UUID_INVALID;
    storage->Stats.DeviceID = bufferHandle;
    storage->Stats.SectorCount = size / 512;
    storage->Stats.SectorSize  = 512; // TODO this can change in the future
    storage->Stats.Flags = 0;
    storage->Stats.LUNCount = 1;

    // generate model and serial
    snprintf(
            &storage->Stats.Serial[0], sizeof(storage->Stats.Serial),
            "buffer-%u", bufferHandle
    );
    snprintf(
            &storage->Stats.Model[0], sizeof(storage->Stats.Model),
            "ephemeral"
    );
    return storage;
}

static oserr_t __ReadBuffer(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             buffer,
        _In_ size_t             offset,
        _In_ UInteger64_t*      sector,
        _In_ size_t             count,
        _In_ size_t*            read)
{
    size_t       bytesRead;
    UInteger64_t sourceOffset = { .QuadPart = sector->QuadPart * 512 };
    uint64_t     bytesAvailable;
    ERROR("__ReadBuffer called from Ephemeral Storage");

    if (sourceOffset.QuadPart >= storage->Protocol.Storage.Memory.Size) {
        *read = 0;
        return OS_EOK;
    }

    bytesAvailable = storage->Protocol.Storage.Memory.Size -= (size_t)sourceOffset.QuadPart;
    // TODO transfer from one dma to another

    *read = bytesAvailable / 512;
    return OS_EOK;
}

static oserr_t __WriteBuffer(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             buffer,
        _In_ size_t             offset,
        _In_ UInteger64_t*      sector,
        _In_ size_t             count,
        _In_ size_t*            written)
{
    size_t       bytesWritten;
    UInteger64_t destinationOffset = { .QuadPart = sector->QuadPart * 512 };
    uint64_t     bytesAvailable;
    ERROR("__WriteBuffer called from Ephemeral Storage");

    if (destinationOffset.QuadPart >= storage->Protocol.Storage.Memory.Size) {
        *written = 0;
        return OS_EOK;
    }

    bytesAvailable = storage->Protocol.Storage.Memory.Size -= (size_t)destinationOffset.QuadPart;
    // TODO transfer from one dma to another

    *written = bytesAvailable / 512;
    return OS_EOK;
}
