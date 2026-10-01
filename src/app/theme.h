// The look shared by every screen: a dark, quiet frame around bright pictures. Neutral greys carry everything;
// each scene brings one accent colour (its light: warm gold, cold blue, safety orange). IBM Plex throughout:
// Serif for names, Sans for words, Mono for numbers and keys. Sizes are for a 2560x1440 screen, times scale().
#pragma once
#include "ui/ui.h"

#include <cstdio>
#include <string>

namespace theme {

// ---- colour
inline ui::Color ink() { return ui::hex(0x0A0B0D); }               // the background behind everything
inline ui::Color text() { return ui::hex(0xF2EFE9); }
inline ui::Color muted() { return ui::hex(0xF2EFE9, 0.62f); }
inline ui::Color faint() { return ui::hex(0xF2EFE9, 0.38f); }
inline ui::Color glass() { return ui::hex(0x101216, 0.80f); }      // panels over a picture
inline ui::Color glassBorder() { return ui::hex(0xFFFFFF, 0.08f); }
inline ui::Color hairline() { return ui::hex(0xFFFFFF, 0.10f); }
inline ui::Color capFill() { return ui::hex(0xFFFFFF, 0.07f); }   // key caps
inline ui::Color capBorder() { return ui::hex(0xFFFFFF, 0.16f); }
inline ui::Color shadowColor() { return ui::hex(0x000000, 0.55f); }

// ---- size: everything is drawn for 1440 lines and scaled to the window
struct Scale {
    float s = 1.0f;
    float operator()(float v) const { return v * s; }
};

// ---- type
inline ui::TextStyle caption(const Scale& S, ui::Color c = muted()) { return {ui::Font::SansMedium, S(14.5f), c, S(2.6f), true}; }
inline ui::TextStyle body(const Scale& S, ui::Color c = muted()) { return {ui::Font::Sans, S(20.0f), c, 0.0f, false}; }
inline ui::TextStyle bodyStrong(const Scale& S, ui::Color c = text()) { return {ui::Font::SansMedium, S(20.0f), c, 0.0f, false}; }
inline ui::TextStyle small(const Scale& S, ui::Color c = muted()) { return {ui::Font::Sans, S(16.5f), c, 0.0f, false}; }
inline ui::TextStyle mono(const Scale& S, float size, ui::Color c = text()) { return {ui::Font::MonoMedium, S(size), c, 0.0f, false}; }
inline ui::TextStyle serif(const Scale& S, float size, ui::Color c = text()) { return {ui::Font::SerifMedium, S(size), c, 0.0f, false}; }
inline ui::TextStyle display(const Scale& S, float size, ui::Color c = text()) { return {ui::Font::Serif, S(size), c, -S(size) * 0.01f, false}; }

// ---- numbers the Turkish way: a decimal comma
inline std::string num(double v, int decimals) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    std::string s(b);
    for (char& c : s)
        if (c == '.') c = ',';
    return s;
}

}   // namespace theme
