#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// Checkbox
// ------------------------------------------------------------------
Checkbox::Checkbox() = default;
Checkbox::~Checkbox() = default;

void Checkbox::toggle() {
    m_checked = !m_checked;
    if (m_onChanged) m_onChanged(m_checked);
}

void Checkbox::onPointerDown(MouseButton, const Offset2D&) {
    toggle();
}

void Checkbox::onKeyDown(KeyCode key, uint32_t) {
    if (key == Key_Space || key == Key_Enter) toggle();
}

void Checkbox::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    const float side = std::min(b.h, 18.0f);
    const Rect box{b.x, b.y + (b.h - side) * 0.5f, side, side};
    if (m_checked) {
        // Solid accent fill; the tick sits on top in white.
        r.drawRoundedRect(box, m_checkColor, 3.0f);
    } else if (isHovered()) {
        r.drawRoundedRect(box, withAlpha(m_boxColor, 28), 3.0f);
    }
    r.drawRectBorder(box, m_checked ? m_checkColor
                       : (isHovered() ? shade(m_boxColor, 1.4f) : m_boxColor),
                     1.0f, 3.0f);

    if (m_checked) {
        // Two strokes make the tick; a filled square reads as "pressed".
        const float x0 = box.x + box.w * 0.20f;
        const float y0 = box.y + box.h * 0.52f;
        const float x1 = box.x + box.w * 0.42f;
        const float y1 = box.y + box.h * 0.76f;
        const float x2 = box.x + box.w * 0.80f;
        const float y2 = box.y + box.h * 0.26f;
        const float t = std::max(1.5f, box.w * 0.14f);
        r.drawLine({x0, y0}, {x1, y1}, palette().white, t);
        r.drawLine({x1, y1}, {x2, y2}, palette().white, t);
    }

    if (!m_text.empty()) {
        r.drawText(m_text.c_str(), box.x + side + 8.0f, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   Align::Left, VertAlign::Middle);
    }
}

// ------------------------------------------------------------------
// RadioButton
// ------------------------------------------------------------------
RadioButton::RadioButton() = default;
RadioButton::~RadioButton() = default;

void RadioButton::toggle() {
    // Radio semantics: selecting is sticky; the only way to uncheck a
    // selected radio is a sibling in the same group taking over.
    if (m_checked) return;
    if (!m_group.empty() && kernel()) {
        kernel()->eachWidget([this](Widget* w) {
            if (w == this) return;
            if (auto* rb = dynamic_cast<RadioButton*>(w))
                if (rb->group() == m_group) rb->setChecked(false);
        });
    }
    m_checked = true;
    if (m_onChanged) m_onChanged(m_checked);
}

void RadioButton::setChecked(bool c) {
    m_checked = c;
}

void RadioButton::onPointerDown(MouseButton, const Offset2D&) {
    // The Kernel already hit-tested this widget (bounds are parent-relative,
    // so re-checking the absolute point here would mostly miss).
    toggle();
}

void RadioButton::onKeyDown(KeyCode key, uint32_t) {
    if (key == Key_Space || key == Key_Enter) toggle();
}

void RadioButton::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    const float side = std::min(b.h, 18.0f);
    const Rect box{b.x, b.y + (b.h - side) * 0.5f, side, side};

    // Draw circle outline (brighter on hover)
    r.drawRectBorder(box, isHovered() ? shade(m_dotColor, 1.4f) : m_dotColor,
                     2.0f, side * 0.5f);

    // Draw circle fill if checked
    if (m_checked) {
        const float di = side - 6.0f;
        r.drawRoundedRect(Rect{box.x + 3, box.y + 3, di, di}, m_circleColor,
                          di * 0.5f);
    } else if (isHovered()) {
        const float di = side - 8.0f;
        r.drawRoundedRect(Rect{box.x + 4, box.y + 4, di, di},
                          withAlpha(m_circleColor, 46), di * 0.5f);
    }

    // Draw text
    if (!m_text.empty()) {
        r.drawText(m_text.c_str(), box.x + side + 8.0f, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   Align::Left, VertAlign::Middle);
    }

}

