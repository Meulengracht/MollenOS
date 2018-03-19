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
#include "program.hpp"
#include "../graphics/opengl/opengl_exts.hpp"

CProgram::CProgram(const std::vector<CShader> &Shaders)
{
    // Make sure there are shaders present
    GLint Status;
    assert(Shaders.size() > 0);

    // Create the program
    m_Handle = sOpenGL.glCreateProgram();
    assert(m_Handle != 0);

    // Attach shaders for compilation
    for(unsigned i = 0; i < Shaders.size(); ++i) {
        sOpenGL.glAttachShader(m_Handle, Shaders[i].GetHandle());
    }
    sOpenGL.glLinkProgram(m_Handle);

    // Cleanup
    for(unsigned i = 0; i < Shaders.size(); ++i) {
        sOpenGL.glDetachShader(m_Handle, Shaders[i].GetHandle());
    }

    // Sanitize compilation
    glGetProgramiv(m_Handle, GL_LINK_STATUS, &Status);
    assert(Status != GL_FALSE);
}

CProgram::~CProgram()
{
    if (m_Handle != 0) {
        sOpenGL.glDeleteProgram(m_Handle);
    }
}

void CProgram::Use() const
{
    glUseProgram(m_Handle);
}

bool CProgram::IsInUse() const
{
    GLint CurrentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &CurrentProgram);
    return (CurrentProgram == (GLint)m_Handle);
}

void CProgram::Unuse() const
{
    assert(IsInUse());
    sOpenGL.glUseProgram(0);
}

GLint CProgram::Attribute(const GLchar* AttributeName) const
{
    assert(AttributeName != nullptr);
    GLint Attribute = glGetAttribLocation(m_Handle, AttributeName);
    assert(Attribute != -1);
    return Attribute;
}

GLint CProgram::Uniform(const GLchar* UniformName) const
{
    assert(UniformName != nullptr);
    GLint Uniform = glGetUniformLocation(m_Handle, UniformName);
    assert(Uniform != -1);
    return Uniform;
}

GLuint CProgram::GetHandle() const
{
    return m_Handle;
}
