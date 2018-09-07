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

#include "label.hpp"
#include "../../utils/log_manager.hpp"

CLabel::CLabel(CEntity* Parent, NVGcontext* VgContext) 
    : CEntity(Parent, VgContext), m_Size(14.0f), m_Text("new-label"), m_Font("sans-normal") {
}

CLabel::CLabel(NVGcontext* VgContext) 
    : CLabel(nullptr, VgContext) { }

CLabel::~CLabel() {
}

void CLabel::SetText(const std::string& Text)
{
    m_Text = Text;
}

void CLabel::SetFont(const std::string& Font)
{
    m_Font = Font;
}

void CLabel::SetFontSize(float Size)
{
    m_Size = Size;
}

void CLabel::SetFontColor(NVGcolor Color)
{
    m_Color = Color;
}

void CLabel::Draw(NVGcontext* VgContext) {
    nvgFontSize(VgContext,  m_Size);
	nvgFontFace(VgContext,  m_Font.c_str());
	nvgTextAlign(VgContext, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    
	nvgFillColor(VgContext, m_Color);
	nvgText(VgContext, 0.0f, 0.0f, m_Text.c_str(), NULL);
}
