#include "scenes/denizalti/dz_gl.h"

#include <cstdio>

namespace dzgl {

#define DZ_GL_DEFINE(type, name) type name = nullptr;
DZ_GL_FUNCS(DZ_GL_DEFINE)
#undef DZ_GL_DEFINE

bool load(std::string& err) {
    err.clear();
#define DZ_GL_LOAD(type, name)                                                  \
    name = reinterpret_cast<type>(glfwGetProcAddress(#name));                   \
    if (!name) err += std::string(err.empty() ? "" : ", ") + #name;
    DZ_GL_FUNCS(DZ_GL_LOAD)
#undef DZ_GL_LOAD
    return err.empty();
}

static void APIENTRY onDebug(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei, const GLchar* msg, const void*) {
    (void)source;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    std::printf("GL %s (%u, tur %x): %s\n", severity == GL_DEBUG_SEVERITY_HIGH ? "HATA" : "uyari", id, type, msg);
}

void debugOutput(bool on) {
    if (!glDebugMessageCallback) return;
    if (on) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(onDebug, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, nullptr, GL_FALSE);
    } else {
        glDisable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(nullptr, nullptr);
    }
}

void restoreDefaults() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glUseProgram(0);
    for (int u = 0; u < 16; ++u) {
        glBindTextureUnit(u, 0);
        glBindSampler(u, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_SAMPLE_SHADING);
    glDisable(GL_MULTISAMPLE);
    glEnable(GL_MULTISAMPLE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (glClipControl) glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    glDepthFunc(GL_LESS);
}

}   // namespace dzgl
