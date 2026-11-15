// Vehicle cockpit widgets: <steeringwheel>, <pedal> and <speedometer>.
//
// These are ordinary ogui widgets -- geometry, style, visual-state overlays,
// tooltips and id lookups all come from the base class. What they add is the
// vocabulary of a driving HUD: an angle that can be grabbed and turned, a
// travel that is held down, a value that becomes a needle. The vehicle model
// stays outside: a script reads `angle`/`value` through the property registry
// and writes `value` back, so nothing here assumes a gearbox or a motor.
#include "ogui/widgets.hpp"
#include "ogui/rasterizer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ogui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float degToRad(float deg) { return deg * kPi / 180.0f; }

// Angle of a point around (cx, cy) in degrees, 0 = right, y growing down.
float angleOf(float dx, float dy) { return std::atan2(dy, dx) * 180.0f / kPi; }

// Shortest signed difference between two angles, in (-180, 180].
float angleDelta(float from, float to) {
    float d = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    return d;
}

// A circle stroke built from line segments: the rasterizer has no arc
// primitive, and a thick polyline is what a rim is anyway.
void drawRing(Rasterizer& r, float cx, float cy, float radius, float thickness,
              const Color& col) {
    if (radius <= 0.0f || thickness <= 0.0f || col.a == 0) return;
    constexpr int kSegments = 48;
    Offset2D prev{cx + radius, cy};
    for (int i = 1; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) /
                        static_cast<float>(kSegments);
        const Offset2D cur{cx + std::cos(a) * radius, cy + std::sin(a) * radius};
        r.drawLine(prev, cur, col, thickness);
        prev = cur;
    }
}

// Filled circle (a rounded rect with a radius of half its side).
void drawDisc(Rasterizer& r, float cx, float cy, float radius, const Color& col) {
    if (radius <= 0.0f || col.a == 0) return;
    r.drawRoundedRect(Rect{cx - radius, cy - radius, radius * 2.0f, radius * 2.0f},
                      col, radius);
}

}  // namespace

// ------------------------------------------------------------------
// SteeringWheel
// ------------------------------------------------------------------
SteeringWheel::SteeringWheel() = default;
SteeringWheel::~SteeringWheel() = default;

void SteeringWheel::setMaxAngle(float deg) {
    m_maxAngle = std::max(1.0f, deg);
    m_angle = std::clamp(m_angle, -m_maxAngle, m_maxAngle);
}

void SteeringWheel::setAngle(float deg) {
    m_angle = std::clamp(deg, -m_maxAngle, m_maxAngle);
}

float SteeringWheel::steering() const {
    return m_maxAngle > 0.0f ? m_angle / m_maxAngle : 0.0f;
}

Offset2D SteeringWheel::centre() const {
    const Rect b = bounds();
    return Offset2D{b.x + b.w * 0.5f, b.y + b.h * 0.5f};
}

void SteeringWheel::commit(float deg) {
    const float next = std::clamp(deg, -m_maxAngle, m_maxAngle);
    if (next == m_angle) return;
    m_angle = next;
    if (m_onChanged) m_onChanged(m_angle);
    announceChange();
    if (Kernel* k = kernel()) k->invalidate();
}

void SteeringWheel::onTick(float dt) {
    Widget::onTick(dt);
    if (m_dragging || !m_autoCenter || m_angle == 0.0f) return;
    // Ease the rim back to centre; the angular speed is proportional to the
    // angle left, so it settles instead of arriving at full speed.
    const float k = std::min(1.0f, dt * 9.0f);
    m_angle += (0.0f - m_angle) * k;
    if (std::fabs(m_angle) < 0.25f) m_angle = 0.0f;
}

void SteeringWheel::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    // The pointer arrives in screen space and bounds() are parent-relative;
    // absoluteOrigin() already folds in this widget's own x/y, so the hub is
    // the middle of the local box.
    const Offset2D o = absoluteOrigin();
    const float dx = (p.x - o.x) - bounds().w * 0.5f;
    const float dy = (p.y - o.y) - bounds().h * 0.5f;
    if (std::fabs(dx) < 0.5f && std::fabs(dy) < 0.5f) return;
    m_dragging = true;
    m_grabAngle = m_angle - angleOf(dx, dy);
    if (Kernel* k = kernel()) k->invalidate();
}

