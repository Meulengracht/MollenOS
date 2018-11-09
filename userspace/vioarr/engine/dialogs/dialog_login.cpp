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
 * MollenOS - Vioarr Engine System (V8)
 *  - The Vioarr V8 Graphics Engine.
 */

#include "../veightengine.hpp"
#include "dialog_login.hpp"
#include <os/input.h>

#define DIALOG_FILL_COLOR_RGBA              229, 229, 232, 255
#define DIALOG_HEIGHT                       114 // 30 + 24 * 2 + 10 padding between
#define DIALOG_WIDTH                        200

CDialogLogin::CDialogLogin(CEntity* Parent, NVGcontext* VgContext)
    : CPriorityEntity(Parent, VgContext, 0)
{
    // Create resources neccessary for the dialog, we need a textbox and
    // a dynamic label, default size of window is 450px x 100px
    SetPosition(sEngine.GetScreenCenterX() - (DIALOG_WIDTH / 2.0f), sEngine.GetScreenCenterY() - (DIALOG_HEIGHT / 2.0f), 0.0f);
    
    // Offset the suggestionbox to the middle
    m_Username = new CTextBox(this, VgContext, DIALOG_WIDTH - 40, 24); // 16 + 8 (4 padding)
    m_Password = new CTextBox(this, VgContext, DIALOG_WIDTH - 40, 24); // 16 + 8 (4 padding)

    m_Username->SetPosition(20.0f, DIALOG_HEIGHT - 54.0f, 0.0f);
    m_Username->SetIcon("$sys/themes/default/user16.png");
    m_Username->SetPlaceholderText("Username");
    m_Username->SetActive(true);                // Username textbox is active default

    m_Password->SetPosition(20.0f, DIALOG_HEIGHT - 88.0f, 0.0f);
    m_Password->SetIcon("$sys/themes/default/lock16.png");
    m_Password->SetPlaceholderText("Password");
    m_Password->SetPasswordField(true);
}

CDialogLogin::CDialogLogin(NVGcontext* VgContext)
    : CDialogLogin(nullptr, VgContext) { }

CDialogLogin::~CDialogLogin() { }

void CDialogLogin::Draw(NVGcontext* VgContext)
{
	NVGpaint ShadowPaint;
    float x = 0.0f, y = 0.0f;

	// Drop shadow
	ShadowPaint = nvgBoxGradient(VgContext, x, y + 2.0f, DIALOG_WIDTH, DIALOG_HEIGHT, 10.0f, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
	nvgBeginPath(VgContext);
	nvgRect(VgContext, x - 10, y - 10, DIALOG_WIDTH + 20, DIALOG_HEIGHT + 30);
	nvgFillPaint(VgContext, ShadowPaint);
	nvgFill(VgContext);

	// Window
	nvgBeginPath(VgContext);
	nvgRect(VgContext, x, y, DIALOG_WIDTH, DIALOG_HEIGHT);
	nvgFillColor(VgContext, nvgRGBA(DIALOG_FILL_COLOR_RGBA));
	nvgFill(VgContext);
}
