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
 * MollenOS - Vioarr Window Compositor System (Display Interface Implementation)
 *  - The window compositor system and general window manager for
 *    MollenOS. This display implementation is of the default display where
 *    we use osmesa as the backend combined with the native framebuffer
 */
#pragma once

/* Includes
 * - OpenGL */
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>

#ifdef _VIOARR_OSMESA
#include <GL/osmesa.h>
#define glGetProcAddress OSMesaGetProcAddress
#endif

class COpenGLExtensions {
public:
	static COpenGLExtensions& GetInstance() {
		// Guaranteed to be destroyed.
		// Is instantiated on first use
		static COpenGLExtensions _Instance;
		return _Instance;
	}
private:
	COpenGLExtensions() {
        // Load extensions Version 2.0
        m_glDrawBuffers = (PFNGLDRAWBUFFERSPROC)glGetProcAddress("glDrawBuffers");

        // Load extensions Version 3.0
        m_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)glGetProcAddress("glCheckFramebufferStatus");
        m_glGenFramebuffers = (PFNGLGENRENDERBUFFERSEXTPROC)glGetProcAddress("glGenFramebuffers");
        m_glBindFramebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)glGetProcAddress("glBindFramebuffer");
        m_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)glGetProcAddress("glFramebufferTexture2D");
    }

    ~COpenGLExtensions() {

    }

public:
	COpenGLExtensions(COpenGLExtensions const&) = delete;
	void operator=(COpenGLExtensions const&) = delete;

    // Version 2.0
    void glDrawBuffers(GLsizei n, const GLenum *bufs) { m_glDrawBuffers(n, bufs); }

    // Version 3.0
    void glGenFramebuffers(GLsizei n, GLuint *framebuffers) { m_glGenFramebuffers(n, framebuffers); }
    void glBindFramebuffer(GLenum target, GLuint framebuffer) { m_glBindFramebuffer(target, framebuffer); }
    void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { m_glFramebufferTexture2D(target, attachment, textarget, texture, level); }
    GLenum glCheckFramebufferStatus(GLenum target) { return m_glCheckFramebufferStatus(target); }

private:
    // Version 2.0
    PFNGLDRAWBUFFERSPROC                m_glDrawBuffers;

    // Version 3.0
    PFNGLCHECKFRAMEBUFFERSTATUSPROC     m_glCheckFramebufferStatus;
    PFNGLGENRENDERBUFFERSEXTPROC        m_glGenFramebuffers;
    PFNGLBINDFRAMEBUFFEREXTPROC         m_glBindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC    m_glFramebufferTexture2D;
};

// Shorthand for the vioarr
#define sOpenGL COpenGLExtensions::GetInstance()
