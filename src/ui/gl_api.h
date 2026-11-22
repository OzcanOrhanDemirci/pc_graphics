// The few OpenGL entry points the interface kit needs beyond the 1.1 that opengl32.lib exports (2.0: shaders,
// multitexture coordinates; 3.0: the off-screen frame), loaded through GLFW once a context is current.
#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <windows.h>
#include <GL/gl.h>

#include <cstddef>

namespace ui::gl {

using GLchar_ = char;
#define UI_GL_FUNCS(X)                                                                                        \
    X(GLuint, CreateShader, (GLenum))                                                                          \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar_* const*, const GLint*))                              \
    X(void, CompileShader, (GLuint))                                                                           \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                                             \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar_*))                                           \
    X(GLuint, CreateProgram, (void))                                                                           \
    X(void, AttachShader, (GLuint, GLuint))                                                                    \
    X(void, LinkProgram, (GLuint))                                                                             \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                                            \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar_*))                                          \
    X(void, UseProgram, (GLuint))                                                                              \
    X(void, DeleteProgram, (GLuint))                                                                           \
    X(void, DeleteShader, (GLuint))                                                                            \
    X(GLint, GetUniformLocation, (GLuint, const GLchar_*))                                                     \
    X(void, Uniform1i, (GLint, GLint))                                                                         \
    X(void, Uniform1f, (GLint, GLfloat))                                                                       \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                              \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                            \
    X(void, ActiveTexture, (GLenum))                                                                           \
    X(void, MultiTexCoord4f, (GLenum, GLfloat, GLfloat, GLfloat, GLfloat))                                         X(void, GenFramebuffers, (GLsizei, GLuint*))                                                                   X(void, DeleteFramebuffers, (GLsizei, const GLuint*))                                                          X(void, BindFramebuffer, (GLenum, GLuint))                                                                     X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                         X(GLenum, CheckFramebufferStatus, (GLenum))                                                                    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define UI_GL_DECLARE(ret, name, args) extern ret(APIENTRY* name) args;
UI_GL_FUNCS(UI_GL_DECLARE)
#undef UI_GL_DECLARE

bool load();   // after glfwMakeContextCurrent

constexpr GLenum FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, TEXTURE0 = 0x84C0,
                 TEXTURE1 = 0x84C1, CLAMP_TO_EDGE = 0x812F, GENERATE_MIPMAP = 0x8191, FRAMEBUFFER = 0x8D40,
                 READ_FRAMEBUFFER = 0x8CA8, DRAW_FRAMEBUFFER = 0x8CA9, COLOR_ATTACHMENT0 = 0x8CE0,
                 FRAMEBUFFER_COMPLETE = 0x8CD5;

}   // namespace ui::gl
