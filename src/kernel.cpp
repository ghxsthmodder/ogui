#include "ogui/kernel.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/widgets.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>

namespace ogui {

namespace {

// Depth-first traversal in draw order (parents before children).
template <typename Fn>
void forEachWidget(const std::vector<Widget::Ptr>& roots, Fn&& fn) {
    for (const auto& w : roots) {
        if (!w) continue;
        fn(w);
        forEachWidget(w->children(), fn);
    }
}

// Hit-test from the back of the list to the front; children win over parents.
// Bounds are parent-relative, so the accumulated parent offset (ox, oy) is
// added before testing and before recursing.
Widget* hitTestTree(const std::vector<Widget::Ptr>& roots, const Offset2D& p,
                    float ox = 0.0f, float oy = 0.0f) {
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
        const auto& w = *it;
        if (!w) continue;
        if (!w->isVisible()) continue;   // hidden widgets (and subtrees) don't take input
        Rect b = w->bounds();
        const float ax = ox + b.x;
        const float ay = oy + b.y;

        // Zero-sized widgets (e.g. the <screen> root) are transparent
        // containers: they never absorb the point, their children still can.
        if (b.w <= 0.0f || b.h <= 0.0f) {
            const Offset2D off = w->childOffset();
            if (Widget* child = hitTestTree(w->children(), p, ax + off.x, ay + off.y))
                return child;
            continue;
        }

        if (p.x < ax || p.y < ay || p.x > ax + b.w || p.y > ay + b.h) continue;

        // Children are drawn clipped: a point over this widget but outside
        // that clip belongs to the widget itself, not to content sticking out.
        Rect clip;
        if (w->clipsChildren(clip)) {
            const float clx = ax + clip.x, cly = ay + clip.y;
            if (p.x < clx || p.y < cly || p.x > clx + clip.w || p.y > cly + clip.h)
                return w.get();
        }

        // Overlays (scrollbars) claim their pixels before the children
        // they are drawn on top of get a chance.
        if (w->claimsPoint(Offset2D{p.x - ax, p.y - ay})) return w.get();

        const Offset2D off = w->childOffset();
        if (Widget* child = hitTestTree(w->children(), p, ax + off.x, ay + off.y))
            return child;
        return w.get();
    }
    return nullptr;
}

void setFocus(Kernel& k, Widget*& focus, Widget* target) {
    if (focus == target) return;
    if (focus) {
        std::string id = focus->id();
        focus->onFocus(false);
        if (!id.empty()) k.script().fireEvent(id, "blur");
    }
    focus = target;
    if (focus) {
        std::string id = focus->id();
        focus->onFocus(true);
        if (!id.empty()) k.script().fireEvent(id, "focus");
    }
}

} // namespace

// ---- Kernel ---------------------------------------------------------------
Kernel::Kernel() = default;
Kernel::~Kernel() = default;

void Kernel::init() {
    m_quit = false;
    m_screenStack.clear();
    m_logs.clear();
    m_theme = nullptr;
    m_focus = nullptr;
    m_hovered = nullptr;
    m_captured = nullptr;
    m_pressed = nullptr;
    m_pressButton = -1;
    m_prevDown = false;
    m_prevChar = 0;
    for (bool& k : m_prevKeys) k = false;
    m_bindings.clear();
    m_script.clear();
    m_script.bindKernel(this);
    m_drawList.clear();

    m_tipTarget = nullptr;
    m_toasts.clear();
    m_tipTimer = 0.0f;
    m_tipPrevTime = 0.0f;
    m_tipMouseX = 0.0f;
    m_tipMouseY = 0.0f;

    m_input = InputState{};
    m_input.mouseButton = -1;
    m_input.lastChar = 0;

    m_popup.reset();
    m_tipFade = 0.0f;
    m_prevTime = 0.0f;
    m_dirty = true;
    m_prevInput = InputState{};
    m_builtW = m_builtH = -1.0f;
}

void Kernel::run(const InputSource& input, const std::function<void()>& draw) {
    if (m_screenStack.empty()) {
        auto s = std::make_shared<Screen>("default");
        m_screenStack.push_back(s);
    }

    auto start = std::chrono::steady_clock::now();
    auto last = start;

    while (!m_quit) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        m_input.time = std::chrono::duration<float>(now - start).count();

        input.send(m_input);
        update();
        buildDrawList();
        if (draw) draw();

        // The host owns pacing; this only avoids a busy spin when the host
        // never sleeps (headless demos).
        if (dt < 1.0f / 240.0f) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
}

