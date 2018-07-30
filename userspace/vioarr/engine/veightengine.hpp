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
#pragma once

/* Includes
 * - OpenGL */
#include <GL/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

/* Includes
 * - System */
#include "graphics/display.hpp"
#include "backend/nanovg.h"

class CEntity;

class CVEightEngine {
public:
    static CVEightEngine& GetInstance() {
        // Guaranteed to be destroyed.
        // Is instantiated on first use
        static CVEightEngine _Instance;
        return _Instance;
    }
private:
    CVEightEngine();
    ~CVEightEngine();

public:
    CVEightEngine(CVEightEngine const&) = delete;
    void operator=(CVEightEngine const&) = delete;

    void        Initialize(CDisplay *Screen);
    void        SetRootEntity(CEntity *Entity);

    // **************************************
    // Render Logic
    void        Update(size_t MilliSeconds);
    void        Render();

    // **************************************
    // Business Logic
    Handle_t    GetExistingWindowForProcess(UUId_t ProcessId);
    bool        IsWindowHandleValid(Handle_t WindowHandle);

    // **************************************
    // Utilities
    float       ClampToScreenAxisX(int Value);
    float       ClampToScreenAxisY(int Value);
    float       ClampMagnitudeToScreenAxisX(int Value);
    float       ClampMagnitudeToScreenAxisY(int Value);

    NVGcontext* GetContext() const;
    CEntity*    GetRootEntity() const;

private:
    CDisplay*   m_Screen;
    CEntity*    m_RootEntity;
    float       m_PixelRatio;
    NVGcontext* m_VgContext;
};

// Shorthand for the vioarr
#define sEngine CVEightEngine::GetInstance()
