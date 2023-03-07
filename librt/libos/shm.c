/**
 * Copyright 2023, Philip Meulengracht
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

#include <internal/_syscalls.h>
#include <os/shm.h>
#include <stdlib.h>

static oserr_t
__ValidateNewBuffer(
        _In_ SHM_t* shm)
{
    if (shm == NULL) {
        return OS_EINVALPARAMS;
    }

    // Type must be 0 when not specifying device memory
    if (!(shm->Flags & SHM_DEVICE) && shm->Type != 0) {
        return OS_ENOTSUPPORTED;
    }

    return OS_EOK;
}

oserr_t
SHMCreate(
        _In_  SHM_t*      shm,
        _Out_ OSHandle_t* handleOut)
{
    oserr_t oserr;

    oserr = __ValidateNewBuffer(shm);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return Syscall_SHMCreate(shm, handle);
}

static oserr_t
__ValidateExistingBuffer(
        _In_ SHM_t* shm)
{
    if (shm == NULL) {
        return OS_EINVALPARAMS;
    }

    // For export buffers, type must always be 0
    if (shm->Type != 0) {
        return OS_ENOTSUPPORTED;
    }

    return OS_EOK;
}

oserr_t
SHMExport(
        _In_  void*       buffer,
        _In_  SHM_t*      shm,
        _Out_ OSHandle_t* handleOut)
{
    oserr_t oserr;

    oserr = __ValidateExistingBuffer(shm);
    if (oserr != OS_EOK) {
        return oserr;
    }
    return Syscall_SHMExport(buffer, shm, handle);
}

oserr_t
SHMAttach(
        _In_  uuid_t      shmID,
        _Out_ OSHandle_t* handleOut)
{
    if (handleOut == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_SHMAttach(shmID, handle);
}

oserr_t
SHMDetach(
        _In_ SHMHandle_t* handle)
{
    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_SHMDetach(handle);
}

oserr_t
SHMMap(
        _In_ OSHandle_t*  handle,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags)
{
    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_SHMMap(handle, offset, length, flags);
}

oserr_t
SHMCommit(
        _In_ OSHandle_t* handle,
        _In_ vaddr_t     address,
        _In_ size_t      length)
{
    if (handle == NULL || handle->Buffer == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_SHMCommit(handle, address, length);
}

oserr_t
SHMUnmap(
        _In_ OSHandle_t* handle)
{
    if (handle == NULL || handle->Buffer == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_SHMUnmap(handle);
}

void*
SHMBuffer(
        _In_ OSHandle_t* handle)
{

}

size_t
SHMBufferLength(
        _In_ OSHandle_t* handle)
{

}

oserr_t
SHMGetSGTable(
        _In_ OSHandle_t*   handle,
        _In_ SHMSGTable_t* sgTable,
        _In_ int           maxCount)
{
    oserr_t status;

    if (handle == NULL || sgTable == NULL) {
        return OS_EINVALPARAMS;
    }

    // get count unless provided,
    // then allocate space and then retrieve full list
    sgTable->Count = maxCount;
    if (maxCount <= 0) {
        status = Syscall_SHMMetrics(handle->ID, &sgTable->Count, NULL);
        if (status != OS_EOK) {
            return status;
        }
    }

    sgTable->Entries = malloc(sizeof(SHMSG_t) * sgTable->Count);
    if (sgTable->Entries == NULL) {
        return OS_EOOM;
    }
    return Syscall_SHMMetrics(handle->ID, &sgTable->Count, sgTable->Entries);
}

oserr_t
SHMSGTableOffset(
        _In_  SHMSGTable_t* sgTable,
        _In_  size_t        offset,
        _Out_ int*          sgIndex,
        _Out_ size_t*       sgOffset)
{
    if (sgTable == NULL || sgIndex == NULL || sgOffset == NULL) {
        return OS_EINVALPARAMS;
    }

    for (int i = 0; i < sgTable->Count; i++) {
        if (offset < sgTable->Entries[i].Length) {
            *sgIndex  = i;
            *sgOffset = offset;
            return OS_EOK;
        }
        offset -= sgTable->Entries[i].Length;
    }
    return OS_ENOENT;
}
