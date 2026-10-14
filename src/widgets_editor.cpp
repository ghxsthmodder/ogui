#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// ColorPopup / ColorPicker
// ------------------------------------------------------------------
ColorPopup::ColorPopup() = default;
ColorPopup::~ColorPopup() = default;

void ColorPopup::setHSV(float h, float s, float v) {
    m_h = h; m_s = s; m_v = v;
}

void ColorPopup::hsvToRgb(float h, float s, float v,
                          float& r, float& g, float& b) {
    const float c = v * s;
    const float hp = h * 6.0f;
    const float xx = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    const float m = v - c;
    float rr = 0.0f, gg = 0.0f, bb = 0.0f;
    if (hp < 1.0f) { rr = c; gg = xx; }
    else if (hp < 2.0f) { rr = xx; gg = c; }
    else if (hp < 3.0f) { gg = c; bb = xx; }
    else if (hp < 4.0f) { gg = xx; bb = c; }
    else if (hp < 5.0f) { rr = xx; bb = c; }
    else { rr = c; bb = xx; }
    r = rr + m; g = gg + m; b = bb + m;
}

Rect ColorPopup::svRect() const {
    const Rect b = bounds();
    return Rect{b.x + 8.0f, b.y + 8.0f, b.w - 16.0f, b.h - 60.0f};
}

Rect ColorPopup::hueRect() const {
    const Rect b = bounds();
    return Rect{b.x + 8.0f, b.y + b.h - 44.0f, b.w - 16.0f, 12.0f};
}

void ColorPopup::pick(const Offset2D& p, bool fromHue) {
    if (fromHue) {
        const Rect q = hueRect();
        float t = q.w > 0.0f ? (static_cast<float>(p.x) - q.x) / q.w : 0.0f;
        t = std::clamp(t, 0.0f, 0.9999f);
        m_h = t;
    } else {
        const Rect q = svRect();
        float sx = q.w > 0.0f ? (static_cast<float>(p.x) - q.x) / q.w : 0.0f;
        float sy = q.h > 0.0f ? (static_cast<float>(p.y) - q.y) / q.h : 0.0f;
        m_s = std::clamp(sx, 0.0f, 1.0f);
        m_v = std::clamp(1.0f - sy, 0.0f, 1.0f);
    }
    if (m_onChanged) m_onChanged(m_h, m_s, m_v);
    announceChange();
}

void ColorPopup::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Rect sv = svRect(), hue = hueRect();
    const float px = static_cast<float>(p.x), py = static_cast<float>(p.y);
    if (px >= hue.x && px <= hue.x + hue.w && py >= hue.y && py <= hue.y + hue.h) {
        m_drag = 2;
        pick(p, true);
    } else if (px >= sv.x && px <= sv.x + sv.w && py >= sv.y && py <= sv.y + sv.h) {
        m_drag = 1;
        pick(p, false);
    }
}

void ColorPopup::onPointerMove(const Offset2D& p) {
    if (m_drag == 2) pick(p, true);
    else if (m_drag == 1) pick(p, false);
}

void ColorPopup::onPointerUp(MouseButton) {
    m_drag = 0;
}

