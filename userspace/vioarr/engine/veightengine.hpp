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

#include <list>
#include <GL/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "graphics/display.hpp"
#include "backend/nanovg.h"

class CScene;
class CEntity;

class V8Engine {
public:
    static V8Engine& GetInstance() {
        // Guaranteed to be destroyed.
        // Is instantiated on first use
        static V8Engine _Instance;
        return _Instance;
    }
private:
    V8Engine();
    ~V8Engine();

public:
    V8Engine(V8Engine const&) = delete;
    void operator=(V8Engine const&) = delete;

    void Initialize(CDisplay *Screen);

    // **************************************
    // Render Logic
    void Render();

    // **************************************
    // Scene Management
    void AddScene(CScene* Scene);
    bool RemoveScene(CScene* Scene);
    void AddElementToCurrentScene(CEntity* Entity);
    bool RemoveElement(long ElementId);
    bool InvalidateElement(long ElementId);

    long GetTopElementByOwner(UUId_t Owner);
    bool IsElementTopElement(long ElementId);

    // **************************************
    // Utilities
    float ClampToScreenAxisX(int Value);
    float ClampToScreenAxisY(int Value);
    float ClampMagnitudeToScreenAxisX(int Value);
    float ClampMagnitudeToScreenAxisY(int Value);
    float GetScreenCenterX();
    float GetScreenCenterY();

    NVGcontext* GetContext() const;
    CScene*     GetActiveScene() const;

private:
    CDisplay*           m_Screen;
    std::list<CScene*>  m_Scenes;
    CScene*             m_ActiveScene;
    float               m_PixelRatio;
    NVGcontext*         m_VgContext;
};

// Shorthand for the vioarr
#define sEngine V8Engine::GetInstance()
