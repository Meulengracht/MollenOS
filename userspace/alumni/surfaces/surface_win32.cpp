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
 * MollenOS Terminal Implementation (Alumnious)
 * - The terminal emulator implementation for Vali. Built on manual rendering and
 *   using freetype as the font renderer.
 */

#include "surface_win32.hpp"
#include <cstddef>
#include <cstdlib>

CWin32Surface::CWin32Surface(CSurfaceRect& Dimensions)
    : CSurface(Dimensions)
{
    m_Bitmap    = (uint32_t*)::malloc(4 * Dimensions.GetWidth() * Dimensions.GetHeight());
    m_hWnd      = nullptr;
}

CWin32Surface::~CWin32Surface() {
    ::free((void*)m_Bitmap);
}

void CWin32Surface::SetHwnd(HWND hWnd)
{
    m_hWnd = hWnd;
}

void CWin32Surface::Clear(uint32_t Color, const CSurfaceRect& Area, bool InvalidateScreen)
{
    // Get the relevant data pointer
    uint8_t* Pointer    = GetDataPointer(Area.GetX(), Area.GetY());
    size_t Stride       = GetDimensions().GetWidth() * 4;
    
    for (int i = 0; i < Area.GetHeight(); i++) {
        for (int j = 0; j < Area.GetWidth(); j++) {
            *((uint32_t*)&Pointer[j * 4]) = Color;
        }
        Pointer += Stride;
    }

    if (InvalidateScreen) {
        Invalidate();
    }
}

void CWin32Surface::Resize(int Width, int Height) {
    // Unimplemented as the protocol for resizing surfaces is not
    // built yet with Vioarr
}

void CWin32Surface::Invalidate() {
    if (m_hWnd != nullptr) {
        RedrawWindow(m_hWnd, NULL, NULL, RDW_INVALIDATE);
    }
}

uint8_t* CWin32Surface::GetDataPointer(int OffsetX, int OffsetY) {
    uint8_t *Pointer = (uint8_t*)m_Bitmap;
    Pointer += ((OffsetY * (GetDimensions().GetWidth() * 4)) + (OffsetX * 4));
    return Pointer;
}

size_t CWin32Surface::GetStride() {
    return GetDimensions().GetWidth() * 4;
}

// Color helpers
uint32_t CWin32Surface::GetBlendedColor(uint8_t RA, uint8_t GA, uint8_t BA, uint8_t AA,
    uint8_t RB, uint8_t GB, uint8_t BB, uint8_t AB, uint8_t A)
{
    uint32_t ColorA = GetColor(RA, GA, BA, AA);
    uint32_t ColorB = GetColor(RB, GB, BB, AB);
    uint32_t RB1 = ((0x100 - A) * (ColorA & 0xFF00FF)) >> 8;
    uint32_t RB2 = (A * (ColorB & 0xFF00FF)) >> 8;
    uint32_t G1 = ((0x100 - A) * (ColorA & 0x00FF00)) >> 8;
    uint32_t G2 = (A * (ColorB & 0x00FF00)) >> 8;
    return (((RB1 | RB2) & 0xFF00FF) + ((G1 | G2) & 0x00FF00)) | 0xFF000000;
}

uint32_t CWin32Surface::GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A)
{
    return ((A << 24) | (R << 16) | (G << 8) | B);
}
