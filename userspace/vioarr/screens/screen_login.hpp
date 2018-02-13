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
 * MollenOS - Vioarr Window Compositor System (Login State)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

/* Includes
 * - System */
#include "../graphics/textures/texture_manager.hpp"
#include "../graphics/displays/display.hpp"
#include "screen.hpp"

class CLoginScreen : public CScreen {
public:
    CLoginScreen(CDisplay *Display) {
        // Load texture
        // @todo load from some settings
        _TextureBg  = sTextureManager.CreateTexturePNG("%sys%/themes/default/gfxbg.png", &_TextureBgWidth, &_TextureBgHeight);
        _Display    = Display;

        // Initialize to current size
        _X          = _Display->GetX();
        _Y          = _Display->GetY();
        _Width      = _Display->GetWidth();
        _Height     = _Display->GetHeight();
    }
    ~CLoginScreen() {
        glDeleteTextures(1, &_TextureBg);
    }

    // Hide this screen by fading us out or something @todo
    void Hide() {

    }

    // Play a fade-in sequence @todo
    void Show() {
        SetDimensions(_X, _Y, _Width, _Height);
        Update();
    }
    
    // Initializes the viewport for the current display size
    void SetDimensions(int X, int Y, int Width, int Height) {
        _X          = X;
        _Y          = Y;
        _Width      = Width;
        _Height     = Height;

        glViewport(X, Y, _Width, _Height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        //gluPerspective(60, (GLfloat)Width / (GLfloat)Height, 1.0, 100.0);
        //glMatrixMode(GL_MODELVIEW);
    }

    // Perform the render task of this screen
    void Update() {
        glClearColor(0.0, 0.0, 0.0, 1.0);
        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, _TextureBg);

        glBegin (GL_QUADS);
          glTexCoord2d(0.0,0.0); glVertex2d(0.0, 0.0);
          glTexCoord2d(1.0,0.0); glVertex2d((GLdouble)_Width, 0.0);
          glTexCoord2d(1.0,1.0); glVertex2d((GLdouble)_Width, (GLdouble)_Height);
          glTexCoord2d(0.0,1.0); glVertex2d(0.0, (GLdouble)_Height);
        glEnd();

        glFinish();
        _Display->Present();
    }

private:
    // Resources
    CDisplay*   _Display;
    GLuint      _TextureBg;
    int         _TextureBgWidth;
    int         _TextureBgHeight;

    // Information
    int _X, _Y;
    int _Width;
    int _Height;
};
