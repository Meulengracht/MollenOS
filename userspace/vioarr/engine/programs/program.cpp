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
#include "utils/log_manager.hpp"

CProgram::CProgram() {
    m_Handle = sOpenGL.glCreateProgram();
    assert(m_Handle != 0);
}

CProgram::~CProgram() {
    if (m_Handle != 0) {
        sOpenGL.glDeleteProgram(m_Handle);
    }
}

void CProgram::Initialize()
{
    // Variables
    char CompileLog[512];
    GLint Status;

    // Make sure there are shaders present
    assert(m_Shaders.size() > 0);

    // Attach shaders for compilation
    for(unsigned i = 0; i < m_Shaders.size(); ++i) {
        sOpenGL.glAttachShader(m_Handle, m_Shaders[i].GetHandle());
    }
    sOpenGL.glLinkProgram(m_Handle);

    // Cleanup
    for(unsigned i = 0; i < m_Shaders.size(); ++i) {
        sOpenGL.glDetachShader(m_Handle, m_Shaders[i].GetHandle());
    }

    // Sanitize compilation
    sOpenGL.glGetProgramiv(m_Handle, GL_LINK_STATUS, &Status);
    if (Status == GL_FALSE) {
        memset(&CompileLog[0], 0, sizeof(CompileLog));
        sOpenGL.glGetProgramInfoLog(m_Handle, sizeof(CompileLog), NULL, &CompileLog[0]);
        sLog.Error("Program linking failed:");
        sLog.Error(std::string(&CompileLog[0]));
        abort();
    }
}

void CProgram::AddShader(const CShader &Shader) {
    m_Shaders.push_back(Shader);
}

void CProgram::Use() const
{
    sOpenGL.glUseProgram(m_Handle);
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
    GLint Attribute = sOpenGL.glGetAttribLocation(m_Handle, AttributeName);
    assert(Attribute != -1);
    return Attribute;
}

GLint CProgram::Uniform(const GLchar* UniformName) const
{
    assert(UniformName != nullptr);
    GLint Uniform = sOpenGL.glGetUniformLocation(m_Handle, UniformName);
    assert(Uniform != -1);
    return Uniform;
}

GLuint CProgram::GetHandle() const
{
    return m_Handle;
}

#define ATTRIB_N_UNIFORM_SETTERS(OGL_TYPE, TYPE_PREFIX, TYPE_SUFFIX) \
\
void CProgram::SetAttribute(const GLchar* AttributeName, OGL_TYPE v0) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 1 ## TYPE_SUFFIX (Attribute(AttributeName), v0); } \
void CProgram::SetAttribute(const GLchar* AttributeName, OGL_TYPE v0, OGL_TYPE v1) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 2 ## TYPE_SUFFIX (Attribute(AttributeName), v0, v1); } \
void CProgram::SetAttribute(const GLchar* AttributeName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 3 ## TYPE_SUFFIX (Attribute(AttributeName), v0, v1, v2); } \
void CProgram::SetAttribute(const GLchar* AttributeName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2, OGL_TYPE v3) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 4 ## TYPE_SUFFIX (Attribute(AttributeName), v0, v1, v2, v3); } \
\
void CProgram::SetAttribute1v(const GLchar* AttributeName, const OGL_TYPE* v) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 1 ## TYPE_SUFFIX ## v (Attribute(AttributeName), v); } \
void CProgram::SetAttribute2v(const GLchar* AttributeName, const OGL_TYPE* v) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 2 ## TYPE_SUFFIX ## v (Attribute(AttributeName), v); } \
void CProgram::SetAttribute3v(const GLchar* AttributeName, const OGL_TYPE* v) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 3 ## TYPE_SUFFIX ## v (Attribute(AttributeName), v); } \
void CProgram::SetAttribute4v(const GLchar* AttributeName, const OGL_TYPE* v) \
    { assert(IsInUse()); sOpenGL.glVertexAttrib ## TYPE_PREFIX ## 4 ## TYPE_SUFFIX ## v (Attribute(AttributeName), v); } \
\
void CProgram::SetUniform(const GLchar* UniformName, OGL_TYPE v0) \
    { assert(IsInUse()); sOpenGL.glUniform1 ## TYPE_SUFFIX (Uniform(UniformName), v0); } \
void CProgram::SetUniform(const GLchar* UniformName, OGL_TYPE v0, OGL_TYPE v1) \
    { assert(IsInUse()); sOpenGL.glUniform2 ## TYPE_SUFFIX (Uniform(UniformName), v0, v1); } \
void CProgram::SetUniform(const GLchar* UniformName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2) \
    { assert(IsInUse()); sOpenGL.glUniform3 ## TYPE_SUFFIX (Uniform(UniformName), v0, v1, v2); } \
void CProgram::SetUniform(const GLchar* UniformName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2, OGL_TYPE v3) \
    { assert(IsInUse()); sOpenGL.glUniform4 ## TYPE_SUFFIX (Uniform(UniformName), v0, v1, v2, v3); } \
\
void CProgram::SetUniform1v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count) \
    { assert(IsInUse()); sOpenGL.glUniform1 ## TYPE_SUFFIX ## v (Uniform(UniformName), count, v); } \
void CProgram::SetUniform2v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count) \
    { assert(IsInUse()); sOpenGL.glUniform2 ## TYPE_SUFFIX ## v (Uniform(UniformName), count, v); } \
void CProgram::SetUniform3v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count) \
    { assert(IsInUse()); sOpenGL.glUniform3 ## TYPE_SUFFIX ## v (Uniform(UniformName), count, v); } \
void CProgram::SetUniform4v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count) \
    { assert(IsInUse()); sOpenGL.glUniform4 ## TYPE_SUFFIX ## v (Uniform(UniformName), count, v); }

ATTRIB_N_UNIFORM_SETTERS(GLfloat, , f);
ATTRIB_N_UNIFORM_SETTERS(GLdouble, , d);
ATTRIB_N_UNIFORM_SETTERS(GLint, I, i);
ATTRIB_N_UNIFORM_SETTERS(GLuint, I, ui);

void CProgram::SetUniformMatrix2(const GLchar* UniformName, const GLfloat* v, GLsizei count, GLboolean transpose) {
    assert(IsInUse());
    sOpenGL.glUniformMatrix2fv(Uniform(UniformName), count, transpose, v);
}

void CProgram::SetUniformMatrix3(const GLchar* UniformName, const GLfloat* v, GLsizei count, GLboolean transpose) {
    assert(IsInUse());
    sOpenGL.glUniformMatrix3fv(Uniform(UniformName), count, transpose, v);
}

void CProgram::SetUniformMatrix4(const GLchar* UniformName, const GLfloat* v, GLsizei count, GLboolean transpose) {
    assert(IsInUse());
    sOpenGL.glUniformMatrix4fv(Uniform(UniformName), count, transpose, v);
}