void ColorPopup::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_panel);
    r.drawRectBorder(b, m_border, 1.0f, 4.0f);

    // SV square as a coarse grid of flat cells (cheap, no shader).
    const Rect sv = svRect();
    constexpr int kCols = 12, kRows = 9;
    const float cw = sv.w / kCols, ch = sv.h / kRows;
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kCols; ++i) {
            float rr, gg, bb;
            hsvToRgb(m_h, static_cast<float>(i) / (kCols - 1),
                     1.0f - static_cast<float>(j) / (kRows - 1), rr, gg, bb);
            Color c;
            c.r = static_cast<uint8_t>(rr * 255.0f);
            c.g = static_cast<uint8_t>(gg * 255.0f);
            c.b = static_cast<uint8_t>(bb * 255.0f);
            r.drawRect({sv.x + static_cast<float>(i) * cw,
                        sv.y + static_cast<float>(j) * ch, cw + 1.0f, ch + 1.0f}, c);
        }
    }
    r.drawRectBorder(sv, m_border, 1.0f, 0.0f);
    {
        const float px = sv.x + m_s * sv.w;
        const float py = sv.y + (1.0f - m_v) * sv.h;
        r.drawRectBorder({px - 4.0f, py - 4.0f, 8.0f, 8.0f},
                         palette().white, 1.5f, 4.0f);
    }

    // Hue strip.
    const Rect hue = hueRect();
    constexpr int kSeg = 12;
    for (int i = 0; i < kSeg; ++i) {
        float rr, gg, bb;
        hsvToRgb(static_cast<float>(i) / kSeg, 1.0f, 1.0f, rr, gg, bb);
        Color c;
        c.r = static_cast<uint8_t>(rr * 255.0f);
        c.g = static_cast<uint8_t>(gg * 255.0f);
        c.b = static_cast<uint8_t>(bb * 255.0f);
        r.drawRect({hue.x + static_cast<float>(i) * hue.w / kSeg, hue.y,
                    hue.w / kSeg + 1.0f, hue.h}, c);
    }
    r.drawRectBorder(hue, m_border, 1.0f, 2.0f);
    {
        const float px = hue.x + m_h * hue.w;
        r.drawRect({px - 2.0f, hue.y - 2.0f, 4.0f, hue.h + 4.0f},
                   palette().white);
    }

    // Current colour + hex label.
    float rr, gg, bb;
    hsvToRgb(m_h, m_s, m_v, rr, gg, bb);
    Color cur;
    cur.r = static_cast<uint8_t>(rr * 255.0f);
    cur.g = static_cast<uint8_t>(gg * 255.0f);
    cur.b = static_cast<uint8_t>(bb * 255.0f);
    const Rect chip{b.x + 8.0f, b.y + b.h - 24.0f, 16.0f, 16.0f};
    r.drawRect(chip, cur);
    r.drawRectBorder(chip, m_border, 1.0f, 2.0f);
    char hex[8];
    std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", cur.r, cur.g, cur.b);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (f)
        r.drawTextShifted(hex, chip.x + 22.0f, b.y + b.h - 22.0f,
                          m_fontSize, *f, palette().text);
}

ColorPicker::ColorPicker() = default;
ColorPicker::~ColorPicker() = default;

void ColorPicker::setColorHex(const std::string& hex) {
    m_color = hexColor(hex);
    // rgb -> hsv so the popup opens on the right spot.
    const float r = m_color.r / 255.0f, g = m_color.g / 255.0f,
                b = m_color.b / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const float d = mx - mn;
    float h = 0.0f;
    if (d > 0.0f) {
        if (mx == r) h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    m_h = h;
    m_s = mx > 0.0f ? d / mx : 0.0f;
    m_v = mx;
}

std::string ColorPicker::colorHex() const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", m_color.r, m_color.g,
                  m_color.b);
    return buf;
}

void ColorPicker::applyHsv() {
    float rr, gg, bb;
    ColorPopup::hsvToRgb(m_h, m_s, m_v, rr, gg, bb);
    m_color.r = static_cast<uint8_t>(rr * 255.0f);
    m_color.g = static_cast<uint8_t>(gg * 255.0f);
    m_color.b = static_cast<uint8_t>(bb * 255.0f);
}

void ColorPicker::onPointerDown(MouseButton btn, const Offset2D&) {
    if (btn != Mouse_Left) return;
    if (!m_popup) m_popup = std::make_shared<ColorPopup>();
    m_popup->setHSV(m_h, m_s, m_v);
    m_popup->setVisible(true);
    m_popup->setOnChanged([this](float h, float s, float v) {
        m_h = h; m_s = s; m_v = v;
        applyHsv();
        if (m_onChanged) m_onChanged(colorHex());
        announceChange();
        if (Kernel* k = kernel()) k->invokeBinding(binding());
    });
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    float sw = 10000.0f, sh = 10000.0f;
    if (Kernel* k = kernel()) {
        if (auto scr = k->currentScreen()) { sw = scr->width; sh = scr->height; }
    }
    m_popup->setBounds(placePopup(Offset2D{o.x, o.y}, o.y + b.h + 2.0f,
                                  180.0f, 170.0f, sw, sh));
    if (Kernel* k = kernel()) k->openPopup(m_popup);
}

