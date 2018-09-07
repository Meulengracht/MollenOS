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

#include "sprite.hpp"
#include "../../utils/log_manager.hpp"

CSprite::CSprite(CEntity* Parent, NVGcontext* VgContext, 
    const std::string &Path, int Width, int Height) : CEntity(Parent, VgContext) {
    m_ResourceId = nvgCreateImage(VgContext, Path.c_str(), NVG_IMAGE_FLIPY);
    assert(m_ResourceId != 0);
    m_Width      = Width;
    m_Height     = Height;
}

CSprite::CSprite(NVGcontext* VgContext, const std::string &Path, int Width, int Height) 
    : CSprite(nullptr, VgContext, Path, Width, Height) { }

CSprite::~CSprite() {
    nvgDeleteImage(m_VgContext, m_ResourceId);
}

void CSprite::Draw(NVGcontext* VgContext) {
    NVGpaint imgPaint = nvgImagePattern(VgContext, 0.0f, 0.0f, m_Width, m_Height, 0.0f, m_ResourceId, 1.0f);
    nvgBeginPath(VgContext);
    nvgRect(VgContext, 0, 0, m_Width, m_Height);
    nvgFillPaint(VgContext, imgPaint);
    nvgFill(VgContext);
}

