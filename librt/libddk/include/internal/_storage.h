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

#ifndef __DDK_INTERNAL_STORAGE_H__
#define __DDK_INTERNAL_STORAGE_H__

#include <ddk/convert/grants.h>
#include <os/shm.h>

/**
 * @brief Transfers a range of a local SHM buffer to or from a storage device.
 * A temporary grant token is created for the range, and the sector size comes
 * from the cached device geometry. The driver checks the sector size and byte
 * length again on its side.
 */
oserr_t
__StorageTransfer(
    _In_  uuid_t             driverID,
    _In_  uuid_t             deviceID,
    _In_  uuid_t             bufferID,
    _In_  size_t             offset,
    _In_  const UInteger64_t* sector,
    _In_  size_t             sectorSize,
    _In_  size_t             count,
    _In_  bool               write,
    _Out_ size_t*            completed);

/**
 * @brief Validates a decoded storage request and attaches to its restricted buffer.
 * buffer must be an empty handle on entry. The driver can release it once it has
 * set up its own mapping or reference to the memory, or once CPU access is done.
 */
static inline oserr_t
__StorageAttachTransfer(
    _In_  const struct sys_shm_transfer* description,
    _In_  size_t                         sectorSize,
    _In_  size_t                         count,
    _In_  bool                           write,
    _Out_ OSHandle_t*                    buffer,
    _Out_ size_t*                        offsetOut)
{
    SHMTransfer_t transfer;
    unsigned int  scope = write ? GrantTokenScopeRead : GrantTokenScopeWrite;
    size_t        length;
    oserr_t       status;

    status = __FromSysSHMTransfer(description, &transfer);
    if (status != OS_EOK) {
        return status;
    }
    
    if (sectorSize == 0 || count == 0 || count > UINT32_MAX / sectorSize) {
        return OS_EINVALPARAMS;
    }
    
    length = __SHMTransferLength(&transfer);
    if (length != count * sectorSize) {
        return OS_EINVALPARAMS;
    }

    status = __SHMTransferAttach(&transfer, scope, buffer);
    if (status == OS_EOK) {
        *offsetOut = transfer.RelativeOffset;
    }
    return status;
}

#endif //!__DDK_INTERNAL_STORAGE_H__
