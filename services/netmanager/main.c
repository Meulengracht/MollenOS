/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Network Manager
 * - Contains the implementation of the network-manager which keeps track
 *   of sockets, network interfaces and connectivity status
 */
#define __TRACE

#include <ddk/services/net.h>
#include <ddk/services/service.h>
#include <ddk/utils.h>
#include "manager.h"
#include "socket.h"
#include <os/ipc.h>

OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
    *ServicePathOut = SERVICE_NET_PATH;
    return SocketManagerInitialize();
}

OsStatus_t
OnUnload(void)
{
    return OsSuccess;
}

OsStatus_t
OnEvent(
	_In_ IpcMessage_t* Message)
{
    OsStatus_t Handled = OsSuccess;
    
    TRACE("Networkmanager.OnEvent(%i)", IPC_GET_TYPED(Message, 0));

    switch (IPC_GET_TYPED(Message, 0)) {
        case __NETMANAGER_CREATE_SOCKET: {
            SocketDescriptorPackage_t Package;
            
            UUId_t ProcessHandle = IPC_GET_TYPED(Message, 1);
            int    Domain        = IPC_GET_TYPED(Message, 2);
            int    Type          = IPC_GET_TYPED(Message, 3);
            int    Protocol      = IPC_GET_TYPED(Message, 4);
            
            Package.Status = SocketCreateImpl(ProcessHandle, Domain, Type, Protocol, 
                &Package.SocketHandle, &Package.SendBufferHandle,
                &Package.RecvBufferHandle);
            Handled = IpcReply(Message, &Package, sizeof(SocketDescriptorPackage_t));
        } break;
        case __NETMANAGER_INHERIT_SOCKET: {
            SocketDescriptorPackage_t Package;
            
            UUId_t ProcessHandle = IPC_GET_TYPED(Message, 1);
            UUId_t SocketHandle  = IPC_GET_TYPED(Message, 2);
            
            Package.SocketHandle = SocketHandle;
            Package.Status       = SocketInheritImpl(ProcessHandle, SocketHandle,
                &Package.SendBufferHandle, &Package.RecvBufferHandle);
            Handled = IpcReply(Message, &Package, sizeof(SocketDescriptorPackage_t));
        } break;
        case __NETMANAGER_CLOSE_SOCKET: {
            OsStatus_t Result;
            
            UUId_t       ProcessHandle = IPC_GET_TYPED(Message, 1);
            UUId_t       SocketHandle  = IPC_GET_TYPED(Message, 2);
            unsigned int CloseOptions  = IPC_GET_TYPED(Message, 3);
            
            Result  = SocketShutdownImpl(ProcessHandle, SocketHandle, CloseOptions);
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;
        case __NETMANAGER_BIND_SOCKET: {
            OsStatus_t Result;
            
            const struct sockaddr* Address       = IPC_GET_UNTYPED(Message, 0);
            UUId_t                 ProcessHandle = IPC_GET_TYPED(Message, 1);
            UUId_t                 SocketHandle  = IPC_GET_TYPED(Message, 2);
            
            Result  = SocketBindImpl(ProcessHandle, SocketHandle, Address);
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;
        case __NETMANAGER_CONNECT_SOCKET: {
            OsStatus_t Result;
            
            const struct sockaddr* Address       = IPC_GET_UNTYPED(Message, 0);
            UUId_t                 ProcessHandle = IPC_GET_TYPED(Message, 1);
            UUId_t                 SocketHandle  = IPC_GET_TYPED(Message, 2);
            
            Result  = SocketConnectImpl(ProcessHandle, SocketHandle, Address);
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;
        case __NETMANAGER_ACCEPT_SOCKET: {
            GetSocketAddressPackage_t Package;
            
            UUId_t ProcessHandle = IPC_GET_TYPED(Message, 1);
            UUId_t SocketHandle  = IPC_GET_TYPED(Message, 2);
            
            Package.Status = SocketAcceptImpl(ProcessHandle, SocketHandle, 
                (struct sockaddr*)&Package.Address);
            Handled = IpcReply(Message, &Package, sizeof(GetSocketAddressPackage_t));
        } break;
        case __NETMANAGER_LISTEN_SOCKET: {
            OsStatus_t Result;
            
            UUId_t ProcessHandle   = IPC_GET_TYPED(Message, 1);
            UUId_t SocketHandle    = IPC_GET_TYPED(Message, 2);
            int    ConnectionCount = IPC_GET_TYPED(Message, 3);
            
            Result  = SocketListenImpl(ProcessHandle, SocketHandle, ConnectionCount);
            Handled = IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;
        case __NETMANAGER_GET_SOCKET_ADDRESS: {
            GetSocketAddressPackage_t Package;
            
            UUId_t ProcessHandle = IPC_GET_TYPED(Message, 1);
            UUId_t SocketHandle  = IPC_GET_TYPED(Message, 2);
            
            Package.Status = GetSocketAddressImpl(ProcessHandle, SocketHandle, 
                (struct sockaddr*)&Package.Address);
            Handled = IpcReply(Message, &Package, sizeof(GetSocketAddressPackage_t));
        } break;
        
        default: {
            break;
        }
    }
    return Handled;
}
