/**
 * Copyright, Philip Meulengracht
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
 */

#ifndef __OS_INTERNAL_SHM_TRANSFER_H__
#define __OS_INTERNAL_SHM_TRANSFER_H__

#include <os/grants.h>
#include <os/memory.h>

/**
 * @brief Creates a temporary grant token for a nonempty payload. The token covers
 * the whole pages around the payload, while the selector (RelativeOffset and
 * RequestedLength) describes only the payload bytes.
 * The caller must have a grant that covers those pages, and must revoke the
 * returned control handle when the transfer is done.
 */
static inline oserr_t
__SHMTransferCreate(
    _In_  uuid_t            bufferID,
    _In_  size_t            offset,
    _In_  size_t            length,
    _In_  GrantTokenScope_t scope,
    _Out_ SHMTransfer_t*    transferOut)
{
    SHMTransfer_t transfer = { .RangeMode = SHMTransferRangeSubrange };
    size_t        pageSize = MemoryPageSize();
    size_t        start;
    size_t        shmLength;
    oserr_t       status;

    if (length == 0 || offset > UINT32_MAX || length > UINT32_MAX) {
        return OS_EINVALPARAMS;
    }

    start = offset - offset % pageSize;
    if (length > UINT32_MAX - (offset - start) ||
        length + (offset - start) > SIZE_MAX - (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    shmLength = ((length + offset - start + pageSize - 1) / pageSize) * pageSize;
    if (shmLength > UINT32_MAX) {
        return OS_EINVALPARAMS;
    }

    status = OSGrantTokenCreate(
        bufferID,
        &(GrantTokenOptions_t) {
            .ResourceType = GrantTokenResourceTypeSHM,
            .TargetType = GrantTokenTargetTypeAny,
            .TargetGlobalHandleID = UUID_INVALID,
            .Scope = scope,
            .Data.SHM = {
                .Offset = (uint32_t)start,
                .Length = (uint32_t)shmLength
            }
        },
        &transfer.Token
    );
    if (status != OS_EOK) {
        return status;
    }

    transfer.RelativeOffset = (uint32_t)(offset - start);
    transfer.RequestedLength = (uint32_t)length;

    *transferOut = transfer;
    return OS_EOK;
}

#endif //!__OS_INTERNAL_SHM_TRANSFER_H__
