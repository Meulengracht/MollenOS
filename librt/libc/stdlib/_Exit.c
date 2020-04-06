/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());
    OsStatus_t               status;
    
    if (IsProcessModule()) {
        Syscall_ModuleExit(exitCode);
    }
    else {
        svc_process_terminate_sync(GetGrachtClient(), &msg, exitCode, &status);
        gracht_vali_message_finish(&msg);
    }
    Syscall_ThreadExit(exitCode);
    for(;;);
}
