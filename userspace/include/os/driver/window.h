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
 * MollenOS InterProcess Comm Interface
 */

#ifndef _MOLLENOS_WINDOW_H_
#define _MOLLENOS_WINDOW_H_

/* Includes
 * - C-Library */
#include <os/mollenos.h>
#include <os/osdefs.h>
#include <os/ipc/ipc.h>

/* Includes
 * - System */
#include <os/driver/service.h>
#include <os/driver/buffer.h>

/* These definitions are in-place to allow a custom
 * setting of the windowmanager, these are set to values
 * where in theory it should never be needed to have more */
#define __WINDOWMANAGER_INTERFACE_VERSION	1
	
/* These are the different IPC functions supported
 * by the windowmanager, note that some of them might
 * be changed in the different versions, and/or new
 * functions will be added */
#define __WINDOWMANAGER_CREATE				IPC_DECL_FUNCTION(0)
#define __WINDOWMANAGER_DESTROY				IPC_DECL_FUNCTION(1)
#define __WINDOWMANAGER_INVALIDATE			IPC_DECL_FUNCTION(2)
#define __WINDOWMANAGER_CONFIGURE			IPC_DECL_FUNCTION(3)
#define __WINDOWMANAGER_SWAPBUFFER			IPC_DECL_FUNCTION(4)
#define __WINDOWMANAGER_QUERY				IPC_DECL_FUNCTION(5)
#define __WINDOWMANAGER_NEWINPUT			IPC_DECL_FUNCTION(6)

/* SurfaceFormat 
 * Describes the types of pixel formats that are available
 * for surfaces */
typedef enum _MSurfaceFormat {
	ARGB32
} SurfaceFormat_t;

/* SurfaceDescriptor
 * Structure to represent a surface in a 
 * window and information about the buffer size */
typedef struct _MSurfaceDescriptor {
	Rect_t					 Dimensions;
	SurfaceFormat_t			 Format;
	size_t					 Pitch;			// Bytes
} SurfaceDescriptor_t;

/* WindowParameters_t
 * Structure used by the the create window function call, the structure 
 * specifies creation details and flags about the window */
typedef struct _MWindowParameters {
	SurfaceDescriptor_t		Surface;
	unsigned				Flags;
} WindowParameters_t;

/* Structure returned by the window query
 * function, it describes the backbuffer details
 * and the dimensions of the inner/outer region */
typedef struct _MWindowDescriptor {
	Rect_t					Dimensions;
	SurfaceDescriptor_t		Surface;
} WindowDescriptor_t;

/* CreateWindow 
 * Creates a window of the given
 * dimensions and flags. The returned
 * value is the id of the newly created
 * window. Returns NULL on failure */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI Handle_t CreateWindow(WindowParameters_t *Params);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
CreateWindow(
	_In_ WindowParameters_t *Params,
	_In_ BufferObject_t *SurfaceBuffer,
	_Out_ Handle_t *Handle)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize rpc request
	RPCInitialize(&Request, __WINDOWMANAGER_INTERFACE_VERSION, 
		PIPE_RPCOUT, __WINDOWMANAGER_CREATE);

	// Setup rpc arguments
	RPCSetArgument(&Request, 0, (__CONST void*)Params, 
		sizeof(WindowParameters_t));
	RPCSetArgument(&Request, 1, (__CONST void*)SurfaceBuffer,
		GetBufferObjectSize(SurfaceBuffer));

	// Install result buffer
	RPCSetResult(&Request, (__CONST void*)Handle, sizeof(Handle_t));
	
	// Execute the request
	return RPCExecute(&Request, __WINDOWMANAGER_TARGET);
}
#endif

/* DestroyWindow 
 * Destroys a given window 
 * and frees the resources associated with it. */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI void DestroyWindow(Handle_t Handle);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
DestroyWindow(
	_In_ Handle_t Handle)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize rpc request
	RPCInitialize(&Request, __WINDOWMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __WINDOWMANAGER_DESTROY);

	// Setup rpc arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, 
		sizeof(Handle_t));
	
	// Fire off asynchronous event
	return RPCEvent(&Request, __WINDOWMANAGER_TARGET);
}
#endif

/* QueryWindow
 * Queries the window for information about dimensions
 * and its surface, that can be used for direct pixel access */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI void QueryWindow(Handle_t Handle, MWindowDescriptor_t *Descriptor);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
QueryWindow(
	_In_ Handle_t Handle, 
	_Out_ WindowDescriptor_t *Descriptor)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize rpc request
	RPCInitialize(&Request, __WINDOWMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __WINDOWMANAGER_QUERY);

	// Setup rpc arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, 
		sizeof(Handle_t));

	// Install result buffer
	RPCSetResult(&Request, (const void*)Descriptor, 
		sizeof(WindowDescriptor_t));

	// Execute the request
	return RPCExecute(&Request, __WINDOWMANAGER_TARGET);
}
#endif

/* InvalidateWindow
 * Invalides a region of the window
 * based on relative coordinates in the window 
 * if its called with NULL as dimensions it invalidates all */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI void InvalidateWindow(Handle_t Handle, Rect_t *Rectangle);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
InvalidateWindow(
	_In_ Handle_t Handle, 
	_In_ Rect_t *Rectangle)
{
	// Variables
	MRemoteCall_t Request;

	// Initialize rpc request
	RPCInitialize(&Request, __WINDOWMANAGER_INTERFACE_VERSION,
		PIPE_RPCOUT, __WINDOWMANAGER_INVALIDATE);
	
	// Setup rpc arguments
	RPCSetArgument(&Request, 0, (__CONST void*)&Handle, 
		sizeof(Handle_t));
	RPCSetArgument(&Request, 1, (__CONST void*)Rectangle, 
		sizeof(Rect_t));
	
	// Fire off asynchronous event
	return RPCEvent(&Request, __WINDOWMANAGER_TARGET);
}
#endif

#endif //!_MOLLENOS_WINDOW_H_
