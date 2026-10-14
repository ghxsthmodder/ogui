#pragma once
#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include "ogui/font.hpp"
#include "ogui/tween.hpp"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <functional>

namespace ogui {

// Forward declarations
class Rasterizer;
class Kernel;
struct InputState;

// ------------------------------------------------------------------
// Widget: base of the retained-mode widget tree
// ------------------------------------------------------------------
class Widget : public std::enable_shared_from_this<Widget> {
public:
    using Ptr = std::shared_ptr<Widget>;
    using Callback = std::function<void()>;

    Widget();
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    Widget(Widget&&) = default;
    Widget& operator=(Widget&&) = default;

    // ---- identity ----
    const std::string& id() const { return m_id; }
    void setId(const std::string& id) { m_id = id; }

    // Event binding name (from XML onclick="..."), resolved via Kernel::bind.
    const std::string& binding() const { return m_binding; }
    void setBinding(const std::string& b) { m_binding = b; }

    // Id of the <contextmenu> opened on right-click over this widget.
    const std::string& contextMenuName() const { return m_contextMenu; }
    void setContextMenuName(const std::string& n) { m_contextMenu = n; }

    // Every attribute the XML parser saw on this element (keys lowercased).
    // Lets data-only tags (e.g. <manifest><screen path="..."/></manifest>)
    // carry information that no built-in widget consumes.
    void setRawAttributes(const std::map<std::string, std::string>& attrs) {
        m_rawAttrs = attrs;
    }
    const std::map<std::string, std::string>& rawAttributes() const {
        return m_rawAttrs;
    }
    const std::string* rawAttribute(const std::string& key) const {
        auto it = m_rawAttrs.find(key);
        return it == m_rawAttrs.end() ? nullptr : &it->second;
    }

    // Direct click handler (set by the host or by Kernel::bind).
    void setClickHandler(Callback fn) { m_onClick = std::move(fn); }
    const Callback& clickHandler() const { return m_onClick; }
    void click() { if (m_onClick) m_onClick(); }

    // Fire a Lua per-widget event ("change" / "submit") on the kernel's
    // script host. No-ops when the widget has no id or no kernel attached.
    // Call these only from user-interaction paths, never from programmatic
    // setters, so a listener that writes the value back cannot loop.
    void announceChange();
    void announceSubmit();

    // ---- hierarchy ----
    virtual Widget* parent() const { return m_parent; }
    virtual void setParent(Widget* parent);

    const std::vector<Ptr>& children() const { return m_children; }

    void addChild(Ptr child);
    void removeChild(Ptr child);
    // Raw-pointer identity removal (script-driven ogui.remove): no shared_ptr
    // at hand to compare against.
    void removeChild(Widget* child);
    // Detach and return every child (parent cleared); used to move parsed
    // fragments into the tree and to empty a container.
    std::vector<Ptr> takeChildren();

    // Kernel that owns this widget tree, refreshed by the Kernel at the top
    // of every frame; lets widgets reach popup/event APIs from handlers.
    Kernel* kernel() const { return m_kernel; }
    void setKernel(Kernel* k) { m_kernel = k; }

    // Visual-state helpers: true while the Kernel's pointer/focus sits on
    // this exact widget. Used by onDraw() for hover/pressed/focus feedback.
    bool isHovered() const;
    bool isFocused() const;

    // ---- layout ----
    Rect bounds() const { return m_bounds; }
    void setBounds(const Rect& b) { m_bounds = b; }
    void setPos(float x, float y) { m_bounds.x = x; m_bounds.y = y; }
    void setSize(float w, float h) { m_bounds.w = w; m_bounds.h = h; }
    void setSize(const Rect& r) { m_bounds = r; }

    // Relative geometry (authored by the XML parser).
    // Percent scales against the parent's content size; Fill (w/h) takes it
    // whole. Absolute components keep whatever the bounds currently hold, so
    // runtime moves/resizes of non-relative widgets survive untouched.
    enum GeomMode : uint8_t { GeomAbsolute = 0, GeomPercent = 1, GeomFill = 2 };

