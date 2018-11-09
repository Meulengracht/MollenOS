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

#include <algorithm>
#include "utils/log_manager.hpp"
#include "veightengine.hpp"
#include "entity.hpp"
#include "../graphics/opengl/opengl_exts.hpp"
#include "backend/nanovg_gl.h"
#include "elements/window.hpp"
#include "scene.hpp"

long CEntity::g_EntityId = 0;

V8Engine::V8Engine()
{
    m_Screen        = nullptr;
    m_VgContext     = nullptr;
    m_ActiveScene   = nullptr;
}

V8Engine::~V8Engine()
{
    nvgDeleteGL3(m_VgContext);
}

void V8Engine::Initialize(CDisplay *Screen)
{
    m_Screen = Screen;

    // Initialize the viewport
    sLog.Info("Creating nvg context");
    glViewport(0, 0, Screen->GetWidth(), Screen->GetHeight());
    m_PixelRatio = (float)Screen->GetWidth() / (float)Screen->GetHeight();
#ifdef QUALITY_MSAA
	m_VgContext = nvgCreateGL3(NVG_STENCIL_STROKES | NVG_DEBUG);
#else
	m_VgContext = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
#endif
    assert(m_VgContext != nullptr);

    // Load fonts
    sLog.Info("Loading fonts");
    nvgCreateFont(m_VgContext, "sans-normal", "$sys/fonts/DejaVuSans.ttf");
    nvgCreateFont(m_VgContext, "sans-light", "$sys/fonts/DejaVuSans-ExtraLight.ttf");
}

void V8Engine::Render()
{
    // Initialize screen
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    
    nvgBeginFrame(m_VgContext, m_Screen->GetWidth(), m_Screen->GetHeight(), m_PixelRatio);
    if (m_ActiveScene != nullptr) {
        m_ActiveScene->Render(m_VgContext);
    }
    nvgEndFrame(m_VgContext);
    
    glFinish();
    m_Screen->Present();
}

void V8Engine::AddScene(CScene* Scene)
{
    auto Position = std::find(m_Scenes.begin(), m_Scenes.end(), Scene);
    if (Position == m_Scenes.end()) {
        m_Scenes.push_back(Scene);

        // Initialize the index to 0
        if (m_ActiveScene == nullptr) {
            m_ActiveScene = Scene;
        }
    }
}

bool V8Engine::RemoveScene(CScene* Scene)
{
    auto Position = std::find(m_Scenes.begin(), m_Scenes.end(), Scene);
    if (Position != m_Scenes.end()) {
        m_Scenes.erase(Position);
        
        // Validate active scene
        CScene* Next = m_ActiveScene;
        if (m_ActiveScene == Scene) {
            auto it = m_Scenes.begin();
            Next    = (it != m_Scenes.end()) ? *it : nullptr;
        }
        m_ActiveScene = Next;
        delete Scene;
        return true;
    }
    return false;
}

void V8Engine::AddElementToCurrentScene(CEntity* Entity)
{
    if (m_ActiveScene != nullptr) {
        m_ActiveScene->Add(Entity);
    }
}

bool V8Engine::RemoveElement(long ElementId)
{
    // Check active scene first
    if (m_ActiveScene != nullptr) {
        if (m_ActiveScene->HasEntity(ElementId)) {
            return m_ActiveScene->Remove(ElementId);
        }
    }

    for (auto s : m_Scenes) {
        if (s->HasEntity(ElementId)) {
            return s->Remove(ElementId);
        }
    }
    return false;
}

bool V8Engine::InvalidateElement(long ElementId)
{
    // Check active scene first
    if (m_ActiveScene != nullptr) {
        if (m_ActiveScene->HasEntity(ElementId)) {
            return m_ActiveScene->InvalidateElement(ElementId);
        }
    }

    for (auto s : m_Scenes) {
        if (s->HasEntity(ElementId)) {
            return s->InvalidateElement(ElementId);
        }
    }
    return false;
}

long V8Engine::GetTopElementByOwner(UUId_t Owner)
{
    for (auto s : m_Scenes) {
        auto Id = s->GetEntityIdForOwner(Owner);
        if (Id != -1) {
            return Id;
        }
    }
    return -1;
}

bool V8Engine::IsElementTopElement(long ElementId)
{
    for (auto s : m_Scenes) {
        if (s->HasEntity(ElementId)) {
            return true;
        }
    }
    return false;
}

float V8Engine::GetScreenCenterX()
{
    return m_Screen->GetWidth() / 2.0f;
}

float V8Engine::GetScreenCenterY()
{
    return m_Screen->GetHeight() / 2.0f;
}

// ClampToScreenAxisX
// Clamps the given value to screen coordinates on the X axis
// to the range of -1:1
float V8Engine::ClampToScreenAxisX(int Value)
{
    // Unsigned: Value/MaxValue
    // Signed:   max((Value/MaxValue), -1.0)
    return std::max<float>(((Value - ((m_Screen->GetWidth() / 2.0f))) / (m_Screen->GetWidth() / 2.0f)), -1.0f);
}

// ClampToScreenAxisY
// Clamps the given value to screen coordinates on the Y axis
// to the range of -1:1
float V8Engine::ClampToScreenAxisY(int Value)
{
    // Unsigned: Value/MaxValue
    // Signed:   max((Value/MaxValue), -1.0)
    return std::max<float>(((Value - ((m_Screen->GetHeight() / 2.0f))) / (m_Screen->GetHeight() / 2.0f)), -1.0f);
}

// ClampMagnitudeToScreenAxisX
// Clamps the given value to screen coordinates on the Y axis
// to the range of 0:2
float V8Engine::ClampMagnitudeToScreenAxisX(int Value)
{
    return (Value * 2.0f) / (float)m_Screen->GetWidth();
}

// ClampMagnitudeToScreenAxisY
// Clamps the given value to screen coordinates on the Y axis
// to the range of 0:2
float V8Engine::ClampMagnitudeToScreenAxisY(int Value)
{
    return (Value * 2.0f) / (float)m_Screen->GetHeight();
}

NVGcontext *V8Engine::GetContext() const
{
    return m_VgContext;
}

CScene* V8Engine::GetActiveScene() const
{
    return m_ActiveScene;
}
