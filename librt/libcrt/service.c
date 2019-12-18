/* MollenOS
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
 * MollenOS C Library - Server Entry 
 */

#include <ddk/services/service.h>
#include <ddk/threadpool.h>
#include <ddk/utils.h>
#include <os/ipc.h>
#include <os/mollenos.h>
#include "../libc/threads/tls.h"
#include <stdlib.h>

extern OsStatus_t OnLoad(char**);
extern OsStatus_t OnUnload(void);
extern OsStatus_t OnEvent(IpcMessage_t* Message);

extern char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               IsModule,
    _Out_ int*              ArgumentCount);

int __CrtHandleEvent(void *Argument)
{
    // Initiate the message pointer
    IpcMessage_t* Message = (IpcMessage_t*)Argument;
    OsStatus_t Result = OnEvent(Message);
    
    // Cleanup and return result
    free(Message);
    return Result == OsSuccess ? 0 : -1;
}

void __CrtServiceEntry(void)
{
    thread_storage_t            Tls;
    IpcMessage_t*               Message;
    char*                       Path;
#ifdef __SERVER_MULTITHREADED
    ThreadPool_t *ThreadPool    = NULL;
#endif
    int IsRunning               = 1;

    // Initialize environment
    __CrtInitialize(&Tls, 1, NULL);

    // Call the driver load function 
    // - This will be run once, before loop
    if (OnLoad(&Path) != OsSuccess) {
        exit(-1);
    }
    
    if (RegisterPath(Path) != OsSuccess) {
        goto Cleanup;
    }

    // Initialize threadpool
#ifdef __SERVER_MULTITHREADED
    if (ThreadPoolInitialize(THREADPOOL_DEFAULT_WORKERS, &ThreadPool) != OsSuccess) {
        goto Cleanup;
    }

    // Initialize the server event loop
    while (IsRunning) {
        if (IpcListen(0, &Message) == OsSuccess) {
            IpcMessage_t* MessageItem = (IpcMessage_t*)malloc(Message->MetaLength);
            memcpy(MessageItem, Message, Message->MetaLength);
            ThreadPoolAddWork(ThreadPool, __CrtHandleEvent, MessageItem);
        }
    }

    // Wait for threads to finish
    if (ThreadPoolGetWorkingCount(ThreadPool) != 0) {
        ThreadPoolWait(ThreadPool);
    }

    // Destroy thread-pool
    ThreadPoolDestroy(ThreadPool);

#else
    // Initialize the server event loop
    while (IsRunning) {
        if (IpcListen(0, &Message) == OsSuccess) {
            OsStatus_t Status = OnEvent(Message);
            if (Status != OsSuccess) {
                WARNING("[service] [on_event] returned %u", Status);
            }
        }
    }
#endif

Cleanup:
    OnUnload();
    exit(-1);
}