void SteeringWheel::onPointerMove(const Offset2D& p) {
    if (!m_dragging) return;
    const Offset2D o = absoluteOrigin();
    const float dx = (p.x - o.x) - bounds().w * 0.5f;
    const float dy = (p.y - o.y) - bounds().h * 0.5f;
    // Move by the shortest arc from where the wheel is now, so a drag across
    // the +/-180 seam does not spin the rim the long way round.
    const float wanted = angleOf(dx, dy) + m_grabAngle;
    commit(m_angle + angleDelta(m_angle, wanted));
}

void SteeringWheel::onPointerUp(MouseButton) {
    m_dragging = false;
    if (Kernel* k = kernel()) k->invalidate();
}

void SteeringWheel::onKeyDown(KeyCode key, uint32_t) {
    const float step = m_maxAngle * 0.08f;
    switch (key) {
        case Key_Left:  commit(m_angle - step); break;
        case Key_Right: commit(m_angle + step); break;
        case Key_Home:  commit(0.0f); break;
        default: break;
    }
}

void SteeringWheel::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const Offset2D c = centre();
    const float radius = std::min(b.w, b.h) * 0.5f - 4.0f;
    if (radius <= 3.0f) return;

    const float rim = std::max(5.0f, radius * 0.12f);
    drawRing(r, c.x, c.y, radius - rim * 0.5f, rim, m_rimColor);

    // Three spokes: right, down and left, all carried by the wheel angle.
    const float hubR = radius * 0.26f;
    const float spokeEnd = radius - rim * 1.1f;
    const float spokeWidth = std::max(4.0f, rim * 0.55f);
    for (float s : {0.0f, 90.0f, 180.0f}) {
        const float a = degToRad(m_angle + s);
        r.drawLine({c.x + std::cos(a) * hubR * 0.7f,
                    c.y + std::sin(a) * hubR * 0.7f},
                   {c.x + std::cos(a) * spokeEnd,
                    c.y + std::sin(a) * spokeEnd},
                   m_spokeColor, spokeWidth);
    }

    drawDisc(r, c.x, c.y, hubR, m_hubColor);
    drawDisc(r, c.x, c.y, hubR * 0.55f, shade(m_hubColor, 0.72f));

    // A marker on the rim makes the rotation readable when the wheel is
    // symmetric; it rides the top of the wheel.
    const float ma = degToRad(m_angle - 90.0f);
    const float mr = radius - rim * 0.5f;
    r.drawLine({c.x + std::cos(ma) * (mr - rim * 0.45f),
                c.y + std::sin(ma) * (mr - rim * 0.45f)},
               {c.x + std::cos(ma) * (mr + rim * 0.45f),
                c.y + std::sin(ma) * (mr + rim * 0.45f)},
               palette().accent, std::max(2.0f, rim * 0.22f));

    if (!m_label.empty()) {
        r.drawText(m_label.c_str(), c.x, c.y + radius * 0.55f, m_fontSize,
                   m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    }
    if (isFocused()) {
        r.drawRectBorder(b, palette().focus, 2.0f, 8.0f);
    }
}

// ------------------------------------------------------------------
// Pedal
// ------------------------------------------------------------------
Pedal::Pedal() = default;
Pedal::~Pedal() = default;

void Pedal::setValue(float v) { m_value = std::clamp(v, 0.0f, 1.0f); }

void Pedal::setSpeed(float unitsPerSecond) {
    m_speed = unitsPerSecond > 0.0f ? unitsPerSecond : 1.0f;
}

void Pedal::hold(bool down) {
    m_held = down;
    const float next = down ? 1.0f : 0.0f;
    if (next == m_value) return;
    m_value = next;
    if (m_onChanged) m_onChanged(m_value);
    announceChange();
    if (Kernel* k = kernel()) k->invalidate();
}

void Pedal::onTick(float dt) {
    Widget::onTick(dt);
    if (m_display == m_value) return;
    const float step = m_speed * dt;
    if (std::fabs(m_value - m_display) <= step) m_display = m_value;
    else m_display += (m_value > m_display ? step : -step);
}

void Pedal::onPointerDown(MouseButton btn, const Offset2D&) {
    if (btn == Mouse_Left) hold(true);
}

