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
 * MollenOS - Vioarr Window Compositor System (Object-Window)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

/* Includes
 * - System */
#include "../toolkit/renderable.hpp"
#include <string>

// Window consist of => 
// Top bar
// Maniuplation buttons (left)
// Navigators (right)
// Content (changeable)
class CWindow : public CRenderable {
public:
    enum WindowDecorations {
        DecorTopLeft = 0,
        DecorTopMiddle,
        DecorTopRight,
        DecorFill,
        DecorBottomLeft,
        DecorBottomMiddle,
        DecorBottomRight,
        DecorIconClose,

        DecorCount
    };

public:
    CWindow(const std::string &Title, int X, int Y, int Width, int Height);
    CWindow(const std::string &Title);
    ~CWindow();

    // Inherited Methods
    void Render();

    // Window Logic
    void SetActive(bool Active);

private:
    bool CreateFB(GLuint *Id, GLuint *Texture);
    void RenderQuad(int X, int Y, int Height, int Width, GLuint Texture);
    void RenderDecorations(GLuint Framebuffer, GLuint *Textures);

    std::string m_Title;
    bool        m_Active;
    bool        m_UpdateDecorations;
    void*       m_Backbuffer;

    GLuint      m_ActiveTextures[DecorCount];
    GLuint      m_InactiveTextures[DecorCount];
    GLuint      m_Framebuffers[2];
    GLuint      m_FramebufferTextures[2];
};
