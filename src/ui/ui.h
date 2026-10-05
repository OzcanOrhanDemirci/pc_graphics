// The interface kit shared by every screen of the application: anti-aliased rounded panels and soft shadows (one
// small GLSL program: a signed-distance box), images with rounded corners, and text in IBM Plex (stb_truetype atlases,
// UTF-8, Turkish letters, tracking). Immediate mode on the compatibility context the scenes already use; a few hundred
// shapes a frame cost nothing next to the scenes.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};
// 0xRRGGBB (sRGB, as the screen shows it), with alpha
inline Color hex(uint32_t rgb, float a = 1.0f) {
    return Color{((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a};
}
inline Color withAlpha(Color c, float a) { c.a *= a; return c; }
inline Color mix(Color a, Color b, float t) {
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

enum class Font { Sans, SansMedium, SansSemiBold, Serif, SerifMedium, Mono, MonoMedium, Count };   // IBM Plex, assets/fonts

struct TextStyle {
    Font font = Font::Sans;
    float size = 16.0f;    // pixels, already scaled
    Color color{1, 1, 1, 1};
    float tracking = 0.0f; // extra pixels between letters
    bool upper = false;    // Turkish upper case (i -> İ)
};

struct Image {
    unsigned int tex = 0;
    int w = 0, h = 0;
    bool valid() const { return tex != 0; }
};

// ---- lifetime (a current GL context is needed)
bool init(const std::string& root, std::string& err);
void shutdown();
// the frame: pixel coordinates, origin top left
void begin(int width, int height);
void end();
void clear(Color c);

// ---- shapes
void rect(const Rect& r, Color c, float radius = 0.0f);
void rectGradient(const Rect& r, Color top, Color bottom, float radius = 0.0f);
void rectHGradient(const Rect& r, Color left, Color right, float radius = 0.0f);
void stroke(const Rect& r, Color c, float radius, float width);
void panel(const Rect& r, Color fill, Color border, float radius, float borderWidth = 1.0f);
void shadow(const Rect& r, float radius, float blur, Color c);
void circle(float cx, float cy, float radius, Color c);
// strokes: anti-aliased polylines drawn as one strip with mitered joins (a translucent line does not darken where its
// segments meet); dash and gap in pixels (0: solid), phase shifts the pattern along it
void polyline(const float* xy, int n, float width, Color c, float dash = 0.0f, float gap = 0.0f, float phase = 0.0f);
void line(float x0, float y0, float x1, float y1, float width, Color c, float dash = 0.0f, float gap = 0.0f);
void arc(float cx, float cy, float r, float a0, float a1, float width, Color c, int segments = 0);   // radians, y down
// a convex polygon filled with one colour, its edge hard (for a soft one draw its outline over it with polyline)
void fill(const float* xy, int n, Color c);

// ---- images
Image loadImage(const std::string& path, int maxWidth = 0);
// a soft copy: downscaled to `width` and box-blurred (backdrops)
Image loadBlurred(const std::string& path, int width, int passes);
Image createImage(int w, int h);                       // RGBA8, filled later with updateImage
void updateImage(Image& im, const void* rgba, int w, int h);
void freeImage(Image& im);
// draws `im` filling `r` (cover: cropped, never stretched), zoomed about `focus` (0..1), corners rounded;
// brightness and saturation adjust it (1, 1: as is)
void image(const Image& im, const Rect& r, float radius = 0.0f, float alpha = 1.0f, float zoom = 1.0f, float focusX = 0.5f,
           float focusY = 0.5f, float brightness = 1.0f, float saturation = 1.0f);
// fitted inside r without cropping (letterbox), returns where it went
Rect imageFit(const Image& im, const Rect& r, float alpha = 1.0f);
void scissor(const Rect* r);   // clip to r (null: off)

// ---- text
float text(float x, float y, const std::string& utf8, const TextStyle& s);   // top-left at (x, y); returns width
float measure(const std::string& utf8, const TextStyle& s);
float lineHeight(const TextStyle& s);
float ascent(const TextStyle& s);
// wrapped into `width`; returns the height used (lineSpacing x line height per line)
float textWrapped(float x, float y, float width, const std::string& utf8, const TextStyle& s, float lineSpacing = 1.45f, int maxLines = 0);
float measureWrapped(float width, const std::string& utf8, const TextStyle& s, float lineSpacing = 1.45f);   // the height, drawing nothing
std::string upperTR(const std::string& utf8);
void setTurkishCase(bool on);   // capitals the Turkish way (i -> İ), or the English (i -> I)

}   // namespace ui
