// The few OpenGL 2.0 entry points the interface kit needs beyond the 1.1 that opengl32.lib exports (shaders,
// multitexture coordinates), loaded through GLFW once a context is current.
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
    X(void, MultiTexCoord4f, (GLenum, GLfloat, GLfloat, GLfloat, GLfloat))

#define UI_GL_DECLARE(ret, name, args) extern ret(APIENTRY* name) args;
UI_GL_FUNCS(UI_GL_DECLARE)
#undef UI_GL_DECLARE

bool load();   // after glfwMakeContextCurrent

constexpr GLenum FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, TEXTURE0 = 0x84C0,
                 TEXTURE1 = 0x84C1, CLAMP_TO_EDGE = 0x812F, GENERATE_MIPMAP = 0x8191;

}   // namespace ui::gl
