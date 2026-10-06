#include "scenes/denizalti/dz_render.h"
#include "scenes/denizalti/dz_bake.h"
#include "scenes/denizalti/dz_decal.h"
#include "scenes/denizalti/dz_gl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace dzgl;

namespace dz {

float3 Camera::forward() const {
    return make_float3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
}

namespace {

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// a shader's text with its #include "x" lines replaced by the files (relative to dir), once each
bool preprocess(const std::string& dir, const std::string& name, std::string& out, std::string& err, int depth = 0) {
    std::string src;
    if (!readFile(dir + "/" + name, src)) {
        err = "okunamadi: " + dir + "/" + name;
        return false;
    }
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        const size_t p = line.find("#include");
        if (p != std::string::npos && line.find_first_not_of(" \t") == p) {
            const size_t a = line.find('"'), b = line.rfind('"');
            if (a == std::string::npos || b <= a || depth > 4) {
                err = name + ": #include bozuk";
                return false;
            }
            std::string inc;
            if (!preprocess(dir, line.substr(a + 1, b - a - 1), inc, err, depth + 1)) return false;
            out += inc + "\n";
        } else {
            out += line + "\n";
        }
    }
    return true;
}

unsigned compile(GLenum type, const std::string& dir, const std::string& name, std::string& err) {
    std::string src;
    if (!preprocess(dir, name, src, err)) return 0;
    const unsigned s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        err = name + ":\n" + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned link(const std::vector<unsigned>& shaders, const std::string& what, std::string& err) {
    const unsigned p = glCreateProgram();
    for (unsigned s : shaders) glAttachShader(p, s);
    glLinkProgram(p);
    for (unsigned s : shaders) glDeleteShader(s);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        err = what + " baglanamadi:\n" + log;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

unsigned program(const std::string& dir, const std::vector<std::pair<GLenum, std::string>>& stages, std::string& err) {
    std::vector<unsigned> sh;
    for (const auto& st : stages) {
        const unsigned s = compile(st.first, dir, st.second, err);
        if (!s) {
            for (unsigned x : sh) glDeleteShader(x);
            return 0;
        }
        sh.push_back(s);
    }
    return link(sh, stages.back().second, err);
}

struct FrameUBO {
    float viewProj[16];
    float camPos[4];
    float target[4];
    float viewport[4];
    float misc[4];
    float lampPos[4][4];   // xyz, radius
    float lampL[4][4];     // radiance rgb, w: 1 if present
    float camFw[4];        // the view axis
    float haze[4];         // near, far, slices, on
    float probeLo[4];      // the probe grid's first probe; w: mips (0: no probes)
    float probeStep[4];    // its spacing; w: how many probes a sample blends (1 nearest, 8 trilinear)
    float probeDims[4];
    float levels[4];       // tessellation, graphics, light, effects (0 low .. 2 high)
    float renderSize[4];   // this draw's pixels (a photo tile's own, not the whole picture's)
};

// column-major 4x4 products for the camera
void mul(const float* a, const float* b, float* r) {
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k];
            r[c * 4 + rr] = s;
        }
}

}   // namespace

bool Renderer::buildPrograms(std::string& err) {
    const std::string dir = root_ + "/src/scenes/denizalti/glsl";
    unsigned sc = program(dir, {{GL_VERTEX_SHADER, "scene.vert"}, {GL_TESS_CONTROL_SHADER, "scene.tesc"},
                                {GL_TESS_EVALUATION_SHADER, "scene.tese"}, {GL_FRAGMENT_SHADER, "scene.frag"}}, err);
    if (!sc) return false;
    unsigned rs = program(dir, {{GL_COMPUTE_SHADER, "resolve.comp"}}, err);
    if (!rs) { glDeleteProgram(sc); return false; }
    unsigned po = program(dir, {{GL_VERTEX_SHADER, "fullscreen.vert"}, {GL_FRAGMENT_SHADER, "post.frag"}}, err);
    if (!po) { glDeleteProgram(sc); glDeleteProgram(rs); return false; }
    unsigned fi = program(dir, {{GL_VERTEX_SHADER, "fullscreen.vert"}, {GL_FRAGMENT_SHADER, "final.frag"}}, err);
    if (!fi) { glDeleteProgram(sc); glDeleteProgram(rs); glDeleteProgram(po); return false; }
    unsigned fx = program(dir, {{GL_COMPUTE_SHADER, "froxel.comp"}}, err);
    if (!fx) { glDeleteProgram(sc); glDeleteProgram(rs); glDeleteProgram(po); glDeleteProgram(fi); return false; }
    if (progFroxel_) glDeleteProgram(progFroxel_);
    progFroxel_ = fx;
    unsigned bl = program(dir, {{GL_COMPUTE_SHADER, "bloom.comp"}}, err);
    if (!bl) return false;
    if (progBloom_) glDeleteProgram(progBloom_);
    progBloom_ = bl;
    if (progScene_) glDeleteProgram(progScene_);
    if (progResolve_) glDeleteProgram(progResolve_);
    if (progPost_) glDeleteProgram(progPost_);
    if (progFinal_) glDeleteProgram(progFinal_);
    progScene_ = sc;
    progResolve_ = rs;
    progPost_ = po;
    progFinal_ = fi;
    return true;
}

