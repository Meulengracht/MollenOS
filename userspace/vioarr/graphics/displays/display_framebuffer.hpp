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
#include <os/contracts/video.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

/* Includes
 * - Project */
#include "display.hpp"
#include <cstdlib>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

#define CPUID_FEAT_EDX_SSE		1 << 25
#define CPUID_FEAT_EDX_SSE2     1 << 26

/* Extern 
 * Assembler optimized presenting methods for byte copying */
extern "C" void present_basic(void *Framebuffer, void *Backbuffer, int Rows, int RowLoops, int RowRemaining, int LeftoverBytes);
extern "C" void present_sse(void *Framebuffer, void *Backbuffer, int Rows, int RowLoops, int RowRemaining, int LeftoverBytes);
extern "C" void present_sse2(void *Framebuffer, void *Backbuffer, int Rows, int RowLoops, int RowRemaining, int LeftoverBytes);

class CDisplayOsMesa : public CDisplay {
public:
    
    // Constructor
    // Initializes the os-mesa context and prepares a backbuffer for the
    // display (vbe/vesa) framebuffer
    CDisplayOsMesa() {
        int CpuRegisters[4] = { 0 };
        _Context            = OSMesaCreateContext(OSMESA_RGBA, NULL);

        // Select a present-method (basic/sse/sse2)
#if defined(_MSC_VER) && !defined(__clang__)
	    __cpuid(CpuRegisters, 1);
#else
        __cpuid(1, CpuRegisters[0], CpuRegisters[1], CpuRegisters[2], CpuRegisters[3]);
#endif
        if (CpuRegisters[3] & CPUID_FEAT_EDX_SSE2) {
            _BytesStep      = 128;
            _PresentMethod  = present_sse2;
        }
        else if (CpuRegisters[3] & CPUID_FEAT_EDX_SSE) {
            _BytesStep      = 128;
            _PresentMethod  = present_sse;
        }
        else {
            _BytesStep      = 1;
            _PresentMethod  = present_basic;
        }
    }

    // Destructor
    // Cleans up the opengl context and frees the resources allocated.
    ~CDisplayOsMesa() {
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
        if (QueryDisplayInformation(&_VideoInformation) != OsSuccess) {
            return false;
        }
        
        // Handle -1 in parameters
        Width           = (Width == -1) ? _VideoInformation.Width : Width;
        Height          = (Height == -1) ? _VideoInformation.Height : Height;
        _BackbufferSize = Width * Height * 4 * sizeof(GLubyte);
        _Backbuffer     = std::aligned_alloc(32, _BackbufferSize);
        if (_Backbuffer == nullptr) {
            return false;
        }

        // Calculate some values needed for filling the framebuffer
        _BytesToCopy    = Width * 4  * sizeof(GLubyte);
        _RowLoops       = _BytesToCopy / _BytesStep;
        _BytesRemaining = _BytesToCopy % _BytesStep;
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
        _PresentMethod((void*)_VideoInformation.FrameBufferAddress, _Backbuffer, 
            _VideoInformation.Height, _RowLoops, _BytesRemaining, _VideoInformation.BytesPerScanline - _BytesToCopy);
        return true;
    }

private:
    VideoDescriptor_t   _VideoInformation;
    OSMesaContext       _Context;
    unsigned long       _BackbufferSize;
    void*               _Backbuffer;

    // Needed by flushing
    void(*_PresentMethod)(void*, void*, int, int, int, int);
    int                 _RowLoops;
    int                 _BytesToCopy;
    int                 _BytesRemaining;
    int                 _BytesStep;
};
