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
 * MollenOS - Vioarr Window Compositor System (Object)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

/* Includes
 * - OpenGL */
#include <GL/gl.h>

/* Includes
 * - System */
#include "effects/effect.hpp"

class CRenderable {
public:
    CRenderable(int X, int Y, int Width, int Height) {
        _X = X; _Y = Y; _Width = Width; _Height = Height;
        _Effect = nullptr;
    }
    virtual ~CRenderable() { }

    // Public standard methods
    virtual void SetWidth(int Width) { _Width = Width; }
    virtual void SetHeight(int Height) { _Height = Height; }

    int GetWidth() const { return _Width; }
    int GetHeight() const { return _Height; }

    virtual void PreProcess() { if(_Effect) _Effect->Set(); }
    virtual void Render() = 0;
    virtual void PostProcess() { if(_Effect) _Effect->Unset(); }

private:
    // Object information
    int _X, _Y;
    int _Width;
    int _Height;

    // Object attachables
    CEffect *_Effect;
};
