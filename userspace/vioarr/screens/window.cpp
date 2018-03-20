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

/* Includes
 * - System */
#include "texture_manager.hpp"
#include "window.hpp"

/* Window Decorations
 * Sprites and icons for an active window. */
const char *m_WindowDecorationsActive[CWindow::DecorCount] = {
    "$sys/themes/default/active/00.png",
    "$sys/themes/default/active/01.png",
    "$sys/themes/default/active/02.png",
    "$sys/themes/default/active/10.png",
    "$sys/themes/default/active/20.png",
    "$sys/themes/default/active/21.png",
    "$sys/themes/default/active/22.png",
    "$sys/themes/default/active/close.png"
};

/* Window Decorations
 * Sprites and icons for an in-active window. */
const char *m_WindowDecorationsInactive[CWindow::DecorCount] = {
    "$sys/themes/default/inactive/00.png",
    "$sys/themes/default/inactive/01.png",
    "$sys/themes/default/inactive/02.png",
    "$sys/themes/default/inactive/10.png",
    "$sys/themes/default/inactive/20.png",
    "$sys/themes/default/inactive/21.png",
    "$sys/themes/default/inactive/22.png",
    "$sys/themes/default/inactive/close.png"
};

CWindow::CWindow(const std::string &Title, int X, int Y, int Width, int Height) 
    : CRenderable(X, Y, Width, Height), m_Title(Title)
{
    // Load window textures
    for (int i = 0; i < (int)DecorCount; i++) {
        m_ActiveTextures[i]     = sTextureManager.CreateTexturePNG(m_WindowDecorationsActive[i], NULL, NULL);
        m_InactiveTextures[i]   = sTextureManager.CreateTexturePNG(m_WindowDecorationsInactive[i], NULL, NULL);
    }

    // Set initially to true
    m_Active            = true;
    m_UpdateDecorations = true;

    // Generate a texture that will represent this window
    CreateFB(&m_Framebuffers[0], &m_FramebufferTextures[0]);

    // Generate the user backbuffer
    m_Backbuffer = nullptr; // @todo
}

CWindow::CWindow(const std::string &Title) : 
    CWindow(Title, 100, 100, 450, 300) { }

CWindow::~CWindow()
{
    glDeleteTextures(DecorCount, &m_ActiveTextures[0]);
    glDeleteTextures(DecorCount, &m_InactiveTextures[0]);
}

void CWindow::SetActive(bool Active)
{
    m_UpdateDecorations = !(m_Active & Active);
    Active              = m_Active;    
}

bool CWindow::CreateFB(GLuint *Id, GLuint *Texture)
{
    // Create the empty framebuffer texture
    glGenTextures(1, Texture);
    glBindTexture(GL_TEXTURE_2D, *Texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GetWidth(), GetHeight() + 48, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Create the framebuffer, which regroups 0, 1, or more textures, and 0 or 1 depth buffer.
    GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    sOpenGL.glGenFramebuffers(1, Id);
    sOpenGL.glBindFramebuffer(GL_FRAMEBUFFER, *Id);
    sOpenGL.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *Texture, 0);
    sOpenGL.glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers
    GLenum FbStatus = sOpenGL.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    sOpenGL.glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return (FbStatus == GL_FRAMEBUFFER_COMPLETE);
}

void CWindow::RenderTexturedQuad(int X, int Y, int Height, int Width, GLuint Texture)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, Texture);

    glPushMatrix();
    glTranslated((double)X, (double)Y, 0.0);
    glBegin(GL_QUADS); // Top left, Bot Left, Top Right, Bot Right (Vertices, not texture)
        glTexCoord2d(0.0, 0.0); glVertex2i(0, 0);
        glTexCoord2d(0.0, 1.0); glVertex2i(0, Height);
        glTexCoord2d(1.0, 1.0); glVertex2i(Width, Height);
        glTexCoord2d(1.0, 0.0); glVertex2i(Width, 0);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glPopMatrix();
}

void CWindow::RenderColoredQuad(int X, int Y, int Height, int Width, unsigned Color)
{
    glPushMatrix();
    glTranslated((double)X, (double)Y, 0.0);
    glBegin(GL_QUADS); // Top left, Bot Left, Top Right, Bot Right (Vertices, not texture)
        glColor3f((float)((Color >> 16) & 0xFF) / 255.0f, (float)((Color >> 8) & 0xFF) / 255.0f, (float)(Color & 0xFF) / 255.0f);
        glVertex2i(0, 0);
        glVertex2i(0, Height);
        glVertex2i(Width, Height);
        glVertex2i(Width, 0);
    glEnd();
    glPopMatrix();
}

void CWindow::RenderDecorations(GLuint Framebuffer, GLuint *Textures)
{
    // Bind the fbo
    sOpenGL.glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
    glViewport(0, 0, GetWidth(), GetHeight() + 48);
    glClearColor(0.9f, 0.9f, 0.9f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, GetWidth(), GetHeight() + 48, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);

    // Draw textures
    glPushMatrix();
    glLoadIdentity();
        RenderTexturedQuad(48, 0, 48, GetWidth() - (2 * 48), Textures[DecorTopMiddle]);
        //RenderColoredQuad(48, 0, 48, GetWidth() - (2 * 48), 0x676767);
        RenderTexturedQuad(0, 0, 48, 48, Textures[DecorTopLeft]);
        RenderTexturedQuad(GetWidth() - 48, 0, 48, 48, Textures[DecorTopRight]);
        //RenderTexturedQuad(0, 48, GetHeight(), GetWidth(), Textures[DecorFill]);
        //RenderTexturedQuad(0, 48, GetHeight() - (2 * 48), GetWidth(), Textures[DecorFill]);
        //RenderTexturedQuad(0, GetHeight() - 48, 48, 48, Textures[DecorBottomLeft]);
        //RenderTexturedQuad(48, GetHeight() - 48, 48, GetWidth() - (2 * 48), Textures[DecorBottomMiddle]);
        //RenderTexturedQuad(GetWidth() - 48, GetHeight() - 48, 48, 48, Textures[DecorBottomRight]);
    glPopMatrix();

    // Done, unbind
    sOpenGL.glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CWindow::Render() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Update decorations?
    if (m_UpdateDecorations) {
        GLint Viewport[4];
        glGetIntegerv(GL_VIEWPORT, Viewport);
        RenderDecorations(m_Framebuffers[0], m_Active ? &m_ActiveTextures[0] : &m_InactiveTextures[0]);
        m_UpdateDecorations = false;

        // Update the viewport       
        glViewport(Viewport[0], Viewport[1], Viewport[2], Viewport[3]);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0, Viewport[2], Viewport[3], 0, -1, 1);
        glMatrixMode(GL_MODELVIEW);
    }

    // Translate
    RenderTexturedQuad(GetX(), GetY(), GetHeight() + 48, GetWidth(), m_FramebufferTextures[0]);
    glDisable(GL_BLEND);
}
