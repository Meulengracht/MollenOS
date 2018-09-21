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
    : CEntity(Parent, VgContext), m_Alignment(NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE), 
      m_Size(14.0f), m_Text("new-label"), m_Font("sans-normal")
{
}

CLabel::CLabel(NVGcontext* VgContext) 
    : CLabel(nullptr, VgContext) { }

CLabel::~CLabel() {
}

void CLabel::SetText(const std::string& Text)
{
    m_Text = Text;
}

void CLabel::SetTextAlignment(HorizontalAlignment HzAlignment, VerticalAlignment VzAlignment)
{
    m_Alignment = 0;

    // Handle horizontal alignment
    if (HzAlignment & AlignLeft) {
        m_Alignment |= NVG_ALIGN_LEFT;
    }
    else if (HzAlignment & AlignCenter) {
        m_Alignment |= NVG_ALIGN_CENTER;
    }
    else if (HzAlignment & AlignRight) {
        m_Alignment |= NVG_ALIGN_RIGHT;
    }

    // Handle vertical alignment
    if (VzAlignment & AlignTop) {
        m_Alignment |= NVG_ALIGN_TOP;
    }
    else if (VzAlignment & AlignMiddle) {
        m_Alignment |= NVG_ALIGN_MIDDLE;
    }
    else if (VzAlignment & AlignBottom) {
        m_Alignment |= NVG_ALIGN_BOTTOM;
    }
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
	nvgTextAlign(VgContext, m_Alignment);
    
	nvgFillColor(VgContext, m_Color);
	nvgText(VgContext, 0.0f, 0.0f, m_Text.c_str(), NULL);
	
    // Text
    //nvgBeginPath(vg);
	//nvgMoveTo(vg, x+0.5f, y+0.5f+30);
	//nvgLineTo(vg, x+0.5f+w-1, y+0.5f+30);
	//nvgStrokeColor(vg, nvgRGBA(0,0,0,32));
	//nvgStroke(vg);

	//nvgFontSize(vg, 18.0f);
	//nvgFontFace(vg, "sans-bold");
	//nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);

	//nvgFontBlur(vg,2);
	//nvgFillColor(vg, nvgRGBA(0,0,0,128));
	//nvgText(vg, x+w/2,y+16+1, title, NULL);

	//nvgFontBlur(vg,0);
	//nvgFillColor(vg, nvgRGBA(220,220,220,160));
	//nvgText(vg, x+w/2,y+16, title, NULL);
}
