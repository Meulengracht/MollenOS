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
 * MollenOS C Library - Server Entry 
 */

#include <ddk/threadpool.h>
#include <ddk/service.h>
#include <os/mollenos.h>
#include "../libc/threads/tls.h"
#include <stdlib.h>

extern OsStatus_t OnLoad(void);
extern OsStatus_t OnUnload(void);
extern OsStatus_t OnEvent(MRemoteCall_t *Message);

extern char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               IsModule,
    _Out_ int*              ArgumentCount);

int __CrtHandleEvent(void *Argument)
{
    // Initiate the message pointer
    MRemoteCall_t *Message = (MRemoteCall_t*)Argument;
    OsStatus_t Result = OnEvent(Message);
    
    // Cleanup and return result
    free(Message);
    return Result == OsSuccess ? 0 : -1;
}

void __CrtServiceEntry(void)
{
    thread_storage_t            Tls;
    MRemoteCall_t               Message;
#ifdef __SERVER_MULTITHREADED
    ThreadPool_t *ThreadPool    = NULL;
#endif
    char *ArgumentBuffer        = NULL;
    int IsRunning               = 1;

    // Initialize environment
    __CrtInitialize(&Tls, 1, NULL);

    // Call the driver load function 
    // - This will be run once, before loop
    if (OnLoad() != OsSuccess) {
        exit(-1);
    }

    // Initialize threadpool
#ifdef __SERVER_MULTITHREADED
    if (ThreadPoolInitialize(THREADPOOL_DEFAULT_WORKERS, &ThreadPool) != OsSuccess) {
        goto Cleanup;
    }

    // Initialize the server event loop
    while (IsRunning) {
        if (RPCListen(UUID_INVALID, &Message, ArgumentBuffer) == OsSuccess) {
            MRemoteCall_t *RpcCopy = (MRemoteCall_t*)malloc(sizeof(MRemoteCall_t));
            memcpy(RpcCopy, &Message, sizeof(MRemoteCall_t));
            ThreadPoolAddWork(ThreadPool, __CrtHandleEvent, RpcCopy);
        }
        else {}
    }

    // Wait for threads to finish
    if (ThreadPoolGetWorkingCount(ThreadPool) != 0) {
        ThreadPoolWait(ThreadPool);
    }

    // Destroy thread-pool
    ThreadPoolDestroy(ThreadPool);

#else
    // Initialize the server event loop
    ArgumentBuffer = (char*)malloc(IPC_MAX_MESSAGELENGTH);
    while (IsRunning) {
        if (RPCListen(UUID_INVALID, &Message, ArgumentBuffer) == OsSuccess) {
            OnEvent(&Message);
        }
    }
#endif

Cleanup:
    OnUnload();
    exit(-1);
}
