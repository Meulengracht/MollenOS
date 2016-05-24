/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS - Graphical UI Functions
*/

#ifndef __MOLLENOS_LIBUI__
#define __MOLLENOS_LIBUI__

/* C-Library - Includes */
#include <crtdefs.h>

/* This describes a window handle
* used by UI functions */
#ifndef MWNDHANDLE
#define MWNDHANDLE
typedef void *WndHandle_t;
#endif

/* Define the standard os
* rectangle used for ui
* operations */
#ifndef MRECTANGLE_DEFINED
#define MRECTANGLE_DEFINED
typedef struct _mRectangle
{
	/* Origin */
	int x, y;

	/* Size */
	int h, w;

} Rect_t;
#endif

/* Different kind of ui types 
 * None, Console or App */
#ifndef MUITYPES_DEFINED
#define MUITYPES_DEFINED
typedef enum _MUiType
{
	UiNone,
	UiConsole,
	UiApp

} UiType_t;
#endif

/***********************
* Ui Prototypes
***********************/
_MOS_API void UiConnect(UiType_t UiType);
_MOS_API void UiDisconnect(void);

/* UiCreateWindow
* Creates a window of the given
* dimensions and flags. The returned
* value is the id of the newly created
* window. Returns NULL on failure */
_MOS_API WndHandle_t UiCreateWindow(Rect_t *Dimensions, int Flags);

/* UiDestroyWindow
* Destroys a given window
* and frees the resources associated
* with it. Returns 0 on success */
_MOS_API int UiDestroyWindow(WndHandle_t Handle);

/* UiQueryDimensions
* Queries the dimensions of a window
* handle */
_MOS_API int UiQueryDimensions(WndHandle_t Handle, Rect_t *Dimensions);

/* UiQueryBackbuffer
* Queries the backbuffer handle of a window
* that can be used for direct pixel access to it */
_MOS_API int UiQueryBackbuffer(WndHandle_t Handle, void **Backbuffer, size_t *BackbufferSize);

/* UiInvalidateRect
* Invalides a region of the window
* based on relative coordinates in the window
* if its called with NULL as dimensions it
* invalidates all */
_MOS_API void UiInvalidateRect(WndHandle_t Handle, Rect_t *Rect);

#endif //!__MOLLENOS_LIBUI__