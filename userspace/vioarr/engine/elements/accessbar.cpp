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
#include "button.hpp"
#include "sprite.hpp"
#include "label.hpp"

#define RGBA_ACCESSBAR_TOP              103, 103, 103, 127
#define RGBA_ACCESSBAR_SIDE             103, 103, 103, 127
constexpr float ACCESSBAR_USER_RADIUS   = 64.0f;
constexpr int ACCESSBAR_TOP_HEIGHT      = 20;
constexpr int ACCESSBAR_SIDE_WIDTH      = 40;

CAccessBar::CAccessBar(CEntity *Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext), m_Width(Width), m_Height(Height)
{
    // Create resources
    auto UserIcon           = new CSprite(this, VgContext, "$sys/themes/default/user32.png", 32, 32);
    UserIcon->Move(Width - 32 - 8, Height - 32 - 8, 0);

    auto ShutdownButton     = new CButton(this, VgContext, 16, 16);
    ShutdownButton->SetButtonStateIcon(CButton::ButtonStateNormal, "$sys/themes/default/power16.png");
    ShutdownButton->Move(4.0f, Height - 18, 0);

    //auto SettingsButton     = new CButton(this, VgContext, 32, 32);
    //SettingsButton->SetButtonStateIcon(CButton::ButtonStateNormal, "$sys/themes/default/settings32.png");
    //SettingsButton->Move(Width - 32 - 20, 8, 0);

    auto ApplicationsButton = new CButton(this, VgContext, 32, 32);
    ApplicationsButton->SetButtonStateIcon(CButton::ButtonStateNormal, "$sys/themes/default/apps32.png");
    ApplicationsButton->Move(GetSideBarElementSlotX(0), m_Height - 110, 0.0f);

    auto DateTimeLabel  = new CLabel(this, VgContext);
    DateTimeLabel->SetFont("sans-normal");
    DateTimeLabel->SetFontSize(14.0f);
    DateTimeLabel->SetFontColor(nvgRGBA(255, 255, 255, 255));
    DateTimeLabel->Move(28.0f, Height - 14, 0.0f);
    DateTimeLabel->SetText("13:37 Wed 24");
}

CAccessBar::CAccessBar(NVGcontext* VgContext, int Width, int Height) 
    : CAccessBar(nullptr, VgContext, Width, Height) { }

CAccessBar::~CAccessBar() {
}

float CAccessBar::GetSideBarElementSlotX(int Index)
{
    return (float)(m_Width - ACCESSBAR_SIDE_WIDTH + 4);
}

float CAccessBar::GetSideBarElementSlotY(int Index)
{
    return (float)m_Height - ((ACCESSBAR_USER_RADIUS * 1.8f) + 40.0f + (36.0f * Index));
}

void CAccessBar::Update() {
    // Update the date and time
}

void CAccessBar::Draw(NVGcontext* VgContext) {
    // Draw from 0, (height - bar_height) to width, (height - bar_height)
	nvgBeginPath(VgContext);
	nvgRect(VgContext, 0.0f, (m_Height - ACCESSBAR_TOP_HEIGHT), m_Width, (m_Height - ACCESSBAR_TOP_HEIGHT));
	nvgFillColor(VgContext, nvgRGBA(RGBA_ACCESSBAR_TOP));
	nvgFill(VgContext);

    // Draw from width - bar_width, 0, to width - bar_height, height
	nvgBeginPath(VgContext);
	nvgRect(VgContext, (m_Width - ACCESSBAR_SIDE_WIDTH), 0, (m_Width - ACCESSBAR_SIDE_WIDTH), m_Height);
	nvgFillColor(VgContext, nvgRGBA(RGBA_ACCESSBAR_SIDE));
	nvgFill(VgContext);

    // Draw a quarter circle
	nvgBeginPath(VgContext);
    nvgCircle(VgContext, m_Width, m_Height, ACCESSBAR_USER_RADIUS);
	nvgFillColor(VgContext, nvgRGBA(ACCESSBAR_FILL_COLOR_RGBA));
	nvgFill(VgContext);
}
