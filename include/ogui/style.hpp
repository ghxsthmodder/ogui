#pragma once
#include "ogui/api.hpp"
#include <string>
#include <map>

namespace ogui {

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
    std::map<std::string, Style> namedStyles;
};

} // namespace ogui