// ------------------------------------------------------------------
// ProgressBar
// ------------------------------------------------------------------
ProgressBar::ProgressBar() = default;
ProgressBar::~ProgressBar() = default;

void ProgressBar::setValue(float v) {
    m_value = std::clamp(v, 0.0f, 1.0f);
}

void ProgressBar::onTick(float dt) {
    Widget::onTick(dt);
    if (m_indeterminate) m_phase += dt * 1.4f;
}

void ProgressBar::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);

    const float inset = m_borderWidth > 0.0f ? m_borderWidth : 0.0f;
    if (m_indeterminate) {
        // Ping-pong a 28% block across the track while ticking.
        const float span = std::max(b.w - inset * 2.0f, 0.0f);
        const float blockW = span * 0.28f;
        const float t = m_phase - std::floor(m_phase * 0.5f) * 2.0f;  // 0..2
        const float tri = t < 1.0f ? t : 2.0f - t;                    // 0..1..0
        Rect fill{b.x + inset + (span - blockW) * tri, b.y + inset,
                  blockW, b.h - inset * 2.0f};
        if (fill.w > 0.0f && fill.h > 0.0f) r.drawRect(fill, m_barColor);
    } else if (m_value > 0.0f) {
        Rect fill{b.x + inset, b.y + inset,
                  (b.w - inset * 2.0f) * m_value, b.h - inset * 2.0f};
        if (fill.w > 0.0f && fill.h > 0.0f) r.drawRect(fill, m_barColor);
    }

    if (m_borderWidth > 0.0f)
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
}

// ------------------------------------------------------------------
// Slider
// ------------------------------------------------------------------
Slider::Slider() = default;
Slider::~Slider() = default;

void Slider::setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    m_min = lo;
    m_max = hi;
    m_value = std::clamp(m_value, m_min, m_max);   // no notify: programmatic
}

void Slider::setValue(float v) {
    m_value = std::clamp(v, m_min, m_max);
}

void Slider::commit(float v) {
    const float nv = std::clamp(v, m_min, m_max);
    if (nv == m_value) return;
    m_value = nv;
    if (m_onChanged) m_onChanged(m_value);
}

float Slider::tFromValue() const {
    const float span = m_max - m_min;
    if (span <= 0.0f) return 0.0f;
    return (m_value - m_min) / span;
}

void Slider::valueFromLocalX(float localX) {
    const Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float span = b.w - pad * 2.0f;
    if (span <= 0.0f) {
        commit(m_min);
        return;
    }
    const float t = std::clamp((localX - pad) / span, 0.0f, 1.0f);
    commit(m_min + t * (m_max - m_min));
}

void Slider::onPointerDown(MouseButton, const Offset2D& p) {
    m_dragging = true;
    valueFromLocalX(static_cast<float>(p.x) - absoluteOrigin().x);
}

void Slider::onPointerUp(MouseButton) {
    m_dragging = false;
}

void Slider::onPointerMove(const Offset2D& p) {
    if (!m_dragging) return;
    valueFromLocalX(static_cast<float>(p.x) - absoluteOrigin().x);
}

void Slider::onKeyDown(KeyCode key, uint32_t) {
    const float step = (m_max - m_min) / 20.0f;
    switch (key) {
        case Key_Left:
        case Key_Down:
            commit(m_value - step);
            break;
        case Key_Right:
        case Key_Up:
            commit(m_value + step);
            break;
        case Key_Home:
            commit(m_min);
            break;
        case Key_End:
            commit(m_max);
            break;
        default:
            break;
    }
}

