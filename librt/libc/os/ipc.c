/**
 * MollenOS
 *
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
 * Inter-Process Communication Interface
 */

#include <internal/_syscalls.h>
#include <os/ipc.h>

OsStatus_t
IpcInvoke()
{
    // syscall
}

OsStatus_t
IpcGetResponse(
    _In_  size_t Timeout,
    _Out_ void** BufferOut)
{
    IpcArena_t* IpcArena;// get_utcb();
    int         SyncValue;

    // Wait for response by 'polling' the value
    SyncValue = atomic_exchange(&IpcArena->ResponseSyncObject, 0);
    while (!SyncValue) {
        if (FutexWait(&IpcArena->ResponseSyncObject, SyncValue, 0, Timeout) == OsTimeout) {
            return OsTimeout;
        }
        SyncValue = atomic_exchange(&IpcArena->ResponseSyncObject, 0);
    }
    *BufferOut = (void*)&IpcArena->Buffer[IPC_ARENA_SIZE - IPC_RESPONSE_MAX_SIZE];
    return OsSuccess;
}

OsStatus_t
IpcReply()
{
    // syscall
}

OsStatus_t
IpcListen(
    _In_  size_t         Timeout,
    _Out_ IpcMessage_t** MessageOut)
{
    IpcArena_t* IpcArena;// get_utcb();
    int         SyncValue;

    // Clear the WriteSyncObject
    atomic_store(&IpcArena->WriteSyncObject, 0);
    (void)FutexWake(&IpcArena->WriteSyncObject, 1, 0);

    // Wait for response by 'polling' the value
    SyncValue = atomic_exchange(&IpcArena->ReadSyncObject, 0);
    while (!SyncValue) {
        if (FutexWait(&IpcArena->ReadSyncObject, SyncValue, 0, Timeout) == OsTimeout) {
            return OsTimeout;
        }
        SyncValue = atomic_exchange(&IpcArena->ReadSyncObject, 0);
    }

    *MessageOut = &IpcArena->Message;
    return OsSuccess;
}