bool Renderer::init(const std::string& root, std::string& err) {
    root_ = root;
    if (!dzgl::load(err)) {
        err = "OpenGL 4.6 islevleri eksik: " + err;
        return false;
    }
    glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    glRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxColorSamples);
    glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &maxDepthSamples);
    glGetIntegerv(GL_MAX_TESS_GEN_LEVEL, &maxTessLevel);
    std::printf("denizalti: GL %s | %s | ornek: en cok %d (renk %d, derinlik %d) | tessellation en cok %d\n", glVersion.c_str(),
                glRenderer.c_str(), maxSamples, maxColorSamples, maxDepthSamples, maxTessLevel);
    if (!buildPrograms(err)) return false;
    makeNoise();
    glCreateBuffers(1, &ubo_);
    glNamedBufferStorage(ubo_, sizeof(FrameUBO), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, &emptyVao_);
    for (auto& f : queries_) glCreateQueries(GL_TIMESTAMP, 5, f);
    return true;
}

bool Renderer::reloadShaders(std::string& err) { return buildPrograms(err); }

namespace {
// the shader's gradient noise (common.glsl gnoise), periodic in 32 lattice cells: value and gradient
uint32_t nhash(uint32_t x, uint32_t y, uint32_t z) {
    x = (x & 31u) + 65536u;   // periodic, and the shader's offset into unsigned range
    y = (y & 31u) + 65536u;
    z = (z & 31u) + 65536u;
    x *= 1597334673u;
    y *= 3812015801u;
    z *= 2798796415u;
    uint32_t n = (x ^ y ^ z) * 1597334673u;
    n ^= n >> 16;
    n *= 2246822519u;
    n ^= n >> 13;
    return n;
}
void ngrad(uint32_t x, uint32_t y, uint32_t z, float g[3]) {
    const uint32_t h = nhash(x, y, z);
    g[0] = float(h & 1023u) / 511.5f - 1.0f;
    g[1] = float((h >> 10) & 1023u) / 511.5f - 1.0f;
    g[2] = float((h >> 20) & 1023u) / 511.5f - 1.0f;
}
float gnoise(float px, float py, float pz, float d[3]) {
    const float ix = std::floor(px), iy = std::floor(py), iz = std::floor(pz);
    const float f[3] = {px - ix, py - iy, pz - iz};
    const uint32_t cx = uint32_t(int(ix)), cy = uint32_t(int(iy)), cz = uint32_t(int(iz));
    float u[3], du[3];
    for (int k = 0; k < 3; ++k) {
        u[k] = f[k] * f[k] * f[k] * (f[k] * (f[k] * 6.0f - 15.0f) + 10.0f);
        du[k] = 30.0f * f[k] * f[k] * (f[k] * (f[k] - 2.0f) + 1.0f);
    }
    float v[8], gs[8][3];
    for (int c = 0; c < 8; ++c) {
        const int ox = c & 1, oy = (c >> 1) & 1, oz = (c >> 2) & 1;
        ngrad(cx + ox, cy + oy, cz + oz, gs[c]);
        v[c] = gs[c][0] * (f[0] - ox) + gs[c][1] * (f[1] - oy) + gs[c][2] * (f[2] - oz);
    }
    // trilinear in the quintic weights, and its derivative (numerically exact enough for a texture)
    auto lerpf = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerpf(v[0], v[1], u[0]), x10 = lerpf(v[2], v[3], u[0]), x01 = lerpf(v[4], v[5], u[0]), x11 = lerpf(v[6], v[7], u[0]);
    const float y0 = lerpf(x00, x10, u[1]), y1 = lerpf(x01, x11, u[1]);
    const float val = lerpf(y0, y1, u[2]);
    // the gradient: the weights' derivatives plus the interpolated corner gradients
    float g[3] = {0, 0, 0};
    for (int c = 0; c < 8; ++c) {
        const int ox = c & 1, oy = (c >> 1) & 1, oz = (c >> 2) & 1;
        const float wx = ox ? u[0] : 1.0f - u[0], wy = oy ? u[1] : 1.0f - u[1], wz = oz ? u[2] : 1.0f - u[2];
        const float w = wx * wy * wz;
        for (int k = 0; k < 3; ++k) g[k] += w * gs[c][k];
        g[0] += (ox ? du[0] : -du[0]) * wy * wz * v[c];
        g[1] += wx * (oy ? du[1] : -du[1]) * wz * v[c];
        g[2] += wx * wy * (oz ? du[2] : -du[2]) * v[c];
    }
    for (int k = 0; k < 3; ++k) d[k] = g[k];
    return val;
}
}   // namespace