void ColorPicker::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const Rect chip{b.x + 4.0f, b.y + (b.h - 18.0f) * 0.5f, 18.0f, 18.0f};
    r.drawRect(chip, m_color);
    r.drawRectBorder(chip, palette().dim, 1.0f, 3.0f);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (f) {
        const std::string hex = colorHex();
        r.drawTextShifted(hex.c_str(), chip.x + 26.0f,
                          b.y + (b.h - static_cast<float>(m_fontSize)) * 0.5f,
                          m_fontSize, *f, palette().text);
    }
}

// ------------------------------------------------------------------
// PathPicker
// ------------------------------------------------------------------
PathPicker::PathPicker() = default;
PathPicker::~PathPicker() = default;

void PathPicker::listDir(const std::string& dir) {
    std::vector<MenuEntry> entries;
    if (!m_popup) m_popup = std::make_shared<MenuPopup>();

    if (dir != "/" && !dir.empty()) {
        MenuEntry up;
        up.label = "..";
        up.command = "d:..";
        entries.push_back(up);
    }

    std::error_code ec;
    std::vector<std::string> dirs, files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (e.is_directory(ec)) dirs.push_back(name);
        else if (e.is_regular_file(ec)) {
            if (!m_filter.empty() &&
                e.path().extension().string() != m_filter)
                continue;
            files.push_back(name);
        }
        if (dirs.size() + files.size() >= 40) break;   // keep the menu sane
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (const auto& d : dirs) {
        if (entries.size() >= 40) break;
        MenuEntry e;
        e.label = d + "/";
        e.command = "d:" + d;
        entries.push_back(e);
    }
    for (const auto& fl : files) {
        if (entries.size() >= 40) break;
        MenuEntry e;
        e.label = fl;
        e.command = "f:" + fl;
        entries.push_back(e);
    }

    m_popup->setEntries(entries);
    m_popup->fadeIn();
    m_popup->setVisible(true);
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    float sw = 10000.0f, sh = 10000.0f;
    if (Kernel* k = kernel()) {
        if (auto scr = k->currentScreen()) { sw = scr->width; sh = scr->height; }
    }
    const float h = std::min(m_popup->contentHeight(), 340.0f);
    m_popup->setBounds(placePopup(Offset2D{o.x, o.y}, o.y + b.h + 2.0f,
                                  std::max(b.w, 220.0f), h, sw, sh));
    m_popup->setOnSelect([this, dir](const std::string& cmd) {
        if (!cmd.empty() && cmd[0] == 'd' && cmd.size() > 1 && cmd[1] == ':') {
            std::string next;
            if (cmd.substr(2) == "..") {
                next = dir;
                while (next.size() > 1 && next.back() != '/') next.pop_back();
                if (next.size() > 1) next.pop_back();
            } else {
                next = dir;
                if (!next.empty() && next.back() != '/') next += '/';
                next += cmd.substr(2);
            }
            listDir(next);   // stay open, show the new folder
            return;
        }
        if (!cmd.empty() && cmd[0] == 'f' && cmd.size() > 1 && cmd[1] == ':') {
            std::string path = dir;
            if (!path.empty() && path.back() != '/') path += '/';
            path += cmd.substr(2);
            m_value = path;
            if (Kernel* k = kernel()) k->closePopup();
            if (m_popup) m_popup->setVisible(false);
            if (m_onChanged) m_onChanged(m_value);
            announceChange();
            if (Kernel* k = kernel()) k->invokeBinding(binding());
        }
    });
    if (Kernel* k = kernel()) k->openPopup(m_popup);
}

void PathPicker::openBrowser() {
    std::string dir = m_start;
    if (!m_value.empty()) {
        std::filesystem::path p(m_value);
        if (p.has_parent_path()) dir = p.parent_path().string();
    }
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) dir = ".";
    listDir(dir);
}

void PathPicker::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const float lx = static_cast<float>(p.x) - o.x + b.x;
    const Rect button{b.x + b.w - 30.0f, b.y + 2.0f, 28.0f, b.h - 4.0f};
    if (lx >= button.x && lx <= button.x + button.w) {
        openBrowser();
        return;
    }
    (void)p;
}

