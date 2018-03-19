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
#include <GL/gl.h>
#include <assert.h>

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
#define INIT_OPENGL_FUNCTION(Function, Blueprint) m_##Function = (Blueprint)glGetProcAddress(#Function); \
                                                  assert(m_##Function != nullptr)
        // Load extensions Version 2.0
        INIT_OPENGL_FUNCTION(glGenBuffers, PFNGLGENBUFFERSPROC);
        INIT_OPENGL_FUNCTION(glBindBuffer, PFNGLBINDBUFFERPROC);
        INIT_OPENGL_FUNCTION(glDrawBuffers, PFNGLDRAWBUFFERSPROC);
        INIT_OPENGL_FUNCTION(glBufferData, PFNGLBUFFERDATAPROC);
        INIT_OPENGL_FUNCTION(glDeleteBuffers, PFNGLDELETEBUFFERSPROC);
        
        // Load extensions Version 3.0
        INIT_OPENGL_FUNCTION(glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC);
        INIT_OPENGL_FUNCTION(glGenFramebuffers, PFNGLGENRENDERBUFFERSEXTPROC);
        INIT_OPENGL_FUNCTION(glBindFramebuffer, PFNGLBINDFRAMEBUFFEREXTPROC);
        INIT_OPENGL_FUNCTION(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DEXTPROC);
        
        INIT_OPENGL_FUNCTION(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
        INIT_OPENGL_FUNCTION(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
        INIT_OPENGL_FUNCTION(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
        INIT_OPENGL_FUNCTION(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC);

        INIT_OPENGL_FUNCTION(glActiveTexture, PFNGLACTIVETEXTUREPROC);

        INIT_OPENGL_FUNCTION(glCreateProgram, PFNGLCREATEPROGRAMPROC);
        INIT_OPENGL_FUNCTION(glCreateShader, PFNGLCREATESHADERPROC);
        INIT_OPENGL_FUNCTION(glShaderSource, PFNGLSHADERSOURCEPROC);
        INIT_OPENGL_FUNCTION(glCompileShader, PFNGLCOMPILESHADERPROC);
        INIT_OPENGL_FUNCTION(glAttachShader, PFNGLATTACHSHADERPROC);
        INIT_OPENGL_FUNCTION(glDetachShader, PFNGLDETACHSHADERPROC);
        INIT_OPENGL_FUNCTION(glGetShaderiv, PFNGLGETSHADERIVPROC);
        INIT_OPENGL_FUNCTION(glUseProgram, PFNGLUSEPROGRAMPROC);
        INIT_OPENGL_FUNCTION(glLinkProgram, PFNGLLINKPROGRAMPROC);
        INIT_OPENGL_FUNCTION(glDeleteProgram, PFNGLDELETEPROGRAMPROC);
        INIT_OPENGL_FUNCTION(glDeleteShader, PFNGLDELETESHADERPROC);
    }

    ~COpenGLExtensions() {

    }

public:
	COpenGLExtensions(COpenGLExtensions const&) = delete;
	void operator=(COpenGLExtensions const&) = delete;

    // Version 2.0
    void glGenBuffers(GLsizei n, GLuint *buffers) { m_glGenBuffers(n, buffers); }
    void glBindBuffer(GLenum target, GLuint buffer) { m_glBindBuffer(target, buffer); }
    void glDrawBuffers(GLsizei n, const GLenum *bufs) { m_glDrawBuffers(n, bufs); }
    void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) { m_glBufferData(target, size, data, usage); }
    void glDeleteBuffers(GLsizei n, const GLuint *buffers) { m_glDeleteBuffers(n, buffers); }

    // Version 3.0
    void glGenFramebuffers(GLsizei n, GLuint *framebuffers) { m_glGenFramebuffers(n, framebuffers); }
    void glBindFramebuffer(GLenum target, GLuint framebuffer) { m_glBindFramebuffer(target, framebuffer); }
    void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { m_glFramebufferTexture2D(target, attachment, textarget, texture, level); }
    GLenum glCheckFramebufferStatus(GLenum target) { return m_glCheckFramebufferStatus(target); }

    void glGenVertexArrays(GLsizei n, GLuint *arrays) { m_glGenVertexArrays(n, arrays); }
    void glBindVertexArray(GLuint array) { m_glBindVertexArray(array); }
    void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) { m_glVertexAttribPointer(index, size, type, normalized, stride, pointer); }
    void glEnableVertexAttribArray(GLuint index) { m_glEnableVertexAttribArray(index); }
    void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) { m_glDeleteVertexArrays(n, arrays); }

    void glActiveTexture(GLenum texture) { m_glActiveTexture(texture); }

    GLuint glCreateProgram(void) { return m_glCreateProgram(); }
    GLuint glCreateShader(GLenum type) { return m_glCreateShader(type); }
    void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) { m_glShaderSource(shader, count, string, length); }
    void glCompileShader(GLuint shader) { m_glCompileShader(shader); }
    void glAttachShader(GLuint program, GLuint shader) { m_glAttachShader(program, shader); }
    void glDetachShader(GLuint program, GLuint shader) { m_glDetachShader(program, shader); }
    void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) { m_glGetShaderiv(shader, pname, params); }
    void glUseProgram(GLuint program) { m_glUseProgram(program); }
    void glLinkProgram(GLuint program) { m_glLinkProgram(program); }
    void glDeleteProgram(GLuint program) { m_glDeleteProgram(program); }
    void glDeleteShader(GLuint shader) { m_glDeleteShader(shader); }

private:
    // Version 2.0
    PFNGLGENBUFFERSPROC                 m_glGenBuffers;
    PFNGLBINDBUFFERPROC                 m_glBindBuffer;
    PFNGLDRAWBUFFERSPROC                m_glDrawBuffers;
    PFNGLBUFFERDATAPROC                 m_glBufferData;
    PFNGLDELETEBUFFERSPROC              m_glDeleteBuffers;

    // Version 3.0
    PFNGLCHECKFRAMEBUFFERSTATUSPROC     m_glCheckFramebufferStatus;
    PFNGLGENRENDERBUFFERSEXTPROC        m_glGenFramebuffers;
    PFNGLBINDFRAMEBUFFEREXTPROC         m_glBindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DEXTPROC    m_glFramebufferTexture2D;

    PFNGLGENVERTEXARRAYSPROC            m_glGenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC            m_glBindVertexArray;
    PFNGLVERTEXATTRIBPOINTERPROC        m_glVertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC    m_glEnableVertexAttribArray;
    PFNGLDELETEVERTEXARRAYSPROC         m_glDeleteVertexArrays;

    PFNGLACTIVETEXTUREPROC              m_glActiveTexture;

    PFNGLCREATEPROGRAMPROC              m_glCreateProgram;
    PFNGLCREATESHADERPROC               m_glCreateShader;
    PFNGLSHADERSOURCEPROC               m_glShaderSource;
    PFNGLCOMPILESHADERPROC              m_glCompileShader;
    PFNGLATTACHSHADERPROC               m_glAttachShader;
    PFNGLDETACHSHADERPROC               m_glDetachShader;
    PFNGLGETSHADERIVPROC                m_glGetShaderiv;
    PFNGLUSEPROGRAMPROC                 m_glUseProgram;
    PFNGLLINKPROGRAMPROC                m_glLinkProgram;
    PFNGLDELETEPROGRAMPROC              m_glDeleteProgram;
    PFNGLDELETESHADERPROC               m_glDeleteShader;
};

// Shorthand for the vioarr
#define sOpenGL COpenGLExtensions::GetInstance()