void Pedal::onPointerUp(MouseButton btn) {
    if (btn == Mouse_Left) hold(false);
}

void Pedal::onKeyDown(KeyCode key, uint32_t) {
    if (key == Key_Space || key == Key_Enter || key == Key_KP_Enter) hold(true);
}

void Pedal::onKeyUp(KeyCode key) {
    if (key == Key_Space || key == Key_Enter || key == Key_KP_Enter) hold(false);
}

void Pedal::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    if (b.w <= 2.0f || b.h <= 2.0f) return;

    const float radius = std::max(2.0f, std::min(b.w, b.h) * 0.10f);
    if (m_housingColor.a > 0) r.drawRoundedRect(b, m_housingColor, radius);

    // The plate slides down as the pedal travels, inside the housing.
    const float travel = b.h * 0.26f;
    const float plateW = b.w * 0.74f;
    const float plateH = b.h * 0.42f;
    const Rect plate{b.x + (b.w - plateW) * 0.5f,
                     b.y + b.h * 0.10f + travel * m_display,
                     plateW, plateH};
    Color plateCol = m_plateColor;
    if (m_display > 0.01f) plateCol = shade(plateCol, 1.0f + 0.45f * m_display);
    r.drawRoundedRect(plate, plateCol, radius);
    r.drawRectBorder(plate, shade(plateCol, 0.65f), 1.5f, radius);

    // Rubber ribs: they also measure the travel, because they move with it.
    const int ribs = 4;
    for (int i = 1; i <= ribs; ++i) {
        const float y = plate.y + plate.h * static_cast<float>(i) /
                                      static_cast<float>(ribs + 1);
        r.drawLine({plate.x + plate.w * 0.14f, y},
                   {plate.x + plate.w * 0.86f, y},
                   withAlpha(shade(m_plateColor, 0.55f), 190), 2.0f);
    }

    // Travel rail on the right edge.
    const float railX = b.x + b.w - std::max(5.0f, b.w * 0.055f);
    const Rect rail{railX, b.y + b.h * 0.10f, 3.0f, b.h * 0.56f};
    r.drawRect(rail, withAlpha(palette().border, 120));
    const float fillH = rail.h * m_display;
    if (fillH > 0.0f) {
        r.drawRect(Rect{rail.x, rail.y + rail.h - fillH, rail.w, fillH},
                   m_accentColor);
    }

    if (!m_label.empty()) {
        r.drawText(m_label.c_str(), b.x + b.w * 0.5f, b.y + b.h - m_fontSize * 0.9f,
                   m_fontSize, m_fontFam, m_textColor, Align::Center,
                   VertAlign::Middle);
    }
    if (isFocused()) {
        r.drawRectBorder(b, palette().focus, 2.0f, radius);
    }
}

// ------------------------------------------------------------------
// Speedometer
// ------------------------------------------------------------------
Speedometer::Speedometer() = default;
Speedometer::~Speedometer() = default;

void Speedometer::setRange(float lo, float hi) {
    m_min = lo;
    m_max = hi > lo ? hi : lo + 1.0f;
    m_value = std::clamp(m_value, m_min, m_max);
    // An unset red zone follows the top of the dial, so widening a range does
    // not silently invent one.
    m_redline = m_redlineSet ? std::clamp(m_redline, m_min, m_max) : m_max;
}

void Speedometer::setValue(float v) {
    m_value = std::clamp(v, m_min, m_max);
    if (Kernel* k = kernel()) k->invalidate();
}

void Speedometer::setRedline(float v) {
    m_redline = std::clamp(v, m_min, m_max);
    m_redlineSet = true;
}

