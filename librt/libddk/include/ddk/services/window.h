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

#ifndef __DDK_SERVICES_WINDOW_H__
#define __DDK_SERVICES_WINDOW_H__

#include <ddk/ddkdefs.h>
#include <ddk/ipc/ipc.h>
#include <os/types/ui.h>

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
DDKDECL(OsStatus_t,
CreateWindow(
    _In_  UUId_t                ServiceHandle,
    _In_  UIWindowParameters_t* Params,
    _In_  UUId_t                BufferHandle,
    _Out_ long*                 WindowHandle));

/* DestroyWindow 
 * Destroys a given window and frees the resources associated with it. */
DDKDECL(OsStatus_t,
DestroyWindow(
    _In_ UUId_t ServiceHandle,
    _In_ long   WindowHandle));

/* QueryWindow
 * Queries the window for information about dimensions
 * and its surface, that can be used for direct pixel access */
DDKDECL(OsStatus_t,
QueryWindow(
    _In_  UUId_t                 ServiceHandle,
    _In_  long                   WindowHandle, 
    _Out_ UISurfaceDescriptor_t* Descriptor));

/* SwapWindowBackbuffer
 * Invalidates the window and swaps the backbuffer with screen buffer
 * to render the changes made. */
DDKDECL(OsStatus_t,
SwapWindowBackbuffer(
    _In_ UUId_t ServiceHandle,
    _In_ long   WindowHandle));

#endif //!__SDK_WINDOW_H__
