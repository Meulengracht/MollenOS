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
#include <modules/modules.h>
#include <scheduler.h>
#include <threading.h>
#include <machine.h>
#include <timers.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

/* PhoenixOpenAshPipe
 * Creates a new communication pipe available for use. */
OsStatus_t
PhoenixOpenAshPipe(
    _In_ MCoreAsh_t*    Ash, 
    _In_ int            Port, 
    _In_ int            Type)
{
    // Variables
    SystemPipe_t *Pipe = NULL;
    DataKey_t Key;

    // Debug
    TRACE("PhoenixOpenAshPipe(Port %i)", Port);

    // Sanitize
    if (Ash == NULL || Port < 0) {
        ERROR("Invalid ash-instance or port id");
        return OsError;
    }

    // Make sure that a pipe on the given Port 
    // doesn't already exist!
    Key.Value = Port;
    if (CollectionGetDataByKey(Ash->Pipes, Key, 0) != NULL) {
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
    CollectionAppend(Ash->Pipes, CollectionCreateNode(Key, Pipe));

    // Wake sleepers waiting for pipe creations
    SchedulerHandleSignalAll((uintptr_t*)Ash->Pipes);
    return OsSuccess;
}

/* PhoenixWaitAshPipe
 * Waits for a pipe to be opened on the given
 * ash instance. */
OsStatus_t
PhoenixWaitAshPipe(
    _In_ MCoreAsh_t *Ash, 
    _In_ int         Port)
{
    // Variables
    DataKey_t Key;
    int Run = 1;

    // Sanitize input
    if (Ash == NULL) {
        return OsError;
    }

    // Wait for wake-event on pipe
    Key.Value = Port;
    while (Run) {
        if (CollectionGetDataByKey(Ash->Pipes, Key, 0) != NULL) {
            break;
        }
        if (SchedulerThreadSleep((uintptr_t*)Ash->Pipes, 5000) == SCHEDULER_SLEEP_TIMEOUT) {
            ERROR("Failed to wait for open pipe, timeout after 5 seconds.");
            return OsError;
        }
     }
    return OsSuccess;
}

/* PhoenixCloseAshPipe
 * Closes the pipe for the given Ash, and cleansup
 * resources allocated by the pipe. This shutsdown
 * any communication on the port */
OsStatus_t
PhoenixCloseAshPipe(
    _In_ MCoreAsh_t *Ash, 
    _In_ int         Port)
{
    // Variables
    SystemPipe_t *Pipe = NULL;
    DataKey_t Key;

    // Sanitize input
    if (Ash == NULL || Port < 0) {
        return OsSuccess;
    }

    // Lookup pipe
    Key.Value = Port;
    Pipe = (SystemPipe_t*)CollectionGetDataByKey(Ash->Pipes, Key, 0);
    if (Pipe == NULL) {
        return OsError;
    }

    // Cleanup pipe and remove node
    DestroySystemPipe(Pipe);
    return CollectionRemoveByKey(Ash->Pipes, Key);
}

/* PhoenixGetAshPipe
 * Retrieves an existing pipe instance for the given ash
 * and port-id. If it doesn't exist, returns NULL. */
SystemPipe_t*
PhoenixGetAshPipe(
    _In_ MCoreAsh_t     *Ash, 
    _In_ int             Port)
{
    DataKey_t Key;
    if (Ash == NULL || Port < 0) {
        return NULL;
    }
    
    Key.Value = Port;
    return (SystemPipe_t*)CollectionGetDataByKey(Ash->Pipes, Key, 0);
}
