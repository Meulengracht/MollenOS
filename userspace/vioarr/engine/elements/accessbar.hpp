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
#pragma once
#include "../entity.hpp"
#include <string>

class CLabel;

// AccessBar Settings
// Adjustable access-bar layout settings
#define ACCESSBAR_HEADER_HEIGHT             16

#define ACCESSBAR_FILL_COLOR_RGBA           229, 229, 232, 255
#define ACCESSBAR_HEADER_RGBA               103, 103, 103, 255

class CAccessBar : public CEntity {
public:
    CAccessBar(CEntity* Parent, NVGcontext* VgContext, int Width, int Height);
    CAccessBar(NVGcontext* VgContext, int Width, int Height);
    ~CAccessBar();

protected:
    // Override the inherited methods
    void        Draw(NVGcontext* VgContext);
    void        Update();

private:
    float       GetSideBarElementSlotX(int Index);
    float       GetSideBarElementSlotY(int Index);

private:
    int         m_Width;
    int         m_Height;
    CLabel*     m_DateTime;
};
