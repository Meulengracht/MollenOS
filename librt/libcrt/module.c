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
 * MollenOS C Library - Driver Entry 
 */

#include "../libc/threads/tls.h"
#include <os/mollenos.h>
#include <ddk/driver.h>
#include <stdlib.h>

// Module Interface
extern OsStatus_t OnLoad(void);
extern OsStatus_t OnUnload(void);
extern OsStatus_t OnRegister(MCoreDevice_t*);
extern OsStatus_t OnUnregister(MCoreDevice_t*);
extern OsStatus_t OnQuery(MContractType_t,  int,  MRemoteCallArgument_t*, MRemoteCallArgument_t*, MRemoteCallArgument_t*, MRemoteCallAddress_t*);

extern char**
__CrtInitialize(
    _In_  thread_storage_t* Tls,
    _In_  int               IsModule,
    _Out_ int*              ArgumentCount);

void __CrtModuleEntry(void)
{
    thread_storage_t Tls;
    MRemoteCall_t    Message;
    char*            ArgumentBuffer;
    int              IsRunning = 1;

    // Initialize environment
    __CrtInitialize(&Tls, 1, NULL);

    // Call the driver load function 
    // - This will be run once, before loop
    if (OnLoad() != OsSuccess) {
        exit(-1);
    }
    
    // Initialize the driver event loop
    ArgumentBuffer = (char*)malloc(IPC_MAX_MESSAGELENGTH);
    while (IsRunning) {
        if (RPCListen(UUID_INVALID, &Message, ArgumentBuffer) == OsSuccess) {
            switch (Message.Function) {
                case __DRIVER_REGISTERINSTANCE: {
                    OnRegister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
                } break;
                case __DRIVER_UNREGISTERINSTANCE: {
                    OnUnregister((MCoreDevice_t*)Message.Arguments[0].Data.Buffer);
                } break;
                case __DRIVER_QUERY: {
                    OnQuery((MContractType_t)Message.Arguments[0].Data.Value, 
                        (int)Message.Arguments[1].Data.Value, 
                        &Message.Arguments[2], &Message.Arguments[3], 
                        &Message.Arguments[4], &Message.From);
                } break;
                case __DRIVER_UNLOAD: {
                    IsRunning = 0;
                } break;

                default: {
                    break;
                }
            }
        }
    }
    OnUnload();
    exit(-1);
}
