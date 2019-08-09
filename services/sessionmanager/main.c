/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Session Manager
 * - Contains the implementation of the session-manager which keeps track
 *   of all users and their running applications.
 */
#define __TRACE

#include <ds/collection.h>
#include <os/services/process.h>
#include <ddk/services/session.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static Collection_t Services               = COLLECTION_INIT(KeyId);
static UUId_t       ServiceHandleGenerator = 0;
static UUId_t       WindowingSystemId      = UUID_INVALID;

OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
    *ServicePathOut = SERVICE_SESSION_PATH;
    return OsSuccess;
}

OsStatus_t
OnUnload(void)
{
    return OsSuccess;
}

OsStatus_t
OnEvent(
	_In_ MRemoteCall_t* Message)
{
    ProcessStartupInformation_t StartupInformation;
    OsStatus_t Result = OsSuccess;
    char       PathBuffer[64];
    
    TRACE("Sessionmanager.OnEvent(%i)", Message->Function);

    switch (Message->Function) {
        case __SESSIONMANAGER_REGISTER: {
            ServiceObject_t*      Service;
            const char*           Name    = RPCGetStringArgument(Message, 0);
            ServiceCapabilities_t Caps    = (ServiceCapabilities_t)Message->Arguments[1].Data.Value;
            UUId_t                Channel = (UUId_t)Message->Arguments[2].Data.Value;
            UUId_t                Handle  = ServiceHandleGenerator++;
            DataKey_t             Key     = { .Value.Id = Handle };
            
            Service = (ServiceObject_t*)malloc(sizeof(ServiceObject_t));
            if (!Service) {
                return OsOutOfMemory;
            }
            memset(Service, 0, sizeof(ServiceObject_t));
            memcpy(&Service->Name[0], Name, strlen(Name));
            
            Service->Capabilities  = Caps;
            Service->ChannelHandle = Channel;
            CollectionAppend(&Services, CollectionCreateNode(Key, Service));
            return RPCRespond(&Message->From, (const void*)&Handle, sizeof(UUId_t));
        } break;
        
        case __SESSIONMANAGER_UNREGISTER: {
            UUId_t           ServiceHandle = (UUId_t)Message->Arguments[0].Data.Value;
            DataKey_t        Key           = { .Value.Id = ServiceHandle };
            
            Result = CollectionRemoveByKey(&Services, Key);
            return RPCRespond(&Message->From, (const void*)&Result, sizeof(OsStatus_t));
        } break;
        
        case __SESSIONMANAGER_LOOKUP: {
            ServiceCapabilities_t Caps           = (ServiceCapabilities_t)Message->Arguments[0].Data.Value;
            size_t                Count          = MIN(Message->Arguments[1].Data.Value, 8);
            ServiceObject_t*      ServiceResults = malloc(sizeof(ServiceObject_t) * Count);
            int                   i              = 0;
            if (!ServiceResults) {
                return OsOutOfMemory;
            }
            
            memset(ServiceResults, 0, sizeof(ServiceObject_t) * Count);
            
            foreach(Node, &Services) {
                ServiceObject_t* Service = Node->Data;
                if ((Service->Capabilities & Caps) == Caps) {
                    memcpy(&ServiceResults[i], Service, sizeof(ServiceObject_t));
                    i++;
                }
            }
            
            Result = RPCRespond(&Message->From, (const void*)ServiceResults, sizeof(ServiceObject_t) * Count);
            free(ServiceResults);
            return Result;
        } break;
        
        case __SESSIONMANAGER_NEWDEVICE: {
            if (WindowingSystemId == UUID_INVALID) {
                // The identifier might be stored as a value here if less than a specific
                // amount of bytes
                const char* DiskIdentifier = RPCGetStringArgument(Message, 0);

                // Clear up buffer and spawn app
                memset(&PathBuffer[0], 0, sizeof(PathBuffer));
#ifdef __OSCONFIG_RUN_CPPTESTS
                sprintf(&PathBuffer[0], "%s:/shared/bin/stest.app", DiskIdentifier);
#else
                sprintf(&PathBuffer[0], "%s:/shared/bin/vioarr.app", DiskIdentifier);
#endif
                TRACE("Spawning %s", &PathBuffer[0]);
                InitializeStartupInformation(&StartupInformation);
                StartupInformation.InheritFlags = PROCESS_INHERIT_STDOUT | PROCESS_INHERIT_STDERR;
                WindowingSystemId = ProcessSpawnEx(&PathBuffer[0], NULL, &StartupInformation);
            }
        } break;
        case __SESSIONMANAGER_LOGIN: {
            // if error give a fake delay of 1 << min(attempt_num, 31) if the first 5 attempts are wrong
            // reset on login_success
        } break;
        case __SESSIONMANAGER_LOGOUT: {

        } break;

        default: {
            break;
        }
    }
    return Result;
}
