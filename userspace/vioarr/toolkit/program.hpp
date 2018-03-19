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
#include "../utils/vectors.hpp"

class CProgram {
public:
    CProgram(const std::vector<CShader> &Shaders);
    ~CProgram();

    void Use() const;
    bool IsInUse() const;
    void Unuse() const;

    GLint Attribute(const GLchar* AttributeName) const;
    GLint Uniform(const GLchar* UniformName) const;

    GLuint GetHandle() const;
private:
    GLuint m_Handle;
};

