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

class CScreen {
public:
    virtual ~CScreen() { }
    virtual void Hide() = 0;
    virtual void Show() = 0;
    virtual void SetDimensions(int X, int Y, int Width, int Height) = 0;
    virtual void Update() = 0;

private:
    CScene *_Scene;
};

class CScreenManager {
public:
    enum EScreenType {
        ScreenLogin     = 0,
        ScreenDesktop,

        ScreenCount
    };
    CScreenManager() {
        for (int i = 0; i < ScreenCount; i++) {
            _AvailableScreens[i] = nullptr;
        }
        _ActiveScreen = nullptr;
    }
    ~CScreenManager() {
        for (int i = 0; i < ScreenCount; i++) {
            if (_AvailableScreens[i] != nullptr) {
                delete _AvailableScreens[i];
            }
        }
    }

    void RegisterScreen(CScreen *Screen, EScreenType Type) {
        _AvailableScreens[Type] = Screen;
    }
    
    void SetActiveScreen(EScreenType Type, bool Animate) {
        if (_ActiveScreen != nullptr) {
            _ActiveScreen->Hide();
        }
        _ActiveScreen = _AvailableScreens[Type];
        if (_ActiveScreen != nullptr) {
            _ActiveScreen->Show();
        }
    }
    
    void UpdateScreen() {
        if (_ActiveScreen != nullptr) {
            _ActiveScreen->Update();
        }
    }

private:
    CScreen* _AvailableScreens[ScreenCount];
    CScreen* _ActiveScreen;
};
