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
 * - Project */
#include "../../utils/log_manager.hpp"
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

class CDisplayFramebuffer : public CDisplay {
public:
    
    // Constructor
    // Initializes the os-mesa context and prepares a backbuffer for the
    // display (vbe/vesa) framebuffer
    CDisplayFramebuffer() {
        int CpuRegisters[4] = { 0 };
        sLog.Info("Creating the opengl context");
        _Context            = OSMesaCreateContext(OSMESA_BGRA, NULL);

        // Select a present-method (basic/sse/sse2)
#if defined(_MSC_VER) && !defined(__clang__)
	    __cpuid(CpuRegisters, 1);
#else
        __cpuid(1, CpuRegisters[0], CpuRegisters[1], CpuRegisters[2], CpuRegisters[3]);
#endif
        if (CpuRegisters[3] & CPUID_FEAT_EDX_SSE2) {
            sLog.Info("Using SSE2 presentation method");
            _BytesStep      = 128;
            _PresentMethod  = present_sse2;
        }
        else if (CpuRegisters[3] & CPUID_FEAT_EDX_SSE) {
            sLog.Info("Using SSE presentation method");
            _BytesStep      = 128;
            _PresentMethod  = present_sse;
        }
        else {
            sLog.Info("Using basic presentation method");
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
    bool Initialize() {
        std::string logmessage = "";
        if (!IsValid()) {
            return false;
        }
        if (QueryDisplayInformation(&_VideoInformation) != OsSuccess) {
            return false;
        }
        
        int Width       = _VideoInformation.Width;
        int Height      = _VideoInformation.Height;
        int Depth       = _VideoInformation.Depth;
        logmessage      = "Creating a backbuffer of size: ";
        logmessage      += std::to_string(Width) + ", " + std::to_string(Height);
        logmessage      += ", " + std::to_string(Depth);
        logmessage      += " (R=" + std::to_string(_VideoInformation.RedPosition);
        logmessage      += ", G=" + std::to_string(_VideoInformation.GreenPosition);
        logmessage      += ", B=" + std::to_string(_VideoInformation.BluePosition) + ")";
        sLog.Info(logmessage.c_str());
        _BackbufferSize = Width * Height * 4 * sizeof(GLubyte);
        _Backbuffer     = std::aligned_alloc(32, _BackbufferSize);
        if (_Backbuffer == nullptr) {
            return false;
        }
        SetDimensions(0, 0, Width, Height);

        // Calculate some values needed for filling the framebuffer
        sLog.Info("Creating access to the display framebuffer");
        _Framebuffer    = CreateDisplayFramebuffer();
        _BytesToCopy    = Width * 4  * sizeof(GLubyte);
        _RowLoops       = _BytesToCopy / _BytesStep;
        _BytesRemaining = _BytesToCopy % _BytesStep;
        _FramebufferEnd = ((char*)_Framebuffer + (_VideoInformation.BytesPerScanline * (Height - 1)));
        return OSMesaMakeCurrent(_Context, _Backbuffer, GL_UNSIGNED_BYTE, Width, Height);
    }

    // IsValid
    // This returns false if the creation of the os-mesa context failed
    bool IsValid() {
        return (_Context != nullptr);
    }

    // Present
    // Flushes the entire backbuffer to the display in a reverse manner
    // as opengl's buffer is backwards
    bool Present() {
        _PresentMethod(_FramebufferEnd, _Backbuffer, _VideoInformation.Height, 
            _RowLoops, _BytesRemaining, _VideoInformation.BytesPerScanline);
        return true;
    }
    
private:
    VideoDescriptor_t   _VideoInformation;
    OSMesaContext       _Context;
    unsigned long       _BackbufferSize;
    void*               _Backbuffer;
    void*               _Framebuffer;
    void*               _FramebufferEnd;

    // Needed by flushing
    void(*_PresentMethod)(void*, void*, int, int, int, int);
    int                 _RowLoops;
    int                 _BytesToCopy;
    int                 _BytesRemaining;
    int                 _BytesStep;
};
