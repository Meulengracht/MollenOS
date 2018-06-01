/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */

#include <os/service.h>
#include <os/window.h>
#include "vioarr.hpp"
#include "engine/elements/window.hpp"
#include "events/event_window.hpp"

bool ConvertSurfaceFormatToGLFormat(SurfaceFormat_t Format, GLenum &FormatResult, 
    GLenum &InternalFormatResult, int &BytesPerPixel) {
    bool Success = true;
    switch (Format) {
        case SurfaceRGBA: {
            FormatResult            = GL_RGBA;
            InternalFormatResult    = GL_RGBA8;
            BytesPerPixel           = 4;
        } break;

        default: {
            Success = false;
        } break;
    }
    return Success;
} 

Handle_t HandleCreateWindowRequest(MRemoteCallAddress_t *Process,
    WindowParameters_t *Parameters, BufferObject_t *Buffer) {
    CWindow *Window = nullptr;
    Handle_t Result = nullptr;
    GLenum Format, InternalFormat;
    int BytesPerPixel;

    // Does the process already own a window? Then don't create
    // one as we don't want a process to flood us
    Result = sEngine.GetExistingWindowForProcess(Process->Process);
    if (Result != nullptr) {
        return Result;
    }

    // Now we must validate the parameters of the request, so validate
    // the sizes, surface-type and buffer-size
    if (Parameters->Surface.Dimensions.w < 0 || Parameters->Surface.Dimensions.h < 0
        || !ConvertSurfaceFormatToGLFormat(Parameters->Surface.Format, Format, InternalFormat, BytesPerPixel)) {
        return nullptr;
    }

    // Validate the size of the buffer before acquiring it
    if (GetBufferSize(Buffer) < (Parameters->Surface.Dimensions.w * Parameters->Surface.Dimensions.h * BytesPerPixel)) {
        return nullptr;
    }

    // Last step is to acquire the buffer and make sure we have access to it
    if (AcquireBuffer(Buffer) != OsSuccess) {
        return nullptr;
    }

    // Everything is ok, create the window, set elements up and queue up for render
    Window = new CWindow(sEngine.GetContext());
    Window->SetOwner(Process->Process);
    Window->SetWidth(Parameters->Surface.Dimensions.w);
    Window->SetHeight(Parameters->Surface.Dimensions.h);

    Window->SetStreamingBufferFormat(Format, InternalFormat);
    Window->SetStreamingBufferDimensions(Parameters->Surface.Dimensions.w, Parameters->Surface.Dimensions.h);
    Window->SetStreamingBuffer(BufferObject);

    Window->SwapOnNextUpdate(true);
    Window->SetStreaming(true);
    Window->SetActive(true);
    sVioarr.QueueEvent(new CWindowCreatedEvent(Window));
    return (Handle_t)Window;
}

void MessageHandler() {
    char *ArgumentBuffer    = NULL;
    bool IsRunning          = true;
    MRemoteCall_t Message;

    // Listen for messages
    ArgumentBuffer = (char*)::malloc(IPC_MAX_MESSAGELENGTH);
    while (IsRunning) {
        if (RPCListen(&Message, ArgumentBuffer) == OsSuccess) {
            if (Message.Function == __WINDOWMANAGER_CREATE) {
                WindowParameters_t *Parameters  = nullptr;
                BufferObject_t *BufferCopy      = nullptr;
                BufferObject_t *Buffer          = nullptr;
                Handle_t Result                 = nullptr;
                RPCCastArgumentToPointer(&Message.Arguments[0], (void**)&Parameters);
                RPCCastArgumentToPointer(&Message.Arguments[1], (void**)&Buffer);
                
                // Create a copy of the buffer-object first
                BufferCopy = (BufferObject_t*)::malloc(GetBufferObjectSize(Buffer));
                memcpy((void*)BufferCopy, Buffer, GetBufferObjectSize(Buffer));
                Result = HandleCreateWindowRequest(&Message.From, Parameters, BufferCopy);
                RPCRespond(&Message, Result, sizeof(Result));
            }
            if (Message.Function == __WINDOWMANAGER_DESTROY) {
                Handle_t Pointer = (Handle_t)Message.Arguments[0].Data.Value;
                if (sEngine.IsWindowHandleValid(Pointer)) {
                    sVioarr.QueueEvent(new CWindowDestroyEvent((CWindow*)Pointer));
                }
            }
            if (Message.Function == __WINDOWMANAGER_SWAPBUFFER) {
                Handle_t Pointer = (Handle_t)Message.Arguments[0].Data.Value;
                if (sEngine.IsWindowHandleValid(Pointer)) {
                    sVioarr.QueueEvent(new CWindowUpdateEvent((CWindow*)Pointer));
                }
            }
            if (Message.Function == __WINDOWMANAGER_QUERY) {
                
            }
            if (Message.Function == __WINDOWMANAGER_NEWINPUT) {
                
            }
        }
    }
}

// Spawn the message handler for compositor
void VioarrCompositor::SpawnMessageHandler() {
    _MessageThread = new std::thread(MessageHandler);
}

int main(int argc, char **argv) {
    if (RegisterService(__WINDOWMANAGER_TARGET) != OsSuccess) {
        // Only once instance at the time
        return -1;
    }
    return sVioarr.Run();
}
