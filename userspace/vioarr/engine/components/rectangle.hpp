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
 * MollenOS - Vioarr Engine System (V8)
 *  - The Vioarr V8 Graphics Engine.
 */
#pragma once
#include "component.hpp"

class CRectangle : public CComponent {
public:
    CRectangle(int X, int Y, int Width, int Height, bool Textured);
    CRectangle(float ClampX, float ClampY, float ClampWidth, float ClampHeigth, bool Textured);
    ~CRectangle();

    void Bind();
    void Draw();
    void Unbind();

private:
    float*          m_Vertices;
    unsigned int    m_Indices[6];
    unsigned int    m_VertexBuffer;
    unsigned int    m_ArrayBuffer;
    unsigned int    m_ElementBuffer;
};
