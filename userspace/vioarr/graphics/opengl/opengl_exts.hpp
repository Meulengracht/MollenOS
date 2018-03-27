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
#define INIT_OPENGL_FUNCTION_OPT(Function, Blueprint) m_##Function = (Blueprint)glGetProcAddress(#Function);
        
        // Load extensions Version 1.4
        INIT_OPENGL_FUNCTION(glBlendFuncSeparate, PFNGLBLENDFUNCSEPARATEPROC);
        INIT_OPENGL_FUNCTION(glMultiDrawArrays, PFNGLMULTIDRAWARRAYSPROC);
        INIT_OPENGL_FUNCTION(glMultiDrawElements, PFNGLMULTIDRAWELEMENTSPROC);
        INIT_OPENGL_FUNCTION(glPointParameterf, PFNGLPOINTPARAMETERFPROC);
        INIT_OPENGL_FUNCTION(glPointParameterfv, PFNGLPOINTPARAMETERFVPROC);
        INIT_OPENGL_FUNCTION(glPointParameteri, PFNGLPOINTPARAMETERIPROC);
        INIT_OPENGL_FUNCTION(glPointParameteriv, PFNGLPOINTPARAMETERIVPROC);
        INIT_OPENGL_FUNCTION(glFogCoordf, PFNGLFOGCOORDFPROC);
        INIT_OPENGL_FUNCTION(glFogCoordfv, PFNGLFOGCOORDFVPROC);
        INIT_OPENGL_FUNCTION(glFogCoordd, PFNGLFOGCOORDDPROC);
        INIT_OPENGL_FUNCTION(glFogCoorddv, PFNGLFOGCOORDDVPROC);
        INIT_OPENGL_FUNCTION(glFogCoordPointer, PFNGLFOGCOORDPOINTERPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3b, PFNGLSECONDARYCOLOR3BPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3bv, PFNGLSECONDARYCOLOR3BVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3d, PFNGLSECONDARYCOLOR3DPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3dv, PFNGLSECONDARYCOLOR3DVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3f, PFNGLSECONDARYCOLOR3FPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3fv, PFNGLSECONDARYCOLOR3FVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3i, PFNGLSECONDARYCOLOR3IPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3iv, PFNGLSECONDARYCOLOR3IVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3s, PFNGLSECONDARYCOLOR3SPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3sv, PFNGLSECONDARYCOLOR3SVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3ub, PFNGLSECONDARYCOLOR3UBPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3ubv, PFNGLSECONDARYCOLOR3UBVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3ui, PFNGLSECONDARYCOLOR3UIPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3uiv, PFNGLSECONDARYCOLOR3UIVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3us, PFNGLSECONDARYCOLOR3USPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColor3usv, PFNGLSECONDARYCOLOR3USVPROC);
        INIT_OPENGL_FUNCTION(glSecondaryColorPointer, PFNGLSECONDARYCOLORPOINTERPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2d, PFNGLWINDOWPOS2DPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2dv, PFNGLWINDOWPOS2DVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2f, PFNGLWINDOWPOS2FPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2fv, PFNGLWINDOWPOS2FVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2i, PFNGLWINDOWPOS2IPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2iv, PFNGLWINDOWPOS2IVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2s, PFNGLWINDOWPOS2SPROC);
        INIT_OPENGL_FUNCTION(glWindowPos2sv, PFNGLWINDOWPOS2SVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3d, PFNGLWINDOWPOS3DPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3dv, PFNGLWINDOWPOS3DVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3f, PFNGLWINDOWPOS3FPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3fv, PFNGLWINDOWPOS3FVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3i, PFNGLWINDOWPOS3IPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3iv, PFNGLWINDOWPOS3IVPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3s, PFNGLWINDOWPOS3SPROC);
        INIT_OPENGL_FUNCTION(glWindowPos3sv, PFNGLWINDOWPOS3SVPROC);
        INIT_OPENGL_FUNCTION(glBlendColor, PFNGLBLENDCOLORPROC);
        INIT_OPENGL_FUNCTION(glBlendEquation, PFNGLBLENDEQUATIONPROC);

        // Load extensions Version 2.0
        INIT_OPENGL_FUNCTION(glGenBuffers, PFNGLGENBUFFERSPROC);
        INIT_OPENGL_FUNCTION(glBindBuffer, PFNGLBINDBUFFERPROC);
        INIT_OPENGL_FUNCTION(glDrawBuffers, PFNGLDRAWBUFFERSPROC);
        INIT_OPENGL_FUNCTION(glBufferData, PFNGLBUFFERDATAPROC);
        INIT_OPENGL_FUNCTION(glDeleteBuffers, PFNGLDELETEBUFFERSPROC);
        INIT_OPENGL_FUNCTION(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC);

        INIT_OPENGL_FUNCTION(glVertexAttrib1d, PFNGLVERTEXATTRIB1DPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib1dv, PFNGLVERTEXATTRIB1DVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib1f, PFNGLVERTEXATTRIB1FPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib1fv, PFNGLVERTEXATTRIB1FVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib1s, PFNGLVERTEXATTRIB1SPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib1sv, PFNGLVERTEXATTRIB1SVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib2d, PFNGLVERTEXATTRIB2DPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib2dv, PFNGLVERTEXATTRIB2DVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib2f, PFNGLVERTEXATTRIB2FPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib2fv, PFNGLVERTEXATTRIB2FVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib2s, PFNGLVERTEXATTRIB2SPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib2sv, PFNGLVERTEXATTRIB2SVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib3d, PFNGLVERTEXATTRIB3DPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib3dv, PFNGLVERTEXATTRIB3DVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib3f, PFNGLVERTEXATTRIB3FPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib3fv, PFNGLVERTEXATTRIB3FVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib3s, PFNGLVERTEXATTRIB3SPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib3sv, PFNGLVERTEXATTRIB3SVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Nbv, PFNGLVERTEXATTRIB4NBVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Niv, PFNGLVERTEXATTRIB4NIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Nsv, PFNGLVERTEXATTRIB4NSVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Nub, PFNGLVERTEXATTRIB4NUBPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Nubv, PFNGLVERTEXATTRIB4NUBVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Nuiv, PFNGLVERTEXATTRIB4NUIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4Nusv, PFNGLVERTEXATTRIB4NUSVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4bv, PFNGLVERTEXATTRIB4BVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4d, PFNGLVERTEXATTRIB4DPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4dv, PFNGLVERTEXATTRIB4DVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4f, PFNGLVERTEXATTRIB4FPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4fv, PFNGLVERTEXATTRIB4FVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4iv, PFNGLVERTEXATTRIB4IVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4s, PFNGLVERTEXATTRIB4SPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4sv, PFNGLVERTEXATTRIB4SVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4ubv, PFNGLVERTEXATTRIB4UBVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4uiv, PFNGLVERTEXATTRIB4UIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttrib4usv, PFNGLVERTEXATTRIB4USVPROC);

        INIT_OPENGL_FUNCTION(glUniform1f, PFNGLUNIFORM1FPROC);
        INIT_OPENGL_FUNCTION(glUniform2f, PFNGLUNIFORM2FPROC);
        INIT_OPENGL_FUNCTION(glUniform3f, PFNGLUNIFORM3FPROC);
        INIT_OPENGL_FUNCTION(glUniform4f, PFNGLUNIFORM4FPROC);
        INIT_OPENGL_FUNCTION(glUniform1i, PFNGLUNIFORM1IPROC);
        INIT_OPENGL_FUNCTION(glUniform2i, PFNGLUNIFORM2IPROC);
        INIT_OPENGL_FUNCTION(glUniform3i, PFNGLUNIFORM3IPROC);
        INIT_OPENGL_FUNCTION(glUniform4i, PFNGLUNIFORM4IPROC);
        INIT_OPENGL_FUNCTION(glUniform1fv, PFNGLUNIFORM1FVPROC);
        INIT_OPENGL_FUNCTION(glUniform2fv, PFNGLUNIFORM2FVPROC);
        INIT_OPENGL_FUNCTION(glUniform3fv, PFNGLUNIFORM3FVPROC);
        INIT_OPENGL_FUNCTION(glUniform4fv, PFNGLUNIFORM4FVPROC);
        INIT_OPENGL_FUNCTION(glUniform1iv, PFNGLUNIFORM1IVPROC);
        INIT_OPENGL_FUNCTION(glUniform2iv, PFNGLUNIFORM2IVPROC);
        INIT_OPENGL_FUNCTION(glUniform3iv, PFNGLUNIFORM3IVPROC);
        INIT_OPENGL_FUNCTION(glUniform4iv, PFNGLUNIFORM4IVPROC);
        INIT_OPENGL_FUNCTION(glUniformMatrix2fv, PFNGLUNIFORMMATRIX2FVPROC);
        INIT_OPENGL_FUNCTION(glUniformMatrix3fv, PFNGLUNIFORMMATRIX3FVPROC);
        INIT_OPENGL_FUNCTION(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);
        
        // Load extensions Version 3.0
        INIT_OPENGL_FUNCTION(glStencilOpSeparate, PFNGLSTENCILOPSEPARATEPROC);
        INIT_OPENGL_FUNCTION(glBindBufferRange, PFNGLBINDBUFFERRANGEPROC);
        INIT_OPENGL_FUNCTION(glGenerateMipmap, PFNGLGENERATEMIPMAPPROC);
        INIT_OPENGL_FUNCTION(glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC);
        INIT_OPENGL_FUNCTION(glGenFramebuffers, PFNGLGENRENDERBUFFERSEXTPROC);
        INIT_OPENGL_FUNCTION(glBindFramebuffer, PFNGLBINDFRAMEBUFFEREXTPROC);
        INIT_OPENGL_FUNCTION(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DEXTPROC);
        
        INIT_OPENGL_FUNCTION(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
        INIT_OPENGL_FUNCTION(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
        INIT_OPENGL_FUNCTION(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
        INIT_OPENGL_FUNCTION(glDisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC);
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

        INIT_OPENGL_FUNCTION(glVertexAttribI1i, PFNGLVERTEXATTRIBI1IPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI2i, PFNGLVERTEXATTRIBI2IPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI3i, PFNGLVERTEXATTRIBI3IPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4i, PFNGLVERTEXATTRIBI4IPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI1ui, PFNGLVERTEXATTRIBI1UIPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI2ui, PFNGLVERTEXATTRIBI2UIPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI3ui, PFNGLVERTEXATTRIBI3UIPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4ui, PFNGLVERTEXATTRIBI4UIPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI1iv, PFNGLVERTEXATTRIBI1IVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI2iv, PFNGLVERTEXATTRIBI2IVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI3iv, PFNGLVERTEXATTRIBI3IVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4iv, PFNGLVERTEXATTRIBI4IVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI1uiv, PFNGLVERTEXATTRIBI1UIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI2uiv, PFNGLVERTEXATTRIBI2UIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI3uiv, PFNGLVERTEXATTRIBI3UIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4uiv, PFNGLVERTEXATTRIBI4UIVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4bv, PFNGLVERTEXATTRIBI4BVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4sv, PFNGLVERTEXATTRIBI4SVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4ubv, PFNGLVERTEXATTRIBI4UBVPROC);
        INIT_OPENGL_FUNCTION(glVertexAttribI4usv, PFNGLVERTEXATTRIBI4USVPROC);

        INIT_OPENGL_FUNCTION(glUniform1ui, PFNGLUNIFORM1UIPROC);
        INIT_OPENGL_FUNCTION(glUniform2ui, PFNGLUNIFORM2UIPROC);
        INIT_OPENGL_FUNCTION(glUniform3ui, PFNGLUNIFORM3UIPROC);
        INIT_OPENGL_FUNCTION(glUniform4ui, PFNGLUNIFORM4UIPROC);
        INIT_OPENGL_FUNCTION(glUniform1uiv, PFNGLUNIFORM1UIVPROC);
        INIT_OPENGL_FUNCTION(glUniform2uiv, PFNGLUNIFORM2UIVPROC);
        INIT_OPENGL_FUNCTION(glUniform3uiv, PFNGLUNIFORM3UIVPROC);
        INIT_OPENGL_FUNCTION(glUniform4uiv, PFNGLUNIFORM4UIVPROC);

        // Version 3.1
        INIT_OPENGL_FUNCTION(glDrawArraysInstanced, PFNGLDRAWARRAYSINSTANCEDPROC);
        INIT_OPENGL_FUNCTION(glDrawElementsInstanced, PFNGLDRAWELEMENTSINSTANCEDPROC);
        INIT_OPENGL_FUNCTION(glTexBuffer, PFNGLTEXBUFFERPROC);
        INIT_OPENGL_FUNCTION(glPrimitiveRestartIndex, PFNGLPRIMITIVERESTARTINDEXPROC);
        INIT_OPENGL_FUNCTION(glCopyBufferSubData, PFNGLCOPYBUFFERSUBDATAPROC);
        INIT_OPENGL_FUNCTION(glGetUniformIndices, PFNGLGETUNIFORMINDICESPROC);
        INIT_OPENGL_FUNCTION(glGetActiveUniformsiv, PFNGLGETACTIVEUNIFORMSIVPROC);
        INIT_OPENGL_FUNCTION(glGetActiveUniformName, PFNGLGETACTIVEUNIFORMNAMEPROC);
        INIT_OPENGL_FUNCTION(glGetUniformBlockIndex, PFNGLGETUNIFORMBLOCKINDEXPROC);
        INIT_OPENGL_FUNCTION(glGetActiveUniformBlockiv, PFNGLGETACTIVEUNIFORMBLOCKIVPROC);
        INIT_OPENGL_FUNCTION(glUniformBlockBinding, PFNGLUNIFORMBLOCKBINDINGPROC);
        INIT_OPENGL_FUNCTION(glGetActiveUniformBlockName, PFNGLGETACTIVEUNIFORMBLOCKNAMEPROC);

        // Version 4.0
        INIT_OPENGL_FUNCTION_OPT(glMinSampleShading, PFNGLMINSAMPLESHADINGPROC);
        INIT_OPENGL_FUNCTION_OPT(glBlendEquationi, PFNGLBLENDEQUATIONIPROC);
        INIT_OPENGL_FUNCTION_OPT(glBlendEquationSeparatei, PFNGLBLENDEQUATIONSEPARATEIPROC);
        INIT_OPENGL_FUNCTION_OPT(glBlendFunci, PFNGLBLENDFUNCIPROC);
        INIT_OPENGL_FUNCTION_OPT(glBlendFuncSeparatei, PFNGLBLENDFUNCSEPARATEIPROC);
        INIT_OPENGL_FUNCTION_OPT(glDrawArraysIndirect, PFNGLDRAWARRAYSINDIRECTPROC);
        INIT_OPENGL_FUNCTION_OPT(glDrawElementsIndirect, PFNGLDRAWELEMENTSINDIRECTPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform1d, PFNGLUNIFORM1DPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform2d, PFNGLUNIFORM2DPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform3d, PFNGLUNIFORM3DPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform4d, PFNGLUNIFORM4DPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform1dv, PFNGLUNIFORM1DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform2dv, PFNGLUNIFORM2DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform3dv, PFNGLUNIFORM3DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniform4dv, PFNGLUNIFORM4DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix2dv, PFNGLUNIFORMMATRIX2DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix3dv, PFNGLUNIFORMMATRIX3DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix4dv, PFNGLUNIFORMMATRIX4DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix2x3dv, PFNGLUNIFORMMATRIX2X3DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix2x4dv, PFNGLUNIFORMMATRIX2X4DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix3x2dv, PFNGLUNIFORMMATRIX3X2DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix3x4dv, PFNGLUNIFORMMATRIX3X4DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix4x2dv, PFNGLUNIFORMMATRIX4X2DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformMatrix4x3dv, PFNGLUNIFORMMATRIX4X3DVPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetUniformdv, PFNGLGETUNIFORMDVPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetSubroutineUniformLocation, PFNGLGETSUBROUTINEUNIFORMLOCATIONPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetSubroutineIndex, PFNGLGETSUBROUTINEINDEXPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetActiveSubroutineUniformiv, PFNGLGETACTIVESUBROUTINEUNIFORMIVPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetActiveSubroutineUniformName, PFNGLGETACTIVESUBROUTINEUNIFORMNAMEPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetActiveSubroutineName, PFNGLGETACTIVESUBROUTINENAMEPROC);
        INIT_OPENGL_FUNCTION_OPT(glUniformSubroutinesuiv, PFNGLUNIFORMSUBROUTINESUIVPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetUniformSubroutineuiv, PFNGLGETUNIFORMSUBROUTINEUIVPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetProgramStageiv, PFNGLGETPROGRAMSTAGEIVPROC);
        INIT_OPENGL_FUNCTION_OPT(glPatchParameteri, PFNGLPATCHPARAMETERIPROC);
        INIT_OPENGL_FUNCTION_OPT(glPatchParameterfv, PFNGLPATCHPARAMETERFVPROC);
        INIT_OPENGL_FUNCTION_OPT(glBindTransformFeedback, PFNGLBINDTRANSFORMFEEDBACKPROC);
        INIT_OPENGL_FUNCTION_OPT(glDeleteTransformFeedbacks, PFNGLDELETETRANSFORMFEEDBACKSPROC);
        INIT_OPENGL_FUNCTION_OPT(glGenTransformFeedbacks, PFNGLGENTRANSFORMFEEDBACKSPROC);
        INIT_OPENGL_FUNCTION_OPT(glIsTransformFeedback, PFNGLISTRANSFORMFEEDBACKPROC);
        INIT_OPENGL_FUNCTION_OPT(glPauseTransformFeedback, PFNGLPAUSETRANSFORMFEEDBACKPROC);
        INIT_OPENGL_FUNCTION_OPT(glResumeTransformFeedback, PFNGLRESUMETRANSFORMFEEDBACKPROC);
        INIT_OPENGL_FUNCTION_OPT(glDrawTransformFeedback, PFNGLDRAWTRANSFORMFEEDBACKPROC);
        INIT_OPENGL_FUNCTION_OPT(glDrawTransformFeedbackStream, PFNGLDRAWTRANSFORMFEEDBACKSTREAMPROC);
        INIT_OPENGL_FUNCTION_OPT(glBeginQueryIndexed, PFNGLBEGINQUERYINDEXEDPROC);
        INIT_OPENGL_FUNCTION_OPT(glEndQueryIndexed, PFNGLENDQUERYINDEXEDPROC);
        INIT_OPENGL_FUNCTION_OPT(glGetQueryIndexediv, PFNGLGETQUERYINDEXEDIVPROC);

#undef INIT_OPENGL_FUNCTION
#undef INIT_OPENGL_FUNCTION_OPT
    }

    ~COpenGLExtensions() {

    }

public:
	COpenGLExtensions(COpenGLExtensions const&) = delete;
	void operator=(COpenGLExtensions const&) = delete;

    // Version 1.4
    void glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha) { m_glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha); }
    void glMultiDrawArrays(GLenum mode, const GLint *first, const GLsizei *count, GLsizei drawcount) { m_glMultiDrawArrays(mode, first, count, drawcount); }
    void glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type, const void *const*indices, GLsizei drawcount) { m_glMultiDrawElements(mode, count, type, indices, drawcount); }
    void glPointParameterf(GLenum pname, GLfloat param) { m_glPointParameterf(pname, param); }
    void glPointParameterfv(GLenum pname, const GLfloat *params) { m_glPointParameterfv(pname, params); }
    void glPointParameteri(GLenum pname, GLint param) { m_glPointParameteri(pname, param); }
    void glPointParameteriv(GLenum pname, const GLint *params) { m_glPointParameteriv(pname, params); }
    void glFogCoordf(GLfloat coord) { m_glFogCoordf(coord); }
    void glFogCoordfv(const GLfloat *coord) { m_glFogCoordfv(coord); }
    void glFogCoordd(GLdouble coord) { m_glFogCoordd(coord); }
    void glFogCoorddv(const GLdouble *coord) { m_glFogCoorddv(coord); }
    void glFogCoordPointer(GLenum type, GLsizei stride, const void *pointer) { m_glFogCoordPointer(type, stride, pointer); }
    void glSecondaryColor3b(GLbyte red, GLbyte green, GLbyte blue) { m_glSecondaryColor3b(red, green, blue); }
    void glSecondaryColor3bv(const GLbyte *v) { m_glSecondaryColor3bv(v); }
    void glSecondaryColor3d(GLdouble red, GLdouble green, GLdouble blue) { m_glSecondaryColor3d(red, green, blue); }
    void glSecondaryColor3dv(const GLdouble *v) { m_glSecondaryColor3dv(v); }
    void glSecondaryColor3f(GLfloat red, GLfloat green, GLfloat blue) { m_glSecondaryColor3f(red, green, blue); }
    void glSecondaryColor3fv(const GLfloat *v) { m_glSecondaryColor3fv(v); }
    void glSecondaryColor3i(GLint red, GLint green, GLint blue) { m_glSecondaryColor3i(red, green, blue); }
    void glSecondaryColor3iv(const GLint *v) { m_glSecondaryColor3iv(v); }
    void glSecondaryColor3s(GLshort red, GLshort green, GLshort blue) { m_glSecondaryColor3s(red, green, blue); }
    void glSecondaryColor3sv(const GLshort *v) { m_glSecondaryColor3sv(v); }
    void glSecondaryColor3ub(GLubyte red, GLubyte green, GLubyte blue) { m_glSecondaryColor3ub(red, green, blue); }
    void glSecondaryColor3ubv(const GLubyte *v) { m_glSecondaryColor3ubv(v); }
    void glSecondaryColor3ui(GLuint red, GLuint green, GLuint blue) { m_glSecondaryColor3ui(red, green, blue); }
    void glSecondaryColor3uiv(const GLuint *v) { m_glSecondaryColor3uiv(v); }
    void glSecondaryColor3us(GLushort red, GLushort green, GLushort blue) { m_glSecondaryColor3us(red, green, blue); }
    void glSecondaryColor3usv(const GLushort *v) { m_glSecondaryColor3usv(v); }
    void glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer) { m_glSecondaryColorPointer(size, type, stride, pointer); }
    void glWindowPos2d(GLdouble x, GLdouble y) { m_glWindowPos2d(x, y); }
    void glWindowPos2dv(const GLdouble *v) { m_glWindowPos2dv(v); }
    void glWindowPos2f(GLfloat x, GLfloat y) { m_glWindowPos2f(x, y); }
    void glWindowPos2fv(const GLfloat *v) { m_glWindowPos2fv(v); }
    void glWindowPos2i(GLint x, GLint y) { m_glWindowPos2i(x, y); }
    void glWindowPos2iv(const GLint *v) { m_glWindowPos2iv(v); }
    void glWindowPos2s(GLshort x, GLshort y) { m_glWindowPos2s(x, y); }
    void glWindowPos2sv(const GLshort *v) { m_glWindowPos2sv(v); }
    void glWindowPos3d(GLdouble x, GLdouble y, GLdouble z) { m_glWindowPos3d(x, y, z); }
    void glWindowPos3dv(const GLdouble *v) { m_glWindowPos3dv(v); }
    void glWindowPos3f(GLfloat x, GLfloat y, GLfloat z) { m_glWindowPos3f(x, y, z); }
    void glWindowPos3fv(const GLfloat *v) { m_glWindowPos3fv(v); }
    void glWindowPos3i(GLint x, GLint y, GLint z) { m_glWindowPos3i(x, y, z); }
    void glWindowPos3iv(const GLint *v) { m_glWindowPos3iv(v); }
    void glWindowPos3s(GLshort x, GLshort y, GLshort z) { m_glWindowPos3s(x, y, z); }
    void glWindowPos3sv(const GLshort *v) { m_glWindowPos3sv(v); }
    void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { m_glBlendColor(red, green, blue, alpha); }
    void glBlendEquation(GLenum mode) { m_glBlendEquation(mode); }

    // Version 2.0
    void glGenBuffers(GLsizei n, GLuint *buffers) { m_glGenBuffers(n, buffers); }
    void glBindBuffer(GLenum target, GLuint buffer) { m_glBindBuffer(target, buffer); }
    void glDrawBuffers(GLsizei n, const GLenum *bufs) { m_glDrawBuffers(n, bufs); }
    void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) { m_glBufferData(target, size, data, usage); }
    void glDeleteBuffers(GLsizei n, const GLuint *buffers) { m_glDeleteBuffers(n, buffers); }
    void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name) { m_glBindAttribLocation(program, index, name); }

    // Version 3.0
    void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) { m_glStencilOpSeparate(face, sfail, dpfail, dppass); }
    void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) { m_glBindBufferRange(target, index, buffer, offset, size); }
    void glGenerateMipmap(GLenum target) { m_glGenerateMipmap(target); }
    void glGenFramebuffers(GLsizei n, GLuint *framebuffers) { m_glGenFramebuffers(n, framebuffers); }
    void glBindFramebuffer(GLenum target, GLuint framebuffer) { m_glBindFramebuffer(target, framebuffer); }
    void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) { m_glFramebufferTexture2D(target, attachment, textarget, texture, level); }
    GLenum glCheckFramebufferStatus(GLenum target) { return m_glCheckFramebufferStatus(target); }

    void glGenVertexArrays(GLsizei n, GLuint *arrays) { m_glGenVertexArrays(n, arrays); }
    void glBindVertexArray(GLuint array) { m_glBindVertexArray(array); }
    void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) { m_glVertexAttribPointer(index, size, type, normalized, stride, pointer); }
    void glEnableVertexAttribArray(GLuint index) { m_glEnableVertexAttribArray(index); }
    void glDisableVertexAttribArray(GLuint index) { m_glDisableVertexAttribArray(index); }
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

    void glVertexAttribI1i(GLuint index, GLint x) { m_glVertexAttribI1i(index, x); }
    void glVertexAttribI2i(GLuint index, GLint x, GLint y) { m_glVertexAttribI2i(index, x, y); }
    void glVertexAttribI3i(GLuint index, GLint x, GLint y, GLint z) { m_glVertexAttribI3i(index, x, y, z); }
    void glVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) { m_glVertexAttribI4i(index, x, y, z, w); }
    void glVertexAttribI1ui(GLuint index, GLuint x) { m_glVertexAttribI1ui(index, x); }
    void glVertexAttribI2ui(GLuint index, GLuint x, GLuint y) { m_glVertexAttribI2ui(index, x, y); }
    void glVertexAttribI3ui(GLuint index, GLuint x, GLuint y, GLuint z) { m_glVertexAttribI3ui(index, x, y, z); }
    void glVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) { m_glVertexAttribI4ui(index, x, y, z, w); }
    void glVertexAttribI1iv(GLuint index, const GLint *v) { m_glVertexAttribI1iv(index, v); }
    void glVertexAttribI2iv(GLuint index, const GLint *v) { m_glVertexAttribI2iv(index, v); }
    void glVertexAttribI3iv(GLuint index, const GLint *v) { m_glVertexAttribI3iv(index, v); }
    void glVertexAttribI4iv(GLuint index, const GLint *v) { m_glVertexAttribI4iv(index, v); }
    void glVertexAttribI1uiv(GLuint index, const GLuint *v) { m_glVertexAttribI1uiv(index, v); }
    void glVertexAttribI2uiv(GLuint index, const GLuint *v) { m_glVertexAttribI2uiv(index, v); }
    void glVertexAttribI3uiv(GLuint index, const GLuint *v) { m_glVertexAttribI3uiv(index, v); }
    void glVertexAttribI4uiv(GLuint index, const GLuint *v) { m_glVertexAttribI4uiv(index, v); }
    void glVertexAttribI4bv(GLuint index, const GLbyte *v) { m_glVertexAttribI4bv(index, v); }
    void glVertexAttribI4sv(GLuint index, const GLshort *v) { m_glVertexAttribI4sv(index, v); }
    void glVertexAttribI4ubv(GLuint index, const GLubyte *v) { m_glVertexAttribI4ubv(index, v); }
    void glVertexAttribI4usv(GLuint index, const GLushort *v) { m_glVertexAttribI4usv(index, v); }

    void glVertexAttrib1d(GLuint index, GLdouble x) { m_glVertexAttrib1d(index, x); }
    void glVertexAttrib1dv(GLuint index, const GLdouble *v) { m_glVertexAttrib1dv(index, v); }
    void glVertexAttrib1f(GLuint index, GLfloat x) { m_glVertexAttrib1f(index, x); }
    void glVertexAttrib1fv(GLuint index, const GLfloat *v) { m_glVertexAttrib1fv(index, v); }
    void glVertexAttrib1s(GLuint index, GLshort x) { m_glVertexAttrib1s(index, x); }
    void glVertexAttrib1sv(GLuint index, const GLshort *v) { m_glVertexAttrib1sv(index, v); }
    void glVertexAttrib2d(GLuint index, GLdouble x, GLdouble y) { m_glVertexAttrib2d(index, x, y); }
    void glVertexAttrib2dv(GLuint index, const GLdouble *v) { m_glVertexAttrib2dv(index, v); }
    void glVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) { m_glVertexAttrib2f(index, x, y); }
    void glVertexAttrib2fv(GLuint index, const GLfloat *v) { m_glVertexAttrib2fv(index, v); }
    void glVertexAttrib2s(GLuint index, GLshort x, GLshort y) { m_glVertexAttrib2s(index, x, y); }
    void glVertexAttrib2sv(GLuint index, const GLshort *v) { m_glVertexAttrib2sv(index, v); }
    void glVertexAttrib3d(GLuint index, GLdouble x, GLdouble y, GLdouble z) { m_glVertexAttrib3d(index, x, y, z); }
    void glVertexAttrib3dv(GLuint index, const GLdouble *v) { m_glVertexAttrib3dv(index, v); }
    void glVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) { m_glVertexAttrib3f(index, x, y, z); }
    void glVertexAttrib3fv(GLuint index, const GLfloat *v) { m_glVertexAttrib3fv(index, v); }
    void glVertexAttrib3s(GLuint index, GLshort x, GLshort y, GLshort z) { m_glVertexAttrib3s(index, x, y, z); }
    void glVertexAttrib3sv(GLuint index, const GLshort *v) { m_glVertexAttrib3sv(index, v); }
    void glVertexAttrib4Nbv(GLuint index, const GLbyte *v) { m_glVertexAttrib4Nbv(index, v); }
    void glVertexAttrib4Niv(GLuint index, const GLint *v) { m_glVertexAttrib4Niv(index, v); }
    void glVertexAttrib4Nsv(GLuint index, const GLshort *v) { m_glVertexAttrib4Nsv(index, v); }
    void glVertexAttrib4Nub(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) { m_glVertexAttrib4Nub(index, x, y, z, w); }
    void glVertexAttrib4Nubv(GLuint index, const GLubyte *v) { m_glVertexAttrib4Nubv(index, v); }
    void glVertexAttrib4Nuiv(GLuint index, const GLuint *v) { m_glVertexAttrib4Nuiv(index, v); }
    void glVertexAttrib4Nusv(GLuint index, const GLushort *v) { m_glVertexAttrib4Nusv(index, v); }
    void glVertexAttrib4bv(GLuint index, const GLbyte *v) { m_glVertexAttrib4bv(index, v); }
    void glVertexAttrib4d(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { m_glVertexAttrib4d(index, x, y, z, w); }
    void glVertexAttrib4dv(GLuint index, const GLdouble *v) { m_glVertexAttrib4dv(index, v); }
    void glVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { m_glVertexAttrib4f(index, x, y, z, w); }
    void glVertexAttrib4fv(GLuint index, const GLfloat *v) { m_glVertexAttrib4fv(index, v); }
    void glVertexAttrib4iv(GLuint index, const GLint *v) { m_glVertexAttrib4iv(index, v); }
    void glVertexAttrib4s(GLuint index, GLshort x, GLshort y, GLshort z, GLshort w) { m_glVertexAttrib4s(index, x, y, z, w); }
    void glVertexAttrib4sv(GLuint index, const GLshort *v) { m_glVertexAttrib4sv(index, v); }
    void glVertexAttrib4ubv(GLuint index, const GLubyte *v) { m_glVertexAttrib4ubv(index, v); }
    void glVertexAttrib4uiv(GLuint index, const GLuint *v) { m_glVertexAttrib4uiv(index, v); }
    void glVertexAttrib4usv(GLuint index, const GLushort *v) { m_glVertexAttrib4usv(index, v); }

    void glUniform1ui(GLint location, GLuint v0) { m_glUniform1ui(location, v0); }
    void glUniform2ui(GLint location, GLuint v0, GLuint v1) { m_glUniform2ui(location, v0, v1); }
    void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) { m_glUniform3ui(location, v0, v1, v2); }
    void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { m_glUniform4ui(location, v0, v1, v2, v3); }
    void glUniform1uiv(GLint location, GLsizei count, const GLuint *value) { m_glUniform1uiv(location, count, value); }
    void glUniform2uiv(GLint location, GLsizei count, const GLuint *value) { m_glUniform2uiv(location, count, value); }
    void glUniform3uiv(GLint location, GLsizei count, const GLuint *value) { m_glUniform3uiv(location, count, value); }
    void glUniform4uiv(GLint location, GLsizei count, const GLuint *value) { m_glUniform4uiv(location, count, value); }

    void glUniform1f(GLint location, GLfloat v0) { m_glUniform1f(location, v0); }
    void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { m_glUniform2f(location, v0, v1); }
    void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { m_glUniform3f(location, v0, v1, v2); }
    void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { m_glUniform4f(location, v0, v1, v2, v3); }
    void glUniform1i(GLint location, GLint v0) { m_glUniform1i(location, v0); }
    void glUniform2i(GLint location, GLint v0, GLint v1) { m_glUniform2i(location, v0, v1); }
    void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) { m_glUniform3i(location, v0, v1, v2); }
    void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { m_glUniform4i(location, v0, v1, v2, v3); }
    void glUniform1fv(GLint location, GLsizei count, const GLfloat *value) { m_glUniform1fv(location, count, value); }
    void glUniform2fv(GLint location, GLsizei count, const GLfloat *value) { m_glUniform2fv(location, count, value); }
    void glUniform3fv(GLint location, GLsizei count, const GLfloat *value) { m_glUniform3fv(location, count, value); }
    void glUniform4fv(GLint location, GLsizei count, const GLfloat *value) { m_glUniform4fv(location, count, value); }
    void glUniform1iv(GLint location, GLsizei count, const GLint *value) { m_glUniform1iv(location, count, value); }
    void glUniform2iv(GLint location, GLsizei count, const GLint *value) { m_glUniform2iv(location, count, value); }
    void glUniform3iv(GLint location, GLsizei count, const GLint *value) { m_glUniform3iv(location, count, value); }
    void glUniform4iv(GLint location, GLsizei count, const GLint *value) { m_glUniform4iv(location, count, value); }
    void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { m_glUniformMatrix2fv(location, count, transpose, value); }
    void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { m_glUniformMatrix3fv(location, count, transpose, value); }
    void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { m_glUniformMatrix4fv(location, count, transpose, value); }

    // Version 3.1
    void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) { m_glDrawArraysInstanced(mode, first, count, instancecount); }
    void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) { m_glDrawElementsInstanced(mode, count, type, indices, instancecount); }
    void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) { m_glTexBuffer(target, internalformat, buffer); }
    void glPrimitiveRestartIndex(GLuint index) { m_glPrimitiveRestartIndex(index); }
    void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) { m_glCopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset, size); }
    void glGetUniformIndices(GLuint program, GLsizei uniformCount, const GLchar *const*uniformNames, GLuint *uniformIndices) { m_glGetUniformIndices(program, uniformCount, uniformNames, uniformIndices); }
    void glGetActiveUniformsiv(GLuint program, GLsizei uniformCount, const GLuint *uniformIndices, GLenum pname, GLint *params) { m_glGetActiveUniformsiv(program, uniformCount, uniformIndices, pname, params); }
    void glGetActiveUniformName(GLuint program, GLuint uniformIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformName) { m_glGetActiveUniformName(program, uniformIndex, bufSize, length, uniformName); }
    GLuint glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName) { return m_glGetUniformBlockIndex(program, uniformBlockName); }
    void glGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params) { m_glGetActiveUniformBlockiv(program, uniformBlockIndex, pname, params); }
    void glGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName) { m_glGetActiveUniformBlockName(program, uniformBlockIndex, bufSize, length, uniformBlockName); }
    void glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding) { m_glUniformBlockBinding(program, uniformBlockIndex, uniformBlockBinding); }

    // Version 4.0
    void glMinSampleShading(GLfloat value) { m_glMinSampleShading(value); }
    void glBlendEquationi(GLuint buf, GLenum mode) { m_glBlendEquationi(buf, mode); }
    void glBlendEquationSeparatei(GLuint buf, GLenum modeRGB, GLenum modeAlpha) { m_glBlendEquationSeparatei(buf, modeRGB, modeAlpha); }
    void glBlendFunci(GLuint buf, GLenum src, GLenum dst) { m_glBlendFunci(buf, src, dst); }
    void glBlendFuncSeparatei(GLuint buf, GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha) { m_glBlendFuncSeparatei(buf, srcRGB, dstRGB, srcAlpha, dstAlpha); }
    void glDrawArraysIndirect(GLenum mode, const void *indirect) { m_glDrawArraysIndirect(mode, indirect); }
    void glDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect) { m_glDrawElementsIndirect(mode, type, indirect); }
    void glUniform1d(GLint location, GLdouble x) { m_glUniform1d(location, x); }
    void glUniform2d(GLint location, GLdouble x, GLdouble y) { m_glUniform2d(location, x, y); }
    void glUniform3d(GLint location, GLdouble x, GLdouble y, GLdouble z) { m_glUniform3d(location, x, y, z); }
    void glUniform4d(GLint location, GLdouble x, GLdouble y, GLdouble z, GLdouble w) { m_glUniform4d(location, x, y, z, w); }
    void glUniform1dv(GLint location, GLsizei count, const GLdouble *value) { m_glUniform1dv(location, count, value); }
    void glUniform2dv(GLint location, GLsizei count, const GLdouble *value) { m_glUniform2dv(location, count, value); }
    void glUniform3dv(GLint location, GLsizei count, const GLdouble *value) { m_glUniform3dv(location, count, value); }
    void glUniform4dv(GLint location, GLsizei count, const GLdouble *value) { m_glUniform4dv(location, count, value); }
    void glUniformMatrix2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix2dv(location, count, transpose, value); }
    void glUniformMatrix3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix3dv(location, count, transpose, value); }
    void glUniformMatrix4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix4dv(location, count, transpose, value); }
    void glUniformMatrix2x3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix2x3dv(location, count, transpose, value); }
    void glUniformMatrix2x4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix2x4dv(location, count, transpose, value); }
    void glUniformMatrix3x2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix3x2dv(location, count, transpose, value); }
    void glUniformMatrix3x4dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix3x4dv(location, count, transpose, value); }
    void glUniformMatrix4x2dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix4x2dv(location, count, transpose, value); }
    void glUniformMatrix4x3dv(GLint location, GLsizei count, GLboolean transpose, const GLdouble *value) { m_glUniformMatrix4x3dv(location, count, transpose, value); }
    void glGetUniformdv(GLuint program, GLint location, GLdouble *params) { m_glGetUniformdv(program, location, params); }
    GLint glGetSubroutineUniformLocation(GLuint program, GLenum shadertype, const GLchar *name) { return m_glGetSubroutineUniformLocation(program, shadertype, name); }
    GLuint glGetSubroutineIndex(GLuint program, GLenum shadertype, const GLchar *name) { return m_glGetSubroutineIndex(program, shadertype, name); }
    void glGetActiveSubroutineUniformiv(GLuint program, GLenum shadertype, GLuint index, GLenum pname, GLint *values) { m_glGetActiveSubroutineUniformiv(program, shadertype, index, pname, values); }
    void glGetActiveSubroutineUniformName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize, GLsizei *length, GLchar *name) { m_glGetActiveSubroutineUniformName(program, shadertype, index, bufsize, length, name); }
    void glGetActiveSubroutineName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize, GLsizei *length, GLchar *name) { m_glGetActiveSubroutineName(program, shadertype, index, bufsize, length, name); }
    void glUniformSubroutinesuiv(GLenum shadertype, GLsizei count, const GLuint *indices) { m_glUniformSubroutinesuiv(shadertype, count, indices); }
    void glGetUniformSubroutineuiv(GLenum shadertype, GLint location, GLuint *params) { m_glGetUniformSubroutineuiv(shadertype, location, params); }
    void glGetProgramStageiv(GLuint program, GLenum shadertype, GLenum pname, GLint *values) { m_glGetProgramStageiv(program, shadertype, pname, values); }
    void glPatchParameteri(GLenum pname, GLint value) { m_glPatchParameteri(pname, value); }
    void glPatchParameterfv(GLenum pname, const GLfloat *values) { m_glPatchParameterfv(pname, values); }
    void glBindTransformFeedback(GLenum target, GLuint id) { m_glBindTransformFeedback(target, id); }
    void glDeleteTransformFeedbacks(GLsizei n, const GLuint *ids) { m_glDeleteTransformFeedbacks(n, ids); }
    void glGenTransformFeedbacks(GLsizei n, GLuint *ids) { m_glGenTransformFeedbacks(n, ids); }
    GLboolean glIsTransformFeedback(GLuint id) { return m_glIsTransformFeedback(id); }
    void glPauseTransformFeedback(void) { m_glPauseTransformFeedback(); }
    void glResumeTransformFeedback(void) { m_glResumeTransformFeedback(); }
    void glDrawTransformFeedback(GLenum mode, GLuint id) { m_glDrawTransformFeedback(mode, id); }
    void glDrawTransformFeedbackStream(GLenum mode, GLuint id, GLuint stream) { m_glDrawTransformFeedbackStream(mode, id, stream); }
    void glBeginQueryIndexed(GLenum target, GLuint index, GLuint id) { m_glBeginQueryIndexed(target, index, id); }
    void glEndQueryIndexed(GLenum target, GLuint index) { m_glEndQueryIndexed(target, index); }
    void glGetQueryIndexediv(GLenum target, GLuint index, GLenum pname, GLint *params) { m_glGetQueryIndexediv(target, index, pname, params); }

