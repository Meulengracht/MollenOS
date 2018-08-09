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
#include "button.hpp"
#include "sprite.hpp"
#include "label.hpp"

CAccessBar::CAccessBar(CEntity *Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext)
{
    m_Width     = Width;
    m_Height    = Height;

    // Create buttons
    auto SettingsButton     = new CButton(VgContext, 32, 32);
    auto ShutdownButton     = new CButton(VgContext, 32, 32);
    auto ApplicationsButton = new CButton(VgContext, 16, 16);

    auto ApplicationsLabel  = new CLabel(VgContext);

    auto UserIcon = new CSprite(VgContext, "$sys/themes/default/user64.png", 64, 64);

    ShutdownButton->SetButtonStateIcon(CButton::ButtonStateNormal, "$sys/themes/default/power32.png");
    ShutdownButton->Move(20, 8, 0);

    SettingsButton->SetButtonStateIcon(CButton::ButtonStateNormal, "$sys/themes/default/settings32.png");
    SettingsButton->Move(Width - 32 - 20, 8, 0);

    ApplicationsButton->SetButtonStateIcon(CButton::ButtonStateNormal, "$sys/themes/default/apps16.png");
    ApplicationsButton->Move(Width - 16 - 16, m_Height - 162, 0);

    ApplicationsLabel->SetText("Applications");
    ApplicationsLabel->SetFontSize(18.0f);
    ApplicationsLabel->SetFontColor(nvgRGBA(ACCESSBAR_HEADER_RGBA));
    ApplicationsLabel->Move(14.0f, m_Height - 159, 0);

    UserIcon->Move((Width / 2) - 32, (m_Height - (16 + 64)), 0);

    // Add icons to our children
    this->AddEntity(UserIcon);
    this->AddEntity(SettingsButton);
    this->AddEntity(ShutdownButton);
    this->AddEntity(ApplicationsButton);
    this->AddEntity(ApplicationsLabel);
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
	nvgFillColor(VgContext, nvgRGBA(ACCESSBAR_FILL_COLOR_RGBA));
	nvgFill(VgContext);

    // Draw the lower divider
    nvgBeginPath(VgContext);
    nvgMoveTo(VgContext, 8.0f, 48.0f);
    nvgLineTo(VgContext, (m_Width - 8), 48.0f);
    nvgMoveTo(VgContext, (m_Width / 2.0f), 40.0f);
    nvgLineTo(VgContext, (m_Width / 2.0f), 8.0f);
    nvgStrokeWidth(VgContext, 1.0f);
    nvgStrokeColor(VgContext, nvgRGBA(ACCESSBAR_HEADER_RGBA));
    nvgStroke(VgContext);

    // Draw the upper divider
    nvgBeginPath(VgContext);
    nvgMoveTo(VgContext, 8.0f, (m_Height - 160.0f - 8.0f));
    nvgLineTo(VgContext, (m_Width - 8), (m_Height - 160.0f - 8.0f));
    nvgStrokeWidth(VgContext, 1.0f);
    nvgStrokeColor(VgContext, nvgRGBA(ACCESSBAR_HEADER_RGBA));
    nvgStroke(VgContext);

    // First 16 pixels + (icon_height / 2) are colored with
    // the same color as the title bar of windows
    y += (m_Height - (ACCESSBAR_HEADER_HEIGHT + 32));
	nvgBeginPath(VgContext);
	nvgRect(VgContext, x, y, m_Width, ACCESSBAR_HEADER_HEIGHT + 32);
	nvgFillColor(VgContext, nvgRGBA(ACCESSBAR_HEADER_RGBA));
	nvgFill(VgContext);

    // Drop shadow for the header
	//ShadowPaint = nvgBoxGradient(VgContext, x, y + 2.0f, m_Width, ACCESSBAR_HEADER_HEIGHT + 32, 0, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
	//nvgBeginPath(VgContext);
	//nvgRect(VgContext, x, y, m_Width, ACCESSBAR_HEADER_HEIGHT + 32);
	//nvgPathWinding(VgContext, NVG_HOLE);
	//nvgFillPaint(VgContext, ShadowPaint);
	//nvgFill(VgContext);
}
