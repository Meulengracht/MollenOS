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
 * MollenOS - Vioarr Window Compositor System (Display Interface)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

class CDisplay {
public:
    CDisplay() { _X = 0; _Y = 0; _Width = 0; _Height = 0; }
    virtual ~CDisplay() { }

    // Builtins
    void SetDimensions(int X, int Y, int Width, int Height) {
        _X      = X;
        _Y      = Y;
        _Width  = Width;
        _Height = Height;
    }
    
    int GetX() { return _X; }
    int GetY() { return _Y; }
    int GetWidth() { return _Width; }
    int GetHeight() { return _Height; }

    // Overrides
    virtual bool Initialize() = 0;
    virtual bool IsValid() { return false; }
    virtual bool Present() = 0;

protected:
    int _X, _Y;
    int _Width;
    int _Height;
};