void Slider::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    const float pad = m_thumbWidth * 0.5f;
    const float y = b.y + b.h * 0.5f;
    const float x0 = b.x + pad;
    const float x1 = b.x + b.w - pad;
    const bool hot = isHovered() || m_dragging;
    r.drawLine({x0, y}, {x1, y}, hot ? shade(m_trackColor, 1.3f) : m_trackColor,
               m_trackThickness);

    const float t = tFromValue();
    const float x = x0 + (x1 - x0) * t;
    if (t > 0.0f)
        r.drawLine({x0, y}, {x, y}, hot ? shade(m_barColor, 1.2f) : m_barColor,
                   m_trackThickness);

    // Rounded thumb that grows a touch while hovered/dragged.
    const float grow = hot ? 2.0f : 0.0f;
    const Rect thumb{x - pad - grow * 0.5f, b.y + 2.0f - grow * 0.5f,
                     m_thumbWidth + grow, b.h - 4.0f + grow};
    if (m_thumbColor.a > 0)
        r.drawRoundedRect(thumb, hot ? shade(m_thumbColor, 1.15f) : m_thumbColor,
                          3.0f);
}

// ------------------------------------------------------------------
// SpinBox
// ------------------------------------------------------------------
SpinBox::SpinBox() = default;
SpinBox::~SpinBox() = default;

void SpinBox::setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    m_min = lo;
    m_max = hi;
    m_value = std::clamp(m_value, m_min, m_max);   // programmatic: no notify
}

void SpinBox::setValue(float v) {
    m_value = std::clamp(v, m_min, m_max);
}

void SpinBox::stepBy(int dir) {
    float nv = m_value + static_cast<float>(dir) * m_step;
    if (nv > m_max) nv = m_wrap ? m_min : m_max;
    else if (nv < m_min) nv = m_wrap ? m_max : m_min;
    if (nv == m_value) return;
    m_value = nv;
    if (m_onChanged) m_onChanged(m_value);
}

std::string SpinBox::formatValue() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", m_decimals, static_cast<double>(m_value));
    return m_prefix + buf + m_suffix;
}

void SpinBox::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    // p is screen-absolute; lx/ly are widget-local (origin at 0,0).
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (ly < 0.0f || ly > b.h) return;
    const float btnW = b.h;                 // square buttons on each end
    if (lx >= 0.0f && lx <= btnW) stepBy(-1);              // left = minus
    else if (lx >= b.w - btnW && lx <= b.w) stepBy(+1);    // right = plus
}

void SpinBox::onKeyDown(KeyCode key, uint32_t) {
    switch (key) {
        case Key_Up:
        case Key_Right: stepBy(+1); break;
        case Key_Down:
        case Key_Left:  stepBy(-1); break;
        case Key_Home:  setValue(m_min); if (m_onChanged) m_onChanged(m_value); break;
        case Key_End:   setValue(m_max); if (m_onChanged) m_onChanged(m_value); break;
        default: break;
    }
}

