#include "ogui/widget.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/kernel.hpp"
#include "ogui/script.hpp"
#include <cstring>

namespace ogui {

// ---- Widget ---------------------------------------------------------------
Widget::Widget() = default;
Widget::~Widget() = default;

bool Widget::isHovered() const {
    return m_kernel != nullptr && m_kernel->hoveredWidget() == this;
}

bool Widget::isFocused() const {
    return m_kernel != nullptr && m_kernel->focusedWidget() == this;
}

void Widget::setParent(Widget* parent) {
    if (m_parent == parent) return;   // already in place
    if (m_parent) {
        // detach from old parent
        for (auto it = m_parent->m_children.begin(); it != m_parent->m_children.end(); ++it) {
            if (it->get() == this) { m_parent->m_children.erase(it); break; }
        }
    }
    m_parent = parent;
    if (parent) {
        // Avoid duplicate entries when re-attaching.
        for (const auto& c : parent->m_children) {
            if (c.get() == this) return;
        }
        parent->m_children.push_back(shared_from_this());
    }
}

void Widget::addChild(Ptr child) {
    if (!child) return;
    child->setParent(this);
    if (m_kernel) m_kernel->invalidate();
}

void Widget::announceChange() {
    if (m_kernel && !m_id.empty()) m_kernel->script().fireEvent(m_id, "change");
}

void Widget::announceSubmit() {
    if (m_kernel && !m_id.empty()) m_kernel->script().fireEvent(m_id, "submit");
}

void Widget::removeChild(Ptr child) {
    removeChild(child.get());
}

void Widget::removeChild(Widget* child) {
    if (!child) return;
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (it->get() == child) {
            if (child->m_parent == this) child->m_parent = nullptr;
            m_children.erase(it);
            if (m_kernel) m_kernel->invalidate();
            break;
        }
    }
}

std::vector<Widget::Ptr> Widget::takeChildren() {
    std::vector<Ptr> out;
    out.swap(m_children);
    for (auto& c : out)
        if (c && c->m_parent == this) c->m_parent = nullptr;
    if (m_kernel) m_kernel->invalidate();
    return out;
}

void Widget::setSizeLimits(float minW, float minH, float maxW, float maxH) {
    m_minW = minW; m_minH = minH; m_maxW = maxW; m_maxH = maxH;
    clampToBounds();
}

void Widget::clampToBounds() {
    if (m_minW <= 0.0f && m_minH <= 0.0f && m_maxW <= 0.0f && m_maxH <= 0.0f)
        return;
    Rect b = m_bounds;
    if (m_maxW > 0.0f && b.w > m_maxW) b.w = m_maxW;
    if (m_maxH > 0.0f && b.h > m_maxH) b.h = m_maxH;
    if (m_minW > 0.0f && b.w < m_minW) b.w = m_minW;
    if (m_minH > 0.0f && b.h < m_minH) b.h = m_minH;
    m_bounds = b;
}

void Widget::resolveGeometry(float parentW, float parentH) {
    if (m_geomModes == 0 && m_anchor == 0) { clampToBounds(); return; }
    auto mode = [this](int shift) {
        return static_cast<GeomMode>((m_geomModes >> shift) & 3u);
    };
    Rect b = m_bounds;
    const GeomMode xm = mode(0), ym = mode(2), wm = mode(4), hm = mode(6);
    if (xm == GeomPercent) b.x = m_geomSpec.x * 0.01f * parentW;
    if (ym == GeomPercent) b.y = m_geomSpec.y * 0.01f * parentH;
    if (wm == GeomPercent)      b.w = m_geomSpec.w * 0.01f * parentW;
    else if (wm == GeomFill)    b.w = parentW;
    if (hm == GeomPercent)      b.h = m_geomSpec.h * 0.01f * parentH;
    else if (hm == GeomFill)    b.h = parentH;

    if (m_anchor != 0) {
        if (parentW > 0.0f && parentH > 0.0f) {
            if (!m_anchorCaptured) {
                m_anchorMargins[0] = b.x;
                m_anchorMargins[1] = parentW - (b.x + b.w);
                m_anchorMargins[2] = b.y;
                m_anchorMargins[3] = parentH - (b.y + b.h);
                m_anchorCaptured = true;
            } else {
                const float dL = m_anchorMargins[0], dR = m_anchorMargins[1];
                const float dT = m_anchorMargins[2], dB = m_anchorMargins[3];
                const bool L = m_anchor & AnchorLeft, R = m_anchor & AnchorRight;
                const bool T = m_anchor & AnchorTop, B = m_anchor & AnchorBottom;
                if (L && R)      { b.x = dL; b.w = parentW - dL - dR; }
                else if (R)       b.x = parentW - dR - b.w;
                else if (L)       b.x = dL;
                if (T && B)      { b.y = dT; b.h = parentH - dT - dB; }
                else if (B)       b.y = parentH - dB - b.h;
                else if (T)       b.y = dT;
            }
        }
    }
    m_bounds = b;
    clampToBounds();
}

