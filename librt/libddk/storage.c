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
 */

#include <internal/_storage.h>
#include <internal/_gracht.h>
#include <internal/_shm_transfer.h>
#include <internal/_utils.h>
#include <gracht/link/vali.h>
#include <ctt_storage_service_client.h>

oserr_t
__StorageTransfer(
    _In_  uuid_t              driverID,
    _In_  uuid_t              deviceID,
    _In_  uuid_t              bufferID,
    _In_  size_t              offset,
    _In_  const UInteger64_t* sector,
    _In_  size_t              sectorSize,
    _In_  size_t              count,
    _In_  bool                write,
    _Out_ size_t*             completed)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverID);
    struct sys_shm_transfer  description;
    SHMTransfer_t            transfer;
    size_t                   result = 0;
    oserr_t                  status;

    // reset even on failures
    *completed = 0;

    if (count == 0) {
        return OS_EOK;
    }

    if (sectorSize == 0 || count > UINT32_MAX / sectorSize) {
        return OS_EINVALPARAMS;
    }
    
    status = __SHMTransferCreate(
        bufferID,
        offset,
        count * sectorSize,
        write ? GrantTokenScopeRead : GrantTokenScopeRead | GrantTokenScopeWrite,
        &transfer
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __ToSysSHMTransfer(&transfer, &description);
    if (status != OS_EOK) {
        goto cleanup;
    }

    if (ctt_storage_transfer(GetGrachtClient(), &msg.base, deviceID,
                             write ? SYS_TRANSFER_DIRECTION_WRITE : SYS_TRANSFER_DIRECTION_READ,
                             sector->u.LowPart, sector->u.HighPart, &description, count) != 0) {
        status = OS_EUNKNOWN;
        goto cleanup;
    }

    status = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (status != OS_EOK) {
        goto cleanup;
    }

    if (ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, &result) != 0 || result > count) {
        status = OS_EPROTOCOL;
        goto cleanup;
    }

    *completed = result;

cleanup:
    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    return status;
}
