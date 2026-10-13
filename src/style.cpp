#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include <cstring>

namespace ogui {

Theme& activeTheme() {
    static Theme theme;
    return theme;
}

void setActiveTheme(const Theme& theme) {
    if (&theme == &activeTheme()) return;  // parse(x, &activeTheme()) re-installs
    activeTheme() = theme;
}

// ---- palette token lookup --------------------------------------------------
const Color& Palette::token(const std::string& raw) const {
    std::string name;
    name.reserve(raw.size());
    for (char c : raw)
        name.push_back(c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
    if (name == "window")       return window;
    if (name == "screen")       return screen;
    if (name == "surface")      return surface;
    if (name == "surfacehi")    return surfaceHi;
    if (name == "surfacelo")    return surfaceLo;
    if (name == "border")       return border;
    if (name == "borderstrong") return borderStrong;
    if (name == "track")        return track;
    if (name == "guide")        return guide;
    if (name == "text")         return text;
    if (name == "textbright")   return textBright;
    if (name == "white")        return white;
    if (name == "dim")          return dim;
    if (name == "focus")        return focus;
    if (name == "accent")       return accent;
    if (name == "danger")       return danger;
    if (name == "info")         return info;
    if (name == "warn")         return warn;
    if (name == "error")        return error;
    if (name == "overlay")      return overlay;
    if (name == "shadow")       return shadow;
    return window;
}

bool Palette::setToken(const std::string& raw, const Color& c) {
    std::string name;
    name.reserve(raw.size());
    for (char ch : raw)
        name.push_back(ch >= 'A' && ch <= 'Z' ? char(ch - 'A' + 'a') : ch);
    if (name == "window")       { window = c; return true; }
    if (name == "screen")       { screen = c; return true; }
    if (name == "surface")      { surface = c; return true; }
    if (name == "surfacehi")    { surfaceHi = c; return true; }
    if (name == "surfacelo")    { surfaceLo = c; return true; }
    if (name == "border")       { border = c; return true; }
    if (name == "borderstrong") { borderStrong = c; return true; }
    if (name == "track")        { track = c; return true; }
    if (name == "guide")        { guide = c; return true; }
    if (name == "text")         { text = c; return true; }
    if (name == "textbright")   { textBright = c; return true; }
    if (name == "white")        { white = c; return true; }
    if (name == "dim")          { dim = c; return true; }
    if (name == "focus")        { focus = c; return true; }
    if (name == "accent")       { accent = c; return true; }
    if (name == "danger")       { danger = c; return true; }
    if (name == "info")         { info = c; return true; }
    if (name == "warn")         { warn = c; return true; }
    if (name == "error")        { error = c; return true; }
    if (name == "overlay")      { overlay = c; return true; }
    if (name == "shadow")       { shadow = c; return true; }
    return false;
}

// ---- presets ---------------------------------------------------------------
const Theme& darkTheme() {
    static Theme theme;  // palette/Style defaults already describe dark
    return theme;
}

const Theme& lightTheme() {
    static Theme theme = [] {
        Theme t;
        Palette& p = t.palette;
        p.window       = hexColor("#e8e8ec");
        p.screen       = hexColor("#f2f2f5");
        p.surface      = hexColor("#ffffff");
        p.surfaceHi    = hexColor("#e4e6ee");
        p.surfaceLo    = hexColor("#d8dade");
        p.border       = hexColor("#b8bac4");
        p.borderStrong = hexColor("#8a8c96");
        p.track        = hexColor("#c4c6ce");
        p.guide        = hexColor("#a8aab4");
        p.text         = hexColor("#333333");
        p.textBright   = hexColor("#111111");
        p.white        = hexColor("#ffffff");
        p.dim          = hexColor("#70727a");
        p.focus        = hexColor("#2266cc");
        p.accent       = hexColor("#2266dd");
        p.danger       = hexColor("#cc3333");
        p.info         = hexColor("#2b78e4");
        p.warn         = hexColor("#b07818");
        p.error        = hexColor("#c03a1c");
        p.overlay      = hexColor("#000000");
        p.shadow       = hexColor("#000000");
        t.defaultStyle.textColor       = p.text;
        t.defaultStyle.backgroundColor = p.window;
        t.defaultStyle.borderColor     = p.accent;
        return t;
    }();
    return theme;
}

// ---- Style helpers --------------------------------------------------------
Style* Style::setFont(const std::string& family, int size) {
    fontFamily = family;
    fontSize = size;
    return this;
}

Style* Style::setColor(const std::string& hex, Color* out) {
    Color c = hexColor(hex);
    if (out) *out = c;
    return this;
}

} // namespace ogui
