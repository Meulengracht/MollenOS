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

class CSprite : public CEntity {
public:
    CSprite(CEntity* Parent, NVGcontext* VgContext, const std::string &Path, int Width, int Height);
    CSprite(NVGcontext* VgContext, const std::string &Path, int Width, int Height);
    ~CSprite();

protected:
    // Override the inherited methods
    void Draw(NVGcontext* VgContext);

private:
    int m_ResourceId;
    int m_Width;
    int m_Height;
};
