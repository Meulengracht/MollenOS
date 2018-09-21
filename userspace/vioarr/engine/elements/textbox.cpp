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

#include "textbox.hpp"
#include "sprite.hpp"

#define BOX_BORDER_COLOR            103, 103, 103, 255
#define BOX_FILL_COLOR              255, 255, 255, 255

#define BOX_TEXT_PLACEHOLDER_COLOR  103, 103, 103, 255
#define BOX_TEXT_COLOR              0, 0, 0, 255

CTextBox::CTextBox(CEntity* Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext), 
      m_PlaceholderText(""), m_Width(Width), 
      m_Height(Height), m_IsPassword(false), 
      m_PasswordCharacter('*'), m_LastText(""), m_OffsetX(4.0f) { }

CTextBox::CTextBox(NVGcontext* VgContext, int Width, int Height) 
    : CTextBox(nullptr, VgContext, Width, Height) { }

CTextBox::~CTextBox() { }

void CTextBox::SetPlaceholderText(const std::string& Text) {
    m_PlaceholderText   = Text;
    m_LastText          = Text;
    m_LastColor         = nvgRGBA(BOX_TEXT_PLACEHOLDER_COLOR);
}

void CTextBox::Add(char Character) {
    m_InputBuffer << Character;
    m_LastColor = nvgRGBA(BOX_TEXT_COLOR);

    if (m_IsPassword) {
        m_LastText = std::string(m_InputBuffer.tellg(), m_PasswordCharacter);
    }
    else {
        m_LastText  = m_InputBuffer.str();
    }
}

void CTextBox::Remove() {
    if (m_InputBuffer.tellg() > 0) {
        m_InputBuffer.seekp(-1, m_InputBuffer.cur);
    }

    if (m_InputBuffer.tellg() == 0) {
        m_LastText  = m_PlaceholderText;
        m_LastColor = nvgRGBA(BOX_TEXT_PLACEHOLDER_COLOR);
    }
}

void CTextBox::SetIcon(const std::string& IconPath) {
    auto SearchIcon = new CSprite(this, m_VgContext, IconPath, 16, 16);
    SearchIcon->SetPosition(4.0f, 4.0f, 0.0f);
    m_OffsetX = 24.0f;
}

void CTextBox::SetPasswordField(bool HideText, char PassCharacter) {
    m_IsPassword        = HideText;
    m_PasswordCharacter = PassCharacter;
}

void CTextBox::Draw(NVGcontext* VgContext) {
    // Fill
	nvgBeginPath(VgContext);
	nvgRoundedRect(VgContext, 0.0f, 0.0f, m_Width, m_Height, 5.0f);
	nvgFillColor(VgContext, nvgRGBA(BOX_FILL_COLOR));
	nvgFill(VgContext);

	// Border
	nvgBeginPath(VgContext);
	nvgRoundedRect(VgContext, 0.0f, 0.0f, m_Width, m_Height, 5.0f);
    nvgStrokeWidth(VgContext, 1.0f);
    nvgStrokeColor(VgContext, nvgRGBA(BOX_BORDER_COLOR));
    nvgStroke(VgContext);

    // Text
    nvgFontSize(VgContext,  14.0f);
    nvgFontFace(VgContext,  "sans-normal");
	nvgTextAlign(VgContext, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(VgContext, m_LastColor);
    nvgText(VgContext, m_OffsetX, 8.0f, m_LastText.c_str(), NULL);
}
