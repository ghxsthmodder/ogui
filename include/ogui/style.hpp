#pragma once
#include "ogui/api.hpp"
#include <string>
#include <map>

namespace ogui {

// ---- palette: named colour tokens -----------------------------------------
// Every widget pulls its neutral colours from here instead of hardcoding
// hex literals, so one swap of the active Theme restyles the whole UI.
struct Palette {
    Color window       = hexColor("#111111");  // screen fallback
    Color screen       = hexColor("#161616");  // full-bleed layout backgrounds
    Color surface      = hexColor("#262626");  // popups, modals, toasts
    Color surfaceHi    = hexColor("#2f3340");  // raised fills (buttons)
    Color surfaceLo    = hexColor("#0d0e12");  // sunken fills (icon boxes)
    Color border       = hexColor("#3a3b44");  // generic 1px hairline
    Color borderStrong = hexColor("#555555");  // popup outlines
    Color track        = hexColor("#333333");  // tab underline, tracks
    Color guide        = hexColor("#4a4f5a");  // tree guide lines
    Color text         = hexColor("#cccccc");
    Color textBright   = hexColor("#eeeeee");
    Color white        = hexColor("#ffffff");
    Color dim          = hexColor("#777777");
    Color focus        = hexColor("#88bbff");  // focus ring / open popup
    Color accent       = hexColor("#4488ff");
    Color danger       = hexColor("#ff8888");
    Color info         = hexColor("#2b78e4");  // toast kinds
    Color warn         = hexColor("#e4a12b");
    Color error        = hexColor("#e44b2b");
    Color overlay      = hexColor("#000000");  // modal dim (alpha at use)
    Color shadow       = hexColor("#000000");  // drop shadows

    // Named lookup for XML "@token" colour refs (case-insensitive);
    // returns the token colour or window when unknown.
    const Color& token(const std::string& name) const;
};

// ---- style: layout + style, merged from XML <style> and per-element attrs --
struct Style {
    // font
    std::string fontFamily = "sans";
    int fontSize = 14;
    // colors
    Color textColor = hexColor("#ccc");
    Color backgroundColor = hexColor("#111");
    Color borderColor = hexColor("#4488ff");
    float borderWidth = 0.0f;
    float radius = 0.0f;
    // drop shadow (blur 0 = none); drawn by the Kernel behind the widget.
    Color shadowColor = hexColor("#000000");
    float shadowBlur = 0.0f;
    float shadowDx = 0.0f;
    float shadowDy = 0.0f;
    // vertical gradient: backgroundColor (top) -> gradientColor (bottom).
    // With gradientHorizontal the ramp runs backgroundColor (left) ->
    // gradientColor (right) and corner radius is ignored.
    Color gradientColor = hexColor("#000000");
    bool hasGradient = false;
    bool gradientHorizontal = false;
    // geometry
    float padding = 0.0f;
    float margin = 0.0f;
    // z-order / flags
    uint32_t zOrder = 0;
    bool isVisible = true;

    Style* setFont(const std::string& family, int size);
    Style* setColor(const std::string& hex, Color* out = nullptr);
};

// ---- theme -----------------------------------------------------------------
struct Theme {
    Style defaultStyle;
    Palette palette;
    std::map<std::string, Style> namedStyles;
};

// The process-wide active theme: widgets resolve palette()/defaultStyle
// through it. XmlParser::parse(xml, theme) installs the parsed document's
// theme; hosts may call setActiveTheme() directly.
Theme& activeTheme();
inline const Palette& palette() { return activeTheme().palette; }
void setActiveTheme(const Theme& theme);

// Built-in presets: dark matches the default palette; light flips the
// neutrals while keeping accent/semantic hues. Switch at runtime with
// setActiveTheme() and re-parse layouts so @tokens re-resolve.
const Theme& darkTheme();
const Theme& lightTheme();

} // namespace ogui
