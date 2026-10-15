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
namespace {
// The one list of token names. `token`, `setToken` and `hasToken` all resolve
// through it, so a token can never be settable but unknown (or the reverse).
struct TokenRef {
    const char* name;                       // lowercase, as @name is spelled
    Color Palette::*member;
};
const TokenRef kTokens[] = {
    {"window", &Palette::window},
    {"screen", &Palette::screen},
    {"surface", &Palette::surface},
    {"surfacehi", &Palette::surfaceHi},
    {"surfacelo", &Palette::surfaceLo},
    {"border", &Palette::border},
    {"borderstrong", &Palette::borderStrong},
    {"track", &Palette::track},
    {"guide", &Palette::guide},
    {"text", &Palette::text},
    {"textbright", &Palette::textBright},
    {"white", &Palette::white},
    {"dim", &Palette::dim},
    {"focus", &Palette::focus},
    {"accent", &Palette::accent},
    {"danger", &Palette::danger},
    {"info", &Palette::info},
    {"warn", &Palette::warn},
    {"error", &Palette::error},
    {"overlay", &Palette::overlay},
    {"shadow", &Palette::shadow},
};

std::string lowerToken(const std::string& raw) {
    std::string name;
    name.reserve(raw.size());
    for (char c : raw)
        name.push_back(c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c);
    return name;
}
} // namespace

const Color& Palette::token(const std::string& raw) const {
    const std::string name = lowerToken(raw);
    for (const auto& t : kTokens) {
        if (name == t.name) return this->*(t.member);
    }
    return window;
}

bool Palette::setToken(const std::string& raw, const Color& c) {
    const std::string name = lowerToken(raw);
    for (const auto& t : kTokens) {
        if (name == t.name) { this->*(t.member) = c; return true; }
    }
    return false;
}

bool Palette::hasToken(const std::string& raw) const {
    const std::string name = lowerToken(raw);
    for (const auto& t : kTokens) {
        if (name == t.name) return true;
    }
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