    void setGeometrySpec(const Rect& spec, GeomMode xm, GeomMode ym,
                         GeomMode wm, GeomMode hm) {
        m_geomSpec = spec;
        m_geomModes = static_cast<uint8_t>(xm | (ym << 2) | (wm << 4) | (hm << 6));
    }
    const Rect& geometrySpec() const { return m_geomSpec; }

    // Re-resolve the percent/fill components of the spec against the parent
    // content size. layout() passes call this on children; containers that
    // reposition children call it themselves before reading their bounds.
    void resolveGeometry(float parentW, float parentH);

    // Edge anchors. `left`/`top` pin the authored distance to that edge,
    // `right`/`bottom` pin the distance to the opposite edge; combining both
    // edges of an axis (`"left,right"`) keeps both margins and stretches the
    // widget between them. Margins are captured on the first layout pass
    // (from the resolved geometry, whatever the x/y/w/h units were) and the
    // anchored edges then hold that distance while the parent resizes.
    // Unanchored edges keep the normal per-unit behaviour.
    enum AnchorBits : uint8_t {
        AnchorLeft = 1, AnchorRight = 2, AnchorTop = 4, AnchorBottom = 8,
    };
    void setAnchor(uint8_t bits) {
        m_anchor = bits;
        m_anchorCaptured = false;
    }
    uint8_t anchor() const { return m_anchor; }

    // Size constraints (px; 0 = unconstrained). They clamp the resolved
    // bounds on every layout pass, whatever unit x/y/w/h were authored in,
    // so a percent or fill width can still keep a pixel floor or ceiling.
    void setSizeLimits(float minW, float minH, float maxW, float maxH);
    Rect sizeLimits() const { return Rect{m_minW, m_minH, m_maxW, m_maxH}; }

    // Clamp m_bounds to the size limits; called on every geometry resolve.
    void clampToBounds();

    // Flex weight: inside a `column` it shares the leftover HEIGHT, inside
    // a `row` the leftover WIDTH, proportionally to the weights of the
    // other weighted children. 0 (default) keeps the authored size.
    void setWeight(float w) { m_weight = w < 0.0f ? 0.0f : w; }
    float weight() const { return m_weight; }

    virtual void layout(float parentWidth, float parentHeight);

    // Per-frame tick (dt in seconds). The Kernel advances every widget once
    // per frame using the input clock; animating widgets override the base
    // but call it to keep bounds tweens alive.
    virtual void onTick(float dt);

    // True while this widget's appearance changes on its own (running
    // tween/marquee/spinner). The Kernel keeps the redraw dirty flag up while
    // any widget reports this; static screens then skip the rebuild.
    virtual bool isAnimating() const { return m_boundsAnimating; }

    // Tween this widget's bounds to `target` over `duration` seconds. The
    // base onTick() advances the animation; animateTo() snaps an in-flight
    // animation to its end state and starts the new one from the current
    // (possibly mid-flight) bounds, so calls stack smoothly.
    void animateTo(const Rect& target, float duration,
                   TweenEase ease = TweenEase::EaseOutCubic);

    // ---- transform ----
    virtual void transform(const Matrix4x4& m) { m_transform = m; }

    // ---- input ----
    // Returns true if the point (in parent-local coords, y-down) hits this widget.
    virtual bool hitTest(const Offset2D& p) const;

    // Origin of this widget in the coordinate space the Kernel delivers
    // pointer events in (bounds are parent-relative, so walk the chain).
    Offset2D absoluteOrigin() const;

