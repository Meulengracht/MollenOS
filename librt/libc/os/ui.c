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
 * UI Communication interface with window service
 *  - Provides functionality to create and manage windows used by the program
 */

#include <ddk/window.h>
#include <ddk/buffer.h>
#include <assert.h>
#include <stdlib.h>
#include "../stdio/local.h"

static DmaBuffer_t* ProgramWindowBuffer = NULL;
static long         ProgramWindowHandle = -1;

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
    if (ProgramWindowHandle != -1) {
        DestroyWindow(ProgramWindowHandle);
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
    Status = CreateWindow(Descriptor, GetBufferHandle(ProgramWindowBuffer), &ProgramWindowHandle);
    if (Status == OsSuccess) {
        atexit(UiUnregisterWindow);
    }
    return Status;
}

OsStatus_t
UiSwapBackbuffer(void)
{
    if (ProgramWindowHandle == -1) {
        return OsError;
    }
    return SwapWindowBackbuffer(ProgramWindowHandle);
}