void Kernel::frame(const InputState& input) {
    m_input = input;
    update();
    buildDrawList();
}

void Kernel::reloadScreenScripts() {
    // The visible screen's script state is destroyed on every switch (it is
    // rebuilt from the Screen's chunks), so hide is always followed by
    // unload; ogui_on_show closes the loop on the incoming side.
    if (m_script.state()) {
        m_script.notifyHide();
        m_script.notifyUnload();
    }
    m_script.clear();
    m_script.bindKernel(this);
    auto screen = currentScreen();
    if (!screen) return;
    for (const auto& [payload, isFile] : screen->scripts) {
        if (isFile) m_script.loadFile(payload);
        else m_script.loadString(payload, screen->id);
    }
    m_script.notifyShow();
    invalidate();
}

void Kernel::pushScreen(std::shared_ptr<Screen> screen) {
    if (!screen) return;
    m_screenStack.push_back(screen);
    setFocus(*this, m_focus, nullptr);
    reloadScreenScripts();
    invalidate();
}

void Kernel::popScreen() {
    if (m_screenStack.size() > 1) {
        m_screenStack.pop_back();
        setFocus(*this, m_focus, nullptr);
        reloadScreenScripts();
        invalidate();
    }
}

void Kernel::switchScreen(std::shared_ptr<Screen> screen) {
    if (!screen) return;
    if (m_screenStack.empty()) pushScreen(screen);
    else {
        // Screens share one window: the incoming screen adopts the size the
        // host keeps on the active one, so it never shows up stale.
        if (auto cur = m_screenStack.back()) {
            screen->width = cur->width;
            screen->height = cur->height;
        }
        m_screenStack.back() = screen;
        setFocus(*this, m_focus, nullptr);
        reloadScreenScripts();
        invalidate();
    }
}

void Kernel::setTheme(const Theme* theme) { m_theme = theme; invalidate(); }

void Kernel::processInput(const InputState& st) { m_input = st; }

Widget* Kernel::popupHitTest(const Offset2D& p) const {
    if (!m_popup) return nullptr;
    std::vector<Widget::Ptr> roots{m_popup};
    return hitTestTree(roots, p);
}

void Kernel::advanceAnimations(float dt) {
    auto screen = currentScreen();
    if (!screen) return;
    forEachWidget(screen->widgets, [&](const Widget::Ptr& w) {
        if (w) {
            w->setKernel(this);
            if (dt > 0.0f) {
                w->onTick(dt);
                if (w->isAnimating()) m_dirty = true;
            }
        }
    });
    // The popup lives outside the tree (drawn above it); it still ticks so
    // its open-in fade/slide runs on the same clock.
    if (m_popup) {
        m_popup->setKernel(this);
        if (dt > 0.0f) {
            m_popup->onTick(dt);
            if (m_popup->isAnimating()) m_dirty = true;
        }
    }
}

bool Kernel::tooltipVisible() const {
    return m_captured == nullptr && m_tipTarget != nullptr &&
           m_tipTarget->isVisible() && !m_tipTarget->tooltip().empty() &&
           m_tipTimer >= m_tipDelay;
}

uint32_t Kernel::currentMods() const {
    uint32_t m = Mod_None;
    if (m_input.keyDown[Key_LSHIFT] || m_input.keyDown[Key_RSHIFT]) m |= Mod_Shift;
    if (m_input.keyDown[Key_LCTRL] || m_input.keyDown[Key_RCTRL]) m |= Mod_Ctrl;
    if (m_input.keyDown[Key_LALT] || m_input.keyDown[Key_RALT]) m |= Mod_Alt;
    if (m_input.keyDown[Key_LGUI] || m_input.keyDown[Key_RGUI]) m |= Mod_Super;
    return m;
}

void Kernel::addLog(const std::string& msg) {
    m_logs.insert(m_logs.begin(), msg);
}

void Kernel::bind(const std::string& name, std::function<void()> fn) {
    // Stored by name; resolved at click time against the widget's binding
    // (onclick=) first, then its id. Widgets parsed later are covered too.
    m_bindings[name] = std::move(fn);
}

Widget* Kernel::findWidget(const std::string& id) {
    auto screen = currentScreen();
    if (!screen || id.empty()) return nullptr;
    Widget* found = nullptr;
    forEachWidget(screen->widgets, [&](const Widget::Ptr& w) {
        if (!found && w->id() == id) found = w.get();
    });
    return found;
}

