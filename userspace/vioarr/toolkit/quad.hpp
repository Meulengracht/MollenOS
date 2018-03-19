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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

/* Includes
 * - OpenGL */
#include "../graphics/opengl/opengl_exts.hpp"
#include "renderable.hpp"

class CQuad : public CRenderable
{
    public:
        CQuad(int X, int Y, int Width, int Height) 
            : CRenderable(X, Y, Width, Height) {
            m_Vertices[0] = (float)X;
            m_Vertices[1] = (float)Y;
            m_Vertices[2] = 0.0f;
            m_Vertices[3] = 0.0f;
            m_Vertices[4] = 0.0f;
            
            m_Vertices[5] = (float)(X + Width);
            m_Vertices[6] = (float)Y;
            m_Vertices[7] = 0.0f;
            m_Vertices[8] = 1.0f;
            m_Vertices[9] = 0.0f;
            
            m_Vertices[10] = (float)(X + Width);
            m_Vertices[11] = (float)(Y + Height);
            m_Vertices[12] = 0.0f;
            m_Vertices[13] = 1.0f;
            m_Vertices[14] = 1.0f;
            
            m_Vertices[15] = (float)X;
            m_Vertices[16] = (float)(Y + Height);
            m_Vertices[17] = 0.0f;
            m_Vertices[18] = 0.0f;
            m_Vertices[19] = 1.0f;

            // Create the buffers
            sOpenGL.glGenVertexArrays(1, &m_Vao); // vao saves state of array buffer, element array, etc
            sOpenGL.glGenBuffers(1, &m_Vbo);      // vbo stores vertex data

            sOpenGL.glBindVertexArray(m_Vao);
            sOpenGL.glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);

            sOpenGL.glBufferData(GL_ARRAY_BUFFER, sizeof(m_Vertices), &m_Vertices[0], GL_STATIC_DRAW);

            sOpenGL.glEnableVertexAttribArray(0);
            sOpenGL.glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(float)*5, nullptr);
            
            sOpenGL.glEnableVertexAttribArray(1);
            sOpenGL.glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float)*5, (void*)(sizeof(float) * 3));

            sOpenGL.glBindVertexArray(0);
        }

        ~CQuad(){
            sOpenGL.glDeleteVertexArrays(1, &m_Vao);
            sOpenGL.glDeleteBuffers(1, &m_Vbo);
        }

        void Render() {
            sOpenGL.glBindVertexArray(m_Vao);
            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
            sOpenGL.glBindVertexArray(0);
        }

private:
        GLuint  m_Vao;
        GLuint  m_Vbo;
        GLfloat m_Vertices[20];
};