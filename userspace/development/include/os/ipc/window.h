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
#include <os/driver/server.h>

/* The export macro, and is only set by the
 * the actual implementation of the windowmanager */
#ifdef __WINDOWMANAGER_EXPORT
#define __WNDAPI __CRT_EXTERN
#else
#define __WNDAPI static __CRT_INLINE
#endif

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
#define __WINDOWMANAGER_QUERY				IPC_DECL_FUNCTION(3)

/* Structure to represent a surface in a 
 * window, it consists of a pixel-buffer
 * and information about the buffer size */
typedef struct _MSurfaceDescriptor {
	Rect_t					Dimensions;
	const void				*Backbuffer;
	size_t					BufferSize;
	size_t					BufferPitch;
} MSurfaceDescriptor_t;

/* Structure used by the the create
 * window function call, the structure 
 * specifies creation details and flags about
 * the window */
typedef struct _MWindowParameters {
	Rect_t					Dimensions;
	unsigned				Flags;
} WindowParameters_t;

/* Structure returned by the window query
 * function, it describes the backbuffer details
 * and the dimensions of the inner/outer region */
typedef struct _MWindowDescriptor {
	Rect_t					Dimensions;
	MSurfaceDescriptor_t	Surface;
} MWindowDescriptor_t;

/* CreateWindow 
 * Creates a window of the given
 * dimensions and flags. The returned
 * value is the id of the newly created
 * window. Returns NULL on failure */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI WndHandle_t CreateWindow(WindowParameters_t *Params);
#else
__WNDAPI WndHandle_t CreateWindow(WindowParameters_t *Params)
{
	/* Variables */
	MRemoteCall_t Request;
	WndHandle_t Result;
	RPCInitialize(&Request, PIPE_DEFAULT, __WINDOWMANAGER_CREATE);
	RPCSetArgument(&Request, 0, (const void*)Params, sizeof(WindowParameters_t));
	RPCSetResult(&Request, (const void*)&Result, sizeof(WndHandle_t));
	RPCEvaluate(&Request, __WINDOWMANAGER_TARGET);
	return Result;
}
#endif

/* DestroyWindow 
 * Destroys a given window 
 * and frees the resources associated with it. */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI void DestroyWindow(WndHandle_t Handle);
#else
__WNDAPI OsStatus_t DestroyWindow(WndHandle_t Handle)
{
	/* Variables */
	MRemoteCall_t Request;
	RPCInitialize(&Request, PIPE_DEFAULT, __WINDOWMANAGER_DESTROY);
	RPCSetArgument(&Request, 0, (const void*)Handle, sizeof(WndHandle_t));
	return RPCExecute(&Request, __WINDOWMANAGER_TARGET);
}
#endif

/* QueryWindow
 * Queries the window for information about dimensions
 * and its surface, that can be used for direct pixel access */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI void QueryWindow(WndHandle_t Handle, MWindowDescriptor_t *Descriptor);
#else
__WNDAPI OsStatus_t QueryWindow(WndHandle_t Handle, MWindowDescriptor_t *Descriptor)
{
	/* Variables */
	MRemoteCall_t Request;
	RPCInitialize(&Request, PIPE_DEFAULT, __WINDOWMANAGER_QUERY);
	RPCSetArgument(&Request, 0, (const void*)Handle, sizeof(WndHandle_t));
	RPCSetResult(&Request, (const void*)Descriptor, sizeof(MWindowDescriptor_t));
	return RPCEvaluate(&Request, __WINDOWMANAGER_TARGET);
}
#endif

/* InvalidateWindow
 * Invalides a region of the window
 * based on relative coordinates in the window 
 * if its called with NULL as dimensions it invalidates all */
#ifdef __WINDOWMANAGER_EXPORT
__WNDAPI void InvalidateWindow(WndHandle_t Handle, Rect_t *Rectangle);
#else
__WNDAPI OsStatus_t InvalidateWindow(WndHandle_t Handle, Rect_t *Rectangle)
{
	/* Variables */
	MRemoteCall_t Request;
	RPCInitialize(&Request, PIPE_DEFAULT, __WINDOWMANAGER_INVALIDATE);
	RPCSetArgument(&Request, 0, (const void*)Handle, sizeof(WndHandle_t));
	RPCSetArgument(&Request, 1, (const void*)Rectangle, sizeof(Rect_t));
	return RPCExecute(&Request, __WINDOWMANAGER_TARGET);
}
#endif

#endif //!_MOLLENOS_WINDOW_H_
