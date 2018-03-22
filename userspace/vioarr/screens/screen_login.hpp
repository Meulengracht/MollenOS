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
#include "graphics/display.hpp"
#include "utils/log_manager.hpp"
#include "screen.hpp"

class CLoginScreen : public CScreen {
public:
    CLoginScreen(CDisplay *Display) : CScreen() {
        // Initialize to current size
        _Display    = Display;
        _X          = _Display->GetX();
        _Y          = _Display->GetY();
        _Width      = _Display->GetWidth();
        _Height     = _Display->GetHeight();

        // @todo load from some settings
        //AddRenderable(new CSprite("$sys/themes/default/gfxbg.png", _Width, _Height));
        //AddRenderable(new CWindow("Test"));
    }
    ~CLoginScreen() {

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
        glOrtho(0.0f, _Width, _Height, 0.0f, -1.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
    }

    // Perform the render task of this screen
    void Update() {
        glClear(GL_COLOR_BUFFER_BIT);
        RenderScene();
        glFinish();
        _Display->Present();
    }

private:
    // Resources
    CDisplay*   _Display;

    // Information
    int _X, _Y;
    int _Width;
    int _Height;
};