void Widget::layout(float parentWidth, float parentHeight) {
    // Base widgets have no opinion about their children's placement, but
    // containers nested below still need to run their own layout pass.
    // Subclasses that position children (Column/Row/ScrollView) override this
    // and do NOT call the base implementation.
    const float pw = m_bounds.w > 0.0f ? m_bounds.w : parentWidth;
    const float ph = m_bounds.h > 0.0f ? m_bounds.h : parentHeight;
    for (const auto& c : m_children) {
        if (!c) continue;
        c->resolveGeometry(pw, ph);
        c->layout(pw, ph);
    }
}

void Widget::onTick(float dt) {
    tickVisualTransition(dt);
    if (!m_boundsAnimating) return;
    if (!m_boundsTween.running()) {
        m_boundsAnimating = false;
        return;
    }
    m_boundsTween.update(dt);
    const float t = m_boundsTween.value();
    const Rect& a = m_boundsStart;
    const Rect& b = m_boundsTarget;
    // Tween from the starting rect toward the fixed target stored by
    // animateTo(); `a`/`b` never move so the curve is stable.
    m_bounds = Rect{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                    a.w + (b.w - a.w) * t, a.h + (b.h - a.h) * t};
    if (!m_boundsTween.running()) m_boundsAnimating = false;
}

void Widget::animateTo(const Rect& target, float duration, TweenEase ease) {
    m_boundsStart = m_bounds;
    m_boundsTarget = target;
    m_boundsTween.start(0.0f, 1.0f, duration, ease);
    m_bounds = target;
    m_boundsAnimating = true;
}

// ---- visual state (#14/#15) --------------------------------------

namespace {
// Priority order: a later name only applies if no earlier active state
// registered an override for the property.
const char* const kVisualStates[] = {
    "disabled", "pressed", "hover", "selected", "checked", "focus"};
const uint8_t kVisualStateBits[] = {
    Widget::VS_Disabled, Widget::VS_Pressed, Widget::VS_Hovered,
    Widget::VS_Selected, Widget::VS_Checked, Widget::VS_Focused};

float mixf(float a, float b, float t) { return a + (b - a) * t; }

Color mixColor(const Color& a, const Color& b, float t) {
    return Color{
        static_cast<uint8_t>(mixf(a.r, b.r, t) + 0.5f),
        static_cast<uint8_t>(mixf(a.g, b.g, t) + 0.5f),
        static_cast<uint8_t>(mixf(a.b, b.b, t) + 0.5f),
        static_cast<uint8_t>(mixf(a.a, b.a, t) + 0.5f)};
}
} // namespace

void Widget::setEnabled(bool e) {
    if (m_enabled == e) return;
    m_enabled = e;
    refreshVisualState();
    if (m_kernel) m_kernel->invalidate();
}

void Widget::addVisualOverride(const std::string& prop, const char* state,
                               const Color& v) {
    auto& p = m_visualColors[prop];
    if (std::string(state) == "normal") {
        p.normal = v;
        p.applied = v;
        p.from = v;
    } else {
        p.byState[state] = v;
    }
}

void Widget::addVisualOverride(const std::string& prop, const char* state,
                               float v) {
    auto& p = m_visualFloats[prop];
    if (std::string(state) == "normal") {
        p.normal = v;
        p.applied = v;
        p.from = v;
    } else {
        p.byState[state] = v;
    }
}

uint8_t Widget::visualStateBits() const {
    uint8_t b = 0;
    if (!m_enabled) b |= VS_Disabled;
    if (m_kernel) {
        if (m_kernel->focusedWidget() == this) b |= VS_Focused;
        if (m_kernel->hoveredWidget() == this) b |= VS_Hovered;
        if (m_kernel->pressedWidget() == this) b |= VS_Pressed;
    }
    if (isCheckedVisual()) b |= VS_Checked;
    if (isSelectedVisual()) b |= VS_Selected;
    return b;
}

Color Widget::visualColorTarget(const std::string& prop) const {
    auto it = m_visualColors.find(prop);
    if (it == m_visualColors.end()) return Color{};
    for (int i = 0; i < 6; ++i) {
        if (!(m_visualBits & kVisualStateBits[i])) continue;
        auto s = it->second.byState.find(kVisualStates[i]);
        if (s != it->second.byState.end()) return s->second;
    }
    return it->second.normal;
}