void SpinBox::onDraw(Rasterizer& r) const {
    // One widget, not three: a single rounded frame; the buttons are flat
    // inset regions behind hairline separators.
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);
    if (isFocused()) {
        r.drawRectBorder(b, palette().focus, 2.0f, m_radius);
    } else if (m_borderColor.a > 0) {
        r.drawRectBorder(b, isHovered() ? shade(m_borderColor, 1.3f)
                                        : m_borderColor, 1.0f, m_radius);
    }

    const float btnW = b.h;
    const Rect minus{b.x, b.y, btnW, b.h};
    const Rect plus{b.x + b.w - btnW, b.y, btnW, b.h};

    // Per-button hover: the pointer region under the cursor gets a soft tint.
    bool hoverMinus = false, hoverPlus = false;
    if (isHovered() && kernel()) {
        const Offset2D m = kernel()->mousePos();
        hoverMinus = m.x >= minus.x && m.x <= minus.x + minus.w;
        hoverPlus = m.x >= plus.x && m.x <= plus.x + plus.w;
    }
    if (hoverMinus) {
        r.drawRoundedRect(Rect{b.x + 2, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }
    if (hoverPlus) {
        r.drawRoundedRect(Rect{plus.x, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }

    Color sep = m_borderColor;
    sep.a = 140;
    r.drawLine({minus.x + minus.w, b.y + 4}, {minus.x + minus.w, b.y + b.h - 4},
               sep, 1.0f);
    r.drawLine({plus.x, b.y + 4}, {plus.x, b.y + b.h - 4}, sep, 1.0f);

    // A single centred glyph on each button keeps it font-independent.
    // Plain '-' (not U+2212): the atlas only packs ASCII + Latin-1.
    r.drawText("-", minus.x + minus.w * 0.5f, minus.y + minus.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    r.drawText("+", plus.x + plus.w * 0.5f, plus.y + plus.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);

    r.drawText(formatValue().c_str(), b.x + b.w * 0.5f, b.y + b.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
}

// ------------------------------------------------------------------
// OptionSpin
// ------------------------------------------------------------------
OptionSpin::OptionSpin() = default;
OptionSpin::~OptionSpin() = default;

void OptionSpin::setOptions(std::vector<std::string> opts) {
    m_options = std::move(opts);
    m_index = m_options.empty() ? 0 : std::clamp(m_index, 0, static_cast<int>(m_options.size()) - 1);
}

void OptionSpin::setIndex(int i) {
    if (m_options.empty()) { m_index = 0; return; }
    m_index = std::clamp(i, 0, static_cast<int>(m_options.size()) - 1);
}

std::string OptionSpin::value() const {
    if (m_index < 0 || m_index >= static_cast<int>(m_options.size())) return std::string();
    return m_options[static_cast<size_t>(m_index)];
}

void OptionSpin::setValue(const std::string& v) {
    for (size_t i = 0; i < m_options.size(); ++i)
        if (m_options[i] == v) { m_index = static_cast<int>(i); return; }
}

void OptionSpin::stepBy(int dir) {
    if (m_options.empty()) return;
    const int n = static_cast<int>(m_options.size());
    int ni = m_index + dir;
    if (ni < 0) ni = m_wrap ? n - 1 : 0;
    else if (ni >= n) ni = m_wrap ? 0 : n - 1;
    if (ni == m_index) return;
    m_index = ni;
    if (m_onChanged) m_onChanged(m_index, m_options[static_cast<size_t>(m_index)]);
}

void OptionSpin::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    // p is screen-absolute; lx/ly are widget-local (origin at 0,0).
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (ly < 0.0f || ly > b.h) return;
    const float btnW = b.h;                 // square arrows on each end
    if (lx >= 0.0f && lx <= btnW) stepBy(-1);              // [ < ]
    else if (lx >= b.w - btnW && lx <= b.w) stepBy(+1);    // [ > ]
}

void OptionSpin::onKeyDown(KeyCode key, uint32_t) {
    switch (key) {
        case Key_Up:
        case Key_Right: stepBy(+1); break;
        case Key_Down:
        case Key_Left:  stepBy(-1); break;
        case Key_Home:  setIndex(0);
                        if (!m_options.empty() && m_onChanged)
                            m_onChanged(m_index, m_options[static_cast<size_t>(m_index)]);
                        break;
        case Key_End:   setIndex(static_cast<int>(m_options.size()) - 1);
                        if (!m_options.empty() && m_onChanged)
                            m_onChanged(m_index, m_options[static_cast<size_t>(m_index)]);
                        break;
        default: break;
    }
}

void OptionSpin::onDraw(Rasterizer& r) const {
    // Same unified frame as SpinBox, with arrows instead of +/-.
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);
    if (isFocused()) {
        r.drawRectBorder(b, palette().focus, 2.0f, m_radius);
    } else if (m_borderColor.a > 0) {
        r.drawRectBorder(b, isHovered() ? shade(m_borderColor, 1.3f)
                                        : m_borderColor, 1.0f, m_radius);
    }

    const float btnW = b.h;
    const Rect back{b.x, b.y, btnW, b.h};
    const Rect fwd{b.x + b.w - btnW, b.y, btnW, b.h};

    bool hoverBack = false, hoverFwd = false;
    if (isHovered() && kernel()) {
        const Offset2D m = kernel()->mousePos();
        hoverBack = m.x >= back.x && m.x <= back.x + back.w;
        hoverFwd = m.x >= fwd.x && m.x <= fwd.x + fwd.w;
    }
    if (hoverBack) {
        r.drawRoundedRect(Rect{b.x + 2, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }
    if (hoverFwd) {
        r.drawRoundedRect(Rect{fwd.x, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }

    Color sep = m_borderColor;
    sep.a = 140;
    r.drawLine({back.x + back.w, b.y + 4}, {back.x + back.w, b.y + b.h - 4}, sep, 1.0f);
    r.drawLine({fwd.x, b.y + 4}, {fwd.x, b.y + b.h - 4}, sep, 1.0f);

    r.drawText("<", back.x + back.w * 0.5f, back.y + back.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    r.drawText(">", fwd.x + fwd.w * 0.5f, fwd.y + fwd.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);

    r.beginFrame(Rect{back.x + back.w, b.y, fwd.x - (back.x + back.w), b.h});
    r.drawText(value().c_str(), b.x + b.w * 0.5f, b.y + b.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    r.endFrame();
}

// ------------------------------------------------------------------
// RangeSlider
// ------------------------------------------------------------------
RangeSlider::RangeSlider() = default;
RangeSlider::~RangeSlider() = default;

void RangeSlider::setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    m_min = lo;
    m_max = hi;
    m_low = std::clamp(m_low, m_min, m_max);
    m_high = std::clamp(m_high, m_min, m_max);
    if (m_low > m_high) std::swap(m_low, m_high);
}

void RangeSlider::setLow(float v) {
    const float nv = std::clamp(v, m_min, m_high);
    if (nv != m_low) { m_low = nv; if (m_onChanged) m_onChanged(m_low, m_high); }
}

void RangeSlider::setHigh(float v) {
    const float nv = std::clamp(v, m_low, m_max);
    if (nv != m_high) { m_high = nv; if (m_onChanged) m_onChanged(m_low, m_high); }
}

float RangeSlider::valueFromLocalX(float localX) const {
    const Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float span = b.w - pad * 2.0f;
    if (span <= 0.0f) return m_min;
    const float t = std::clamp((localX - pad) / span, 0.0f, 1.0f);
    return m_min + t * (m_max - m_min);
}

float RangeSlider::thumbX(float value) const {
    const Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float span = b.w - pad * 2.0f;
    const float t = (m_max - m_min) > 0.0f ? (value - m_min) / (m_max - m_min) : 0.0f;
    return b.x + pad + span * t;
}

void RangeSlider::commit(float low, float high) {
    low = std::clamp(low, m_min, m_max);
    high = std::clamp(high, m_min, m_max);
    if (low > high) std::swap(low, high);
    if (low == m_low && high == m_high) return;
    m_low = low;
    m_high = high;
    if (m_onChanged) m_onChanged(m_low, m_high);
}

void RangeSlider::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float v = valueFromLocalX(lx);
    // Grab whichever thumb is closer to the click.
    m_drag = (std::fabs(v - m_low) <= std::fabs(v - m_high)) ? 0 : 1;
    if (m_drag == 0) commit(v, m_high);
    else commit(m_low, v);
}

void RangeSlider::onPointerUp(MouseButton) {
    m_drag = -1;
}

void RangeSlider::onPointerMove(const Offset2D& p) {
    if (m_drag < 0) return;
    const Offset2D o = absoluteOrigin();
    const float v = valueFromLocalX(static_cast<float>(p.x) - o.x);
    if (m_drag == 0) commit(v, std::max(m_high, v));
    else commit(std::min(m_low, v), v);
}

void RangeSlider::onKeyDown(KeyCode key, uint32_t) {
    const float step = (m_max - m_min) / 20.0f;
    switch (key) {
        case Key_Left:  commit(m_low - step, m_high); break;
        case Key_Right: commit(m_low + step, m_high); break;
        case Key_Down:  commit(m_low, m_high - step); break;
        case Key_Up:    commit(m_low, m_high + step); break;
        case Key_Home:  commit(m_min, m_high); break;
        case Key_End:   commit(m_low, m_max); break;
        default: break;
    }
}

void RangeSlider::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float y = b.y + b.h * 0.5f;
    const float x0 = b.x + pad;
    const float x1 = b.x + b.w - pad;
    const bool hot = isHovered() || m_drag >= 0;
    r.drawLine({x0, y}, {x1, y}, hot ? shade(m_trackColor, 1.3f) : m_trackColor,
               m_trackThickness);

    const float lx = thumbX(m_low);
    const float hx = thumbX(m_high);
    r.drawLine({lx, y}, {hx, y}, m_barColor, m_trackThickness);

    // Rounded thumbs; the grabbed one grows and brightens.
    const float ty = b.y + 2.0f, th = b.h - 4.0f;
    for (int i = 0; i < 2; ++i) {
        const float tx = i == 0 ? lx : hx;
        const bool active = m_drag == i;
        const float grow = active ? 3.0f : (isHovered() ? 1.5f : 0.0f);
        r.drawRoundedRect(Rect{tx - pad - grow * 0.5f, ty - grow * 0.5f,
                               m_thumbWidth + grow, th + grow},
                          active ? shade(m_thumbColor, 1.25f) : m_thumbColor,
                          3.0f);
    }
}

// ------------------------------------------------------------------
// ToggleSwitch
// ------------------------------------------------------------------
ToggleSwitch::ToggleSwitch() {
    m_textColor = palette().text;
    m_bgColor = palette().surfaceLo;
    m_dotColor = palette().accent;
    m_activeBgColor = palette().track;
}
ToggleSwitch::~ToggleSwitch() = default;

void ToggleSwitch::setState(bool state) {
    if (m_checked == state) return;
    m_checked = state;
    // Slide the knob; snap while still being set up (no kernel yet).
    if (kernel()) m_knobTween.start(m_knob, state ? 1.0f : 0.0f, 0.32f,
                                    TweenEase::EaseInOutCubic);
    else { m_knob = state ? 1.0f : 0.0f; m_knobTween.stop(); }
    if (Kernel* kn = kernel()) kn->invalidate();
    if (m_onChanged) m_onChanged(m_checked);
}

void ToggleSwitch::onTick(float dt) {
    Widget::onTick(dt);
    if (!m_knobTween.running()) return;
    m_knobTween.update(dt);
    m_knob = m_knobTween.value();
}

void ToggleSwitch::toggle() { setState(!m_checked); }

Rect ToggleSwitch::trackRect() const {
    const Rect b = bounds();
    const float h = std::min(b.h, 24.0f);
    return Rect{b.x, b.y + (b.h - h) * 0.5f, h * 2.0f, h};
}

void ToggleSwitch::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    (void)p;
    toggle();
}

void ToggleSwitch::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const Rect tr = trackRect();
    const float radius = tr.h * 0.5f;

    // Track: dark when off, accent-tinted when on; fades with the knob.
    const Color fill = lerpColor(m_bgColor, m_dotColor, m_knob);
    r.drawRoundedRect(tr, fill, radius);
    r.drawRectBorder(tr, m_knob > 0.5f ? m_dotColor : palette().border, 1.0f,
                     radius);

    // Knob slides to the active side.
    const Font* f = r.resolveFont(m_fontFamily, m_fontSize);
    const float kr = tr.h - 6.0f;
    const float kx = tr.x + 3.0f + (tr.w - kr - 6.0f) * m_knob;
    r.drawRoundedRect(Rect{kx, tr.y + 3.0f, kr, kr}, withAlpha(palette().white, 242), kr * 0.5f);

    // Trailing text label.
    if (!m_text.empty() && f) {
        r.drawText(m_text.c_str(), tr.x + tr.w + m_gap, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   Align::Left, VertAlign::Middle);
    }
}

} // namespace ogui
