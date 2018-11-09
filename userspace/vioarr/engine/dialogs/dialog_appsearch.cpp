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
#include "dialog_appsearch.hpp"
#include <os/input.h>

#define DIALOG_FILL_COLOR_RGBA              229, 229, 232, 255
#define DIALOG_HEIGHT                       60
#define DIALOG_WIDTH                        450

CDialogApplicationSearch::CDialogApplicationSearch(CEntity* Parent, NVGcontext* VgContext)
    : CPriorityEntity(Parent, VgContext, CPriorityEntity::DeleteOnFocusLost | CPriorityEntity::DeleteOnEscape)
{
    // Create resources neccessary for the dialog, we need a textbox and
    // a dynamic label, default size of window is 450px x 100px
    SetPosition(sEngine.GetScreenCenterX() - (DIALOG_WIDTH / 2.0f), sEngine.GetScreenCenterY() - (DIALOG_HEIGHT / 2.0f), 0.0f);
    
    // Offset the suggestionbox to the middle
    m_SuggestionBox = new CSuggestionBox(this, VgContext, DIALOG_WIDTH - 20, 24); // 16 + 8 (4 padding)
    m_SuggestionBox->SetPosition(10.0f, 18.0f, 0.0f);
    m_SuggestionBox->SetPlaceholderText("Find Application");
}

CDialogApplicationSearch::CDialogApplicationSearch(NVGcontext* VgContext)
    : CDialogApplicationSearch(nullptr, VgContext) { }

CDialogApplicationSearch::~CDialogApplicationSearch() { }

void CDialogApplicationSearch::HandleKeyEvent(SystemKey_t* Key)
{
    // Skip released keys
    if (Key->Flags & KEY_MODIFIER_RELEASED) {
        return;
    }

    if (Key->KeyCode == VK_BACK) {
        m_SuggestionBox->Remove();
    }
    else {
        // Translate the system key
        if (TranslateSystemKey(Key) != OsSuccess) {
            return;
        }
        m_SuggestionBox->Add((char)(Key->KeyAscii & 0xFF));
    }
    InvalidateScreen();
}

void CDialogApplicationSearch::Draw(NVGcontext* VgContext)
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
