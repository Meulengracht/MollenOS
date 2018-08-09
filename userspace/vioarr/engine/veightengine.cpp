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

#include "utils/log_manager.hpp"
#include "veightengine.hpp"
#include "entity.hpp"
#include <algorithm>

#include "../graphics/opengl/opengl_exts.hpp"
#include "backend/nanovg_gl.h"

CVEightEngine::CVEightEngine()
{
    // Null members
    m_Screen        = nullptr;
    m_RootEntity    = nullptr;
    m_VgContext     = nullptr;
}

CVEightEngine::~CVEightEngine() {
    nvgDeleteGL3(m_VgContext);
}

void CVEightEngine::Initialize(CDisplay *Screen) {
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

void CVEightEngine::SetRootEntity(CEntity *Entity) {
    m_RootEntity = Entity;
}

void CVEightEngine::Update(size_t MilliSeconds) {
    if (m_RootEntity != nullptr) {
        m_RootEntity->PreProcess(MilliSeconds);
    }
}

void CVEightEngine::Render()
{
    // Initialize screen
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    
    nvgBeginFrame(m_VgContext, m_Screen->GetWidth(), m_Screen->GetHeight(), m_PixelRatio);
    if (m_RootEntity != nullptr) {
        m_RootEntity->Render(m_VgContext);
    }
    nvgEndFrame(m_VgContext);
    
    glFinish();
    m_Screen->Present();
}

// GetExistingWindowForProcess
// Iterate through all root-entities and find if any of them are owned
// by the given id
Handle_t CVEightEngine::GetExistingWindowForProcess(UUId_t ProcessId) {
    CWindow *WindowInstance = nullptr;
    auto Elements           = m_RootEntity->GetChildren();

    for (auto i = Elements.begin(); i != Elements.end(); i++) {
        CEntity *Element    = *i;
        WindowInstance      = dynamic_cast<CWindow*>(Element);
        if (WindowInstance != nullptr && WindowInstance->GetOwner() == ProcessId) {
            return (Handle_t)WindowInstance;
        }
    }
    return nullptr;
}

// IsWindowHandleValid
// Iterates all root handles to find the given window handle, to validate it is not bogus
bool CVEightEngine::IsWindowHandleValid(Handle_t WindowHandle) {
    auto Elements = m_RootEntity->GetChildren();

    for (auto i = Elements.begin(); i != Elements.end(); i++) {
        CEntity *Element = *i;
        if ((Handle_t)Element == WindowHandle) {
            return true;
        }
    }
    return false;
}

// GetActiveWindow
// Retrieves the active window by iterating to last element of type CWindow
CEntity* CVEightEngine::GetActiveWindow()
{
    // @todo have a pointer to this instead of searching
    CWindow *WindowInstance = nullptr;
    CWindow *Found          = nullptr;
    auto Elements           = m_RootEntity->GetChildren();

    for (auto i = Elements.begin(); i != Elements.end(); i++) {
        CEntity *Element    = *i;
        WindowInstance      = dynamic_cast<CWindow*>(Element);
        if (WindowInstance != nullptr) {
            Found = WindowInstance;
        }
    }
    return Found;
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
float CVEightEngine::ClampMagnitudeToScreenAxisX(int Value) {
    return (Value * 2.0f) / (float)m_Screen->GetWidth();
}

// ClampMagnitudeToScreenAxisY
// Clamps the given value to screen coordinates on the Y axis
// to the range of 0:2
float CVEightEngine::ClampMagnitudeToScreenAxisY(int Value) {
    return (Value * 2.0f) / (float)m_Screen->GetHeight();
}

NVGcontext *CVEightEngine::GetContext() const {
    return m_VgContext;
}

CEntity *CVEightEngine::GetRootEntity() const {
    return m_RootEntity;
}