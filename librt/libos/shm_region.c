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

#include <os/shm_region.h>
#include <internal/_syscalls.h>
#include <limits.h>
#include <string.h>

oserr_t
SHMRegionsRegister(
    _In_ const OSHandle_t*            buffer,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                count)
{
    if (buffer == NULL || buffer->Type != OSHANDLE_SHM || ranges == NULL) {
        return OS_EINVALPARAMS;
    }
    return Syscall_ShmRegionsRegister(buffer->ID, ranges, count);
}

oserr_t
SHMRegionSessionOpen(
    _In_    const OSHandle_t*         buffer,
    _In_    unsigned int             region,
    _In_    unsigned int             capacity,
    _InOut_ struct SHMRegionSession* session)
{
    struct SHMRegionSession result;
    oserr_t                status;

    if (buffer == NULL || session == NULL || buffer->Type != OSHANDLE_SHM) {
        return OS_EINVALPARAMS;
    }
    if (session->Handle != UUID_INVALID) {
        return OS_EBUSY;
    }
    status = Syscall_ShmRegionSessionOpen(buffer->ID, region, capacity, &result);
    if (status == OS_EOK) {
        *session = result;
    }
    return status;
}

oserr_t
SHMRegionTryBeginCpu(
    _In_    const struct SHMRegionSession* session,
    _InOut_ struct SHMRegionCpuToken*      token,
    _In_    enum SHMCpuAccess              access)
{
    unsigned int generation;
    oserr_t      status;

    if (session == NULL || token == NULL) {
        return OS_EINVALPARAMS;
    }
    if (token->Active) {
        return OS_EBUSY;
    }
    if (token->Generation == UINT_MAX) {
        return OS_EOVERFLOW;
    }
    generation = token->Generation + 1;
    status = Syscall_ShmRegionBeginCpu(session->Handle, token->Slot, generation, access);
    if (status == OS_EOK) {
        token->Generation = generation;
        token->Active = true;
    }
    return status;
}

oserr_t
SHMRegionEndCpu(
    _In_    const struct SHMRegionSession* session,
    _InOut_ struct SHMRegionCpuToken*      token)
{
    oserr_t status;

    if (session == NULL || token == NULL) {
        return OS_EINVALPARAMS;
    }
    if (!token->Active) {
        return OS_EINVALPARAMS;
    }
    status = Syscall_ShmRegionEndCpu(session->Handle, token->Slot, token->Generation);
    if (status == OS_EOK) {
        token->Active = false;
    }
    return status;
}

oserr_t
SHMRegionSessionClose(
    _InOut_ struct SHMRegionSession* session)
{
    oserr_t status;

    if (session == NULL || session->Handle == UUID_INVALID) {
        return OS_EOK;
    }
    status = Syscall_ResourceRelease(session->Handle);
    if (status == OS_EOK) {
        memset(session, 0, sizeof(*session));
    }
    return status;
}
