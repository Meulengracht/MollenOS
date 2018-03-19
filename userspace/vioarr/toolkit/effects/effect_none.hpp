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
 * MollenOS - Vioarr Window Compositor System (Effect)
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

#include "effect.hpp"

/* Simple basic texture effect
 * Does nothing but map texture to vertices */
static const char *m_SimpleVertexShader = 
    "#version 330 core\n\n" \
    "layout(location = 0) in vec3 vertex;\n" \
    "layout(location = 0) in vec2 uv;\n" \
    "out vec2 uv_frag;\n\n" \
    "void main() {\n" \
    "uv_frag = uv;\n" \
    "gl_Position = vec4(vertex, 1.0);\n}";
static const char *m_SimpleFragmentShader = 
    "#version 330 core\n\n" \
    "in vec2 uv_frag;\n" \
    "uniform sampler2D tex;\n\n" \
    "out vec4 frag_color;\n\n" \
    "void main() {\n" \
    "frag_color = texture(tex, uv_frag);\n}";

class CBasicEffect : public CEffect {
public:
    CBasicEffect() : CEffect() {
        // Create the program
        m_ShaderId          = sOpenGL.glCreateProgram();
        m_VertexProgram     = sOpenGL.glCreateShader(GL_VERTEX_SHADER);
        m_FragmentProgram   = sOpenGL.glCreateShader(GL_FRAGMENT_SHADER);

        auto lvalue = [](auto &&arg) -> decltype(arg)& { return arg; };
        sOpenGL.glShaderSource(m_VertexProgram, 1, &lvalue(m_SimpleVertexShader), nullptr);
        sOpenGL.glShaderSource(m_FragmentProgram, 1, &lvalue(m_SimpleFragmentShader), nullptr);

        sOpenGL.glCompileShader(m_VertexProgram);
        sOpenGL.glCompileShader(m_FragmentProgram);

        sOpenGL.glAttachShader(m_ShaderId, m_VertexProgram);
        sOpenGL.glAttachShader(m_ShaderId, m_FragmentProgram);
        sOpenGL.glLinkProgram(m_ShaderId);
    }

    ~CBasicEffect() {
        sOpenGL.glDeleteProgram(m_ShaderId);
        sOpenGL.glDeleteShader(m_VertexProgram);
        sOpenGL.glDeleteShader(m_FragmentProgram);
    }

private:
    GLuint m_VertexProgram;
    GLuint m_FragmentProgram;
};
