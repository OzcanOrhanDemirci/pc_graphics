#include "ui/ui.h"
#include "ui/gl_api.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>

namespace ui {
namespace gl {
#define UI_GL_DEFINE(ret, name, args) ret(APIENTRY* name) args = nullptr;
UI_GL_FUNCS(UI_GL_DEFINE)
#undef UI_GL_DEFINE
bool load() {
    bool ok = true;
#define UI_GL_LOAD(ret, name, args)                                                     \
    name = reinterpret_cast<ret(APIENTRY*) args>(glfwGetProcAddress("gl" #name));       \
    if (!name) { std::fprintf(stderr, "ui: no gl" #name "\n"); ok = false; }
    UI_GL_FUNCS(UI_GL_LOAD)
#undef UI_GL_LOAD
    return ok;
}
}   // namespace gl

namespace {

// ---------------------------------------------------------------------------------------------------
// one program for everything: a signed-distance rounded box gives the shapes, the image corners and the shadows
// their soft edges; text samples a coverage atlas
const char* kVS = R"(#version 120
varying vec2 vLocal;
varying vec2 vUV;
varying vec4 vColor;
void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    vLocal = gl_MultiTexCoord0.xy;
    vUV = gl_MultiTexCoord0.zw;
    vColor = gl_Color;
}
)";
const char* kFS = R"(#version 120
uniform int uMode;          // 0 shape, 1 image, 2 text, 3 shadow
uniform vec2 uHalf;
uniform float uRadius;
uniform float uBorder;      // > 0: an outline this wide, inside the shape
uniform float uBlur;
uniform vec4 uAdj;          // image: brightness, saturation
uniform sampler2D uTex;
varying vec2 vLocal;
varying vec2 vUV;
varying vec4 vColor;
float sdBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + vec2(r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    if (uMode == 2) {
        float a = texture2D(uTex, vUV).r;
        gl_FragColor = vec4(vColor.rgb, vColor.a * a);
        return;
    }
    float d = sdBox(vLocal, uHalf, uRadius);
    if (uMode == 3) {
        float a = 1.0 - smoothstep(-uBlur, uBlur, d);
        gl_FragColor = vec4(vColor.rgb, vColor.a * a * a);
        return;
    }
    float cover = clamp(0.5 - d, 0.0, 1.0);
    if (uBorder > 0.0) cover *= 1.0 - clamp(0.5 - (d + uBorder), 0.0, 1.0);
    vec4 c = vColor;
    if (uMode == 1) {
        vec3 t = texture2D(uTex, vUV).rgb;
        float l = dot(t, vec3(0.2126, 0.7152, 0.0722));
        t = mix(vec3(l), t, uAdj.y) * uAdj.x;
        c = vec4(t * vColor.rgb, vColor.a);
    }
    gl_FragColor = vec4(c.rgb, c.a * cover);
}
)";

struct Program {
    GLuint id = 0;
    GLint mode = -1, half = -1, radius = -1, border = -1, blur = -1, adj = -1, tex = -1;
} g_prog;

GLuint compile(GLenum type, const char* src, std::string& err) {
    const GLuint s = gl::CreateShader(type);
    gl::ShaderSource(s, 1, &src, nullptr);
    gl::CompileShader(s);
    GLint ok = 0;
    gl::GetShaderiv(s, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetShaderInfoLog(s, sizeof(log), nullptr, log);
        err = std::string("shader: ") + log;
        return 0;
    }
    return s;
}

// ---------------------------------------------------------------------------------------------------
// fonts: one atlas per (face, pixel size), baked when first asked for
const char* kFontFiles[int(Font::Count)] = {"IBMPlexSans-Regular.ttf",  "IBMPlexSans-Medium.ttf",  "IBMPlexSans-SemiBold.ttf",
                                            "IBMPlexSerif-Regular.ttf", "IBMPlexSerif-Medium.ttf", "IBMPlexMono-Regular.ttf",
                                            "IBMPlexMono-Medium.ttf"};
struct Face {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    bool ok = false;
};
Face g_faces[int(Font::Count)];

// the letters baked: ASCII, Latin-1, Latin Extended-A (Turkish), dashes and quotes, arrows, minus and infinity
struct Range { int first, count; };
const Range kRanges[] = {{32, 95}, {160, 96}, {256, 128}, {0x2010, 0x28}, {0x2190, 4}, {0x2022, 1}, {0x2026, 1}, {0x2212, 1}, {0x221E, 1}};

struct Baked {
    GLuint tex = 0;
    int aw = 0, ah = 0;
    float px = 0, scale = 0, ascent = 0, descent = 0, lineGap = 0;
    std::map<int, stbtt_packedchar> glyphs;
    const stbtt_fontinfo* info = nullptr;
};
std::map<std::pair<int, int>, std::unique_ptr<Baked>> g_baked;
std::string g_root;
int g_w = 0, g_h = 0;
bool g_inited = false;

const Baked* bake(Font f, float size) {
    const int px = std::max(6, int(std::lround(size)));
    auto key = std::make_pair(int(f), px);
    auto it = g_baked.find(key);
    if (it != g_baked.end()) return it->second.get();
    Face& face = g_faces[int(f)];
    if (!face.ok) return nullptr;
    auto b = std::make_unique<Baked>();
    b->px = float(px);
    b->info = &face.info;
    b->scale = stbtt_ScaleForPixelHeight(&face.info, float(px));
    int a, d, g;
    stbtt_GetFontVMetrics(&face.info, &a, &d, &g);
    b->ascent = a * b->scale;
    b->descent = d * b->scale;
    b->lineGap = g * b->scale;
    // atlas size from the glyph count and size; oversampled small text stays crisp
    const int over = px < 40 ? 2 : 1;
    int side = 256;
    while (side < 4096 && side * side < int(px * px * over * 420)) side *= 2;
    std::vector<unsigned char> pixels(size_t(side) * side, 0);
    stbtt_pack_context pc;
    stbtt_PackBegin(&pc, pixels.data(), side, side, 0, 1, nullptr);
    stbtt_PackSetOversampling(&pc, over, over);
    for (const Range& r : kRanges) {
        std::vector<stbtt_packedchar> pcs(r.count);
        stbtt_pack_range pr{};
        pr.font_size = float(px);
        pr.first_unicode_codepoint_in_range = r.first;
        pr.num_chars = r.count;
        pr.chardata_for_range = pcs.data();
        stbtt_PackFontRanges(&pc, face.data.data(), 0, &pr, 1);
        for (int i = 0; i < r.count; ++i)
            if (stbtt_FindGlyphIndex(&face.info, r.first + i)) b->glyphs[r.first + i] = pcs[i];
    }
    stbtt_PackEnd(&pc);
    b->aw = b->ah = side;
    glGenTextures(1, &b->tex);
    glBindTexture(GL_TEXTURE_2D, b->tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, side, side, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const Baked* out = b.get();
    g_baked[key] = std::move(b);
    return out;
}

// UTF-8 -> code points (invalid bytes become '?')
std::vector<int> decode(const std::string& s) {
    std::vector<int> cps;
    cps.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        int cp = '?', n = 1;
        if (c < 0x80) { cp = c; }
        else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = ((c & 31) << 6) | (s[i + 1] & 63); n = 2; }
        else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = ((c & 15) << 12) | ((s[i + 1] & 63) << 6) | (s[i + 2] & 63); n = 3; }
        else if ((c >> 3) == 30 && i + 3 < s.size()) { cp = ((c & 7) << 18) | ((s[i + 1] & 63) << 12) | ((s[i + 2] & 63) << 6) | (s[i + 3] & 63); n = 4; }
        cps.push_back(cp);
        i += n;
    }
    return cps;
}
void encode(int cp, std::string& out) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 63)); }
    else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 63)); out += char(0x80 | (cp & 63)); }
}

