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

#include "veightengine.hpp"
#include "shader.hpp"
#include "program.hpp"
#include "rectangle.hpp"

const char *m_VertexShader1 = "#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
"}\0";

const char *m_FragmentShader1 = "#version 330 core\n"
"out vec4 FragColor;\n"
"void main()\n"
"{\n"
"   FragColor = vec4(1.0f, 0.5f, 0.2f, 1.0f);\n"
"}\n\0";

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
    // Shaders
    CShader VertexShader(m_VertexShader1, GL_VERTEX_SHADER);
    CShader FragmentShader(m_FragmentShader1, GL_FRAGMENT_SHADER);
    std::vector<CShader> Shaders;

    // Add shaders
    Shaders.push_back(VertexShader);
    Shaders.push_back(FragmentShader);
    CProgram Program(Shaders);

    // Create a rectangle
    CRectangle Rect;

    // Initialize screen
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // 2. use our shader program when we want to render an object
    Program.Use();
    Rect.Render();
    Program.Unuse();

    // Present
    glFinish();
    m_Screen->Present();

    // don't leave
    for(;;);
}
