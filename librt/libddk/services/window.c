/* MollenOS
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
 * Windowing Service (Protected) Definitions & Structures
 * - This header describes the base session-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/window.h>

OsStatus_t
CreateWindow(
    _In_  UUId_t                ServiceHandle,
    _In_  UIWindowParameters_t* Params,
    _In_  UUId_t                BufferHandle,
    _Out_ long*                 WindowHandle)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, ServiceHandle, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_CREATE);
    RPCSetArgument(&Request, 0, (const void*)Params,        sizeof(UIWindowParameters_t));
    RPCSetArgument(&Request, 1, (const void*)&BufferHandle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)WindowHandle, sizeof(long));
    return RPCExecute(&Request);
}

OsStatus_t
DestroyWindow(
    _In_ UUId_t ServiceHandle,
    _In_ long   WindowHandle)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, ServiceHandle, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_DESTROY);
    RPCSetArgument(&Request, 0, (const void*)&WindowHandle, sizeof(long));
    return RPCEvent(&Request);
}

OsStatus_t
QueryWindow(
    _In_  UUId_t                 ServiceHandle,
    _In_  long                   WindowHandle, 
    _Out_ UISurfaceDescriptor_t* Descriptor)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, ServiceHandle, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_QUERY);
    RPCSetArgument(&Request, 0, (const void*)&WindowHandle, sizeof(long));
    RPCSetResult(&Request, (const void*)Descriptor, sizeof(UISurfaceDescriptor_t));
    return RPCExecute(&Request);
}

OsStatus_t
SwapWindowBackbuffer(
    _In_ UUId_t ServiceHandle,
    _In_ long   WindowHandle)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, ServiceHandle, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_SWAPBUFFER);
    RPCSetArgument(&Request, 0, (const void*)&WindowHandle, sizeof(long));
    return RPCEvent(&Request);
}