// ---------------------------------------------------------------------------------------------------
void use(int mode, float hw, float hh, float radius, float border = 0.0f, float blur = 0.0f) {
    gl::Uniform1i(g_prog.mode, mode);
    gl::Uniform2f(g_prog.half, hw, hh);
    gl::Uniform1f(g_prog.radius, std::max(0.0f, std::min(radius, std::min(hw, hh))));
    gl::Uniform1f(g_prog.border, border);
    gl::Uniform1f(g_prog.blur, std::max(blur, 0.5f));
}
void color(const Color& c) { glColor4f(c.r, c.g, c.b, c.a); }
// a quad over r grown by `pad`, carrying the offset from r's centre (for the distance field) and texture coordinates
void quad(const Rect& r, float pad, const Color& c00, const Color& c10, const Color& c11, const Color& c01, float u0 = 0, float v0 = 0, float u1 = 1,
          float v1 = 1) {
    const float x0 = r.x - pad, y0 = r.y - pad, x1 = r.x + r.w + pad, y1 = r.y + r.h + pad;
    const float cx = r.x + 0.5f * r.w, cy = r.y + 0.5f * r.h;
    const float du = (u1 - u0) / std::max(r.w, 1e-3f), dv = (v1 - v0) / std::max(r.h, 1e-3f);
    auto vtx = [&](float x, float y, const Color& c) {
        color(c);
        gl::MultiTexCoord4f(gl::TEXTURE0, x - cx, y - cy, u0 + (x - r.x) * du, v0 + (y - r.y) * dv);
        glVertex2f(x, y);
    };
    glBegin(GL_QUADS);
    vtx(x0, y0, c00);
    vtx(x1, y0, c10);
    vtx(x1, y1, c11);
    vtx(x0, y1, c01);
    glEnd();
}

}   // namespace

