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
        INIT_OPENGL_FUNCTION(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
        INIT_OPENGL_FUNCTION(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
        INIT_OPENGL_FUNCTION(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
        
        INIT_OPENGL_FUNCTION(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC);
        INIT_OPENGL_FUNCTION(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);



#undef INIT_OPENGL_FUNCTION
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
    void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) { m_glGetShaderInfoLog(shader, bufSize, length, infoLog); }
    void glGetProgramInfoLog (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) { m_glGetProgramInfoLog(program, bufSize, length, infoLog); }

    void glGetProgramiv(GLuint program, GLenum pname, GLint *params) { m_glGetProgramiv(program, pname, params); }
    GLint glGetAttribLocation(GLuint program, const GLchar *name) { return m_glGetAttribLocation(program, name); }
    GLint glGetUniformLocation(GLuint program, const GLchar *name) { return m_glGetUniformLocation(program, name); }

private:
#define DEFINE_OPENGL_FUNCTION(Function, Prototype) Prototype m_##Function

    // Version 2.0
    DEFINE_OPENGL_FUNCTION(glGenBuffers, PFNGLGENBUFFERSPROC);
    DEFINE_OPENGL_FUNCTION(glBindBuffer, PFNGLBINDBUFFERPROC);
    DEFINE_OPENGL_FUNCTION(glDrawBuffers, PFNGLDRAWBUFFERSPROC);
    DEFINE_OPENGL_FUNCTION(glBufferData, PFNGLBUFFERDATAPROC);
    DEFINE_OPENGL_FUNCTION(glDeleteBuffers, PFNGLDELETEBUFFERSPROC);

    // Version 3.0
    DEFINE_OPENGL_FUNCTION(glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC);
    DEFINE_OPENGL_FUNCTION(glGenFramebuffers, PFNGLGENRENDERBUFFERSEXTPROC);
    DEFINE_OPENGL_FUNCTION(glBindFramebuffer, PFNGLBINDFRAMEBUFFEREXTPROC);
    DEFINE_OPENGL_FUNCTION(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DEXTPROC);

    DEFINE_OPENGL_FUNCTION(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    DEFINE_OPENGL_FUNCTION(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    DEFINE_OPENGL_FUNCTION(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    DEFINE_OPENGL_FUNCTION(glDeleteVertexArrays, PFNGLDELETEVERTEXARRAYSPROC);

    DEFINE_OPENGL_FUNCTION(glActiveTexture, PFNGLACTIVETEXTUREPROC);

    DEFINE_OPENGL_FUNCTION(glCreateProgram, PFNGLCREATEPROGRAMPROC);
    DEFINE_OPENGL_FUNCTION(glCreateShader, PFNGLCREATESHADERPROC);
    DEFINE_OPENGL_FUNCTION(glShaderSource, PFNGLSHADERSOURCEPROC);
    DEFINE_OPENGL_FUNCTION(glCompileShader, PFNGLCOMPILESHADERPROC);
    DEFINE_OPENGL_FUNCTION(glAttachShader, PFNGLATTACHSHADERPROC);
    DEFINE_OPENGL_FUNCTION(glDetachShader, PFNGLDETACHSHADERPROC);
    DEFINE_OPENGL_FUNCTION(glGetShaderiv, PFNGLGETSHADERIVPROC);
    DEFINE_OPENGL_FUNCTION(glGetProgramiv, PFNGLGETPROGRAMIVPROC);
    DEFINE_OPENGL_FUNCTION(glUseProgram, PFNGLUSEPROGRAMPROC);
    DEFINE_OPENGL_FUNCTION(glLinkProgram, PFNGLLINKPROGRAMPROC);
    DEFINE_OPENGL_FUNCTION(glDeleteProgram, PFNGLDELETEPROGRAMPROC);
    DEFINE_OPENGL_FUNCTION(glDeleteShader, PFNGLDELETESHADERPROC);
    DEFINE_OPENGL_FUNCTION(glGetShaderInfoLog, PFNGLGETSHADERINFOLOGPROC);
    DEFINE_OPENGL_FUNCTION(glGetProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC);
    
    DEFINE_OPENGL_FUNCTION(glGetAttribLocation, PFNGLGETATTRIBLOCATIONPROC);
    DEFINE_OPENGL_FUNCTION(glGetUniformLocation, PFNGLGETUNIFORMLOCATIONPROC);


#undef DEFINE_OPENGL_FUNCTION
};

// Shorthand for the vioarr
#define sOpenGL COpenGLExtensions::GetInstance()