bool Kernel::setText(const std::string& id, const std::string& text) {
    Widget* w = findWidget(id);
    if (!w) return false;
    bool changed = false;
    if (auto* l = dynamic_cast<Label*>(w)) { l->setText(text); changed = true; }
    else if (auto* b = dynamic_cast<Button*>(w)) { b->setText(text); changed = true; }
    else if (auto* t = dynamic_cast<TextInput*>(w)) { t->setText(text); changed = true; }
    if (changed) invalidate();
    return changed;
}

std::string Kernel::getText(const std::string& id) const {
    auto screen = m_screenStack.empty() ? nullptr : m_screenStack.back();
    if (!screen || id.empty()) return {};
    Widget* found = nullptr;
    forEachWidget(screen->widgets, [&](const Widget::Ptr& w) {
        if (!found && w->id() == id) found = w.get();
    });
    if (!found) return {};
    if (auto* l = dynamic_cast<Label*>(found)) return l->text();
    if (auto* b = dynamic_cast<Button*>(found)) return b->text();
    if (auto* t = dynamic_cast<TextInput*>(found)) return t->text();
    return {};
}

void Kernel::eachWidget(const std::function<void(Widget*)>& fn) const {
    auto screen = currentScreen();
    if (!screen) return;
    forEachWidget(screen->widgets, [&](const Widget::Ptr& w) {
        if (w) fn(w.get());
    });
}