// ---------------------------------------------------------------------------------------------------
bool init(const std::string& root, std::string& err) {
    if (g_inited) return true;
    g_root = root;
    if (!gl::load()) { err = "OpenGL 2.0 functions missing"; return false; }
    const GLuint vs = compile(gl::VERTEX_SHADER, kVS, err);
    const GLuint fs = vs ? compile(gl::FRAGMENT_SHADER, kFS, err) : 0;
    if (!vs || !fs) return false;
    g_prog.id = gl::CreateProgram();
    gl::AttachShader(g_prog.id, vs);
    gl::AttachShader(g_prog.id, fs);
    gl::LinkProgram(g_prog.id);
    GLint ok = 0;
    gl::GetProgramiv(g_prog.id, gl::LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl::GetProgramInfoLog(g_prog.id, sizeof(log), nullptr, log);
        err = std::string("program: ") + log;
        return false;
    }
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    g_prog.mode = gl::GetUniformLocation(g_prog.id, "uMode");
    g_prog.half = gl::GetUniformLocation(g_prog.id, "uHalf");
    g_prog.radius = gl::GetUniformLocation(g_prog.id, "uRadius");
    g_prog.border = gl::GetUniformLocation(g_prog.id, "uBorder");
    g_prog.blur = gl::GetUniformLocation(g_prog.id, "uBlur");
    g_prog.adj = gl::GetUniformLocation(g_prog.id, "uAdj");
    g_prog.tex = gl::GetUniformLocation(g_prog.id, "uTex");
    for (int f = 0; f < int(Font::Count); ++f) {
        Face& face = g_faces[f];
        std::ifstream in(root + "/assets/fonts/" + kFontFiles[f], std::ios::binary);
        face.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        face.ok = !face.data.empty() && stbtt_InitFont(&face.info, face.data.data(), stbtt_GetFontOffsetForIndex(face.data.data(), 0));
        if (!face.ok) { err = std::string("font missing: assets/fonts/") + kFontFiles[f]; return false; }
    }
    g_inited = true;
    return true;
}

void shutdown() {
    for (auto& kv : g_baked) glDeleteTextures(1, &kv.second->tex);
    g_baked.clear();
    if (g_prog.id) gl::DeleteProgram(g_prog.id);
    g_prog = Program();
    g_inited = false;
}