void PathPicker::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);
    r.drawRectBorder(b, m_borderColor, 1.0f, 4.0f);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    const float ty = b.y + (b.h - static_cast<float>(m_fontSize)) * 0.5f;
    if (!m_value.empty()) {
        r.drawTextShifted(m_value.c_str(), b.x + 8.0f, ty, m_fontSize, *f,
                          m_fgColor);
    } else if (!m_placeholder.empty()) {
        Color dim = m_fgColor;
        dim.a = 120;
        r.drawTextShifted(m_placeholder.c_str(), b.x + 8.0f, ty, m_fontSize,
                          *f, dim);
    }
    const Rect button{b.x + b.w - 30.0f, b.y + 2.0f, 28.0f, b.h - 4.0f};
    r.drawRect(button, palette().surfaceHi);
    r.drawText("...", button.x + button.w * 0.5f,
               button.y + button.h * 0.5f, m_fontSize, "sans",
               palette().textBright, Align::Center, VertAlign::Middle);
}

// ------------------------------------------------------------------
// RampEditor
// ------------------------------------------------------------------
RampEditor::RampEditor() = default;
RampEditor::~RampEditor() = default;

void RampEditor::setStops(const std::vector<Stop>& stops) {
    m_stops = stops;
    std::sort(m_stops.begin(), m_stops.end(),
              [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
}

bool RampEditor::setStopsFromSpec(const std::string& spec) {
    std::vector<Stop> out;
    size_t i = 0;
    while (i < spec.size()) {
        while (i < spec.size() && std::isspace(static_cast<unsigned char>(spec[i]))) ++i;
        size_t j = i;
        while (j < spec.size() && !std::isspace(static_cast<unsigned char>(spec[j]))) ++j;
        if (j > i) {
            const std::string tok = spec.substr(i, j - i);
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) return false;
            Stop s;
            s.pos = std::clamp(static_cast<float>(std::atof(tok.substr(0, colon).c_str())), 0.0f, 1.0f);
            s.color = hexColor(tok.substr(colon + 1));
            out.push_back(s);
        }
        i = j;
    }
    if (out.empty()) return false;
    setStops(out);
    return true;
}

void RampEditor::clear() {
    m_stops.clear();
}

Color RampEditor::sample(float pos) const {
    if (m_stops.empty()) return palette().white;
    std::vector<Stop> sorted = m_stops;
    std::sort(sorted.begin(), sorted.end(),
              [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
    pos = std::clamp(pos, 0.0f, 1.0f);
    if (pos <= sorted.front().pos) return sorted.front().color;
    if (pos >= sorted.back().pos) return sorted.back().color;
    for (size_t i = 1; i < sorted.size(); ++i) {
        if (pos <= sorted[i].pos) {
            const Stop& a = sorted[i - 1];
            const Stop& b = sorted[i];
            const float span = b.pos - a.pos;
            const float t = span > 0.0f ? (pos - a.pos) / span : 0.0f;
            Color c;
            c.r = static_cast<uint8_t>(a.color.r + (b.color.r - a.color.r) * t);
            c.g = static_cast<uint8_t>(a.color.g + (b.color.g - a.color.g) * t);
            c.b = static_cast<uint8_t>(a.color.b + (b.color.b - a.color.b) * t);
            c.a = 255;
            return c;
        }
    }
    return sorted.back().color;
}

void RampEditor::notify() {
    if (m_onChanged) m_onChanged();
    announceChange();
}

void RampEditor::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f) return;
    const float lx = static_cast<float>(p.x) - o.x;   // 0-based local
    // Grab the nearest existing stop within a small radius, else add one.
    int best = -1;
    float bestDist = 8.0f;
    for (int i = 0; i < static_cast<int>(m_stops.size()); ++i) {
        const float tx = m_stops[i].pos * b.w;
        const float d = std::fabs(tx - lx);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best >= 0) {
        m_drag = best;
    } else {
        Stop s;
        s.pos = std::clamp(lx / b.w, 0.0f, 1.0f);
        s.color = sample(s.pos);
        m_stops.push_back(s);
        m_drag = static_cast<int>(m_stops.size()) - 1;
        notify();
    }
}

void RampEditor::onPointerUp(MouseButton) {
    if (m_drag >= 0) {
        m_drag = -1;
        std::sort(m_stops.begin(), m_stops.end(),
                  [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
        notify();
    }
}

void RampEditor::onPointerMove(const Offset2D& p) {
    if (m_drag < 0 || m_drag >= static_cast<int>(m_stops.size())) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f) return;
    const float lx = static_cast<float>(p.x) - o.x;
    m_stops[m_drag].pos = std::clamp(lx / b.w, 0.0f, 1.0f);
}

void RampEditor::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    // Gradient bar: one thin vertical slice per step, colour sampled at centre.
    constexpr float kStep = 3.0f;
    for (float x = 0.0f; x < b.w; x += kStep) {
        const float frac = (x + kStep * 0.5f) / b.w;
        r.drawRect(Rect{b.x + x, b.y, kStep + 1.0f, b.h}, sample(frac));
    }
    r.drawRectBorder(b, m_borderColor, 1.0f, 0.0f);
    // Stop markers: a tick down the bar plus a filled diamond at the base.
    for (const Stop& s : m_stops) {
        const float cx = b.x + s.pos * b.w;
        r.drawLine({cx, b.y}, {cx, b.y + b.h - 6.0f}, withAlpha(palette().shadow, 0x80), 1.0f);
        const float cy = b.y + b.h - 5.0f;
        const float k = 5.0f;
        r.drawTriangle({cx - k, cy}, {cx, cy - k}, {cx + k, cy}, s.color);
        r.drawTriangle({cx - k, cy}, {cx, cy + k}, {cx + k, cy}, s.color);
        r.drawRectBorder(Rect{cx - k, cy - k, k * 2.0f, k * 2.0f}, m_thumbColor, 1.0f, 0.0f);
    }
}

// ------------------------------------------------------------------
// CurveEditor
// ------------------------------------------------------------------
CurveEditor::CurveEditor() = default;
CurveEditor::~CurveEditor() = default;

void CurveEditor::setPoints(const Point pts[4]) {
    for (int i = 0; i < 4; ++i) {
        m_pts[i].x = std::clamp(pts[i].x, 0.0f, 1.0f);
        m_pts[i].y = std::clamp(pts[i].y, 0.0f, 1.0f);
    }
}

bool CurveEditor::setPointsFromSpec(const std::string& spec) {
    Point tmp[4];
    int n = 0;
    size_t i = 0;
    while (i < spec.size() && n < 4) {
        while (i < spec.size() && (std::isspace(static_cast<unsigned char>(spec[i])) || spec[i] == ',')) ++i;
        size_t c = spec.find(',', i);
        if (c == std::string::npos) break;
        tmp[n].x = std::atof(spec.substr(i, c - i).c_str());
        size_t d = spec.find_first_of(" ,", c + 1);
        if (d == std::string::npos) d = spec.size();
        tmp[n].y = std::atof(spec.substr(c + 1, d - c - 1).c_str());
        ++n;
        i = d;
    }
    if (n != 4) return false;
    setPoints(tmp);
    return true;
}

float CurveEditor::evaluate(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    const float mt = 1.0f - t;
    const float y = mt * mt * mt * m_pts[0].y
                  + 3.0f * mt * mt * t * m_pts[1].y
                  + 3.0f * mt * t * t * m_pts[2].y
                  + t * t * t * m_pts[3].y;
    return std::clamp(y, 0.0f, 1.0f);
}

void CurveEditor::notify() {
    if (m_onChanged) m_onChanged();
    announceChange();
}

void CurveEditor::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;
    const float px = static_cast<float>(p.x) - o.x;   // 0-based local pixels
    const float py = static_cast<float>(p.y) - o.y;
    int best = -1;
    float bestDist = 12.0f;
    for (int i = 0; i < 4; ++i) {
        const float hx = m_pts[i].x * b.w;
        const float hy = (1.0f - m_pts[i].y) * b.h;
        const float d = std::sqrt((hx - px) * (hx - px) + (hy - py) * (hy - py));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    m_drag = best;
}

void CurveEditor::onPointerUp(MouseButton) {
    if (m_drag >= 0) { m_drag = -1; notify(); }
}

void CurveEditor::onPointerMove(const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;
    const float px = static_cast<float>(p.x) - o.x;
    const float py = static_cast<float>(p.y) - o.y;

    if (m_drag < 0) {
        // Track the hovered handle so it can grow under the cursor.
        m_hover = -1;
        float bestDist = 12.0f;
        for (int i = 0; i < 4; ++i) {
            const float hx = m_pts[i].x * b.w;
            const float hy = (1.0f - m_pts[i].y) * b.h;
            const float d = std::sqrt((hx - px) * (hx - px) + (hy - py) * (hy - py));
            if (d < bestDist) { bestDist = d; m_hover = i; }
        }
        return;
    }

    const float nx = std::clamp(px / b.w, 0.0f, 1.0f);
    const float ny = std::clamp(1.0f - py / b.h, 0.0f, 1.0f);
    // Endpoints stay pinned at x=0 / x=1 (the curve is an easing function);
    // control points keep x ordered so the curve never loops back on itself.
    if (m_drag == 0 || m_drag == 3) {
        m_pts[m_drag].y = ny;
    } else if (m_drag == 1) {
        m_pts[1].x = std::clamp(nx, 0.0f, m_pts[2].x);
        m_pts[1].y = ny;
    } else {
        m_pts[2].x = std::clamp(nx, m_pts[1].x, 1.0f);
        m_pts[2].y = ny;
    }
}

void CurveEditor::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);

    auto toPx = [&](const Point& pt) {
        return Offset2D{ b.x + pt.x * b.w, b.y + (1.0f - pt.y) * b.h };
    };

    // Unit grid at quarter steps.
    for (int i = 1; i < 4; ++i) {
        const float gx = b.x + b.w * (i / 4.0f);
        const float gy = b.y + b.h * (i / 4.0f);
        r.drawLine({gx, b.y}, {gx, b.y + b.h}, m_gridColor, 1.0f);
        r.drawLine({b.x, gy}, {b.x + b.w, gy}, m_gridColor, 1.0f);
    }
    // Linear (identity) reference: how far the curve deviates from 1:1.
    r.drawLine({b.x, b.y + b.h}, {b.x + b.w, b.y},
               withAlpha(m_gridColor, 220), 1.0f);

    // Control handles P0-P1 and P2-P3.
    r.drawLine(toPx(m_pts[0]), toPx(m_pts[1]), m_handleColor, 1.0f);
    r.drawLine(toPx(m_pts[2]), toPx(m_pts[3]), m_handleColor, 1.0f);

    // The cubic Bezier, sampled as short segments; the same sweep also lays
    // down a soft fill between the curve and the bottom edge.
    const Color fill = withAlpha(m_curveColor, 22);
    const Offset2D base0 = toPx(m_pts[0]);
    Point prev = m_pts[0];
    Offset2D prevPx = base0;
    for (int s = 1; s <= 48; ++s) {
        const float t = s / 48.0f;
        const float mt = 1.0f - t;
        Point cur;
        cur.x = mt * mt * mt * m_pts[0].x + 3.0f * mt * mt * t * m_pts[1].x
              + 3.0f * mt * t * t * m_pts[2].x + t * t * t * m_pts[3].x;
        cur.y = mt * mt * mt * m_pts[0].y + 3.0f * mt * mt * t * m_pts[1].y
              + 3.0f * mt * t * t * m_pts[2].y + t * t * t * m_pts[3].y;
        const Offset2D curPx = toPx(cur);
        const float bottom = b.y + b.h;
        r.drawTriangle(prevPx, curPx, {prevPx.x, bottom}, fill);
        r.drawTriangle(curPx, {curPx.x, bottom}, {prevPx.x, bottom}, fill);
        r.drawLine(prevPx, curPx, m_curveColor, 2.0f);
        prev = cur;
        prevPx = curPx;
    }

    // Draggable nodes: endpoints cyan, control points yellow; the hovered or
    // dragged one grows and gets a white outline.
    for (int i = 0; i < 4; ++i) {
        const Offset2D c = toPx(m_pts[i]);
        const bool end = (i == 0 || i == 3);
        const bool hot = (m_drag == i) || (m_drag < 0 && m_hover == i);
        const float k = hot ? 6.0f : 4.0f;
        r.drawRect(Rect{c.x - k, c.y - k, k * 2.0f, k * 2.0f},
                   end ? m_endColor : m_ctrlColor);
        if (hot)
            r.drawRectBorder(Rect{c.x - k, c.y - k, k * 2.0f, k * 2.0f},
                             palette().white, 1.0f, 0.0f);
    }
    r.drawRectBorder(b, palette().border, 1.0f, 0.0f);
}

} // namespace ogui