// ---- input ----------------------------------------------------------------
void Kernel::update() {
    auto screen = currentScreen();
    if (!screen || screen->widgets.empty()) return;

    // Redraw trigger #1: anything in the input except the clock changed
    // (the host advances `time` every frame even when idle).
    {
        const InputState& a = m_input;
        const InputState& b = m_prevInput;
        if (a.mouseX != b.mouseX || a.mouseY != b.mouseY ||
            a.mouseButton != b.mouseButton ||
            a.scrollX != b.scrollX || a.scrollY != b.scrollY ||
            a.lastChar != b.lastChar ||
            std::memcmp(a.mouseDown, b.mouseDown, sizeof(a.mouseDown)) != 0 ||
            std::memcmp(a.keyDown, b.keyDown, sizeof(a.keyDown)) != 0 ||
            std::memcmp(a.keyRepeat, b.keyRepeat, sizeof(a.keyRepeat)) != 0) {
            m_dirty = true;
        }
        m_prevInput = a;
    }

    // Graphics/HScript-free animation clock: every widget ticks with the host
    // input clock; a stalled host (dt clamped) just freezes the tweens.
    const float dt = std::clamp(m_input.time - m_prevTime, 0.0f, 0.25f);
    m_prevTime = m_input.time;
    advanceAnimations(dt);
    m_script.notifyUpdate(m_input.time, dt);

    for (auto& t : m_toasts) t.age += dt;
    if (!m_toasts.empty()) m_dirty = true;   // fading/aging banners
    while (!m_toasts.empty() && m_toasts.front().age > 3.5f) {
        m_toasts.erase(m_toasts.begin());
        m_dirty = true;
    }
    if (m_toasts.size() > 4)
        m_toasts.erase(m_toasts.begin(),
                       m_toasts.begin() + static_cast<long>(m_toasts.size() - 4));

    // Frame 0 delivers input before the first buildDrawList would lay the
    // tree out; laying out here keeps hit-testing honest from the start.
    // Every layout implementation is idempotent, so the second pass in
    // buildDrawList is harmless.
    for (const auto& w : screen->widgets) {
        if (!w) continue;
        w->resolveGeometry(screen->width, screen->height);
        w->layout(screen->width, screen->height);
    }

    const Offset2D p{m_input.mouseX, m_input.mouseY};
    const int btn = (m_input.mouseButton >= 0 && m_input.mouseButton < 4)
                        ? m_input.mouseButton : 0;
    const bool down = m_input.mouseDown[btn];
    const bool pressedEdge = down && !m_prevDown;
    const bool releasedEdge = !down && m_prevDown;

    // Hover is recomputed every frame (the pointer may have moved).
    m_hovered = hitTestTree(screen->widgets, p);

    // The popup layer sits on top: it owns hover/move while the pointer is
    // inside it, and a press anywhere else closes it (and is swallowed).
    Widget* popupHit = m_popup ? popupHitTest(p) : nullptr;

    // Tooltip hover timing: restart when the pointer changes widget or moves
    // at all; otherwise accumulate the input clock (host-provided, testable).
    const bool moved = (m_input.mouseX != m_tipMouseX || m_input.mouseY != m_tipMouseY);
    m_tipMouseX = m_input.mouseX;
    m_tipMouseY = m_input.mouseY;
    if (m_hovered != m_tipTarget || moved) {
        m_tipTarget = m_hovered;
        m_tipTimer = 0.0f;
    } else if (m_tipTarget) {
        const float tdt = m_input.time - m_tipPrevTime;
        m_tipTimer += tdt > 0.0f ? tdt : 0.0f;
    }
    m_tipPrevTime = m_input.time;

    // Tooltip fades in when it shows, out once the pointer leaves.
    const bool tipShowing = tooltipVisible();
    const float prevFade = m_tipFade;
    if (tipShowing) m_tipFade = std::min(1.0f, m_tipFade + dt / 0.12f);
    else m_tipFade = std::max(0.0f, m_tipFade - dt * 6.0f);
    if (m_tipFade != prevFade) m_dirty = true;

    if (pressedEdge) {
        // Multi-click: same button, same spot (6px) inside 0.5s counts up.
        const bool chained = btn == m_lastClickBtn &&
                             m_input.time - m_lastClickTime <= 0.5f &&
                             std::fabs(p.x - m_lastClickX) <= 6.0f &&
                             std::fabs(p.y - m_lastClickY) <= 6.0f;
        m_clickCount = chained ? m_clickCount + 1 : 1;
        m_lastClickBtn = btn;
        m_lastClickTime = m_input.time;
        m_lastClickX = p.x;
        m_lastClickY = p.y;

        bool swallowed = false;
        if (m_popup) {
            if (popupHit) {
                m_captured = popupHit;
                m_pressed = popupHit;
                m_pressButton = btn;
                popupHit->onPointerDown(static_cast<MouseButton>(btn), p);
                swallowed = true;   // popup press owns the capture and focus
            } else {
                closePopup();
                swallowed = true;   // outside click: close but don't click through
            }
        }
        if (!swallowed) {
            m_captured = m_hovered;
            m_pressed = m_hovered;
            m_pressButton = btn;
            setFocus(*this, m_focus, m_hovered);
            if (m_captured) m_captured->onPointerDown(static_cast<MouseButton>(btn), p);
        }
        // Right-click: open the nearest ancestor's <contextmenu>, if any.
        if (btn == Mouse_Right && !swallowed) {
            for (Widget* t = m_hovered; t != nullptr; t = t->parent()) {
                if (t->contextMenuName().empty()) continue;
                if (auto* cm = dynamic_cast<ContextMenu*>(
                        findWidget(t->contextMenuName())))
                    cm->openAt(p.x, p.y);
                break;
            }
        }
    }

    // While a button is down the capture owns the moves: dragging a slider
    // or a text selection past the widget's edge must keep feeding it (the
    // pointer is no longer "hovering" it there).
    Widget* moveTarget = (popupHit && m_popup) ? popupHit
                        : (m_captured ? m_captured : m_hovered);
    if (moveTarget) moveTarget->onPointerMove(p);

    // Wheel events: the popup layer owns them while the pointer is inside
    // (a long combo list scrolls, not the tree under it); otherwise a drag
    // in progress wins, then the widget under the pointer, bubbling up to
    // the nearest ancestor that wants scrolling.
    if (m_input.scrollX != 0.0f || m_input.scrollY != 0.0f) {
        Widget* target = popupHit ? popupHit
                       : (m_captured ? m_captured : m_hovered);
        while (target && !target->wantsScroll()) target = target->parent();
        if (target) target->onPointerScroll(m_input.scrollX, m_input.scrollY);
    }

    if (releasedEdge) {
        if (m_captured) {
            m_captured->onPointerUp(static_cast<MouseButton>(
                m_pressButton >= 0 ? m_pressButton : 0));
        }
        // Click = press and release happened on the same widget, with the
        // left button (a right-click belongs to the context menu).
        if (m_pressed && m_pressed == m_hovered && m_pressButton != Mouse_Right) {
            // Resolve by binding name first (onclick=), then by id.
            auto lookup = [&](const std::string& key) -> std::function<void()> {
                auto it = m_bindings.find(key);
                return (it != m_bindings.end()) ? it->second : nullptr;
            };
            bool handled = false;
            std::string pressedId = m_pressed->id();
            if (auto fn = lookup(m_pressed->binding())) { fn(); handled = true; }
            else if (auto fn2 = lookup(m_pressed->id())) { fn2(); handled = true; }
            else if (const auto& h = m_pressed->clickHandler()) { h(); handled = true; }
            // Nothing C++-bound: route the click to the screen's Lua
            // ogui_on_click hook with the widget id (onclick name if set).
            if (!handled && !pressedId.empty())
                m_script.notifyClick(m_pressed ?
                                         (m_pressed->binding().empty()
                                              ? m_pressed->id()
                                              : m_pressed->binding())
                                         : pressedId);
            // A handler may have removed the pressed widget from the tree
            // (ogui.remove); forgetWidget nulls m_pressed in that case.
            if (m_pressed) m_pressed->dispatchEvent(EventType::MouseButton, p);
        }
        m_captured = nullptr;
        m_pressed = nullptr;
        m_pressButton = -1;
    }

    // Characters go to the focused widget; edges only (host clears lastChar).
    if (m_input.lastChar && m_input.lastChar != m_prevChar) {
        if (m_focus) m_focus->onChar(m_input.lastChar);
    }
    m_prevChar = m_input.lastChar;

    // Key edges: forward press/release transitions to the focused widget.
    // Repeats (host sets keyRepeat[k] for a held key) re-send onKeyDown so
    // caret/arrow navigation repeats like the OS editor does.
    const uint32_t mods = currentMods();
    for (int k = 0; k < 256; ++k) {
        bool now = m_input.keyDown[k];
        if (now && !m_prevKeys[k]) {
            if (m_focus) m_focus->onKeyDown(static_cast<KeyCode>(k), mods);
        } else if (now && m_prevKeys[k] && m_input.keyRepeat[k]) {
            if (m_focus) m_focus->onKeyDown(static_cast<KeyCode>(k), mods);
        } else if (!now && m_prevKeys[k]) {
            if (m_focus) m_focus->onKeyUp(static_cast<KeyCode>(k));
        }
        m_prevKeys[k] = now;
    }

    m_prevDown = down;
}

