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

#include <GL/gl.h>
#include "graphics/opengl/opengl_exts.hpp"
#include "texture.hpp"

// Extern our image loaders
extern GLuint CreateTexturePNG(const char *Path, int *Width, int *Height);

CTexture::CTexture(const std::string &Path) {
    // Detect the kind of loader to use (img extension) @todo
    m_TextureId = CreateTexturePNG(Path.c_str(), &m_TextureWidth, &m_TextureHeight);
}

CTexture::~CTexture() {
    glDeleteTextures(1, &m_TextureId);
}

void CTexture::Bind() {
    glBindTexture(GL_TEXTURE_2D, m_TextureId);
}

void CTexture::Draw() {
    // Empty
}

void CTexture::Unbind() {
    glBindTexture(GL_TEXTURE_2D, 0);
}

