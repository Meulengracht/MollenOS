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
#include <functional>
#include <string>

class CAccessBarWidget : public CEntity {
public:
    CAccessBarWidget(CEntity* Parent, NVGcontext* VgContext, int Width, int Height);
    CAccessBarWidget(NVGcontext* VgContext, int Width, int Height);
    ~CAccessBarWidget();

    void        SetWidgetText(std::string& Text);
    void        SetWidgetIcon(std::string& IconPath);
    void        SetWidgetEntity(CEntity* Entity);
    void        SetWidgetFunction(std::function<void(CEntity*)>& Function);

protected:
    // Override the inherited methods
    void        Update(size_t MilliSeconds);
    void        Draw(NVGcontext* VgContext);

private:
    int                             m_Width;
    int                             m_Height;
    CEntity*                        m_Entity;
    std::function<void(CEntity*)>   m_Callback;
    std::string                     m_Text;
};
