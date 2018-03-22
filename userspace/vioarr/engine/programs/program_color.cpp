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
 * MollenOS - Vioarr Window Compositor System (OpenGL Program)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#include "program_color.hpp"

static const char *g_VertexShaderColor = 
"#version 330 core\n"
"layout (location = 0) in vec3 InPosition;\n"
"void main()\n"
"{\n"
"   gl_Position = vec4(InPosition, 1.0);\n"
"}\0";

static const char *g_FragmentShaderColor = 
"#version 330 core\n"
"out vec4 ResultColor;\n"
"uniform vec4 InColor;\n"
"void main()\n"
"{\n"
"   ResultColor = InColor;\n"
"}\n\0";

CColorProgram::CColorProgram() : CProgram()
{
    // Shaders
    CShader VertexShader(g_VertexShaderColor, GL_VERTEX_SHADER);
    CShader FragmentShader(g_FragmentShaderColor, GL_FRAGMENT_SHADER);

    // Add shaders and initialize
    AddShader(VertexShader);
    AddShader(FragmentShader);
    Initialize();
}

CColorProgram::~CColorProgram() {

}

void CColorProgram::SetColor(float r, float g, float b, float a) {
    SetUniform("InColor", r, g, b, a);
}
