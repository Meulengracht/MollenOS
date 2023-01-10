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

#include <os/shm.h>

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
        _In_ SHM_t*       shm,
        _In_ SHMHandle_t* handle)
{
    oserr_t oserr;

    oserr = __ValidateNewBuffer(shm);
    if (oserr != OS_EOK) {
        return oserr;
    }

    return OS_ENOTSUPPORTED;
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
        _In_ void*        buffer,
        _In_ SHM_t*       shm,
        _In_ SHMHandle_t* handle)
{
    oserr_t oserr;

    oserr = __ValidateExistingBuffer(shm);
    if (oserr != OS_EOK) {
        return oserr;
    }

    return OS_ENOTSUPPORTED;
}

oserr_t
SHMAttach(
        _In_ uuid_t       shmID,
        _In_ SHMHandle_t* handle)
{

    return OS_ENOTSUPPORTED;
}

oserr_t
SHMDetach(
        _In_ SHMHandle_t* handle)
{

    return OS_ENOTSUPPORTED;
}

oserr_t
SHMMap(
        _In_ SHMHandle_t* handle,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags)
{

    return OS_ENOTSUPPORTED;
}

oserr_t
SHMCommit(
        _In_ SHMHandle_t* handle,
        _In_ vaddr_t      address,
        _In_ size_t       length)
{

    return OS_ENOTSUPPORTED;
}

oserr_t
SHMUnmap(
        _In_ SHMHandle_t* handle)
{

    return OS_ENOTSUPPORTED;
}

oserr_t
SHMGetSGTable(
        _In_ SHMHandle_t*  handle,
        _In_ SHMSGTable_t* sgTable,
        _In_ int           maxCount)
{

    return OS_ENOTSUPPORTED;
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
