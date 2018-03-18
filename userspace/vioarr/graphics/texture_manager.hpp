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
 * MollenOS - Vioarr Window Compositor System
 *  - The window compositor system and general window manager for
 *    MollenOS.
 */
#pragma once

/* Includes
 * - OpenGL */
#ifdef _VIOARR_OPENGL
#include "opengl/opengl_exts.hpp"
#endif

/* Includes
 * - System */
#include "display.hpp"
#include <vector>

class CTexture {
public:
    CTexture(GLuint Id, int References) : m_Id(Id), m_References(References) { }
    ~CTexture() { }

    void DecreaseReferences() { m_References--; }

    GLuint GetId() { return m_Id; }
    int GetReferences() { return m_References; }
private:
    GLuint  m_Id;
    int     m_References;
};

class CTextureManager {
public:
	static CTextureManager& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static CTextureManager _Instance;
		return _Instance;
	}
private:
	CTextureManager() {}                     // Constructor? (the {} brackets) are needed here.
    ~CTextureManager() {

    }

public:
	CTextureManager(CTextureManager const&) = delete;
	void operator=(CTextureManager const&) = delete;

    // Texture loaders
#ifdef _VIOARR_OPENGL
    GLuint  CreateTexturePNG(const char *Path, int *Width, int *Height);
#else

#endif

private:
    std::vector<GLuint> m_Textures;
};

// Shorthand for the vioarr
#define sTextureManager CTextureManager::GetInstance()
