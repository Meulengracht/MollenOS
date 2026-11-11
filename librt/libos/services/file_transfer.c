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

#include <ddk/convert/grants.h>
#include <ddk/service.h>
#include <gracht/link/vali.h>
#include <internal/_gracht.h>
#include <internal/_utils.h>
#include <internal/_shm_transfer.h>
#include <os/grants.h>
#include <os/memory.h>
#include <os/services/file.h>
#include <os/shm.h>
#include <sys_file_service_client.h>

static oserr_t
__TransferFileBuffer(
    _In_  uuid_t               handle,
    _In_  const UInteger64_t*  position,
    _In_  const SHMTransfer_t* transfer,
    _In_  bool                 write,
    _Out_ size_t*              bytesTransferred)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    struct sys_shm_transfer  description;
    unsigned int             scope = GrantTokenScopeRead;
    size_t                   length;
    size_t                   completed = 0;
    oserr_t                  status;
    int                      result;

    // mark as 0 even on errors
    *bytesTransferred = 0;

    status = __ToSysSHMTransfer(transfer, &description);
    if (status != OS_EOK) {
        return status;
    }
    
    if (!write) {
        scope |= GrantTokenScopeWrite;
    }

    if (((unsigned int)transfer->Token.Options.Scope & scope) != scope) {
        return OS_EPERMISSIONS;
    }

    length = __SHMTransferLength(transfer);

    if (position == NULL) {
        result = sys_file_transfer(
            GetGrachtClient(), &msg.base, __crt_process_id(), handle,
            write ? SYS_TRANSFER_DIRECTION_WRITE : SYS_TRANSFER_DIRECTION_READ,
            &description
        );
    } else {
        result = sys_file_transfer_absolute(
            GetGrachtClient(), &msg.base, __crt_process_id(), handle,
            write ? SYS_TRANSFER_DIRECTION_WRITE : SYS_TRANSFER_DIRECTION_READ,
            position->u.LowPart, position->u.HighPart, &description
        );
    }

    if (result != 0) {
        return OS_EUNKNOWN;
    }

    status = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (status != OS_EOK) {
        return status;
    }

    if (position == NULL) {
        result = sys_file_transfer_result(GetGrachtClient(), &msg.base, &status, &completed);
    } else {
        result = sys_file_transfer_absolute_result(GetGrachtClient(), &msg.base, &status, &completed);
    }

    if (result != 0 || completed > length) {
        return OS_EPROTOCOL;
    }
    
    *bytesTransferred = completed;
    return status;
}

oserr_t
OSTransferFileBuffer(
    _In_  uuid_t               handle,
    _In_  const SHMTransfer_t* transfer,
    _In_  bool                 write,
    _Out_ size_t*              bytesTransferred)
{
    return __TransferFileBuffer(handle, NULL, transfer, write, bytesTransferred);
}

oserr_t
OSTransferFileBufferAt(
    _In_  uuid_t               handle,
    _In_  const UInteger64_t*   position,
    _In_  const SHMTransfer_t* transfer,
    _In_  bool                 write,
    _Out_ size_t*              bytesTransferred)
{
    if (position == NULL) {
        *bytesTransferred = 0;
        return OS_EINVALPARAMS;
    }
    return __TransferFileBuffer(handle, position, transfer, write, bytesTransferred);
}

static oserr_t
__TransferLocalBuffer(
    _In_  uuid_t             handle,
    _In_  const UInteger64_t* position,
    _In_  uuid_t             bufferID,
    _In_  size_t             offset,
    _In_  bool               write,
    _In_  size_t             length,
    _Out_ size_t*            bytesTransferred)
{
    SHMTransfer_t transfer = { 0 };
    oserr_t       status;

    *bytesTransferred = 0;
    if (length == 0) {
        return OS_EOK;
    }
    status = __SHMTransferCreate(
        bufferID, offset, length,
        write ? GrantTokenScopeRead : GrantTokenScopeRead | GrantTokenScopeWrite,
        &transfer
    );
    if (status != OS_EOK) {
        return status;
    }
    status = __TransferFileBuffer(handle, position, &transfer, write, bytesTransferred);
    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    return status;
}

oserr_t
OSTransferFile(
    _In_  uuid_t  handle,
    _In_  uuid_t  bufferID,
    _In_  size_t  bufferOffset,
    _In_  bool    write,
    _In_  size_t  length,
    _Out_ size_t* bytesTransferred)
{
    return __TransferLocalBuffer(handle, NULL, bufferID, bufferOffset, write, length, bytesTransferred);
}

oserr_t
OSTransferFileAt(
    _In_  uuid_t             handle,
    _In_  const UInteger64_t* position,
    _In_  uuid_t             bufferID,
    _In_  size_t             bufferOffset,
    _In_  bool               write,
    _In_  size_t             length,
    _Out_ size_t*            bytesTransferred)
{
    if (position == NULL) {
        *bytesTransferred = 0;
        return OS_EINVALPARAMS;
    }
    return __TransferLocalBuffer(handle, position, bufferID, bufferOffset, write, length, bytesTransferred);
}
