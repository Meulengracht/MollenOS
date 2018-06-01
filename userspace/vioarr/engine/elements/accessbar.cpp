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
#include "accessbar.hpp"
#include "sprite.hpp"

CAccessBar::CAccessBar(CEntity *Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext)
{
    m_Width     = Width;
    m_Height    = Height;

    // Create resources
    auto UserIcon = new CSprite(VgContext, "$sys/themes/default/user64.png", 64, 64);
    UserIcon->Move((Width / 2) - 32, (m_Height - (16 + 64)), 0);

    // Widgets?
    // User-widget 
    // Shelve widget

    // Add icon
    this->AddEntity(UserIcon);
}

CAccessBar::CAccessBar(NVGcontext* VgContext, int Width, int Height) 
    : CAccessBar(nullptr, VgContext, Width, Height) { }

CAccessBar::~CAccessBar() {
}

void CAccessBar::Update(size_t MilliSeconds) {
}

void CAccessBar::Draw(NVGcontext* VgContext) {
	//NVGpaint ShadowPaint;
    float x = 0.0f, y = 0.0f;

    // Use the fill color to fill the entirety
	nvgBeginPath(VgContext);
	nvgRect(VgContext, 0.0f, 0.0f, m_Width, m_Height);
	nvgFillColor(VgContext, nvgRGBA(ACESSBAR_FILL_COLOR_RGBA));
	nvgFill(VgContext);

    // First 16 pixels + (icon_height / 2) are colored with
    // the same color as the title bar of windows
    y += (m_Height - (ACCESSBAR_HEADER_HEIGHT + 32));
	nvgBeginPath(VgContext);
	nvgRect(VgContext, x, y, m_Width, ACCESSBAR_HEADER_HEIGHT + 32);
	nvgFillColor(VgContext, nvgRGBA(ACCSSBAR_HEADER_RGBA));
	nvgFill(VgContext);

    // Drop shadow for the header
	//ShadowPaint = nvgBoxGradient(VgContext, x, y + 2.0f, m_Width, ACCESSBAR_HEADER_HEIGHT + 32, 0, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
	//nvgBeginPath(VgContext);
	//nvgRect(VgContext, x, y, m_Width, ACCESSBAR_HEADER_HEIGHT + 32);
	//nvgPathWinding(VgContext, NVG_HOLE);
	//nvgFillPaint(VgContext, ShadowPaint);
	//nvgFill(VgContext);
}