// ---- drawing --------------------------------------------------------------
const DrawList& Kernel::buildDrawList() {
    auto screen = currentScreen();
    if (!screen) {
        m_drawList.clear();
        return m_drawList;
    }
    if (screen->width != m_builtW || screen->height != m_builtH) {
        m_dirty = true;   // host resized the viewport
    }
    if (!m_dirty) return m_drawList;   // nothing changed: reuse last frame

    Rasterizer& r = m_raster;
    r.reset();
    r.beginFrame(Rect{0, 0, screen->width, screen->height});
    for (const auto& w : screen->widgets) {
        if (!w) continue;
        // Containers (Column/Row/ScrollView) position their children first.
        w->resolveGeometry(screen->width, screen->height);
        w->layout(screen->width, screen->height);
        drawTree(r, w.get(), 0.0f, 0.0f);
    }

    // Popup layer: drawn after the tree so dropdowns/menus float above
    // everything, before the tooltip. Bounds are screen-absolute.
    if (m_popup && m_popup->isVisible()) {
        drawTree(r, m_popup.get(), 0.0f, 0.0f);
    }

    // Hover tooltip, drawn last so it floats above everything. It fades in
    // and out via m_tipFade (0..1). Mouse-hover UI, not an input target.
    if (tooltipVisible()) {
        constexpr int kTipFontSize = 12;
        constexpr float kPad = 6.0f;
        const std::string& tip = m_tipTarget->tooltip();
        const Font* f = r.resolveFont("sans", kTipFontSize);
        const float tw = f ? r.textWidth(tip.c_str(), *f) : 0.0f;
        const float lh = std::max(textLineHeight(kTipFontSize, "sans"), 1.0f);
        const float bw = tw + kPad * 2.0f;
        const float bh = lh + kPad * 2.0f;
        float tx = m_input.mouseX + 14.0f;
        float ty = m_input.mouseY + 18.0f;
        if (tx + bw > screen->width) tx = m_input.mouseX - bw - 6.0f;
        if (ty + bh > screen->height) ty = m_input.mouseY - bh - 6.0f;
        if (tx < 0.0f) tx = 0.0f;
        if (ty < 0.0f) ty = 0.0f;
        const Rect box{tx, ty, bw, bh};
        const float fade = std::max(m_tipFade, 0.02f);
        auto tinted = [&](const Color& base) {
            Color c = base;
            c.a = static_cast<uint8_t>(static_cast<float>(c.a) * fade);
            return c;
        };
        const Palette& pal = palette();
        r.drawRect(box, tinted(pal.surface));
        r.drawRectBorder(box, tinted(pal.borderStrong), 1.0f, 4.0f);
        if (f) {
            r.drawTextShifted(tip.c_str(), tx + kPad, ty + kPad, kTipFontSize,
                              *f, tinted(pal.textBright));
        }
    }

    // Toast banners: bottom-right stack, fading out over the last 0.5 s.
    {
        constexpr float kPad = 8.0f;
        float y = screen->height - 8.0f;
        for (size_t i = m_toasts.size(); i-- > 0;) {
            const ToastItem& t = m_toasts[i];
            const float fade = t.age > 3.0f ? (3.5f - t.age) * 2.0f : 1.0f;
            const float a = std::clamp(fade, 0.0f, 1.0f);
            const Palette& pal = palette();
            const Color kKindColor[3] = {pal.info, pal.warn, pal.error};
            const Font* f = r.resolveFont("sans", 13);
            const float tw = f ? r.textWidth(t.text.c_str(), *f) : 120.0f;
            const float bw = tw + kPad * 2.0f + 4.0f;
            const float bh = 28.0f;
            const Rect box{screen->width - bw - 8.0f, y - bh, bw, bh};
            auto tint = [&](const Color& base) {
                Color c = base;
                c.a = static_cast<uint8_t>(static_cast<float>(c.a) * a);
                return c;
            };
            r.drawDropShadow(box, 4.0f, pal.shadow, 5.0f, 0.0f, 2.0f);
            r.drawRect(box, tint(pal.surface));
            r.drawRect(Rect{box.x, box.y, 3.0f, box.h},
                       tint(kKindColor[t.kind % 3]));
            r.drawRectBorder(box, tint(pal.border), 1.0f, 4.0f);
            if (f) {
                r.drawTextShifted(t.text.c_str(), box.x + kPad + 2.0f,
                                  box.y + (box.h - 13.0f) * 0.5f, 13, *f,
                                  tint(pal.textBright));
            }
            y -= bh + 6.0f;
        }
    }

    r.endFrame();
    // Swap instead of move: the rasterizer keeps the previous frame's
    // reserved vectors and refills them without reallocating.
    m_drawList.swap(r.drawList());
    r.drawList().clear();
    m_dirty = false;
    m_builtW = screen->width;
    m_builtH = screen->height;
    return m_drawList;
}

