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
 * User Interface
 *  - Provides functionality to create and manage windows used by the program
 */

#ifndef __USER_INTERFACE__
#define __USER_INTERFACE__

#include <os/osdefs.h>

typedef enum _UiSurfaceFormat {
    SurfaceRGBA,
} UISurfaceFormat_t;

typedef struct _UiRectangle {
    int x, y;
    int w, h;
} Rect_t;

/* UISurfaceDescriptor
 * Structure to represent a surface in a window and information 
 * about the buffer size */
typedef struct _UiSurfaceDescriptor {
    Rect_t            Dimensions;
    UISurfaceFormat_t Format;
} UISurfaceDescriptor_t;

/* UIWindowParameters_t
 * Structure used by the the create window function call, the structure 
 * specifies creation details and flags about the window */
typedef struct _UiWindowParameters {
    UISurfaceDescriptor_t Surface;
    unsigned              Flags;
    UUId_t                WmPipeHandle;
} UIWindowParameters_t;

_CODE_BEGIN
/* UiParametersSetDefault
 * Set(s) default window parameters for the given window param structure. */
CRTDECL(void,
UiParametersSetDefault(
    _In_  UIWindowParameters_t* Descriptor));

/* UiRegisterWindow
 * Registers a new window with the window manage with the given 
 * configuration. If the configuration is invalid, OsError is returned. */
CRTDECL(OsStatus_t,
UiRegisterWindow(
    _In_  UIWindowParameters_t* Descriptor,
    _Out_ void**                WindowBuffer));

/* UiSwapBackbuffer
 * Presents the current backbuffer and rendering all changes made to the window. */
CRTDECL(OsStatus_t,
UiSwapBackbuffer(void));
_CODE_END

#endif // !__USER_INTERFACE__
