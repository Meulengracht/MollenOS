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
#include <os/grants.h>
#include <os/memory.h>
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

static oserr_t
__TransferFile(
    _In_  struct VFSStorage* storage,
    _In_  uuid_t             buffer,
    _In_  size_t             offset,
    _In_  UInteger64_t*      sector,
    _In_  size_t             count,
    _In_  bool               write,
    _Out_ size_t*            completed)
{
    SHMTransfer_t transfer = { .RangeMode = SHMTransferRangeSubrange };
    UInteger64_t  position;
    size_t        pageSize = MemoryPageSize();
    size_t        start;
    size_t        extent;
    size_t        bytes = 0;
    oserr_t       status;

    // always reset the completed count before starting the transfer
    *completed = 0;
    
    if (count == 0) {
        return OS_EOK;
    }
    
    if (sector->QuadPart > UINT64_MAX / 512 || count > UINT32_MAX / 512 || offset > UINT32_MAX) {
        return OS_EINVALPARAMS;
    }
    
    position.QuadPart = sector->QuadPart * 512;
    start = offset - offset % pageSize;
    if (count * 512 > UINT32_MAX - (offset - start) ||
        count * 512 + offset - start > SIZE_MAX - (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    extent = ((count * 512 + offset - start + pageSize - 1) / pageSize) * pageSize;
    if (extent > UINT32_MAX) {
        return OS_EINVALPARAMS;
    }

    status = OSGrantTokenCreate(
        buffer,
        &(GrantTokenOptions_t) {
            .ResourceType = GrantTokenResourceTypeSHM,
            .TargetType = GrantTokenTargetTypeAny,
            .TargetGlobalHandleID = UUID_INVALID,
            .Scope = write ? GrantTokenScopeRead : GrantTokenScopeRead | GrantTokenScopeWrite,
            .Data.SHM = { .Offset = (uint32_t)start, .Length = (uint32_t)extent }
        },
        &transfer.Token
    );
    if (status != OS_EOK) {
        return status;
    }
    
    transfer.RelativeOffset = (uint32_t)(offset - start);
    transfer.RequestedLength = (uint32_t)(count * 512);
    
    if (write) {
        status = VFSNodeWriteAt(storage->Protocol.Storage.File.HandleID, &position, &transfer, &bytes);
    } else {
        status = VFSNodeReadAt(storage->Protocol.Storage.File.HandleID, &position, &transfer, &bytes);
    }

    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    
    *completed = bytes / 512;
    return status;
}

static oserr_t
__ReadFile(
    _In_  struct VFSStorage* storage,
    _In_  uuid_t             buffer,
    _In_  size_t             offset,
    _In_  UInteger64_t*      sector,
    _In_  size_t             count,
    _Out_ size_t*            read)
{
    bool write = false;
    return __TransferFile(
        storage,
        buffer,
        offset,
        sector,
        count,
        write,
        read
    );
}

static oserr_t
__WriteFile(
    _In_  struct VFSStorage* storage,
    _In_  uuid_t             buffer,
    _In_  size_t             offset,
    _In_  UInteger64_t*      sector,
    _In_  size_t             count,
    _Out_ size_t*            written)
{
    bool write = true;
    return __TransferFile(
        storage,
        buffer,
        offset,
        sector,
        count,
        write,
        written
    );
}