void begin(int width, int height) {
    g_w = width;
    g_h = height;
    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl::UseProgram(g_prog.id);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Uniform1i(g_prog.tex, 0);
    gl::Uniform4f(g_prog.adj, 1.0f, 1.0f, 0.0f, 0.0f);
}

void clear(Color c) {
    glClearColor(c.r, c.g, c.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void end() {
    gl::UseProgram(0);
    glDisable(GL_SCISSOR_TEST);
}

// ---- shapes
void rect(const Rect& r, Color c, float radius) { rectGradient(r, c, c, radius); }
void rectGradient(const Rect& r, Color top, Color bottom, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    use(0, 0.5f * r.w, 0.5f * r.h, radius);
    quad(r, 1.0f, top, top, bottom, bottom);
}
void rectHGradient(const Rect& r, Color left, Color right, float radius) {
    if (r.w <= 0 || r.h <= 0) return;
    use(0, 0.5f * r.w, 0.5f * r.h, radius);
    quad(r, 1.0f, left, right, right, left);
}
void stroke(const Rect& r, Color c, float radius, float width) {
    if (r.w <= 0 || r.h <= 0) return;
    use(0, 0.5f * r.w, 0.5f * r.h, radius, width);
    quad(r, 1.0f, c, c, c, c);
}
void panel(const Rect& r, Color fill, Color border, float radius, float borderWidth) {
    rect(r, fill, radius);
    if (borderWidth > 0.0f && border.a > 0.0f) stroke(r, border, radius, borderWidth);
}
void shadow(const Rect& r, float radius, float blur, Color c) {
    use(3, 0.5f * r.w, 0.5f * r.h, radius, 0.0f, blur);
    quad(r, blur * 1.5f, c, c, c, c);
}
void circle(float cx, float cy, float radius, Color c) { rect(Rect{cx - radius, cy - radius, 2 * radius, 2 * radius}, c, radius); }

// ---- images
static Image upload(const unsigned char* rgba, int w, int h) {
    Image im;
    im.w = w;
    im.h = h;
    glGenTextures(1, &im.tex);
    glBindTexture(GL_TEXTURE_2D, im.tex);
    glTexParameteri(GL_TEXTURE_2D, gl::GENERATE_MIPMAP, GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return im;
}

// box downscale of an RGBA image to width tw (keeps the aspect)
static std::vector<unsigned char> downscale(const unsigned char* src, int w, int h, int tw, int& th) {
    th = std::max(1, int(std::lround(double(h) * tw / w)));
    std::vector<unsigned char> out(size_t(tw) * th * 4);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x) {
            const int x0 = x * w / tw, x1 = std::max(x0 + 1, (x + 1) * w / tw), y0 = y * h / th, y1 = std::max(y0 + 1, (y + 1) * h / th);
            unsigned s[4] = {0, 0, 0, 0}, n = 0;
            for (int yy = y0; yy < y1; ++yy)
                for (int xx = x0; xx < x1; ++xx, ++n)
                    for (int c = 0; c < 4; ++c) s[c] += src[(size_t(yy) * w + xx) * 4 + c];
            for (int c = 0; c < 4; ++c) out[(size_t(y) * tw + x) * 4 + c] = (unsigned char)(s[c] / std::max(1u, n));
        }
    return out;
}

Image loadImage(const std::string& path, int maxWidth) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) { std::fprintf(stderr, "ui: cannot read %s\n", path.c_str()); return Image(); }
    Image im;
    if (maxWidth > 0 && w > maxWidth) {
        int th = 0;
        const std::vector<unsigned char> d = downscale(px, w, h, maxWidth, th);
        im = upload(d.data(), maxWidth, th);
    } else {
        im = upload(px, w, h);
    }
    stbi_image_free(px);
    return im;
}

