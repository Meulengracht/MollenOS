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

#include "button.hpp"
#include "../../utils/log_manager.hpp"

CButton::CButton(CEntity* Parent, NVGcontext* VgContext, int Width, int Height) 
    : CEntity(Parent, VgContext)
{
    m_Width         = Width;
    m_Height        = Height;
    m_ActiveState   = ButtonStateNormal;
}

CButton::CButton(NVGcontext* VgContext, int Width, int Height) 
    : CButton(nullptr, VgContext, Width, Height) { }

CButton::~CButton()
{
    for (int i = 0; i < ButtonStateCount; i++) {
        if (m_ResourceIds[i] != 0) {
            nvgDeleteImage(m_VgContext, m_ResourceIds[i]);
        }
    }
}

void CButton::SetButtonStateIcon(const EButtonState State, const std::string& IconPath)
{
    // Make sure we don't leak resources
    if (m_ResourceIds[State] != 0) {
        nvgDeleteImage(m_VgContext, m_ResourceIds[State]);
    }
    m_ResourceIds[State] = nvgCreateImage(m_VgContext, IconPath.c_str(), NVG_IMAGE_FLIPY);
}

void CButton::SetState(EButtonState State)
{
    m_ActiveState = State;
}

void CButton::Draw(NVGcontext* VgContext) {
    if (m_ResourceIds[m_ActiveState] == 0) {
        return;
    }

    NVGpaint imgPaint = nvgImagePattern(VgContext, 0.0f, 0.0f, m_Width, m_Height, 0.0f, m_ResourceIds[m_ActiveState], 1.0f);
    nvgBeginPath(VgContext);
    nvgRect(VgContext, 0, 0, m_Width, m_Height);
    nvgFillPaint(VgContext, imgPaint);
    nvgFill(VgContext);
}
