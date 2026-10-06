// Scene 8's OpenGL 4.6 entry points: everything past the 1.1 that opengl32.lib exports, loaded through GLFW once a
// context is current. Names are the GL names, in namespace dzgl (the interface kit keeps its own few in ui::gl).
#pragma once
#include <windows.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <string>

namespace dzgl {

#define DZ_GL_FUNCS(X)                                                   \
    X(PFNGLGETSTRINGIPROC, glGetStringi)                                 \
    X(PFNGLGETINTEGERI_VPROC, glGetIntegeri_v)                           \
    X(PFNGLGETINTEGER64VPROC, glGetInteger64v)                           \
    X(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback)             \
    X(PFNGLDEBUGMESSAGECONTROLPROC, glDebugMessageControl)               \
    X(PFNGLCREATESHADERPROC, glCreateShader)                             \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                             \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                           \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                               \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                     \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                             \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                           \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                             \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                               \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                             \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                   \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                           \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                 \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                 \
    X(PFNGLPROGRAMUNIFORM1IPROC, glProgramUniform1i)                     \
    X(PFNGLPROGRAMUNIFORM1FPROC, glProgramUniform1f)                     \
    X(PFNGLPROGRAMUNIFORM2FPROC, glProgramUniform2f)                     \
    X(PFNGLPROGRAMUNIFORM3FPROC, glProgramUniform3f)                     \
    X(PFNGLPROGRAMUNIFORM4FPROC, glProgramUniform4f)                     \
    X(PFNGLPROGRAMUNIFORM2IPROC, glProgramUniform2i)                     \
    X(PFNGLPROGRAMUNIFORM3IPROC, glProgramUniform3i)                     \
    X(PFNGLPROGRAMUNIFORMMATRIX4FVPROC, glProgramUniformMatrix4fv)       \
    X(PFNGLCREATEBUFFERSPROC, glCreateBuffers)                           \
    X(PFNGLNAMEDBUFFERSTORAGEPROC, glNamedBufferStorage)                 \
    X(PFNGLNAMEDBUFFERSUBDATAPROC, glNamedBufferSubData)                 \
    X(PFNGLGETNAMEDBUFFERSUBDATAPROC, glGetNamedBufferSubData)           \
    X(PFNGLCLEARNAMEDBUFFERDATAPROC, glClearNamedBufferData)             \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                           \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                 \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase)                         \
    X(PFNGLCREATEVERTEXARRAYSPROC, glCreateVertexArrays)                 \
    X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                 \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                       \
    X(PFNGLVERTEXARRAYVERTEXBUFFERPROC, glVertexArrayVertexBuffer)       \
    X(PFNGLVERTEXARRAYELEMENTBUFFERPROC, glVertexArrayElementBuffer)     \
    X(PFNGLVERTEXARRAYATTRIBFORMATPROC, glVertexArrayAttribFormat)       \
    X(PFNGLVERTEXARRAYATTRIBIFORMATPROC, glVertexArrayAttribIFormat)     \
    X(PFNGLVERTEXARRAYATTRIBBINDINGPROC, glVertexArrayAttribBinding)     \
    X(PFNGLENABLEVERTEXARRAYATTRIBPROC, glEnableVertexArrayAttrib)       \
    X(PFNGLCREATETEXTURESPROC, glCreateTextures)                         \
    X(PFNGLTEXTURESTORAGE2DPROC, glTextureStorage2D)                     \
    X(PFNGLTEXTURESTORAGE3DPROC, glTextureStorage3D)                     \
    X(PFNGLTEXTURESTORAGE2DMULTISAMPLEPROC, glTextureStorage2DMultisample) \
    X(PFNGLTEXTURESUBIMAGE2DPROC, glTextureSubImage2D)                   \
    X(PFNGLTEXTURESUBIMAGE3DPROC, glTextureSubImage3D)                   \
    X(PFNGLCOMPRESSEDTEXTURESUBIMAGE2DPROC, glCompressedTextureSubImage2D) \
    X(PFNGLTEXTUREPARAMETERIPROC, glTextureParameteri)                   \
    X(PFNGLTEXTUREPARAMETERFPROC, glTextureParameterf)                   \
    X(PFNGLGENERATETEXTUREMIPMAPPROC, glGenerateTextureMipmap)           \
    X(PFNGLBINDTEXTUREUNITPROC, glBindTextureUnit)                       \
    X(PFNGLGETTEXTUREIMAGEPROC, glGetTextureImage)                       \
    X(PFNGLGETTEXTURESUBIMAGEPROC, glGetTextureSubImage)                 \
    X(PFNGLCLEARTEXIMAGEPROC, glClearTexImage)                           \
    X(PFNGLBINDIMAGETEXTUREPROC, glBindImageTexture)                     \
    X(PFNGLCREATESAMPLERSPROC, glCreateSamplers)                         \
    X(PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri)                   \
    X(PFNGLSAMPLERPARAMETERFPROC, glSamplerParameterf)                   \
    X(PFNGLBINDSAMPLERPROC, glBindSampler)                               \
    X(PFNGLDELETESAMPLERSPROC, glDeleteSamplers)                         \
    X(PFNGLCREATEFRAMEBUFFERSPROC, glCreateFramebuffers)                 \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                 \
    X(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC, glNamedFramebufferTexture)       \
    X(PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC, glNamedFramebufferDrawBuffers) \
    X(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC, glCheckNamedFramebufferStatus) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                       \
    X(PFNGLCLEARNAMEDFRAMEBUFFERFVPROC, glClearNamedFramebufferfv)       \
    X(PFNGLBLITNAMEDFRAMEBUFFERPROC, glBlitNamedFramebuffer)             \
    X(PFNGLPATCHPARAMETERIPROC, glPatchParameteri)                       \
    X(PFNGLMINSAMPLESHADINGPROC, glMinSampleShading)                     \
    X(PFNGLGETMULTISAMPLEFVPROC, glGetMultisamplefv)                     \
    X(PFNGLDRAWELEMENTSBASEVERTEXPROC, glDrawElementsBaseVertex)         \
    X(PFNGLDRAWARRAYSINSTANCEDPROC, glDrawArraysInstanced)               \
    X(PFNGLMULTIDRAWELEMENTSINDIRECTPROC, glMultiDrawElementsIndirect)   \
    X(PFNGLMULTIDRAWELEMENTSPROC, glMultiDrawElements)                   \
    X(PFNGLDISPATCHCOMPUTEPROC, glDispatchCompute)                       \
    X(PFNGLMEMORYBARRIERPROC, glMemoryBarrier)                           \
    X(PFNGLCREATEQUERIESPROC, glCreateQueries)                           \
    X(PFNGLDELETEQUERIESPROC, glDeleteQueries)                           \
    X(PFNGLQUERYCOUNTERPROC, glQueryCounter)                             \
    X(PFNGLBEGINQUERYPROC, glBeginQuery)                                 \
    X(PFNGLENDQUERYPROC, glEndQuery)                                     \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v)               \
    X(PFNGLGETQUERYOBJECTIVPROC, glGetQueryObjectiv)                     \
    X(PFNGLFENCESYNCPROC, glFenceSync)                                   \
    X(PFNGLCLIENTWAITSYNCPROC, glClientWaitSync)                         \
    X(PFNGLDELETESYNCPROC, glDeleteSync)                                 \
    X(PFNGLCLIPCONTROLPROC, glClipControl)                               \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                   \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                           \
    X(PFNGLCOPYIMAGESUBDATAPROC, glCopyImageSubData)

#define DZ_GL_DECLARE(type, name) extern type name;
DZ_GL_FUNCS(DZ_GL_DECLARE)
#undef DZ_GL_DECLARE

// after glfwMakeContextCurrent; false and the missing names in err if any is absent
bool load(std::string& err);
// GL errors and the driver's debug messages (high and medium) to stdout
void debugOutput(bool on);

// GL state the interface kit expects back after the scene drew (framebuffer 0, no program, no vertex array...)
void restoreDefaults();

}   // namespace dzgl