float Widget::visualFloatTarget(const std::string& prop) const {
    auto it = m_visualFloats.find(prop);
    if (it == m_visualFloats.end()) return 0.0f;
    for (int i = 0; i < 6; ++i) {
        if (!(m_visualBits & kVisualStateBits[i])) continue;
        auto s = it->second.byState.find(kVisualStates[i]);
        if (s != it->second.byState.end()) return s->second;
    }
    return it->second.normal;
}

void Widget::refreshVisualState() {
    if (m_visualColors.empty() && m_visualFloats.empty() && m_enabled) return;
    const uint8_t bits = visualStateBits();
    if (bits == m_visualBits) return;
    m_visualBits = bits;
    bool running = false;
    for (auto& [prop, p] : m_visualColors) {
        const Color to = visualColorTarget(prop);
        if (to.r == p.applied.r && to.g == p.applied.g &&
            to.b == p.applied.b && to.a == p.applied.a)
            continue;
        if (m_transDur > 0.0f) { p.from = p.applied; running = true; }
        else { p.applied = to; applyVisualColor(prop, to); }
    }
    for (auto& [prop, p] : m_visualFloats) {
        const float to = visualFloatTarget(prop);
        if (to == p.applied) continue;
        if (m_transDur > 0.0f) { p.from = p.applied; running = true; }
        else { p.applied = to; applyVisualFloat(prop, to); }
    }
    if (running) { m_transRunning = true; m_transLeft = m_transDur; }
    if (m_kernel) m_kernel->invalidate();
}

void Widget::tickVisualTransition(float dt) {
    if (!m_transRunning) return;
    m_transLeft -= dt;
    float t = m_transDur > 0.0f ? 1.0f - m_transLeft / m_transDur : 1.0f;
    if (t > 1.0f) t = 1.0f;
    for (auto& [prop, p] : m_visualColors) {
        p.applied = mixColor(p.from, visualColorTarget(prop), t);
        applyVisualColor(prop, p.applied);
    }
    for (auto& [prop, p] : m_visualFloats) {
        p.applied = mixf(p.from, visualFloatTarget(prop), t);
        applyVisualFloat(prop, p.applied);
    }
    if (m_transLeft <= 0.0f) m_transRunning = false;
}

const Color* Widget::visualColorNow(const std::string& prop) const {
    auto it = m_visualColors.find(prop);
    return it == m_visualColors.end() ? nullptr : &it->second.applied;
}

const float* Widget::visualFloatNow(const std::string& prop) const {
    auto it = m_visualFloats.find(prop);
    return it == m_visualFloats.end() ? nullptr : &it->second.applied;
}

void Widget::applyVisualColor(const std::string&, const Color&) {}
void Widget::applyVisualFloat(const std::string&, float) {}

bool Widget::hitTest(const Offset2D& p) const {
    Rect b = bounds();
    return p.x >= b.x && p.x <= b.x + b.w &&
           p.y >= b.y && p.y <= b.y + b.h;
}

Offset2D Widget::absoluteOrigin() const {
    Offset2D o{};
    for (const Widget* w = this; w != nullptr; w = w->parent()) {
        o.x += w->bounds().x;
        o.y += w->bounds().y;
    }
    return o;
}

void Widget::onPointerDown(MouseButton, const Offset2D&) {
    (void)this;
}

void Widget::onPointerUp(MouseButton) {
    (void)this;
}

void Widget::onPointerMove(const Offset2D&) {
    (void)this;
}

void Widget::onPointerScroll(float, float) {
    (void)this;
}

Offset2D Widget::childOffset() const {
    return Offset2D{0.0f, 0.0f};
}

bool Widget::clipsChildren(Rect& outRect) const {
    (void)outRect;
    return false;
}

bool Widget::wantsScroll() const {
    return false;
}

void Widget::onKeyDown(KeyCode, uint32_t) {
    (void)this;
}

void Widget::onKeyUp(KeyCode) {
    (void)this;
}

void Widget::onChar(uint32_t) {
    (void)this;
}

void Widget::onFocus(bool) {
    (void)this;
}

void Widget::applyStyle(const Style& style) {
    // The parser hands over a fully-formed Style built from the element's
    // attributes (missing keys keep Style defaults), so this replaces.
    m_style = style;
}

void Widget::onDraw(Rasterizer&) const {
    // subclasses override
}

void Widget::dispatchEvent(EventType, const Offset2D&) {
    // subclasses override
}

} // namespace ogui
