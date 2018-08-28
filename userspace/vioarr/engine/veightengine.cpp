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

CVEightEngine::CVEightEngine()
{
    m_Screen        = nullptr;
    m_VgContext     = nullptr;
    m_ActiveScene   = nullptr;
}

CVEightEngine::~CVEightEngine()
{
    nvgDeleteGL3(m_VgContext);
}

void CVEightEngine::Initialize(CDisplay *Screen)
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

void CVEightEngine::Update(size_t MilliSeconds)
{
    if (m_ActiveScene != nullptr) {
        m_ActiveScene->Update(MilliSeconds);
    }
}

void CVEightEngine::Render()
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

void CVEightEngine::AddScene(CScene* Scene)
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

bool CVEightEngine::RemoveScene(CScene* Scene)
{
    auto Position = std::find(m_Scenes.begin(), m_Scenes.end(), Scene);
    if (Position != m_Scenes.end()) {
        m_Scenes.erase(Position);
        
        // Validate active scene
        CScene* Next = m_ActiveScene;
        if (m_ActiveScene == Scene) {
            auto it     = m_Scenes.begin();
            Next        = (it != m_Scenes.end()) ? *it : nullptr;
        }
        m_ActiveScene = Next;
        delete Scene;
        return true;
    }
    return false;
}

// GetExistingWindowForProcess
// Iterate through all root-entities and find if any of them are owned
// by the given id
Handle_t CVEightEngine::GetExistingWindowForProcess(UUId_t ProcessId)
{
    for (auto s : m_Scenes) {
        auto Entity = s->GetEntityWithOwner(ProcessId);
        if (Entity != nullptr) {
            return (Handle_t)Entity;
        }
    }
    return nullptr;
}

// IsWindowHandleValid
// Iterates all root handles to find the given window handle, to validate it is not bogus
bool CVEightEngine::IsWindowHandleValid(Handle_t WindowHandle)
{
    for (auto s : m_Scenes) {
        if (s->HasEntity(static_cast<CEntity*>(WindowHandle))) {
            return true;
        }
    }
    return false;
}


float CVEightEngine::GetScreenCenterX()
{
    return m_Screen->GetWidth() / 2.0f;
}

float CVEightEngine::GetScreenCenterY()
{
    return m_Screen->GetHeight() / 2.0f;
}

// ClampToScreenAxisX
// Clamps the given value to screen coordinates on the X axis
// to the range of -1:1
float CVEightEngine::ClampToScreenAxisX(int Value)
{
    // Unsigned: Value/MaxValue
    // Signed:   max((Value/MaxValue), -1.0)
    return std::max<float>(((Value - ((m_Screen->GetWidth() / 2.0f))) / (m_Screen->GetWidth() / 2.0f)), -1.0f);
}

// ClampToScreenAxisY
// Clamps the given value to screen coordinates on the Y axis
// to the range of -1:1
float CVEightEngine::ClampToScreenAxisY(int Value)
{
    // Unsigned: Value/MaxValue
    // Signed:   max((Value/MaxValue), -1.0)
    return std::max<float>(((Value - ((m_Screen->GetHeight() / 2.0f))) / (m_Screen->GetHeight() / 2.0f)), -1.0f);
}

// ClampMagnitudeToScreenAxisX
// Clamps the given value to screen coordinates on the Y axis
// to the range of 0:2
float CVEightEngine::ClampMagnitudeToScreenAxisX(int Value)
{
    return (Value * 2.0f) / (float)m_Screen->GetWidth();
}

// ClampMagnitudeToScreenAxisY
// Clamps the given value to screen coordinates on the Y axis
// to the range of 0:2
float CVEightEngine::ClampMagnitudeToScreenAxisY(int Value)
{
    return (Value * 2.0f) / (float)m_Screen->GetHeight();
}

NVGcontext *CVEightEngine::GetContext() const
{
    return m_VgContext;
}

CScene* CVEightEngine::GetActiveScene() const
{
    return m_ActiveScene;
}
