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
 * MollenOS MCore - Server & Process Management
 * - The process/server manager is known as Phoenix
 * - In this file we implement Ash functions
 */
#define __MODULE "ASH1"
//#define __TRACE

#include <system/thread.h>
#include <system/utils.h>
#include <process/phoenix.h>
#include <process/process.h>
#include <modules/modules.h>
#include <scheduler.h>
#include <threading.h>
#include <machine.h>
#include <timers.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

/* CreateProcessPipe
 * Creates a new communication pipe available for use. */
OsStatus_t
CreateProcessPipe(
    _In_ SystemProcess_t*   Process,
    _In_ int                Port, 
    _In_ int                Type)
{
    SystemPipe_t*   Pipe;
    DataKey_t       Key;

    TRACE("CreateProcessPipe(Port %i)", Port);
    if (Process == NULL || Port < 0) {
        ERROR("Invalid ash-instance or port id");
        return OsError;
    }

    // Make sure that a pipe on the given Port 
    // doesn't already exist!
    Key.Value.Integer = Port;
    if (CollectionGetDataByKey(Process->Pipes, Key, 0) != NULL) {
        WARNING("The requested pipe already exists");
        return OsSuccess;
    }

    // Create a new pipe and add it to list 
    if (Type == PIPE_RAW) {
        Pipe = CreateSystemPipe(0, PIPE_DEFAULT_ENTRYCOUNT);
    }
    else {
        Pipe = CreateSystemPipe(PIPE_MPMC | PIPE_STRUCTURED_BUFFER, PIPE_DEFAULT_ENTRYCOUNT);
    }
    CollectionAppend(Process->Pipes, CollectionCreateNode(Key, Pipe));
    SchedulerHandleSignalAll((uintptr_t*)Process->Pipes);
    return OsSuccess;
}

/* WaitForProcessPipe
 * Waits for a pipe to be opened on the given
 * ash instance. */
OsStatus_t
WaitForProcessPipe(
    _In_ SystemProcess_t*   Process,
    _In_ int                Port)
{
    DataKey_t   Key;
    int         Run = 1;
    if (Process == NULL) {
        return OsError;
    }

    // Wait for wake-event on pipe
    Key.Value.Integer = Port;
    while (Run) {
        if (CollectionGetDataByKey(Process->Pipes, Key, 0) != NULL) {
            break;
        }
        if (SchedulerThreadSleep((uintptr_t*)Process->Pipes, 5000) == SCHEDULER_SLEEP_TIMEOUT) {
            ERROR("Failed to wait for open pipe, timeout after 5 seconds.");
            return OsError;
        }
     }
    return OsSuccess;
}

/* DestroyProcessPipe
 * Closes the pipe for the given Ash, and cleansup
 * resources allocated by the pipe. This shutsdown
 * any communication on the port */
OsStatus_t
DestroyProcessPipe(
    _In_ SystemProcess_t*   Process,
    _In_ int                Port)
{
    SystemPipe_t*   Pipe;
    DataKey_t       Key;
    if (Process == NULL || Port < 0) {
        return OsSuccess;
    }

    Key.Value.Integer = Port;
    Pipe = (SystemPipe_t*)CollectionGetDataByKey(Process->Pipes, Key, 0);
    if (Pipe == NULL) {
        return OsError;
    }
    DestroySystemPipe(Pipe);
    return CollectionRemoveByKey(Process->Pipes, Key);
}

/* GetProcessPipe
 * Retrieves an existing pipe instance for the given ash
 * and port-id. If it doesn't exist, returns NULL. */
SystemPipe_t*
GetProcessPipe(
    _In_ SystemProcess_t*   Process, 
    _In_ int                Port)
{
    DataKey_t Key;
    if (Process == NULL || Port < 0) {
        return NULL;
    }
    Key.Value.Integer = Port;
    return (SystemPipe_t*)CollectionGetDataByKey(Process->Pipes, Key, 0);
}