private:
#define DEFINE_OPENGL_FUNCTION(Function, Prototype) Prototype m_##Function

    // Version 1.4
    DEFINE_OPENGL_FUNCTION(glBlendFuncSeparate, PFNGLBLENDFUNCSEPARATEPROC);
    DEFINE_OPENGL_FUNCTION(glMultiDrawArrays, PFNGLMULTIDRAWARRAYSPROC);
    DEFINE_OPENGL_FUNCTION(glMultiDrawElements, PFNGLMULTIDRAWELEMENTSPROC);
    DEFINE_OPENGL_FUNCTION(glPointParameterf, PFNGLPOINTPARAMETERFPROC);
    DEFINE_OPENGL_FUNCTION(glPointParameterfv, PFNGLPOINTPARAMETERFVPROC);
    DEFINE_OPENGL_FUNCTION(glPointParameteri, PFNGLPOINTPARAMETERIPROC);
    DEFINE_OPENGL_FUNCTION(glPointParameteriv, PFNGLPOINTPARAMETERIVPROC);
    DEFINE_OPENGL_FUNCTION(glFogCoordf, PFNGLFOGCOORDFPROC);
    DEFINE_OPENGL_FUNCTION(glFogCoordfv, PFNGLFOGCOORDFVPROC);
    DEFINE_OPENGL_FUNCTION(glFogCoordd, PFNGLFOGCOORDDPROC);
    DEFINE_OPENGL_FUNCTION(glFogCoorddv, PFNGLFOGCOORDDVPROC);
    DEFINE_OPENGL_FUNCTION(glFogCoordPointer, PFNGLFOGCOORDPOINTERPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3b, PFNGLSECONDARYCOLOR3BPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3bv, PFNGLSECONDARYCOLOR3BVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3d, PFNGLSECONDARYCOLOR3DPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3dv, PFNGLSECONDARYCOLOR3DVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3f, PFNGLSECONDARYCOLOR3FPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3fv, PFNGLSECONDARYCOLOR3FVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3i, PFNGLSECONDARYCOLOR3IPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3iv, PFNGLSECONDARYCOLOR3IVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3s, PFNGLSECONDARYCOLOR3SPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3sv, PFNGLSECONDARYCOLOR3SVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3ub, PFNGLSECONDARYCOLOR3UBPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3ubv, PFNGLSECONDARYCOLOR3UBVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3ui, PFNGLSECONDARYCOLOR3UIPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3uiv, PFNGLSECONDARYCOLOR3UIVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3us, PFNGLSECONDARYCOLOR3USPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColor3usv, PFNGLSECONDARYCOLOR3USVPROC);
    DEFINE_OPENGL_FUNCTION(glSecondaryColorPointer, PFNGLSECONDARYCOLORPOINTERPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2d, PFNGLWINDOWPOS2DPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2dv, PFNGLWINDOWPOS2DVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2f, PFNGLWINDOWPOS2FPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2fv, PFNGLWINDOWPOS2FVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2i, PFNGLWINDOWPOS2IPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2iv, PFNGLWINDOWPOS2IVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2s, PFNGLWINDOWPOS2SPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos2sv, PFNGLWINDOWPOS2SVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3d, PFNGLWINDOWPOS3DPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3dv, PFNGLWINDOWPOS3DVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3f, PFNGLWINDOWPOS3FPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3fv, PFNGLWINDOWPOS3FVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3i, PFNGLWINDOWPOS3IPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3iv, PFNGLWINDOWPOS3IVPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3s, PFNGLWINDOWPOS3SPROC);
    DEFINE_OPENGL_FUNCTION(glWindowPos3sv, PFNGLWINDOWPOS3SVPROC);
    DEFINE_OPENGL_FUNCTION(glBlendColor, PFNGLBLENDCOLORPROC);
    DEFINE_OPENGL_FUNCTION(glBlendEquation, PFNGLBLENDEQUATIONPROC);

    // Version 2.0
    DEFINE_OPENGL_FUNCTION(glGenBuffers, PFNGLGENBUFFERSPROC);
    DEFINE_OPENGL_FUNCTION(glBindBuffer, PFNGLBINDBUFFERPROC);
    DEFINE_OPENGL_FUNCTION(glDrawBuffers, PFNGLDRAWBUFFERSPROC);
    DEFINE_OPENGL_FUNCTION(glBufferData, PFNGLBUFFERDATAPROC);
    DEFINE_OPENGL_FUNCTION(glDeleteBuffers, PFNGLDELETEBUFFERSPROC);
    DEFINE_OPENGL_FUNCTION(glBindAttribLocation, PFNGLBINDATTRIBLOCATIONPROC);

    // Version 3.0
    DEFINE_OPENGL_FUNCTION(glStencilOpSeparate, PFNGLSTENCILOPSEPARATEPROC);
    DEFINE_OPENGL_FUNCTION(glBindBufferRange, PFNGLBINDBUFFERRANGEPROC);
    DEFINE_OPENGL_FUNCTION(glGenerateMipmap, PFNGLGENERATEMIPMAPPROC);
    DEFINE_OPENGL_FUNCTION(glCheckFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC);
    DEFINE_OPENGL_FUNCTION(glGenFramebuffers, PFNGLGENRENDERBUFFERSEXTPROC);
    DEFINE_OPENGL_FUNCTION(glBindFramebuffer, PFNGLBINDFRAMEBUFFEREXTPROC);
    DEFINE_OPENGL_FUNCTION(glFramebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DEXTPROC);

    DEFINE_OPENGL_FUNCTION(glGenVertexArrays, PFNGLGENVERTEXARRAYSPROC);
    DEFINE_OPENGL_FUNCTION(glBindVertexArray, PFNGLBINDVERTEXARRAYPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC);
    DEFINE_OPENGL_FUNCTION(glEnableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC);
    DEFINE_OPENGL_FUNCTION(glDisableVertexAttribArray, PFNGLDISABLEVERTEXATTRIBARRAYPROC);
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

    DEFINE_OPENGL_FUNCTION(glVertexAttribI1i, PFNGLVERTEXATTRIBI1IPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI2i, PFNGLVERTEXATTRIBI2IPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI3i, PFNGLVERTEXATTRIBI3IPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4i, PFNGLVERTEXATTRIBI4IPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI1ui, PFNGLVERTEXATTRIBI1UIPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI2ui, PFNGLVERTEXATTRIBI2UIPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI3ui, PFNGLVERTEXATTRIBI3UIPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4ui, PFNGLVERTEXATTRIBI4UIPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI1iv, PFNGLVERTEXATTRIBI1IVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI2iv, PFNGLVERTEXATTRIBI2IVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI3iv, PFNGLVERTEXATTRIBI3IVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4iv, PFNGLVERTEXATTRIBI4IVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI1uiv, PFNGLVERTEXATTRIBI1UIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI2uiv, PFNGLVERTEXATTRIBI2UIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI3uiv, PFNGLVERTEXATTRIBI3UIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4uiv, PFNGLVERTEXATTRIBI4UIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4bv, PFNGLVERTEXATTRIBI4BVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4sv, PFNGLVERTEXATTRIBI4SVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4ubv, PFNGLVERTEXATTRIBI4UBVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttribI4usv, PFNGLVERTEXATTRIBI4USVPROC);

    DEFINE_OPENGL_FUNCTION(glVertexAttrib1d, PFNGLVERTEXATTRIB1DPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib1dv, PFNGLVERTEXATTRIB1DVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib1f, PFNGLVERTEXATTRIB1FPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib1fv, PFNGLVERTEXATTRIB1FVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib1s, PFNGLVERTEXATTRIB1SPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib1sv, PFNGLVERTEXATTRIB1SVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib2d, PFNGLVERTEXATTRIB2DPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib2dv, PFNGLVERTEXATTRIB2DVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib2f, PFNGLVERTEXATTRIB2FPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib2fv, PFNGLVERTEXATTRIB2FVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib2s, PFNGLVERTEXATTRIB2SPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib2sv, PFNGLVERTEXATTRIB2SVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib3d, PFNGLVERTEXATTRIB3DPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib3dv, PFNGLVERTEXATTRIB3DVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib3f, PFNGLVERTEXATTRIB3FPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib3fv, PFNGLVERTEXATTRIB3FVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib3s, PFNGLVERTEXATTRIB3SPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib3sv, PFNGLVERTEXATTRIB3SVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Nbv, PFNGLVERTEXATTRIB4NBVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Niv, PFNGLVERTEXATTRIB4NIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Nsv, PFNGLVERTEXATTRIB4NSVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Nub, PFNGLVERTEXATTRIB4NUBPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Nubv, PFNGLVERTEXATTRIB4NUBVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Nuiv, PFNGLVERTEXATTRIB4NUIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4Nusv, PFNGLVERTEXATTRIB4NUSVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4bv, PFNGLVERTEXATTRIB4BVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4d, PFNGLVERTEXATTRIB4DPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4dv, PFNGLVERTEXATTRIB4DVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4f, PFNGLVERTEXATTRIB4FPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4fv, PFNGLVERTEXATTRIB4FVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4iv, PFNGLVERTEXATTRIB4IVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4s, PFNGLVERTEXATTRIB4SPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4sv, PFNGLVERTEXATTRIB4SVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4ubv, PFNGLVERTEXATTRIB4UBVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4uiv, PFNGLVERTEXATTRIB4UIVPROC);
    DEFINE_OPENGL_FUNCTION(glVertexAttrib4usv, PFNGLVERTEXATTRIB4USVPROC);

    DEFINE_OPENGL_FUNCTION(glUniform1ui, PFNGLUNIFORM1UIPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2ui, PFNGLUNIFORM2UIPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3ui, PFNGLUNIFORM3UIPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4ui, PFNGLUNIFORM4UIPROC);
    DEFINE_OPENGL_FUNCTION(glUniform1uiv, PFNGLUNIFORM1UIVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2uiv, PFNGLUNIFORM2UIVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3uiv, PFNGLUNIFORM3UIVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4uiv, PFNGLUNIFORM4UIVPROC);

    DEFINE_OPENGL_FUNCTION(glUniform1f, PFNGLUNIFORM1FPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2f, PFNGLUNIFORM2FPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3f, PFNGLUNIFORM3FPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4f, PFNGLUNIFORM4FPROC);
    DEFINE_OPENGL_FUNCTION(glUniform1i, PFNGLUNIFORM1IPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2i, PFNGLUNIFORM2IPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3i, PFNGLUNIFORM3IPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4i, PFNGLUNIFORM4IPROC);
    DEFINE_OPENGL_FUNCTION(glUniform1fv, PFNGLUNIFORM1FVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2fv, PFNGLUNIFORM2FVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3fv, PFNGLUNIFORM3FVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4fv, PFNGLUNIFORM4FVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform1iv, PFNGLUNIFORM1IVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2iv, PFNGLUNIFORM2IVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3iv, PFNGLUNIFORM3IVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4iv, PFNGLUNIFORM4IVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix2fv, PFNGLUNIFORMMATRIX2FVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix3fv, PFNGLUNIFORMMATRIX3FVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix4fv, PFNGLUNIFORMMATRIX4FVPROC);

    // Version 3.1
    DEFINE_OPENGL_FUNCTION(glDrawArraysInstanced, PFNGLDRAWARRAYSINSTANCEDPROC);
    DEFINE_OPENGL_FUNCTION(glDrawElementsInstanced, PFNGLDRAWELEMENTSINSTANCEDPROC);
    DEFINE_OPENGL_FUNCTION(glTexBuffer, PFNGLTEXBUFFERPROC);
    DEFINE_OPENGL_FUNCTION(glPrimitiveRestartIndex, PFNGLPRIMITIVERESTARTINDEXPROC);
    DEFINE_OPENGL_FUNCTION(glCopyBufferSubData, PFNGLCOPYBUFFERSUBDATAPROC);
    DEFINE_OPENGL_FUNCTION(glGetUniformIndices, PFNGLGETUNIFORMINDICESPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveUniformsiv, PFNGLGETACTIVEUNIFORMSIVPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveUniformName, PFNGLGETACTIVEUNIFORMNAMEPROC);
    DEFINE_OPENGL_FUNCTION(glGetUniformBlockIndex, PFNGLGETUNIFORMBLOCKINDEXPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveUniformBlockiv, PFNGLGETACTIVEUNIFORMBLOCKIVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformBlockBinding, PFNGLUNIFORMBLOCKBINDINGPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveUniformBlockName, PFNGLGETACTIVEUNIFORMBLOCKNAMEPROC);

    // Version 4.0
    DEFINE_OPENGL_FUNCTION(glMinSampleShading, PFNGLMINSAMPLESHADINGPROC);
    DEFINE_OPENGL_FUNCTION(glBlendEquationi, PFNGLBLENDEQUATIONIPROC);
    DEFINE_OPENGL_FUNCTION(glBlendEquationSeparatei, PFNGLBLENDEQUATIONSEPARATEIPROC);
    DEFINE_OPENGL_FUNCTION(glBlendFunci, PFNGLBLENDFUNCIPROC);
    DEFINE_OPENGL_FUNCTION(glBlendFuncSeparatei, PFNGLBLENDFUNCSEPARATEIPROC);
    DEFINE_OPENGL_FUNCTION(glDrawArraysIndirect, PFNGLDRAWARRAYSINDIRECTPROC);
    DEFINE_OPENGL_FUNCTION(glDrawElementsIndirect, PFNGLDRAWELEMENTSINDIRECTPROC);
    DEFINE_OPENGL_FUNCTION(glUniform1d, PFNGLUNIFORM1DPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2d, PFNGLUNIFORM2DPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3d, PFNGLUNIFORM3DPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4d, PFNGLUNIFORM4DPROC);
    DEFINE_OPENGL_FUNCTION(glUniform1dv, PFNGLUNIFORM1DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform2dv, PFNGLUNIFORM2DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform3dv, PFNGLUNIFORM3DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniform4dv, PFNGLUNIFORM4DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix2dv, PFNGLUNIFORMMATRIX2DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix3dv, PFNGLUNIFORMMATRIX3DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix4dv, PFNGLUNIFORMMATRIX4DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix2x3dv, PFNGLUNIFORMMATRIX2X3DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix2x4dv, PFNGLUNIFORMMATRIX2X4DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix3x2dv, PFNGLUNIFORMMATRIX3X2DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix3x4dv, PFNGLUNIFORMMATRIX3X4DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix4x2dv, PFNGLUNIFORMMATRIX4X2DVPROC);
    DEFINE_OPENGL_FUNCTION(glUniformMatrix4x3dv, PFNGLUNIFORMMATRIX4X3DVPROC);
    DEFINE_OPENGL_FUNCTION(glGetUniformdv, PFNGLGETUNIFORMDVPROC);
    DEFINE_OPENGL_FUNCTION(glGetSubroutineUniformLocation, PFNGLGETSUBROUTINEUNIFORMLOCATIONPROC);
    DEFINE_OPENGL_FUNCTION(glGetSubroutineIndex, PFNGLGETSUBROUTINEINDEXPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveSubroutineUniformiv, PFNGLGETACTIVESUBROUTINEUNIFORMIVPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveSubroutineUniformName, PFNGLGETACTIVESUBROUTINEUNIFORMNAMEPROC);
    DEFINE_OPENGL_FUNCTION(glGetActiveSubroutineName, PFNGLGETACTIVESUBROUTINENAMEPROC);
    DEFINE_OPENGL_FUNCTION(glUniformSubroutinesuiv, PFNGLUNIFORMSUBROUTINESUIVPROC);
    DEFINE_OPENGL_FUNCTION(glGetUniformSubroutineuiv, PFNGLGETUNIFORMSUBROUTINEUIVPROC);
    DEFINE_OPENGL_FUNCTION(glGetProgramStageiv, PFNGLGETPROGRAMSTAGEIVPROC);
    DEFINE_OPENGL_FUNCTION(glPatchParameteri, PFNGLPATCHPARAMETERIPROC);
    DEFINE_OPENGL_FUNCTION(glPatchParameterfv, PFNGLPATCHPARAMETERFVPROC);
    DEFINE_OPENGL_FUNCTION(glBindTransformFeedback, PFNGLBINDTRANSFORMFEEDBACKPROC);
    DEFINE_OPENGL_FUNCTION(glDeleteTransformFeedbacks, PFNGLDELETETRANSFORMFEEDBACKSPROC);
    DEFINE_OPENGL_FUNCTION(glGenTransformFeedbacks, PFNGLGENTRANSFORMFEEDBACKSPROC);
    DEFINE_OPENGL_FUNCTION(glIsTransformFeedback, PFNGLISTRANSFORMFEEDBACKPROC);
    DEFINE_OPENGL_FUNCTION(glPauseTransformFeedback, PFNGLPAUSETRANSFORMFEEDBACKPROC);
    DEFINE_OPENGL_FUNCTION(glResumeTransformFeedback, PFNGLRESUMETRANSFORMFEEDBACKPROC);
    DEFINE_OPENGL_FUNCTION(glDrawTransformFeedback, PFNGLDRAWTRANSFORMFEEDBACKPROC);
    DEFINE_OPENGL_FUNCTION(glDrawTransformFeedbackStream, PFNGLDRAWTRANSFORMFEEDBACKSTREAMPROC);
    DEFINE_OPENGL_FUNCTION(glBeginQueryIndexed, PFNGLBEGINQUERYINDEXEDPROC);
    DEFINE_OPENGL_FUNCTION(glEndQueryIndexed, PFNGLENDQUERYINDEXEDPROC);
    DEFINE_OPENGL_FUNCTION(glGetQueryIndexediv, PFNGLGETQUERYINDEXEDIVPROC);

#undef DEFINE_OPENGL_FUNCTION
};

// Shorthand for the vioarr
#define sOpenGL COpenGLExtensions::GetInstance()
