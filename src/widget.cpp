#include "ogui/widget.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/kernel.hpp"
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
}

void Widget::removeChild(Ptr child) {
    if (!child) return;
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        if (it->get() == child.get()) {
            if (child->m_parent == this) child->m_parent = nullptr;
            m_children.erase(it);
            break;
        }
    }
}

void Widget::resolveGeometry(float parentW, float parentH) {
    if (m_geomModes == 0 && m_anchor == 0) return;
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
