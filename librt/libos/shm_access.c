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

#include <os/shm.h>
#include <internal/_shm.h>

oserr_t
SHMAccessOpen(
    _In_    const OSHandle_t*        buffer,
    _In_    size_t                   offset,
    _In_    size_t                   length,
    _InOut_ struct SHMAccessSession* session)
{
    if (buffer == NULL || session == NULL || buffer->Type != OSHANDLE_SHM) {
        return OS_EINVALPARAMS;
    }

    if (session->Handle != UUID_INVALID) {
        return OS_EBUSY;
    }

    return Syscall_ShmAccessCreate(buffer->ID, offset, length, &session->Handle);
}

oserr_t
SHMAccessBeginCpu(
    _In_ struct SHMAccessSession* session,
    _In_ enum SHMCpuAccess        access,
    _In_ enum SHMWaitMode         mode,
    _In_ const OSTimestamp_t*     deadline)
{
    if (session == NULL) {
        return OS_EINVALPARAMS;
    }
    return __SHMAccessWaitCpu(session->Handle, access, mode, deadline);
}

oserr_t
SHMAccessEndCpu(
    _In_ struct SHMAccessSession* session)
{
    if (session == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_ShmAccessEnd(session->Handle, SHMAccessCpu, 0);
}

oserr_t
SHMAccessClose(
    _InOut_ struct SHMAccessSession* session)
{
    oserr_t status;

    if (session == NULL || session->Handle == UUID_INVALID) {
        return OS_EOK;
    }

    status = Syscall_ResourceRelease(session->Handle);
    if (status == OS_EOK) {
        session->Handle = UUID_INVALID;
    }
    return status;
}
