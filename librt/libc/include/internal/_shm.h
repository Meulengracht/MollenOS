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

#ifndef __INTERNAL_SHM_H__
#define __INTERNAL_SHM_H__

#include <os/types/syscall.h>
#include <internal/_async.h>
#include <internal/_syscalls.h>
#include <internal/_tls.h>

/**
 * @brief Submits a begin request for a shared range. When called from an async
 * context, the caller yields while the request waits.
 *
 * @param handle Handle of the DMA mapping or CPU access session.
 * @param parameters Kind, Write and the range must be filled in by the caller.
 *                   Mode and Deadline are filled in here.
 * @param mode SHMWaitNonBlocking or SHMWaitUntilAvailable.
 * @param deadline Absolute timeout, or NULL to wait forever. Ignored for SHMWaitNonBlocking.
 * @return OS_EINVALPARAMS for an unknown mode, otherwise the result of the syscall.
 */
static inline oserr_t
__SHMAccessWait(
    _In_    uuid_t                           handle,
    _InOut_ struct SHMAccessBeginParameters* parameters,
    _In_    enum SHMWaitMode                 mode,
    _In_    const OSTimestamp_t*             deadline)
{
    OSAsyncContext_t* async = __tls_current()->async_context;
    oserr_t           status;

    if (mode != SHMWaitNonBlocking && mode != SHMWaitUntilAvailable) {
        return OS_EINVALPARAMS;
    }
    
    parameters->Mode = mode;
    if (mode == SHMWaitUntilAvailable) {
        parameters->Deadline = deadline != NULL ? *deadline : *OS_DEADLINE_INFINITE;
    }

    if (async != NULL) {
        OSAsyncContextInitialize(async);
    }
    status = Syscall_ShmAccessBegin(async, handle, parameters);
    return __SyscallAwaitFork(status, async);
}

/**
 * @brief Begins CPU access by building an SHMAccessCpu request for __SHMAccessWait.
 *
 * @param handle Handle of the DMA mapping or CPU access session.
 * @param access SHMCpuAccessRead or SHMCpuAccessWrite.
 * @param mode SHMWaitNonBlocking or SHMWaitUntilAvailable.
 * @param deadline Absolute timeout, or NULL to wait forever. Ignored for SHMWaitNonBlocking.
 * @return OS_EINVALPARAMS for an unknown access value, otherwise the result of __SHMAccessWait.
 */
static inline oserr_t
__SHMAccessWaitCpu(
    _In_ uuid_t               handle,
    _In_ enum SHMCpuAccess    access,
    _In_ enum SHMWaitMode     mode,
    _In_ const OSTimestamp_t* deadline)
{
    struct SHMAccessBeginParameters parameters = {
        .Kind = SHMAccessCpu,
        .Write = access == SHMCpuAccessWrite
    };

    if (access != SHMCpuAccessRead && access != SHMCpuAccessWrite) {
        return OS_EINVALPARAMS;
    }
    return __SHMAccessWait(handle, &parameters, mode, deadline);
}

#endif //!__INTERNAL_SHM_H__
