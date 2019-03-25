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
 * Ui Service Definitions & Structures
 * - This header describes the base ui-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/services/session.h>
#include <os/services/targets.h>
#include <os/services/ui.h>
#include <ddk/services/window.h>
#include <ddk/buffer.h>
#include <assert.h>
#include <stdlib.h>
#include "../stdio/local.h"

static DmaBuffer_t* ProgramWindowBuffer    = NULL;
static long         ProgramWindowHandle    = -1;
static UUId_t       WindowingServiceHandle = UUID_INVALID;

static OsStatus_t
GetWindowingService()
{
    ServiceObject_t WindowService = { { 0 } };
    OsStatus_t      Status        = GetServiceObjectsWithCapabilities(WindowingService, &WindowService, 1);
    if (Status == OsSuccess && WindowService.Capabilities != 0) {
        WindowingServiceHandle = WindowService.ChannelHandle;
    }
    return Status;
}

void
UiParametersSetDefault(
    _In_  UIWindowParameters_t* Descriptor)
{
    StdioObject_t* StdioStdin = get_ioinfo(STDIN_FILENO);
    
    // Sanitize parameters
    assert(Descriptor != NULL);
    Descriptor->Flags           = 0;
    Descriptor->Surface.Format  = SurfaceRGBA;
    
    Descriptor->Surface.Dimensions.x    = -1;
    Descriptor->Surface.Dimensions.y    = -1;
    Descriptor->Surface.Dimensions.w    = 450;
    Descriptor->Surface.Dimensions.h    = 300;
    
    // Sanitize that this is indeed a pipe handle
    if (StdioStdin != NULL && StdioStdin->handle.InheritationType == STDIO_HANDLE_PIPE) {
        Descriptor->InputPipeHandle = StdioStdin->handle.InheritationHandle;
    }
    Descriptor->WmEventPipeHandle = UUID_INVALID;
}

void
UiUnregisterWindow(void)
{
    if (ProgramWindowHandle != -1 && WindowingServiceHandle != UUID_INVALID) {
        DestroyWindow(WindowingServiceHandle, ProgramWindowHandle);
    }
}

OsStatus_t
UiRegisterWindow(
    _In_  UIWindowParameters_t* Descriptor,
    _Out_ void**                WindowBuffer)
{
    OsStatus_t  Status;
    size_t      BytesNeccessary = 0;

    // Sanitize parameters
    assert(Descriptor != NULL);
    assert(WindowBuffer != NULL);
    
    // Find the windowing service
    if (WindowingServiceHandle == UUID_INVALID) {
        if (GetWindowingService() != OsSuccess) {
            return OsNotSupported;
        }
    }
    
    // Calculate how many bytes are needed by checking sizes requested.
    if (Descriptor->Surface.Dimensions.w <= 100)    { Descriptor->Surface.Dimensions.w = 450; }
    if (Descriptor->Surface.Dimensions.h <= 100)    { Descriptor->Surface.Dimensions.h = 300; }
    BytesNeccessary = Descriptor->Surface.Dimensions.w * Descriptor->Surface.Dimensions.h * 4;

    // Create the buffer object
    ProgramWindowBuffer = CreateBuffer(UUID_INVALID, BytesNeccessary);
    if (ProgramWindowBuffer == NULL) {
        return OsError;
    }

    // Create the window
    Status = CreateWindow(WindowingServiceHandle, Descriptor, 
        GetBufferHandle(ProgramWindowBuffer), &ProgramWindowHandle);
    if (Status == OsSuccess) {
        atexit(UiUnregisterWindow);
    }
    return Status;
}

OsStatus_t
UiSwapBackbuffer(void)
{
    if (ProgramWindowHandle == -1 || WindowingServiceHandle == UUID_INVALID) {
        return OsError;
    }
    return SwapWindowBackbuffer(WindowingServiceHandle, ProgramWindowHandle);
}
