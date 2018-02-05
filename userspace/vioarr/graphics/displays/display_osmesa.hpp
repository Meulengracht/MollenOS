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
 * MollenOS - Vioarr Window Compositor System (Display Interface Implementation)
 *  - The window compositor system and general window manager for
 *    MollenOS. This display implementation is of the default display where
 *    we use osmesa as the backend combined with the native framebuffer
 */
#pragma once

/* Includes
 * - OpenGL */
#include <os/driver/contracts/video.h>
#include "GL/osmesa.h"
#include "GL/glu.h"

class DisplayOsMesa {
public:
    
    // Constructor
    // Initializes the os-mesa context and prepares a backbuffer for the
    // display (vbe/vesa) framebuffer
    DisplayOsMesa() {
        // Initialize the context
        _Context = OSMesaCreateContext(OSMESA_RGBA, NULL);
    }

    // Destructor
    // Cleans up the opengl context and frees the resources allocated.
    ~DisplayOsMesa() {
        if (_Context != nullptr) {
            OSMesaDestroyContext(_Context);
        }
    }

    // Initialize
    // Initializes the display to the given parameters, use -1 for maximum size
    bool Initialize(int Width, int Height) {
        if (!IsValid()) {
            return false;
        }
        
        // Handle -1 in parameters
        VideoDescriptor_t VideoInformation;
        if (Width == -1 || Height == -1) {
            if (ScreenQueryGeometry(&ScreenSize) != OsSuccess) {
                return false;
            }
            Width = (Width == -1) ? VideoInformation.Width : Width;
            Height = (Height == -1) ? VideoInformation.Height : Height;
        }
        _BackbufferSize = Width * Height * 4 * sizeof(GLubyte);
        _Backbuffer = malloc(_BackbufferSize);
        if (_Backbuffer == nullptr) {
            return false;
        }
        return OSMesaMakeCurrent(_Context, _Backbuffer, GL_UNSIGNED_BYTE, Width, Height);
    }

    // IsValid
    // This returns false if the creation of the os-mesa context failed
    bool IsValid() {
        return (_Context != nullptr);
    }

    // Present
    // Flushes the entire backbuffer to the display
    bool Present() {

    }

private:
    OSMesaContext   _Context;
    unsigned long   _BackbufferSize;
    void*           _Backbuffer;
}
