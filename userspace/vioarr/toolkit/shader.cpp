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
#include "shader.hpp"
#include "../graphics/opengl/opengl_exts.hpp"

CShader::CShader(const std::string ShaderCode, GLenum ShaderType)
{
    const char* CCode = ShaderCode.c_str();
    GLint Status;

    // Create the shader
    m_Handle = sOpenGL.glCreateShader(ShaderType);
    assert(m_Handle != 0);

    sOpenGL.glShaderSource(m_Handle, 1, (const GLchar**)&CCode, NULL);
    sOpenGL.glCompileShader(m_Handle);

    // Check compilation status
    sOpenGL.glGetShaderiv(m_Handle, GL_COMPILE_STATUS, &Status);
    assert(Status != GL_FALSE);
}

CShader::CShader(const CShader& other)
{
    // Copy handle and ref-count
    m_Handle = other.m_Handle;

    // Increase ref count
}

CShader& CShader::operator =(const CShader& other)
{
    //_release();
    m_Handle = other.m_Handle;
    //_refCount = other._refCount;
    //_retain();
    return *this;
}

CShader::~CShader() {
    //if(_refCount) _release();
}

GLuint CShader::GetHandle() const {
    return m_Handle;
}