void Speedometer::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const float radius = std::min(b.w, b.h) * 0.5f - 3.0f;
    if (radius <= 6.0f) return;
    const float cx = b.x + b.w * 0.5f;
    const float cy = b.y + b.h * 0.5f;

    drawDisc(r, cx, cy, radius, m_dialColor);
    drawRing(r, cx, cy, radius - 1.5f, 2.0f, withAlpha(m_tickColor, 120));

    // The sweep runs 135deg -> 405deg (clockwise through the top).
    const float start = 135.0f;
    const float sweep = 270.0f;
    const float span = m_max - m_min;
    auto angleFor = [&](float t) { return degToRad(start + sweep * t); };
    auto tFor = [&](float v) {
        return span > 0.0f ? std::clamp((v - m_min) / span, 0.0f, 1.0f) : 0.0f;
    };

    // Red zone along the rim.
    if (m_redline < m_max) {
        const float t0 = tFor(m_redline);
        const int segs = 24;
        Offset2D prev{cx + std::cos(angleFor(t0)) * (radius - 6.0f),
                      cy + std::sin(angleFor(t0)) * (radius - 6.0f)};
        for (int i = 1; i <= segs; ++i) {
            const float t = t0 + (1.0f - t0) * static_cast<float>(i) /
                                     static_cast<float>(segs);
            const Offset2D cur{cx + std::cos(angleFor(t)) * (radius - 6.0f),
                               cy + std::sin(angleFor(t)) * (radius - 6.0f)};
            r.drawLine(prev, cur, withAlpha(palette().danger, 190), 5.0f);
            prev = cur;
        }
    }

    // Ticks and their labels: 8 major divisions, 4 minor ticks between each.
    constexpr int kMajor = 8;
    constexpr int kMinorPerMajor = 4;
    const Font* labelFont = r.resolveFont(m_fontFam, std::max(9, m_fontSize / 2));
    for (int i = 0; i <= kMajor; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kMajor);
        const float a = angleFor(t);
        const float v = m_min + span * t;
        const bool hot = v >= m_redline - 0.001f && m_redline < m_max;
        const Color ink = hot ? palette().danger : m_tickColor;
        r.drawLine({cx + std::cos(a) * radius * 0.87f,
                    cy + std::sin(a) * radius * 0.87f},
                   {cx + std::cos(a) * radius * 0.96f,
                    cy + std::sin(a) * radius * 0.96f},
                   ink, 2.0f);
        if (labelFont) {
            char text[24];
            if (span >= 20.0f) std::snprintf(text, sizeof(text), "%.0f", v);
            else std::snprintf(text, sizeof(text), "%.1f", v);
            r.drawText(text, cx + std::cos(a) * radius * 0.70f,
                       cy + std::sin(a) * radius * 0.70f, std::max(9, m_fontSize / 2),
                       m_fontFam, withAlpha(m_textColor, 210), Align::Center,
                       VertAlign::Middle);
        }
    }
    for (int i = 0; i < kMajor * kMinorPerMajor; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(kMajor * kMinorPerMajor);
        const float ma = angleFor(t);
        const float mv = m_min + span * t;
        const Color ink = (m_redline < m_max && mv >= m_redline)
                              ? palette().danger
                              : withAlpha(m_tickColor, 150);
        r.drawLine({cx + std::cos(ma) * radius * 0.90f,
                    cy + std::sin(ma) * radius * 0.90f},
                   {cx + std::cos(ma) * radius * 0.96f,
                    cy + std::sin(ma) * radius * 0.96f},
                   ink, 1.0f);
    }

    // Needle, then the hub over it.
    const float a = angleFor(tFor(m_value));
    r.drawLine({cx - std::cos(a) * radius * 0.12f,
                cy - std::sin(a) * radius * 0.12f},
               {cx + std::cos(a) * radius * 0.82f,
                cy + std::sin(a) * radius * 0.82f},
               m_needleColor, 3.0f);
    drawDisc(r, cx, cy, radius * 0.075f, m_needleColor);

    char value[32];
    std::snprintf(value, sizeof(value), "%.0f", m_value);
    r.drawText(value, cx, cy + radius * 0.38f, m_fontSize, m_fontFam,
               m_textColor, Align::Center, VertAlign::Middle);
    if (!m_unit.empty()) {
        r.drawText(m_unit.c_str(), cx, cy + radius * 0.58f,
                   std::max(9, m_fontSize / 2), m_fontFam,
                   withAlpha(m_textColor, 190), Align::Center, VertAlign::Middle);
    }
    if (!m_label.empty()) {
        r.drawText(m_label.c_str(), cx, cy - radius * 0.42f,
                   std::max(9, m_fontSize / 2), m_fontFam,
                   withAlpha(m_textColor, 190), Align::Center, VertAlign::Middle);
    }
}

}  // namespace ogui
