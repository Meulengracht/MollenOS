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

#include "components/rectangle.hpp"
#include "programs/program_color.hpp"
#include "veightengine.hpp"
#include "utils/log_manager.hpp"

CVEightEngine::CVEightEngine()
{
    // Null members
    m_Screen = nullptr;
}

CVEightEngine::~CVEightEngine()
{

}

void CVEightEngine::Initialize(CDisplay *Screen) {
    m_Screen = Screen;

    // Initialize the viewport
    glViewport(0, 0, Screen->GetWidth(), Screen->GetHeight());
}

void CVEightEngine::Render()
{
    // Create a rectangle and the standard color program
    CColorProgram Program;
    CRectangle Rect(-0.5f, -0.5f, 1.0f, 1.0f, false);

    // Initialize screen
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Use our shader program when we want to render an object
    Program.Use();
    Program.SetColor(0.9f, 0.1f, 0.5f, 1.0f);
    Rect.Bind();
    Rect.Draw();
    Rect.Unbind();
    Program.Unuse();

    // Present
    glFinish();
    m_Screen->Present();

    // don't leave
    for(;;);
}

// ClampToScreenAxisX
// Clamps the given value to screen coordinates on the X axis
float CVEightEngine::ClampToScreenAxisX(int Value)
{
    float ClampedValue = (float)Value / (float)m_Screen->GetWidth();
    return (ClampedValue * 2) - 1.0f;
}

// ClampToScreenAxisY
// Clamps the given value to screen coordinates on the Y axis
float CVEightEngine::ClampToScreenAxisY(int Value)
{
    float ClampedValue = (float)Value / (float)m_Screen->GetHeight();
    return (ClampedValue * 2) - 1.0f;
}