void Kernel::drawTree(Rasterizer& r, Widget* w, float ox, float oy) const {
    if (!w || !w->isVisible()) return;   // skips the whole subtree

    // Widgets keep bounds relative to their parent: shift them into the
    // parent's space for the duration of the draw call.
    Rect b = w->bounds();
    Rect saved = b;
    w->setBounds(Rect{b.x + ox, b.y + oy, b.w, b.h});
    const Style& st = w->style();
    if (st.shadowBlur > 0.0f) {
        r.drawDropShadow(w->bounds(), st.radius, st.shadowColor,
                         st.shadowBlur, st.shadowDx, st.shadowDy);
    }
    w->onDraw(r);
    w->setBounds(saved);

    // Children sit at this widget's origin plus its scroll offset, and are
    // clipped to the container's viewport when it asks for one.
    const Offset2D off = w->childOffset();
    const float cx = ox + b.x + off.x;
    const float cy = oy + b.y + off.y;

    Rect clip;
    const bool clipped = w->clipsChildren(clip);
    if (clipped) {
        r.beginFrame(Rect{ox + b.x + clip.x, oy + b.y + clip.y, clip.w, clip.h});
    }
    for (const auto& c : w->children()) {
        if (c) drawTree(r, c.get(), cx, cy);
    }
    if (clipped) r.endFrame();

    // Foreground pass: overlays (scrollbars) draw above the children, with
    // the same shifted-bounds convention as onDraw above.
    w->setBounds(Rect{b.x + ox, b.y + oy, b.w, b.h});
    w->onDrawForeground(r);
    w->setBounds(saved);
}

} // namespace ogui
