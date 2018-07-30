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
#pragma once
#include "../entity.hpp"
#include <string>

class CLabel : public CEntity {
public:
    CLabel(CEntity* Parent, NVGcontext* VgContext);
    CLabel(NVGcontext* VgContext);
    ~CLabel();

    void SetText(const std::string& Text);
    void SetFont(const std::string& Font);
    void SetFontSize(float Size);
    void SetFontColor(NVGcolor Color);

protected:
    // Override the inherited methods
    void Update(size_t MilliSeconds);
    void Draw(NVGcontext* VgContext);

private:
    float       m_Size;
    NVGcolor    m_Color;
    std::string m_Text;
    std::string m_Font;
};
