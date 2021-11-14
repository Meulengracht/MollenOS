/**
 * Copyright 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C-Support Exit Implementation
 * - Definitions, prototypes and information needed.
 */

#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <internal/_utils.h>

void
_Exit(
    _In_ int exitCode)
{
    if (IsProcessModule()) {
        Syscall_ModuleExit(exitCode);
    }
    else {
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
        OsStatus_t               status;
        sys_process_terminate(GetGrachtClient(), &msg.base, *GetInternalProcessId(), exitCode);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_terminate_result(GetGrachtClient(), &msg.base, &status);
    }

    Syscall_ThreadExit(exitCode);
    for(;;);
}
