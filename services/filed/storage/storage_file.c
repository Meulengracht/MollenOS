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
#include <vfs/stat.h>
#include <vfs/vfs.h>
#include <stdlib.h>
#include <stdio.h>

static oserr_t __ReadFile(struct VFSStorage*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);
static oserr_t __WriteFile(struct VFSStorage*, uuid_t, size_t, UInteger64_t*, size_t, size_t*);

static struct VFSStorageOperations g_operations = {
        .Read = __ReadFile,
        .Write = __WriteFile
};

static oserr_t
__QueryFileStats(
        _In_ uuid_t               fileHandleID,
        _In_ StorageDescriptor_t* stats)
{
    struct VFSStat fstats;
    oserr_t        oserr;

    oserr = VFSNodeStatHandle(fileHandleID, &fstats);
    if (oserr != OS_EOK) {
        return oserr;
    }

    stats->DriverID = UUID_INVALID;
    stats->DeviceID = fileHandleID;
    stats->SectorCount = fstats.Size / 512;
    stats->SectorSize  = 512; // TODO this can change in the future
    stats->Flags = 0;
    stats->LUNCount = 1;

    // generate model and serial
    snprintf(
            &stats->Serial[0], sizeof(stats->Serial),
            "file-%u", fileHandleID
    );
    snprintf(
            &stats->Model[0], sizeof(stats->Model),
            "file-mount"
    );
    return OS_EOK;
}

struct VFSStorage*
VFSStorageCreateFileBacked(
        _In_ uuid_t fileHandleID)
{
    struct VFSStorage* storage;
    oserr_t            oserr;

    storage = VFSStorageNew(&g_operations, 0);
    if (storage == NULL) {
        return NULL;
    }

    // setup protocol stuff
    storage->ID = fileHandleID;
    storage->Protocol.StorageType = VFSSTORAGE_TYPE_FILE;
    storage->Protocol.Storage.File.HandleID = fileHandleID;

    // query file info
    oserr = __QueryFileStats(fileHandleID, &storage->Stats);
    if (oserr != OS_EOK) {
        VFSStorageDelete(storage);
        return NULL;
    }
    return storage;
}

static oserr_t __ReadFile(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             buffer,
        _In_ size_t             offset,
        _In_ UInteger64_t*      sector,
        _In_ size_t             count,
        _In_ size_t*            read)
{
    size_t  bytesRead;
    oserr_t oserr = VFSNodeReadAt(
            storage->Protocol.Storage.File.HandleID,
            &(UInteger64_t) { .QuadPart = sector->QuadPart * 512 },
            buffer,
            offset,
            count * 512,
            &bytesRead
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *read = bytesRead / 512;
    return OS_EOK;
}

static oserr_t __WriteFile(
        _In_ struct VFSStorage* storage,
        _In_ uuid_t             buffer,
        _In_ size_t             offset,
        _In_ UInteger64_t*      sector,
        _In_ size_t             count,
        _In_ size_t*            written)
{
    size_t  bytesWritten;
    oserr_t oserr = VFSNodeWriteAt(
            storage->Protocol.Storage.File.HandleID,
            &(UInteger64_t) { .QuadPart = sector->QuadPart * 512 },
            buffer,
            offset,
            count * 512,
            &bytesWritten
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    *written = bytesWritten / 512;
    return OS_EOK;
}
