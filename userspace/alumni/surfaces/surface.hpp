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
#pragma once

#include <cstdint>
#include <cstddef>

class CSurfaceRect {
public:
    CSurfaceRect(int x, int y, int Width, int Height)
        : m_X(x), m_Y(y), m_Width(Width), m_Height(Height) { }
    CSurfaceRect(int Width, int Height)
        : CSurfaceRect(0, 0, Width, Height) { }
    CSurfaceRect()
        : CSurfaceRect(0, 0) { }
    ~CSurfaceRect() = default;

    int GetX() const { return m_X; }
    int GetY() const { return m_Y; }
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

private:
    int m_X, m_Y, m_Width, m_Height;
};

class CSurface
{
public:
    CSurface(CSurfaceRect& Dimensions)
        : m_Dimensions(Dimensions) { }
    virtual ~CSurface() = default;

    virtual void        Clear(uint32_t Color, const CSurfaceRect& Area, bool InvalidateScreen) = 0;
    virtual void        Resize(int Width, int Height) = 0;
    virtual void        Invalidate() = 0;
    virtual uint8_t*    GetDataPointer(int OffsetX, int OffsetY) = 0;
    virtual size_t      GetStride() = 0;
    
    // Color helpers
    virtual uint32_t GetBlendedColor(uint8_t RA, uint8_t GA, uint8_t BA, uint8_t AA,
        uint8_t RB, uint8_t GB, uint8_t BB, uint8_t AB, uint8_t A) = 0;
    virtual uint32_t GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A) = 0;

    // Shared functionality
    const CSurfaceRect& GetDimensions() const { return m_Dimensions; }

private:
    CSurfaceRect m_Dimensions;
};