Image loadBlurred(const std::string& path, int width, int passes) {
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return Image();
    int th = 0;
    std::vector<unsigned char> a = downscale(px, w, h, width, th);
    stbi_image_free(px);
    std::vector<unsigned char> b(a.size());
    const int r = std::max(1, width / 60);
    for (int p = 0; p < passes; ++p) {   // separable box blurs: three of them approach a gaussian
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < 4; ++c) {
                    int s = 0, k = 0;
                    for (int d = -r; d <= r; ++d) { const int xx = std::min(std::max(x + d, 0), width - 1); s += a[(size_t(y) * width + xx) * 4 + c]; ++k; }
                    b[(size_t(y) * width + x) * 4 + c] = (unsigned char)(s / k);
                }
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < 4; ++c) {
                    int s = 0, k = 0;
                    for (int d = -r; d <= r; ++d) { const int yy = std::min(std::max(y + d, 0), th - 1); s += b[(size_t(yy) * width + x) * 4 + c]; ++k; }
                    a[(size_t(y) * width + x) * 4 + c] = (unsigned char)(s / k);
                }
    }
    return upload(a.data(), width, th);
}

Image createImage(int w, int h) {
    Image im;
    im.w = w;
    im.h = h;
    glGenTextures(1, &im.tex);
    glBindTexture(GL_TEXTURE_2D, im.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    return im;
}

void updateImage(Image& im, const void* rgba, int w, int h) {
    if (!im.tex || im.w != w || im.h != h) {
        freeImage(im);
        im = createImage(w, h);
    }
    glBindTexture(GL_TEXTURE_2D, im.tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void freeImage(Image& im) {
    if (im.tex) glDeleteTextures(1, &im.tex);
    im = Image();
}

void image(const Image& im, const Rect& r, float radius, float alpha, float zoom, float fx, float fy, float brightness, float saturation) {
    if (!im.valid() || r.w <= 0 || r.h <= 0) return;
    // cover: the image's visible window in texture coordinates, zoomed about the focus
    const float ia = float(im.w) / im.h, ra = r.w / r.h;
    float uw = 1.0f, vh = 1.0f;
    if (ia > ra) uw = ra / ia; else vh = ia / ra;
    uw /= zoom;
    vh /= zoom;
    const float u0 = std::min(std::max(fx - 0.5f * uw, 0.0f), 1.0f - uw), v0 = std::min(std::max(fy - 0.5f * vh, 0.0f), 1.0f - vh);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, im.tex);
    gl::Uniform4f(g_prog.adj, brightness, saturation, 0.0f, 0.0f);
    use(1, 0.5f * r.w, 0.5f * r.h, radius);
    const Color c{1, 1, 1, alpha};
    quad(r, 0.0f, c, c, c, c, u0, v0, u0 + uw, v0 + vh);
    gl::Uniform4f(g_prog.adj, 1.0f, 1.0f, 0.0f, 0.0f);
    glDisable(GL_TEXTURE_2D);
}

Rect imageFit(const Image& im, const Rect& r, float alpha) {
    if (!im.valid()) return r;
    const float s = std::min(r.w / im.w, r.h / im.h);
    const Rect d{r.x + 0.5f * (r.w - im.w * s), r.y + 0.5f * (r.h - im.h * s), im.w * s, im.h * s};
    image(im, d, 0.0f, alpha);
    return d;
}

void scissor(const Rect* r) {
    if (!r) { glDisable(GL_SCISSOR_TEST); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor(int(r->x), int(g_h - (r->y + r->h)), int(std::ceil(r->w)), int(std::ceil(r->h)));
}

// ---- text
std::string upperTR(const std::string& s) {
    std::string out;
    for (int cp : decode(s)) {
        if (cp == 'i') cp = 0x130;                     // i -> İ
        else if (cp == 0x131) cp = 'I';                // ı -> I
        else if (cp >= 'a' && cp <= 'z') cp -= 32;
        else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32;            // Latin-1: ç ö ü â ...
        else if (cp >= 0x100 && cp <= 0x17F && (cp & 1) && cp != 0x131) cp -= 1;   // Latin Extended-A pairs: ğ ş ...
        encode(cp, out);
    }
    return out;
}

float lineHeight(const TextStyle& s) {
    const Baked* b = bake(s.font, s.size);
    return b ? b->ascent - b->descent + b->lineGap : s.size;
}
float ascent(const TextStyle& s) {
    const Baked* b = bake(s.font, s.size);
    return b ? b->ascent : s.size;
}

static float layout(const std::vector<int>& cps, const TextStyle& s, const Baked* b, float x, float y, bool draw) {
    float pen = 0.0f;
    const float baseline = y + std::round(b->ascent);
    if (draw) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, b->tex);
        use(2, 1, 1, 0);
        color(s.color);
        glBegin(GL_QUADS);
    }
    for (size_t i = 0; i < cps.size(); ++i) {
        int cp = cps[i];
        auto it = b->glyphs.find(cp);
        if (it == b->glyphs.end()) { cp = '?'; it = b->glyphs.find(cp); if (it == b->glyphs.end()) continue; }
        if (draw) {
            float qx = std::round(x) + pen, qy = baseline;
            stbtt_aligned_quad q;
            stbtt_GetPackedQuad(&it->second, b->aw, b->ah, 0, &qx, &qy, &q, 0);
            gl::MultiTexCoord4f(gl::TEXTURE0, 0, 0, q.s0, q.t0); glVertex2f(q.x0, q.y0);
            gl::MultiTexCoord4f(gl::TEXTURE0, 0, 0, q.s1, q.t0); glVertex2f(q.x1, q.y0);
            gl::MultiTexCoord4f(gl::TEXTURE0, 0, 0, q.s1, q.t1); glVertex2f(q.x1, q.y1);
            gl::MultiTexCoord4f(gl::TEXTURE0, 0, 0, q.s0, q.t1); glVertex2f(q.x0, q.y1);
        }
        pen += it->second.xadvance + s.tracking;
        if (i + 1 < cps.size()) pen += b->scale * stbtt_GetCodepointKernAdvance(b->info, cp, cps[i + 1]);
    }
    if (draw) {
        glEnd();
        glDisable(GL_TEXTURE_2D);
    }
    return cps.empty() ? 0.0f : pen - s.tracking;
}

float text(float x, float y, const std::string& utf8, const TextStyle& s) {
    const Baked* b = bake(s.font, s.size);
    if (!b) return 0.0f;
    return layout(decode(s.upper ? upperTR(utf8) : utf8), s, b, x, y, true);
}

float measure(const std::string& utf8, const TextStyle& s) {
    const Baked* b = bake(s.font, s.size);
    if (!b) return 0.0f;
    return layout(decode(s.upper ? upperTR(utf8) : utf8), s, b, 0, 0, false);
}

static float wrapped(bool draw, float x, float y, float width, const std::string& utf8, const TextStyle& s, float lineSpacing, int maxLines) {
    const float lh = lineHeight(s) * lineSpacing;
    std::string line, word;
    float yy = y;
    int lines = 0;
    auto flush = [&](bool last) {
        if (maxLines > 0 && lines == maxLines - 1 && !last) {
            if (draw) text(x, yy, line + "…", s);
            ++lines;
            return false;
        }
        if (draw) text(x, yy, line, s);
        yy += lh;
        ++lines;
        line.clear();
        return true;
    };
    const std::string src = utf8 + " ";
    for (size_t i = 0; i < src.size(); ++i) {
        const char c = src[i];
        if (c == ' ' || c == '\n') {
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && measure(cand, s) > width) {
                if (!flush(false)) return yy - y + lh;
                line = word;
            } else {
                line = cand;
            }
            word.clear();
            if (c == '\n') { if (!flush(false)) return yy - y + lh; }
        } else {
            word += c;
        }
    }
    if (!line.empty()) flush(true);
    return yy - y;
}

float textWrapped(float x, float y, float width, const std::string& utf8, const TextStyle& s, float lineSpacing, int maxLines) {
    return wrapped(true, x, y, width, utf8, s, lineSpacing, maxLines);
}
float measureWrapped(float width, const std::string& utf8, const TextStyle& s, float lineSpacing) {
    return wrapped(false, 0.0f, 0.0f, width, utf8, s, lineSpacing, 0);
}

}   // namespace ui