void Renderer::uploadDecals(const Decals& d) {
    if (decalTex_) glDeleteTextures(1, &decalTex_);
    if (decalUbo_) glDeleteBuffers(1, &decalUbo_);
    decalTex_ = decalUbo_ = 0;
    if (d.rgba.empty()) return;
    int levels = 1;
    while ((std::max(d.width, d.height) >> levels) > 0) ++levels;
    glCreateTextures(GL_TEXTURE_2D, 1, &decalTex_);
    glTextureStorage2D(decalTex_, levels, GL_SRGB8_ALPHA8, d.width, d.height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(decalTex_, 0, 0, 0, d.width, d.height, GL_RGBA, GL_UNSIGNED_BYTE, d.rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateTextureMipmap(decalTex_);
    glTextureParameteri(decalTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(decalTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameterf(decalTex_, GL_TEXTURE_MAX_ANISOTROPY, 16.0f);
    glTextureParameteri(decalTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(decalTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<float> table(256 * 8, 0.0f);   // vec4 rect[256], vec4 info[256]
    for (size_t i = 0; i < d.entries.size() && i < 256; ++i) {
        const DecalEntry& e = d.entries[i];
        table[i * 4 + 0] = e.u0;
        table[i * 4 + 1] = e.v0;
        table[i * 4 + 2] = e.u1;
        table[i * 4 + 3] = e.v1;
        table[1024 + i * 4 + 0] = e.halfW;
        table[1024 + i * 4 + 1] = e.halfH;
        table[1024 + i * 4 + 2] = float(e.mode);
        table[1024 + i * 4 + 3] = float(e.style);
    }
    glCreateBuffers(1, &decalUbo_);
    glNamedBufferStorage(decalUbo_, table.size() * sizeof(float), table.data(), 0);
    if (d.entries.size() > 256) std::printf("denizalti: %zu cikartma, 256'si kullaniliyor\n", d.entries.size());
}

void Renderer::makeNoise() {
    const int N = 128;   // 4 texels per lattice cell, 32 cells
    std::vector<uint16_t> data(size_t(N) * N * N * 4);
    for (int z = 0; z < N; ++z)
        for (int y = 0; y < N; ++y)
            for (int x = 0; x < N; ++x) {
                float d[3];
                const float v = gnoise((x + 0.5f) / 4.0f, (y + 0.5f) / 4.0f, (z + 0.5f) / 4.0f, d);
                uint16_t* o = &data[((size_t(z) * N + y) * N + x) * 4];
                o[0] = toHalf(v);
                o[1] = toHalf(d[0]);
                o[2] = toHalf(d[1]);
                o[3] = toHalf(d[2]);
            }
    glCreateTextures(GL_TEXTURE_3D, 1, &noiseTex_);
    glTextureStorage3D(noiseTex_, 8, GL_RGBA16F, N, N, N);
    glTextureSubImage3D(noiseTex_, 0, 0, 0, 0, N, N, N, GL_RGBA, GL_HALF_FLOAT, data.data());
    glGenerateTextureMipmap(noiseTex_);
    glTextureParameteri(noiseTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(noiseTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(noiseTex_, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(noiseTex_, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTextureParameteri(noiseTex_, GL_TEXTURE_WRAP_R, GL_REPEAT);
}

void Renderer::upload(const Mesh& m) {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    glCreateBuffers(1, &vbo_);
    glNamedBufferStorage(vbo_, m.v.size() * sizeof(Vtx), m.v.data(), 0);
    // the triangles by cell (0.5 m), so a cell is one contiguous range
    std::vector<uint32_t> idx;
    {
        const size_t nt = m.idx.size() / 3;
        std::vector<std::pair<uint64_t, uint32_t>> key(nt);
        for (size_t t = 0; t < nt; ++t) {
            const float3 a = m.v[m.idx[t * 3]].p, b = m.v[m.idx[t * 3 + 1]].p, c = m.v[m.idx[t * 3 + 2]].p;
            const float3 g = (a + b + c) * (1.0f / 3.0f);
            const uint64_t cx = uint64_t(int64_t(std::floor(g.x / 0.5f)) + 1024), cy = uint64_t(int64_t(std::floor(g.y / 0.5f)) + 1024),
                           cz = uint64_t(int64_t(std::floor(g.z / 0.5f)) + 1024);
            key[t] = {(cx << 40) | (cy << 20) | cz, uint32_t(t)};
        }
        std::sort(key.begin(), key.end());
        idx.reserve(m.idx.size());
        clusters_.clear();
        for (size_t i = 0; i < nt; ++i) {
            if (i == 0 || key[i].first != key[i - 1].first) {
                Cluster c{};
                for (int k = 0; k < 3; ++k) {
                    c.lo[k] = 1e30f;
                    c.hi[k] = -1e30f;
                }
                c.first = uint32_t(idx.size());
                clusters_.push_back(c);
            }
            Cluster& c = clusters_.back();
            for (int v = 0; v < 3; ++v) {
                const uint32_t vi = m.idx[size_t(key[i].second) * 3 + v];
                idx.push_back(vi);
                const float3 p = m.v[vi].p;
                const float q[3] = {p.x, p.y, p.z};
                for (int k = 0; k < 3; ++k) {
                    c.lo[k] = std::min(c.lo[k], q[k] - 0.01f);   // the displacement's and the bulge's margin
                    c.hi[k] = std::max(c.hi[k], q[k] + 0.01f);
                }
            }
            c.count += 3;
        }
    }
    glCreateBuffers(1, &ibo_);
    glNamedBufferStorage(ibo_, idx.size() * sizeof(uint32_t), idx.data(), 0);
    indexCount_ = m.idx.size();
    baseTris = m.tris();
    glCreateVertexArrays(1, &vao_);
    glVertexArrayVertexBuffer(vao_, 0, vbo_, 0, sizeof(Vtx));
    glVertexArrayElementBuffer(vao_, ibo_);
    auto attr = [&](int loc, int n, GLenum type, size_t off, bool integer = false) {
        glEnableVertexArrayAttrib(vao_, loc);
        if (integer) glVertexArrayAttribIFormat(vao_, loc, n, type, GLuint(off));
        else glVertexArrayAttribFormat(vao_, loc, n, type, GL_FALSE, GLuint(off));
        glVertexArrayAttribBinding(vao_, loc, 0);
    };
    attr(0, 3, GL_FLOAT, offsetof(Vtx, p));
    attr(1, 3, GL_FLOAT, offsetof(Vtx, n));
    attr(2, 3, GL_FLOAT, offsetof(Vtx, dn));
    attr(3, 4, GL_FLOAT, offsetof(Vtx, t));
    attr(4, 2, GL_FLOAT, offsetof(Vtx, uv));
    attr(5, 2, GL_FLOAT, offsetof(Vtx, uv2));
    attr(6, 1, GL_UNSIGNED_INT, offsetof(Vtx, mat), true);
    attr(7, 1, GL_UNSIGNED_INT, offsetof(Vtx, feat), true);
    attr(8, 2, GL_FLOAT, offsetof(Vtx, fp));
    attr(9, 1, GL_FLOAT, offsetof(Vtx, edge));
    attr(10, 1, GL_FLOAT, offsetof(Vtx, fmask));
    // the finest triangle the base mesh can be cut to: its longest edge over the largest factor
    float longest = 0.0f;
    for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
        const float3 a = m.v[m.idx[k]].p, b = m.v[m.idx[k + 1]].p, c = m.v[m.idx[k + 2]].p;
        longest = std::max(longest, std::max(length(a - b), std::max(length(b - c), length(c - a))));
    }
    // the band limit's floor: a fixed 0.1 mm. (It was the longest edge over 64: in the room a slab's fan edge of ~0.95 m
    // made it 14.8 mm, and High's fine relief was filtered out of the geometry everywhere. Where a long patch's factor
    // stops at 64 the relief left is far below a pixel: the paint's orange peel.)
    (void)longest;
    finestTriangle = 1e-4f;
}

void Renderer::uploadLight(const LightMap& lm) {
    for (unsigned& t : lightTex_) {
        if (t) glDeleteTextures(1, &t);
        t = 0;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int k = 0; k < 5; ++k) {
        glCreateTextures(GL_TEXTURE_2D, 1, &lightTex_[k]);
        glTextureStorage2D(lightTex_[k], 1, GL_RGBA16F, lm.atlas.width, lm.atlas.height);
        glTextureSubImage2D(lightTex_[k], 0, 0, 0, lm.atlas.width, lm.atlas.height, GL_RGBA, GL_HALF_FLOAT, lm.plane[k].data());
        glTextureParameteri(lightTex_[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(lightTex_[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(lightTex_[k], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(lightTex_[k], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    for (unsigned& t : hazeTex_) {
        if (t) glDeleteTextures(1, &t);
        t = 0;
    }
    if (lm.volDim[0] > 0 && !lm.vol[0].empty()) {
        for (int k = 0; k < 2; ++k) {
            glCreateTextures(GL_TEXTURE_3D, 1, &hazeTex_[k]);
            glTextureStorage3D(hazeTex_[k], 1, GL_RGBA16F, lm.volDim[0], lm.volDim[1], lm.volDim[2]);
            glTextureSubImage3D(hazeTex_[k], 0, 0, 0, 0, lm.volDim[0], lm.volDim[1], lm.volDim[2], GL_RGBA, GL_HALF_FLOAT, lm.vol[k].data());
            glTextureParameteri(hazeTex_[k], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(hazeTex_[k], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(hazeTex_[k], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(hazeTex_[k], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(hazeTex_[k], GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        }
        for (int a = 0; a < 3; ++a) {
            volLo_[a] = lm.volLo[a];
            volSize_[a] = lm.volDim[a] * lm.volCell;
        }
    }
    if (probeTex_) glDeleteTextures(1, &probeTex_);
    probeTex_ = 0;
    const int np = lm.probeDims[0] * lm.probeDims[1] * lm.probeDims[2];
    if (np > 0 && !lm.probes.empty()) {
        glCreateTextures(GL_TEXTURE_CUBE_MAP_ARRAY, 1, &probeTex_);
        glTextureStorage3D(probeTex_, lm.probeMips, GL_RGBA16F, lm.probeRes, lm.probeRes, 6 * np);
        size_t off = 0;
        for (int p = 0; p < np; ++p)
            for (int m = 0; m < lm.probeMips; ++m) {
                const int r = std::max(1, lm.probeRes >> m);
                glTextureSubImage3D(probeTex_, m, 0, 0, 6 * p, r, r, 6, GL_RGBA, GL_HALF_FLOAT, &lm.probes[off * 4]);
                off += size_t(6) * r * r;
            }
        glTextureParameteri(probeTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(probeTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(probeTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(probeTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        for (int a = 0; a < 3; ++a) {
            probeLo_[a] = lm.probeLo[a];
            probeStep_[a] = lm.probeStep[a];
            probeDims_[a] = lm.probeDims[a];
        }
        probeMips_ = lm.probeMips;
    }
}

void Renderer::setLamps(const ::DzLight* l, int n) {
    std::memset(lampPos_, 0, sizeof(lampPos_));
    std::memset(lampL_, 0, sizeof(lampL_));
    for (int k = 0; k < std::min(n, 4); ++k) {
        lampPos_[k][0] = l[k].p.x;
        lampPos_[k][1] = l[k].p.y;
        lampPos_[k][2] = l[k].p.z;
        lampPos_[k][3] = l[k].r;
        lampL_[k][0] = l[k].L.x;
        lampL_[k][1] = l[k].L.y;
        lampL_[k][2] = l[k].L.z;
        lampL_[k][3] = 1.0f;
    }
}

void Renderer::freeTargets() {
    unsigned tex[] = {msColor_, msDepth_, hdrTex_, ldrTex_, outTex_, bloomTex_};
    for (unsigned t : tex)
        if (t) glDeleteTextures(1, &t);
    unsigned fb[] = {msFbo_, ldrFbo_, outFbo_};
    for (unsigned f : fb)
        if (f) glDeleteFramebuffers(1, &f);
    msColor_ = msDepth_ = hdrTex_ = ldrTex_ = outTex_ = msFbo_ = ldrFbo_ = outFbo_ = bloomTex_ = 0;
    rw_ = rh_ = ssaa_ = outW_ = outH_ = 0;
}

void Renderer::targets(int w, int h, int ssaa, int ow, int oh) {
    if (w == rw_ && h == rh_ && ssaa == ssaa_ && ow == outW_ && oh == outH_) return;
    freeTargets();
    rw_ = w;
    rh_ = h;
    ssaa_ = ssaa;
    outW_ = ow;
    outH_ = oh;
    // the supersampling: hardware samples where the card allows them all, else 2 x 2 texels of a quarter each
    const int lim = std::min(maxColorSamples, maxDepthSamples);
    if (ssaa <= lim) {
        msaa = ssaa;
        sx = sy = 1;
    } else {
        msaa = std::max(1, ssaa / 4);
        sx = sy = 2;
    }
    glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &msColor_);
    glTextureStorage2DMultisample(msColor_, msaa, GL_RGBA16F, w * sx, h * sy, GL_TRUE);
    glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &msDepth_);
    glTextureStorage2DMultisample(msDepth_, msaa, GL_DEPTH_COMPONENT32F, w * sx, h * sy, GL_TRUE);
    glCreateFramebuffers(1, &msFbo_);
    glNamedFramebufferTexture(msFbo_, GL_COLOR_ATTACHMENT0, msColor_, 0);
    glNamedFramebufferTexture(msFbo_, GL_DEPTH_ATTACHMENT, msDepth_, 0);
    if (glCheckNamedFramebufferStatus(msFbo_, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::printf("denizalti: cok ornekli hedef eksik (%dx%d, %d ornek)\n", w * sx, h * sy, msaa);
    glCreateTextures(GL_TEXTURE_2D, 1, &hdrTex_);
    glTextureStorage2D(hdrTex_, 1, GL_RGBA16F, w, h);
    // the bloom's pyramid: half the render size, down to a few texels
    bloomLevels_ = 1;
    while (bloomLevels_ < 7 && ((w / 2) >> bloomLevels_) >= 8 && ((h / 2) >> bloomLevels_) >= 8) ++bloomLevels_;
    glCreateTextures(GL_TEXTURE_2D, 1, &bloomTex_);
    glTextureStorage2D(bloomTex_, bloomLevels_, GL_RGBA16F, std::max(1, w / 2), std::max(1, h / 2));
    glTextureParameteri(bloomTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glTextureParameteri(bloomTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(bloomTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(bloomTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateTextures(GL_TEXTURE_2D, 1, &ldrTex_);
    glTextureStorage2D(ldrTex_, 1, GL_RGBA8, w, h);
    glTextureParameteri(ldrTex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(ldrTex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glCreateFramebuffers(1, &ldrFbo_);
    glNamedFramebufferTexture(ldrFbo_, GL_COLOR_ATTACHMENT0, ldrTex_, 0);
    glCreateTextures(GL_TEXTURE_2D, 1, &outTex_);
    glTextureStorage2D(outTex_, 1, GL_RGBA8, ow, oh);
    glTextureParameteri(outTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(outTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(outTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(outTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateFramebuffers(1, &outFbo_);
    glNamedFramebufferTexture(outFbo_, GL_COLOR_ATTACHMENT0, outTex_, 0);
}

void Renderer::draw(const Camera& cam, const Settings& s, int ow, int oh, double time) {
    targets(s.width, s.height, s.ssaa, ow, oh);
    // timestamps of the frame before last are ready by now
    const int qi = qFrame_ % 3;
    {
        const int old = (qFrame_ + 1) % 3;
        if (qValid_[old]) {
            GLuint64 t[5];
            for (int k = 0; k < 5; ++k) glGetQueryObjectui64v(queries_[old][k], GL_QUERY_RESULT, &t[k]);
            times_.scene = (t[1] - t[0]) * 1e-6;
            times_.resolve = (t[2] - t[1]) * 1e-6;
            times_.post = (t[3] - t[2]) * 1e-6;
            times_.final = (t[4] - t[3]) * 1e-6;
            times_.total = (t[4] - t[0]) * 1e-6;
        }
    }
    glQueryCounter(queries_[qi][0], GL_TIMESTAMP);

    // ---- the camera: reverse depth (1 at the near plane, 0 at infinity) for precision from 2 cm to the far wall
    const float zNear = 0.01f;   // ('near' is a Windows macro)
    const bool tiled = s.fullW > 0;
    const int fw0 = tiled ? s.fullW : s.width, fh0 = tiled ? s.fullH : s.height;
    const float aspect = float(fw0) / float(fh0);
    const float f = 1.0f / std::tan(0.5f * cam.fovY);
    // a tile: the full picture's NDC mapped onto the tile (scale, then shift its centre to the origin); y from the top
    float sxT = 1.0f, syT = 1.0f, oxT = 0.0f, oyT = 0.0f;
    if (tiled) {
        sxT = float(fw0) / float(s.width);
        syT = float(fh0) / float(s.height);
        const float cx = (s.tileX + 0.5f * s.width) / fw0 * 2.0f - 1.0f;          // the tile's centre in full NDC
        const float cy = 1.0f - (s.tileY + 0.5f * s.height) / fh0 * 2.0f;
        oxT = -cx * sxT;
        oyT = -cy * syT;
    }
    // clip.x' = sx * clip.x + ox * clip.w (and y); clip.w = -z_view
    const float proj[16] = {sxT * f / aspect, 0, 0, 0, 0, syT * f, 0, 0, -oxT, -oyT, 0, -1, 0, 0, zNear, 0};
    const float3 fw = cam.forward();
    float3 rt = cross(fw, make_float3(0, 1, 0));
    rt = length(rt) > 1e-6f ? normalize(rt) : make_float3(1, 0, 0);
    const float3 up = cross(rt, fw);
    const float3 e = cam.pos;
    const float view[16] = {rt.x, up.x, -fw.x, 0, rt.y, up.y, -fw.y, 0, rt.z, up.z, -fw.z, 0,
                            -dot(rt, e), -dot(up, e), dot(fw, e), 1};
    FrameUBO u{};
    mul(proj, view, u.viewProj);
    u.camPos[0] = e.x;
    u.camPos[1] = e.y;
    u.camPos[2] = e.z;
    u.camPos[3] = std::tan(0.5f * cam.fovY);
    const float rootN = std::sqrt(float(msaa));   // samples per texel along an axis
    const float samplesPerPixel = rootN * float(sy);
    // the target triangle edge in samples: low ~24 px, medium ~6 px, high ~1.5 samples
    const float px[3] = {24.0f, 6.0f, 0.0f};
    u.target[0] = s.tessOff ? 1e9f : s.tess == 2 ? 1.5f : px[s.tess] * samplesPerPixel;
    u.target[1] = float(std::min(64, maxTessLevel));
    u.target[2] = finestTriangle;
    u.target[3] = s.displacement ? 1.0f : 0.0f;
    // the tessellation's sample size is the full picture's (a tile is cut as finely as the whole)
    u.viewport[0] = float(s.width) * samplesPerPixel;
    u.viewport[1] = float(tiled ? fh0 : s.height) * samplesPerPixel;
    u.viewport[2] = 1.0f / samplesPerPixel;
    u.viewport[3] = float(s.tess);
    u.misc[0] = float(time);
    u.misc[1] = s.exposure;
    u.misc[2] = float(s.debug);
    u.misc[3] = s.normalDetail ? 1.0f : 0.0f;
    std::memcpy(u.lampPos, lampPos_, sizeof(lampPos_));
    std::memcpy(u.lampL, lampL_, sizeof(lampL_));
    u.camFw[0] = fw.x;
    u.camFw[1] = fw.y;
    u.camFw[2] = fw.z;
    // the haze's froxels: High 160 x 90 x 64, Medium half each way; Low none
    const int fxDims[3][3] = {{0, 0, 0}, {80, 45, 32}, {160, 90, 64}};
    const bool hazeOn = s.fx > 0 && hazeTex_[0] && s.hazeDensity > 0.0f;
    u.haze[0] = 0.05f;
    u.haze[1] = 12.0f;
    u.haze[2] = hazeOn ? float(fxDims[s.fx][2]) : 0.0f;
    u.haze[3] = hazeOn ? 1.0f : 0.0f;
    for (int a = 0; a < 3; ++a) {
        u.probeLo[a] = probeLo_[a];
        u.probeStep[a] = probeStep_[a];
        u.probeDims[a] = float(probeDims_[a]);
    }
    u.probeLo[3] = (probeTex_ && s.light > 0) ? float(probeMips_) : 0.0f;
    u.probeStep[3] = s.light == 2 ? 8.0f : 1.0f;
    u.levels[0] = float(s.tess);
    u.levels[1] = float(s.gfx);
    u.levels[2] = float(s.light);
    u.levels[3] = float(s.fx);
    u.renderSize[0] = float(s.width);
    u.renderSize[1] = float(s.height);
    glNamedBufferSubData(ubo_, 0, sizeof(u), &u);
    if (hazeOn) {
        const int* fd = fxDims[s.fx];
        if (fd[0] != froxDims_[0] || fd[1] != froxDims_[1] || fd[2] != froxDims_[2]) {
            if (froxelTex_) glDeleteTextures(1, &froxelTex_);
            glCreateTextures(GL_TEXTURE_3D, 1, &froxelTex_);
            glTextureStorage3D(froxelTex_, 1, GL_RGBA16F, fd[0], fd[1], fd[2]);
            glTextureParameteri(froxelTex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTextureParameteri(froxelTex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTextureParameteri(froxelTex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(froxelTex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTextureParameteri(froxelTex_, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            for (int a = 0; a < 3; ++a) froxDims_[a] = fd[a];
        }
        glUseProgram(progFroxel_);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);
        glBindTextureUnit(6, noiseTex_);
        glBindTextureUnit(8, hazeTex_[0]);
        glBindTextureUnit(9, hazeTex_[1]);
        glBindImageTexture(1, froxelTex_, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        auto loc = [&](const char* n) { return glGetUniformLocation(progFroxel_, n); };
        glProgramUniform3i(progFroxel_, loc("dims"), fd[0], fd[1], fd[2]);
        glProgramUniform3f(progFroxel_, loc("volLo"), volLo_[0], volLo_[1], volLo_[2]);
        glProgramUniform3f(progFroxel_, loc("volSize"), volSize_[0], volSize_[1], volSize_[2]);
        glProgramUniform3f(progFroxel_, loc("camFw"), fw.x, fw.y, fw.z);
        glProgramUniform3f(progFroxel_, loc("camRt"), rt.x, rt.y, rt.z);
        glProgramUniform3f(progFroxel_, loc("camUp"), up.x, up.y, up.z);
        glProgramUniform1f(progFroxel_, loc("tanY"), std::tan(0.5f * cam.fovY));
        glProgramUniform1f(progFroxel_, loc("aspect"), aspect);
        glProgramUniform4f(progFroxel_, loc("tile"), 1.0f / sxT, 1.0f / syT, -oxT / sxT, -oyT / syT);
        glProgramUniform1f(progFroxel_, loc("zNear"), u.haze[0]);
        glProgramUniform1f(progFroxel_, loc("zFar"), u.haze[1]);
        glProgramUniform1f(progFroxel_, loc("density"), s.hazeDensity);
        glDispatchCompute((fd[0] + 7) / 8, (fd[1] + 7) / 8, 1);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    // ---- the samples
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo_);
    glViewport(0, 0, s.width * sx, s.height * sy);
    glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
    const float clearC[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const float clearD = 0.0f;
    glClearNamedFramebufferfv(msFbo_, GL_COLOR, 0, clearC);
    glClearNamedFramebufferfv(msFbo_, GL_DEPTH, 0, &clearD);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_GREATER);
    glDepthMask(GL_TRUE);
    if (s.debug == 3) glDisable(GL_CULL_FACE);   // the crack view: back faces drawn (magenta)
    else glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_SAMPLE_SHADING);
    glMinSampleShading(1.0f);
    glUseProgram(progScene_);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo_);
    glBindVertexArray(vao_);
    glPatchParameteri(GL_PATCH_VERTICES, 3);
    for (int k = 0; k < 5; ++k) glBindTextureUnit(1 + k, lightTex_[k]);
    glBindTextureUnit(6, noiseTex_);
    glBindTextureUnit(7, decalTex_);
    glBindTextureUnit(10, hazeOn ? froxelTex_ : 0);
    glBindTextureUnit(11, probeTex_);
    if (decalUbo_) glBindBufferBase(GL_UNIFORM_BUFFER, 1, decalUbo_);
    {   // the clusters inside the view (each box against the clip planes, the projection's rows)
        const float* M = u.viewProj;
        auto row = [&](int r) { return std::array<float, 4>{M[r], M[4 + r], M[8 + r], M[12 + r]}; };
        const auto r0 = row(0), r1 = row(1), r3 = row(3);
        std::array<float, 4> planes[5];
        for (int k = 0; k < 4; ++k) {
            planes[0][k] = r3[k] + r0[k];
            planes[1][k] = r3[k] - r0[k];
            planes[2][k] = r3[k] + r1[k];
            planes[3][k] = r3[k] - r1[k];
            planes[4][k] = r3[k];   // in front of the eye (w > 0)
        }
        drawCounts_.clear();
        drawOffsets_.clear();
        drawnTris = 0;
        for (const Cluster& c : clusters_) {
            bool in = true;
            for (const auto& pl : planes) {
                // the box's corner furthest along the plane's normal
                const float px = pl[0] >= 0 ? c.hi[0] : c.lo[0], py = pl[1] >= 0 ? c.hi[1] : c.lo[1], pz = pl[2] >= 0 ? c.hi[2] : c.lo[2];
                if (pl[0] * px + pl[1] * py + pl[2] * pz + pl[3] < 0.0f) {
                    in = false;
                    break;
                }
            }
            if (!in) continue;
            // neighbouring clusters merge into one range
            if (!drawCounts_.empty() && reinterpret_cast<uintptr_t>(drawOffsets_.back()) / 4 + drawCounts_.back() == c.first) {
                drawCounts_.back() += int(c.count);
            } else {
                drawCounts_.push_back(int(c.count));
                drawOffsets_.push_back(reinterpret_cast<const void*>(uintptr_t(c.first) * 4));
            }
            drawnTris += c.count / 3;
        }
        drawnClusters = drawCounts_.size();
        if (!drawCounts_.empty())
            glMultiDrawElements(GL_PATCHES, drawCounts_.data(), GL_UNSIGNED_INT, drawOffsets_.data(), GLsizei(drawCounts_.size()));
    }
    glDisable(GL_SAMPLE_SHADING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
    glQueryCounter(queries_[qi][1], GL_TIMESTAMP);

    // ---- resolve
    glUseProgram(progResolve_);
    glBindTextureUnit(0, msColor_);
    glBindImageTexture(0, hdrTex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glProgramUniform2i(progResolve_, glGetUniformLocation(progResolve_, "scale"), sx, sy);
    glProgramUniform1i(progResolve_, glGetUniformLocation(progResolve_, "samples"), msaa);
    glProgramUniform2i(progResolve_, glGetUniformLocation(progResolve_, "size"), s.width, s.height);
    glDispatchCompute((s.width + 7) / 8, (s.height + 7) / 8, 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    // ---- the bloom: down the pyramid from the resolved picture, then up adding each level
    const float bloomStrength = s.fx == 2 ? 0.045f : (s.fx == 1 ? 0.03f : 0.018f);
    {
        glUseProgram(progBloom_);
        auto lc = [&](const char* n) { return glGetUniformLocation(progBloom_, n); };
        auto size = [&](int l) { return std::array<int, 2>{std::max(1, (s.width / 2) >> l), std::max(1, (s.height / 2) >> l)}; };
        for (int l = 0; l < bloomLevels_; ++l) {
            const auto sz = size(l);
            glBindTextureUnit(0, l == 0 ? hdrTex_ : bloomTex_);
            glBindImageTexture(2, bloomTex_, l, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
            glProgramUniform1i(progBloom_, lc("mode"), l == 0 ? 0 : 1);
            glProgramUniform1f(progBloom_, lc("srcLod"), l == 0 ? 0.0f : float(l - 1));
            glProgramUniform2i(progBloom_, lc("dstSize"), sz[0], sz[1]);
            glDispatchCompute((sz[0] + 7) / 8, (sz[1] + 7) / 8, 1);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }
        for (int l = bloomLevels_ - 1; l >= 1; --l) {
            const auto sz = size(l - 1);
            glBindTextureUnit(0, bloomTex_);
            glBindImageTexture(2, bloomTex_, l - 1, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
            glProgramUniform1i(progBloom_, lc("mode"), 2);
            glProgramUniform1f(progBloom_, lc("srcLod"), float(l));
            glProgramUniform2i(progBloom_, lc("dstSize"), sz[0], sz[1]);
            glDispatchCompute((sz[0] + 7) / 8, (sz[1] + 7) / 8, 1);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        }
    }
    glQueryCounter(queries_[qi][2], GL_TIMESTAMP);

    // ---- display
    glBindVertexArray(emptyVao_);
    glBindFramebuffer(GL_FRAMEBUFFER, ldrFbo_);
    glViewport(0, 0, s.width, s.height);
    glUseProgram(progPost_);
    glBindTextureUnit(0, hdrTex_);
    glBindTextureUnit(1, bloomTex_);
    glProgramUniform1f(progPost_, glGetUniformLocation(progPost_, "exposure"), std::exp2(s.exposure));
    glProgramUniform1f(progPost_, glGetUniformLocation(progPost_, "bloomMix"), bloomStrength);
    glProgramUniform1f(progPost_, glGetUniformLocation(progPost_, "bloomLevels"), float(bloomLevels_));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glQueryCounter(queries_[qi][3], GL_TIMESTAMP);

    glBindFramebuffer(GL_FRAMEBUFFER, outFbo_);
    glViewport(0, 0, ow, oh);
    glUseProgram(progFinal_);
    glBindTextureUnit(0, ldrTex_);
    glProgramUniform2i(progFinal_, glGetUniformLocation(progFinal_, "srcSize"), s.width, s.height);
    glProgramUniform2i(progFinal_, glGetUniformLocation(progFinal_, "dstSize"), ow, oh);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glQueryCounter(queries_[qi][4], GL_TIMESTAMP);
    qValid_[qi] = true;
    ++qFrame_;
    dzgl::restoreDefaults();
}

void Renderer::readOutput(std::vector<uint8_t>& rgba) {
    rgba.resize(size_t(outW_) * outH_ * 4);
    glGetTextureImage(outTex_, 0, GL_RGBA, GL_UNSIGNED_BYTE, GLsizei(rgba.size()), rgba.data());
}

void Renderer::readHdr(std::vector<float>& rgb, int& w, int& h) {
    w = rw_;
    h = rh_;
    std::vector<float> rgbaF(size_t(w) * h * 4);
    glGetTextureImage(hdrTex_, 0, GL_RGBA, GL_FLOAT, GLsizei(rgbaF.size() * sizeof(float)), rgbaF.data());
    rgb.resize(size_t(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float* s = &rgbaF[(size_t(h - 1 - y) * w + x) * 4];
            float* d = &rgb[(size_t(y) * w + x) * 3];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
}

void Renderer::release() {
    freeTargets();
    unsigned progs[] = {progScene_, progResolve_, progPost_, progFinal_};
    for (unsigned p : progs)
        if (p) glDeleteProgram(p);
    progScene_ = progResolve_ = progPost_ = progFinal_ = 0;
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ibo_) glDeleteBuffers(1, &ibo_);
    if (ubo_) glDeleteBuffers(1, &ubo_);
    vao_ = emptyVao_ = vbo_ = ibo_ = ubo_ = 0;
    for (unsigned& t : lightTex_) {
        if (t) glDeleteTextures(1, &t);
        t = 0;
    }
    if (noiseTex_) glDeleteTextures(1, &noiseTex_);
    noiseTex_ = 0;
    if (decalTex_) glDeleteTextures(1, &decalTex_);
    if (decalUbo_) glDeleteBuffers(1, &decalUbo_);
    decalTex_ = decalUbo_ = 0;
    for (unsigned& t : hazeTex_) {
        if (t) glDeleteTextures(1, &t);
        t = 0;
    }
    if (froxelTex_) glDeleteTextures(1, &froxelTex_);
    froxelTex_ = 0;
    froxDims_[0] = froxDims_[1] = froxDims_[2] = 0;
    if (probeTex_) glDeleteTextures(1, &probeTex_);
    probeTex_ = 0;
    if (progFroxel_) glDeleteProgram(progFroxel_);
    progFroxel_ = 0;
    if (progBloom_) glDeleteProgram(progBloom_);
    progBloom_ = 0;
    for (auto& f : queries_)
        if (f[0]) glDeleteQueries(5, f);
    std::memset(queries_, 0, sizeof(queries_));
}

}   // namespace dz
