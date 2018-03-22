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
#pragma once

/* Includes
 * - OpenGL */
#include <GL/gl.h>
#include <vector>
#include "shader.hpp"

class CProgram {
public:
    CProgram();
    ~CProgram();

    void Use() const;
    bool IsInUse() const;
    void Unuse() const;

    GLint Attribute(const GLchar* AttributeName) const;
    GLint Uniform(const GLchar* UniformName) const;

#define _TDOGL_PROGRAM_ATTRIB_N_UNIFORM_SETTERS(OGL_TYPE) \
    void SetAttribute(const GLchar* AttributeName, OGL_TYPE v0); \
    void SetAttribute(const GLchar* AttributeName, OGL_TYPE v0, OGL_TYPE v1); \
    void SetAttribute(const GLchar* AttributeName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2); \
    void SetAttribute(const GLchar* AttributeName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2, OGL_TYPE v3); \
\
    void SetAttribute1v(const GLchar* AttributeName, const OGL_TYPE* v); \
    void SetAttribute2v(const GLchar* AttributeName, const OGL_TYPE* v); \
    void SetAttribute3v(const GLchar* AttributeName, const OGL_TYPE* v); \
    void SetAttribute4v(const GLchar* AttributeName, const OGL_TYPE* v); \
\
    void SetUniform(const GLchar* UniformName, OGL_TYPE v0); \
    void SetUniform(const GLchar* UniformName, OGL_TYPE v0, OGL_TYPE v1); \
    void SetUniform(const GLchar* UniformName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2); \
    void SetUniform(const GLchar* UniformName, OGL_TYPE v0, OGL_TYPE v1, OGL_TYPE v2, OGL_TYPE v3); \
\
    void SetUniform1v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count=1); \
    void SetUniform2v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count=1); \
    void SetUniform3v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count=1); \
    void SetUniform4v(const GLchar* UniformName, const OGL_TYPE* v, GLsizei count=1); \

    _TDOGL_PROGRAM_ATTRIB_N_UNIFORM_SETTERS(GLfloat)
    _TDOGL_PROGRAM_ATTRIB_N_UNIFORM_SETTERS(GLdouble)
    _TDOGL_PROGRAM_ATTRIB_N_UNIFORM_SETTERS(GLint)
    _TDOGL_PROGRAM_ATTRIB_N_UNIFORM_SETTERS(GLuint)

    void SetUniformMatrix2(const GLchar* UniformName, const GLfloat* v, GLsizei count=1, GLboolean transpose=GL_FALSE);
    void SetUniformMatrix3(const GLchar* UniformName, const GLfloat* v, GLsizei count=1, GLboolean transpose=GL_FALSE);
    void SetUniformMatrix4(const GLchar* UniformName, const GLfloat* v, GLsizei count=1, GLboolean transpose=GL_FALSE);

    GLuint GetHandle() const;

protected:
    void Initialize();
    void AddShader(const CShader &Shader);
    
private:
    std::vector<CShader>    m_Shaders;
    GLuint                  m_Handle;
};

