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
 * MollenOS Windowing System Interface for processes
 *  - Processes can use this interface to manage a window. Only one window
 *    per process can be created.
 */

#ifndef _VALI_WINDOWING_H_
#define _VALI_WINDOWING_H_

#include <os/mollenos.h>
#include <os/osdefs.h>
#include <ddk/ipc/ipc.h>
#include <ddk/service.h>
#include <os/ui.h>

/* These definitions are in-place to allow a custom
 * setting of the windowmanager, these are set to values
 * where in theory it should never be needed to have more */
#define __WINDOWMANAGER_INTERFACE_VERSION   1
    
/* These are the different IPC functions supported
 * by the windowmanager, note that some of them might
 * be changed in the different versions, and/or new functions will be added */
#define __WINDOWMANAGER_CREATE              IPC_DECL_FUNCTION(0)
#define __WINDOWMANAGER_DESTROY             IPC_DECL_FUNCTION(1)
#define __WINDOWMANAGER_SWAPBUFFER          IPC_DECL_FUNCTION(2)
#define __WINDOWMANAGER_QUERY               IPC_DECL_FUNCTION(3)

/* CreateWindow 
 * Creates a window of the given dimensions and flags. The returned
 * value is the id of the newly created window. The handle is NULL on failure
 * or set to previous handle if a handle for this process already exists. */
SERVICEAPI OsStatus_t SERVICEABI
CreateWindow(
    _In_  UIWindowParameters_t* Params,
    _In_  UUId_t                BufferHandle,
    _Out_ long*                 WindowHandle)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_CREATE);
    RPCSetArgument(&Request, 0, (const void*)Params,        sizeof(UIWindowParameters_t));
    RPCSetArgument(&Request, 1, (const void*)&BufferHandle, sizeof(UUId_t));
    RPCSetResult(&Request, (const void*)WindowHandle, sizeof(long));
    return RPCExecute(&Request);
}

/* DestroyWindow 
 * Destroys a given window and frees the resources associated with it. */
SERVICEAPI OsStatus_t SERVICEABI
DestroyWindow(
    _In_ long WindowHandle)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_DESTROY);
    RPCSetArgument(&Request, 0, (const void*)&WindowHandle, sizeof(long));
    return RPCEvent(&Request);
}

/* QueryWindow
 * Queries the window for information about dimensions
 * and its surface, that can be used for direct pixel access */
SERVICEAPI OsStatus_t SERVICEABI
QueryWindow(
    _In_  long                      WindowHandle, 
    _Out_ UISurfaceDescriptor_t*    Descriptor)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_QUERY);
    RPCSetArgument(&Request, 0, (const void*)&WindowHandle, sizeof(long));
    RPCSetResult(&Request, (const void*)Descriptor, sizeof(UISurfaceDescriptor_t));
    return RPCExecute(&Request);
}

/* SwapWindowBackbuffer
 * Invalidates the window and swaps the backbuffer with screen buffer
 * to render the changes made. */
SERVICEAPI OsStatus_t SERVICEABI
SwapWindowBackbuffer(
    _In_ long WindowHandle)
{
    MRemoteCall_t Request;

    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, __WINDOWMANAGER_SWAPBUFFER);
    RPCSetArgument(&Request, 0, (const void*)&WindowHandle, sizeof(long));
    return RPCEvent(&Request);
}

#endif //!_VALI_WINDOWING_H_
