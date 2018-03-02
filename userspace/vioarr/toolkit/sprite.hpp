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
 * MollenOS - Vioarr Window Compositor System (Object-Sprite)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

/* Includes
 * - System */
#include "renderable.hpp"
#include <string>

class CSprite : public CRenderable {
public:
    CSprite(const std::string &Path) : CRenderable(0, 0, 0, 0) {
        _Texture = sTextureManager.CreateTexturePNG(Path.c_str(), &_TextureWidth, &_TextureHeight);
    }
    ~CSprite() {
        glDeleteTextures(1, &_Texture);
    }

    void Render() {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, _Texture);

        glPushMatrix();
        glLoadIdentity();
        glBegin(GL_QUADS); // Top left, Bot Left, Top Right, Bot Right (Vertices, not texture)
          glTexCoord2d(0.0, 1.0); glVertex2d(0.0, 0.0);
          glTexCoord2d(0.0, 0.0); glVertex2d(0.0, GetHeight());
          glTexCoord2d(1.0, 0.0); glVertex2d(GetWidth(), GetHeight());
          glTexCoord2d(1.0, 1.0); glVertex2d(GetWidth(), 0.0);
        glEnd();
        glPopMatrix();

        glDisable(GL_TEXTURE_2D);
    }

private:
    GLuint      _Texture;
    int         _TextureWidth;
    int         _TextureHeight;
};
