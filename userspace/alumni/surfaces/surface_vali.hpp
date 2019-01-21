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

#include <os/ui.h>
#include "surface.hpp"

class CValiSurface : public CSurface
{
public:
    CValiSurface(CSurfaceRect& Dimensions);
    ~CValiSurface();

    void        Clear(uint32_t Color, const CSurfaceRect& Area, bool InvalidateScreen) override;
    void        Resize(int Width, int Height) override;
    void        Invalidate() override;
    uint8_t*    GetDataPointer(int OffsetX = 0, int OffsetY = 0) override;
    size_t      GetStride() override;
    
    // Color helpers
    uint32_t GetBlendedColor(uint8_t RA, uint8_t GA, uint8_t BA, uint8_t AA,
        uint8_t RB, uint8_t GB, uint8_t BB, uint8_t AB, uint8_t A) override;
    uint32_t GetColor(uint8_t R, uint8_t G, uint8_t B, uint8_t A) override;

private:
    UIWindowParameters_t m_WindowParameters;
    void*                m_WindowBuffer;
};
