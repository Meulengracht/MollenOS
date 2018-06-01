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
#include <os/ipc/ipc.h>
#include <os/service.h>
#include <os/buffer.h>

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
#define __WINDOWMANAGER_NEWINPUT            IPC_DECL_FUNCTION(4)

/* SurfaceFormat 
 * Describes the types of pixel formats that are available
 * for surfaces */
typedef enum _MSurfaceFormat {
    SurfaceRGBA,
} SurfaceFormat_t;

/* SurfaceDescriptor
 * Structure to represent a surface in a window and information 
 * about the buffer size */
typedef struct _MSurfaceDescriptor {
    Rect_t              Dimensions;
    SurfaceFormat_t     Format;
} SurfaceDescriptor_t;

/* WindowParameters_t
 * Structure used by the the create window function call, the structure 
 * specifies creation details and flags about the window */
typedef struct _MWindowParameters {
    SurfaceDescriptor_t Surface;
    unsigned            Flags;
} WindowParameters_t;

/* Structure returned by the window query
 * function, it describes the backbuffer details and the dimensions 
 * of the inner/outer region */
typedef struct _MWindowDescriptor {
    Rect_t              Dimensions;
    SurfaceDescriptor_t Surface;
} WindowDescriptor_t;

/* CreateWindow 
 * Creates a window of the given dimensions and flags. The returned
 * value is the id of the newly created window. The handle is NULL on failure
 * or set to previous handle if a handle for this process already exists. */
SERVICEAPI OsStatus_t SERVICEABI
CreateWindow(
    _In_  WindowParameters_t*   Params,
    _In_  BufferObject_t*       SurfaceBuffer,
    _Out_ Handle_t*             Handle)
{
    // Variables
    MRemoteCall_t Request;

    // Initialize rpc request
    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, PIPE_RPCOUT, __WINDOWMANAGER_CREATE);

    // Setup rpc arguments
    RPCSetArgument(&Request, 0, (const void*)Params,        sizeof(WindowParameters_t));
    RPCSetArgument(&Request, 1, (const void*)SurfaceBuffer, GetBufferObjectSize(SurfaceBuffer));
    RPCSetResult(&Request, (const void*)Handle, sizeof(Handle_t));
    return RPCExecute(&Request);
}

/* DestroyWindow 
 * Destroys a given window and frees the resources associated with it. */
SERVICEAPI OsStatus_t SERVICEABI
DestroyWindow(
    _In_ Handle_t Handle)
{
    // Variables
    MRemoteCall_t Request;

    // Initialize rpc request
    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, PIPE_RPCOUT, __WINDOWMANAGER_DESTROY);
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle_t));
    return RPCEvent(&Request);
}

/* QueryWindow
 * Queries the window for information about dimensions
 * and its surface, that can be used for direct pixel access */
SERVICEAPI OsStatus_t SERVICEABI
QueryWindow(
    _In_  Handle_t              Handle, 
    _Out_ WindowDescriptor_t*   Descriptor)
{
    // Variables
    MRemoteCall_t Request;

    // Initialize rpc request
    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, PIPE_RPCOUT, __WINDOWMANAGER_QUERY);

    // Setup rpc arguments
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle_t));
    RPCSetResult(&Request, (const void*)Descriptor, sizeof(WindowDescriptor_t));
    return RPCExecute(&Request);
}

/* SwapWindowBackbuffer
 * Invalidates the window and swaps the backbuffer with screen buffer
 * to render the changes made. */
SERVICEAPI OsStatus_t SERVICEABI
SwapWindowBackbuffer(
    _In_ Handle_t Handle)
{
    // Variables
    MRemoteCall_t Request;

    // Initialize rpc request
    RPCInitialize(&Request, __WINDOWMANAGER_TARGET, 
        __WINDOWMANAGER_INTERFACE_VERSION, PIPE_RPCOUT, __WINDOWMANAGER_SWAPBUFFER);
    
    // Setup rpc arguments
    RPCSetArgument(&Request, 0, (const void*)&Handle, sizeof(Handle_t));
    return RPCEvent(&Request);
}

#endif //!_VALI_WINDOWING_H_
