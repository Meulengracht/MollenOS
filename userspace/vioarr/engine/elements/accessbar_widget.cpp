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
#include "accessbar_widget.hpp"
#include "accessbar.hpp"
#include "sprite.hpp"

// Dummy - do nothing callback
void WidgetDummyCallback(CEntity* Entity) {}

CAccessBarWidget::CAccessBarWidget(CEntity* Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext), m_Entity(nullptr), m_Callback(WidgetDummyCallback), m_Text("Widget")
{
    m_Width     = Width;
    m_Height    = Height;
}

CAccessBarWidget::CAccessBarWidget(NVGcontext* VgContext, int Width, int Height) 
    : CAccessBarWidget(nullptr, VgContext, Width, Height) { }

CAccessBarWidget::~CAccessBarWidget() {
}

void CAccessBarWidget::SetWidgetText(std::string& Text)
{
    m_Text = Text;
}

void CAccessBarWidget::SetWidgetIcon(std::string& IconPath)
{
    auto Icon   = new CSprite(m_VgContext, IconPath, 16, 16);
    float x     = (m_Width - 16) - 4;           // Position to the right, with margin 4 pixels
    float y     = ((m_Height - 16) / 2.0f);     // Position in the vertical middle

    Icon->Move(x, y, 0.0f);
    this->AddEntity(Icon);
}

void CAccessBarWidget::SetWidgetEntity(CEntity* Entity)
{
    m_Entity = Entity;
}

void CAccessBarWidget::SetWidgetFunction(std::function<void(CEntity*)>& Function)
{
    m_Callback = Function;
}

void CAccessBarWidget::Update(size_t MilliSeconds) {
}

void CAccessBarWidget::Draw(NVGcontext* VgContext) {
    // Draw border

}