    // p is in the same space as absoluteOrigin() (screen/framebuffer).
    virtual void onPointerDown(MouseButton btn, const Offset2D& p);
    virtual void onPointerUp(MouseButton btn);
    virtual void onPointerMove(const Offset2D& p);
    virtual void onPointerScroll(float dx, float dy);
    virtual void onKeyDown(KeyCode key, uint32_t mods);
    virtual void onKeyUp(KeyCode key);
    virtual void onChar(uint32_t ch);
    virtual void onFocus(bool focused);
    // Called by the Kernel when an open popup is dismissed without going
    // through the owner (e.g. a press outside closes it). Owners that keep
    // their own open-state mirror here.
    virtual void onPopupDismissed() {}

    // Container interception: return true for points (in THIS widget's
    // local space) that belong to the widget itself rather than to the
    // children it overlaps — an overlay scrollbar drawing on top of the
    // scrolled content uses this so presses reach the bar, not the child
    // underneath. Checked before the children are recursed into.
    virtual bool claimsPoint(const Offset2D& /*localP*/) const { return false; }

    // Drawn after the children (onDraw runs before them). Containers use
    // this for overlays — scrollbars — that must sit above scrolled
    // content. Bounds are shifted into the parent space for the call, the
    // same convention as onDraw.
    virtual void onDrawForeground(Rasterizer& /*r*/) const {}

    // ---- container hooks (tree walk) ----
    // Used by the Kernel while drawing and hit-testing children:
    //   childOffset()   - extra offset applied to the children (ScrollView
    //                     returns its scroll delta);
    //   clipsChildren() - when true, children are clipped to the returned
    //                     rect, given in THIS widget's coordinate space;
    //   wantsScroll()   - marks the widget that should receive wheel events
    //                     raised anywhere in its subtree (the Kernel bubbles
    //                     the event up until it finds one).
    virtual Offset2D childOffset() const;
    virtual bool clipsChildren(Rect& outRect) const;
    virtual bool wantsScroll() const;

    // ---- style ----
    virtual void applyStyle(const Style& style);
    const Style& style() const { return m_style; }

    // ---- visibility / tooltip ----
    // A widget is only shown (and only takes input) when it AND all of its
    // ancestors are visible; hiding e.g. a <tab> hides its whole page.
    // setVisible() flips just this widget's own flag.
    bool isVisible() const {
        for (const Widget* w = this; w != nullptr; w = w->m_parent) {
            if (!w->m_style.isVisible) return false;
        }
        return true;
    }
    void setVisible(bool v) { m_style.isVisible = v; }

    // Shown by the Kernel as a small panel while the pointer hovers this
    // widget for the tooltip delay (empty = none).
    void setTooltip(const std::string& text) { m_tooltip = text; }
    const std::string& tooltip() const { return m_tooltip; }

    // ---- drawing ----
    virtual void onDraw(Rasterizer& r) const;

    // ---- events ----
    void dispatchEvent(EventType type, const Offset2D& pos);

protected:
    std::string m_id;
    std::string m_binding;
    std::string m_contextMenu;
    std::map<std::string, std::string> m_rawAttrs;
    Callback m_onClick;
    Widget* m_parent = nullptr;
    Kernel* m_kernel = nullptr;
    std::vector<Ptr> m_children;
    Rect m_bounds{};            // local bounds
    Rect m_geomSpec{};          // authored x/y/w/h (see m_geomModes)
    uint8_t m_geomModes = 0;    // 2 bits per component: GeomMode
    uint8_t m_anchor = 0;       // AnchorBits
    bool m_anchorCaptured = false;
    float m_anchorMargins[4]{};  // left, right, top, bottom (px)
    float m_minW = 0.0f, m_minH = 0.0f, m_maxW = 0.0f, m_maxH = 0.0f;
    float m_weight = 0.0f;        // flex share inside column/row (0 = fixed)
    Matrix4x4 m_transform{};    // parent-space transform
    Style m_style;
    std::string m_tooltip;

    // Bounds animation state (see animateTo/onTick).
    bool m_boundsAnimating = false;
    Rect m_boundsStart{};
    Rect m_boundsTarget{};
    Tween m_boundsTween{};
};

} // namespace ogui
