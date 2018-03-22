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

/* Includes
 * - System */
#include <GL/gl.h>
#include "graphics/opengl/opengl_exts.hpp"
#include "../veightengine.hpp"
#include "rectangle.hpp"

CRectangle::CRectangle(float ClampX, float ClampY, float ClampWidth, float ClampHeigth, bool Textured)
 : CComponent()
{
    // Variables
    int NumVertices = Textured ? 20 : 12;
    int Index = 0;

    m_Vertices = new float[NumVertices];

    // top right x, y, z
    m_Vertices[Index++] = (ClampX + ClampWidth);
    m_Vertices[Index++] = (ClampY + ClampHeigth);
    m_Vertices[Index++] = 0.0f;
    if (Textured) {
        m_Vertices[Index++] = 1.0f;
        m_Vertices[Index++] = 1.0f;
    }

    // bottom right x, y, z
    m_Vertices[Index++] = (ClampX + ClampWidth);
    m_Vertices[Index++] = ClampY;
    m_Vertices[Index++] = 0.0f;
    if (Textured) {
        m_Vertices[Index++] = 1.0f;
        m_Vertices[Index++] = 0.0f;
    }

    // bottom left x, y, z
    m_Vertices[Index++] = ClampX;
    m_Vertices[Index++] = ClampY;
    m_Vertices[Index++] = 0.0f;
    if (Textured) {
        m_Vertices[Index++] = 0.0f;
        m_Vertices[Index++] = 0.0f;
    }

    // top left x, y, z
    m_Vertices[Index++]  = ClampX;
    m_Vertices[Index++] = (ClampY + ClampHeigth);
    m_Vertices[Index++] = 0.0f;
    if (Textured) {
        m_Vertices[Index++] = 0.0f;
        m_Vertices[Index++] = 1.0f;
    }

    // first Triangle
    m_Indices[0] = 0;
    m_Indices[1] = 1;
    m_Indices[2] = 3;
    
    // second Triangle
    m_Indices[3] = 1;
    m_Indices[4] = 2;
    m_Indices[5] = 3;

    // Generate buffers
    sOpenGL.glGenVertexArrays(1, &m_ArrayBuffer);
    sOpenGL.glGenBuffers(1, &m_VertexBuffer);
    sOpenGL.glGenBuffers(1, &m_ElementBuffer);
    
    // bind the Vertex Array Object first, then bind and set vertex buffer(s), and then configure vertex attributes(s).
    sOpenGL.glBindVertexArray(m_ArrayBuffer);

    sOpenGL.glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    sOpenGL.glBufferData(GL_ARRAY_BUFFER, sizeof(float) * NumVertices, m_Vertices, GL_STATIC_DRAW);

    sOpenGL.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ElementBuffer);
    sOpenGL.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(unsigned int) * 6, m_Indices, GL_STATIC_DRAW);

    // Setup position attributes
    sOpenGL.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, Textured ? (5 * sizeof(float)) : (3 * sizeof(float)), (void*)0);
    sOpenGL.glEnableVertexAttribArray(0);

    // Setup texture attributes
    if (Textured) {
        sOpenGL.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        sOpenGL.glEnableVertexAttribArray(1);
    }

    // note that this is allowed, the call to glVertexAttribPointer registered VBO as the vertex attribute's bound vertex buffer object so afterwards we can safely unbind
    sOpenGL.glBindBuffer(GL_ARRAY_BUFFER, 0); 

    // remember: do NOT unbind the EBO while a VAO is active as the bound element buffer object IS stored in the VAO; keep the EBO bound.
    //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // You can unbind the VAO afterwards so other VAO calls won't accidentally modify this VAO, but this rarely happens. Modifying other
    // VAOs requires a call to glBindVertexArray anyways so we generally don't unbind VAOs (nor VBOs) when it's not directly necessary.
    sOpenGL.glBindVertexArray(0); 
}

CRectangle::CRectangle(int X, int Y, int Width, int Height, bool Textured) 
    : CRectangle(sEngine.ClampToScreenAxisX(X), 
        sEngine.ClampToScreenAxisY(Y), 
        sEngine.ClampToScreenAxisX(Width), 
        sEngine.ClampToScreenAxisY(Height), Textured) { }

CRectangle::~CRectangle() {
    sOpenGL.glDeleteVertexArrays(1, &m_ArrayBuffer);
    sOpenGL.glDeleteBuffers(1, &m_VertexBuffer);
    sOpenGL.glDeleteBuffers(1, &m_ElementBuffer);
    delete[] m_Vertices;
}

void CRectangle::Bind() {
    sOpenGL.glBindVertexArray(m_ArrayBuffer);
}

void CRectangle::Draw() {
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
}

void CRectangle::Unbind() {
    sOpenGL.glBindVertexArray(0);
}
