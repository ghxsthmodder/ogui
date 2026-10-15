#include "ogui/kernel.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/widgets.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <typeinfo>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace ogui {

namespace {

// 0/NaN/negative scale values are treated as "no scaling" rather than
// dividing the whole UI by zero.
float saneScale(float s) { return s > 0.0f ? s : 1.0f; }

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
        if (!w->isEnabled()) continue;   // disabled widgets (and subtrees) are inert
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
    if (k.debugEvents()) k.addLog("focus " + Kernel::describe(target));
    if (focus) {
        Widget* losing = focus;
        losing->onFocus(false);
        Event e;
        e.kind = EventKind::Blur;
        e.target = losing;
        k.dispatchEvent(e);
    }
    focus = target;
    if (focus) {
        Widget* gaining = focus;
        gaining->onFocus(true);
        Event e;
        e.kind = EventKind::Focus;
        e.target = gaining;
        k.dispatchEvent(e);
    }
}

// Depth-first, document-order walk collecting focusable widgets. A hidden or
// disabled subtree is skipped whole, matching hitTestTree's input rules.
void collectFocusable(const std::vector<Widget::Ptr>& ws,
                      std::vector<Widget*>& out) {
    for (const auto& w : ws) {
        if (!w) continue;
        if (!w->isVisible()) continue;
        if (!w->isEnabled()) continue;   // disabled subtree is inert
        if (w->isFocusable()) out.push_back(w.get());
        collectFocusable(w->children(), out);
    }
}

} // namespace

// ---- keyboard focus traversal (P4 #24) ------------------------------------
bool Kernel::focusNext(bool backwards) {
    auto screen = currentScreen();
    if (!screen) return false;
    std::vector<Widget*> order;
    collectFocusable(screen->widgets, order);
    if (order.empty()) { setFocus(*this, m_focus, nullptr); return false; }

    int idx = -1;
    if (m_focus) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == m_focus) { idx = static_cast<int>(i); break; }
    }
    const int n = static_cast<int>(order.size());
    int next = idx < 0
                   ? (backwards ? n - 1 : 0)
                   : ((idx + (backwards ? -1 : 1)) % n + n) % n;
    setFocus(*this, m_focus, order[static_cast<size_t>(next)]);
    invalidate();
    return true;
}

void Kernel::setFocusWidget(Widget* w) {
    if (w == m_focus) return;
    setFocus(*this, m_focus, w);
    invalidate();
}

// ---- inspection (P4 #27) --------------------------------------------------

// Human-readable class name for the dump and the overlay labels. GCC/Clang
// mangle them, so the demangler is used where it exists; the raw id is a
// perfectly readable fallback elsewhere.
std::string Kernel::typeName(const Widget* w) {
    if (!w) return "none";
    const std::type_info& ti = typeid(*w);
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    char* dem = abi::__cxa_demangle(ti.name(), nullptr, nullptr, &status);
    std::string out = dem ? dem : ti.name();
    std::free(dem);
    if (out.rfind("ogui::", 0) == 0) out.erase(0, 6);
    return out;
#else
    return ti.name();
#endif
}

std::string Kernel::describe(const Widget* w) {
    if (!w) return "none";
    std::string out = typeName(w);
    if (!w->id().empty()) out += ' ' + w->id();
    return out;
}

std::string Kernel::dumpTree() const {
    auto screen = currentScreen();
    if (!screen) return std::string();

    std::vector<Widget*> order;
    collectFocusable(screen->widgets, order);

    std::string out;
    std::function<void(const Widget::Ptr&, int)> walk =
        [&](const Widget::Ptr& w, int depth) {
            if (!w) return;
            out.append(static_cast<size_t>(depth) * 2u, ' ');
            const Rect b = w->bounds();
            const Offset2D o = w->absoluteOrigin();
            char head[160];
            std::snprintf(head, sizeof head, "%s %s %.0f,%.0f %.0fx%.0f (%.0f,%.0f)",
                          typeName(w.get()).c_str(),
                          w->id().empty() ? "-" : w->id().c_str(), b.x, b.y, b.w,
                          b.h, o.x, o.y);
            out += head;
            if (!w->isVisible()) out += " hidden";
            if (!w->isEnabled()) out += " disabled";
            if (w->isFocusable()) out += " focusable";
            if (w.get() == m_focus) out += " focused";
            if (w.get() == m_hovered) out += " hovered";
            if (w.get() == m_pressed) out += " pressed";
            for (size_t i = 0; i < order.size(); ++i) {
                if (order[i] != w.get()) continue;
                char tab[32];
                std::snprintf(tab, sizeof tab, " tab=%zu", i);
                out += tab;
                break;
            }
            if (!w->binding().empty()) out += " binding=" + w->binding();
            if (!w->accessibleName().empty())
                out += " accessible=" + w->accessibleName();
            if (!w->tooltip().empty()) out += " tooltip=" + w->tooltip();
            out += '\n';
            for (const auto& c : w->children()) walk(c, depth + 1);
        };
    for (const auto& w : screen->widgets) walk(w, 0);
    return out;
}

int Kernel::tabOrder(const Widget* w) const {
    auto screen = currentScreen();
    if (!screen || !w) return -1;
    std::vector<Widget*> order;
    collectFocusable(screen->widgets, order);
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == w) return static_cast<int>(i);
    return -1;
}

// Authoring geometry on top of a finished frame. Everything here is debug
// output: it is drawn in logical units like the widgets, sits above the toast
// layer and never takes part in hit-testing.
void Kernel::drawDebugOverlay(Rasterizer& r) const {
    auto screen = currentScreen();
    if (!screen || m_debug == 0) return;
    const Palette& pal = palette();
    constexpr int kFont = 10;
    const Font* f = r.resolveFont("mono", kFont);
    if (!f) f = r.resolveFont("sans", kFont);

    // The widget a click would land on next. Hit areas are often whole
    // containers, so the fill stays faint and the outline carries the signal.
    if (m_debug & DebugHover) {
        if (Widget* h = m_hovered) {
            const Offset2D o = h->absoluteOrigin();
            const Rect b = h->bounds();
            const Rect box{o.x, o.y, b.w, b.h};
            r.drawRect(box, withAlpha(pal.info, 38));
            r.drawRectBorder(box, pal.info, 2.0f, 0.0f);
        }
    }

    if (m_debug & (DebugBounds | DebugLabels)) {
        const Color tints[4] = {pal.accent, pal.info, pal.focus, pal.warn};
        std::function<void(const Widget::Ptr&, int)> walk =
            [&](const Widget::Ptr& w, int depth) {
                if (!w || !w->isVisible()) return;
                const Offset2D o = w->absoluteOrigin();
                const Rect b = w->bounds();
                const Rect box{o.x, o.y, b.w, b.h};
                const Color& col = tints[static_cast<size_t>(depth) % 4];
                if (m_debug & DebugBounds)
                    r.drawRectBorder(box, withAlpha(col, 200), 1.0f, 0.0f);
                if ((m_debug & DebugLabels) && f && box.w > 24.0f &&
                    box.h > kFont) {
                    char label[96];
                    std::snprintf(label, sizeof label, "%s%s %.0fx%.0f",
                                  typeName(w.get()).c_str(),
                                  w->id().empty() ? "" : ("#" + w->id()).c_str(),
                                  b.w, b.h);
                    r.drawTextShifted(label, box.x + 1.0f, box.y, kFont, *f,
                                      withAlpha(pal.textBright, 235));
                }
                for (const auto& c : w->children()) walk(c, depth + 1);
            };
        for (const auto& w : screen->widgets) walk(w, 0);
    }

    if ((m_debug & DebugFocus) && f) {
        std::vector<Widget*> order;
        collectFocusable(screen->widgets, order);
        for (size_t i = 0; i < order.size(); ++i) {
            Widget* w = order[i];
            const Offset2D o = w->absoluteOrigin();
            const Rect b = w->bounds();
            const bool focused = (w == m_focus);
            const Rect box{o.x, o.y, b.w, b.h};
            r.drawRectBorder(box, focused ? pal.focus : withAlpha(pal.focus, 130),
                             focused ? 2.0f : 1.0f, 0.0f);
            char tag[24];
            std::snprintf(tag, sizeof tag, "%zu", i);
            r.drawTextShifted(tag, box.x + 2.0f, box.y + 1.0f, kFont, *f,
                              focused ? pal.white : pal.focus);
        }
    }
}

// The click path shared by mouse release and keyboard activation: a named
// binding (onclick=) wins over the id binding, then a C++ handler, then the
// screen's Lua ogui_on_click hook. The event itself is dispatched afterwards
// either way, so listeners on the pressed widget and its ancestors always see
// it -- a C++ action consumes nothing, it just acts.
void Kernel::clickWidget(Widget* w, const Offset2D& point, EventSource source) {
    if (!w) return;
    Widget* prevTarget = m_clickTarget;
    m_clickTarget = w;
    if (m_debugEvents)
        addLog("click " + describe(w) +
               (w->binding().empty() ? "" : " -> " + w->binding()));
    auto lookup = [&](const std::string& key) -> std::function<void()> {
        auto it = m_bindings.find(key);
        return (it != m_bindings.end()) ? it->second : nullptr;
    };
    bool handled = false;
    std::string pressedKey = w->binding().empty() ? w->id() : w->binding();
    if (auto fn = lookup(w->binding())) { fn(); handled = true; }
    else if (auto fn2 = lookup(w->id())) { fn2(); handled = true; }
    else if (const auto& h = w->clickHandler()) { h(); handled = true; }
    // Nothing C++-bound: the click falls through to the screen's Lua
    // ogui_on_click hook, addressed by the binding name when there is one.
    if (!handled && !pressedKey.empty()) m_script.notifyClick(pressedKey);
    // A handler may have removed the widget from the tree (ogui.remove), which
    // routes through forgetWidget and clears m_clickTarget; a nested click on
    // another widget restores it, so this test still means "our target
    // survived".
    if (m_clickTarget == w) {
        Event e;
        e.kind = EventKind::Click;
        e.source = source;
        e.target = w;
        e.point = point;
        e.handled = handled;
        dispatchEvent(e);
    }
    m_clickTarget = prevTarget;
}

// Target first, then every ancestor. The chain is collected as shared_ptrs up
// front: a handler is allowed to ogui.remove() the very widget the walk is
// visiting, and a raw pointer would dangle before its parent got a turn.
void Kernel::dispatchEvent(Event& e) {
    if (!e.target) return;
    std::vector<Widget::Ptr> keep;
    std::vector<Widget*> chain;
    for (Widget* w = e.target; w; w = w->parent()) {
        chain.push_back(w);
        if (auto sp = w->weak_from_this().lock()) keep.push_back(std::move(sp));
    }
    const char* name = eventName(e.kind);
    for (Widget* w : chain) {
        e.currentTarget = w;
        w->onEvent(e);
        if (!w->id().empty() && m_script.fireListener(w->id(), name, e))
            e.stopped = true;
        if (!e.stopped) continue;
        if (m_debugEvents)
            addLog("event stop " + std::string(name) + " at " + describe(w));
        break;
    }
    e.currentTarget = nullptr;
}

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
    m_clickTarget = nullptr;
    m_pressButton = -1;
    m_prevDown = false;
    m_prevChar = 0;
    for (bool& k : m_prevKeys) k = false;
    m_bindings.clear();
    m_debug = 0;
    m_debugEvents = false;
    m_stats = FrameStats{};
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

    m_touchPrimary = -1;
    m_touchDown = false;
    m_touchParked = false;
    m_touchCancelled = false;
    m_touchX = m_touchY = 0.0f;
    m_rawMouseKnown = false;
    m_pointerIsTouch = false;

    m_panTarget = nullptr;
    m_panActive = false;
    m_panStart = m_panLast = Offset2D{};
    m_sampleHead = 0;
    m_sampleCount = 0;
    m_flingWidget = nullptr;
    m_flingVX = m_flingVY = 0.0f;

    m_popup.reset();
    m_tipFade = 0.0f;
    m_prevTime = 0.0f;
    m_dirty = true;
    m_treeGen = 1;
    m_indexGen = 0;
    m_idIndex.clear();
    m_prevInput = InputState{};
    m_builtW = m_builtH = -1.0f;
    m_builtScale = -1.0f;
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
    const auto t0 = std::chrono::steady_clock::now();
    update();
    m_stats.updateMs = std::chrono::duration<float, std::milli>(
                           std::chrono::steady_clock::now() - t0).count();
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
    // Widgets learn their kernel here as well as in advanceAnimations(), so a
    // tree mutated before the first frame still reaches the kernel: addChild()
    // and setId() have to invalidate the id index, and they can only do that
    // once they know which kernel to tell.
    forEachWidget(screen->widgets, [&](const Widget::Ptr& w) {
        if (w) w->setKernel(this);
    });
    invalidate();
}

void Kernel::pushScreen(std::shared_ptr<Screen> screen) {
    if (!screen) return;
    m_screenStack.push_back(screen);
    dropPointerState();
    setFocus(*this, m_focus, nullptr);
    reloadScreenScripts();
    invalidate();
}

void Kernel::popScreen() {
    if (m_screenStack.size() > 1) {
        m_screenStack.pop_back();
        dropPointerState();
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
            screen->scale = cur->scale;
        }
        m_screenStack.back() = screen;
        dropPointerState();
        setFocus(*this, m_focus, nullptr);
        reloadScreenScripts();
        invalidate();
    }
}

// The tree the pointer was walking is gone: everything cached against it goes
// with it. Focus has its own teardown (onFocus), a hover or a press does not,
// and a flick outlives the frame that started it, so it is the one that would
// otherwise keep scrolling a widget that no longer exists.
void Kernel::dropPointerState() {
    m_hovered = nullptr;
    m_captured = nullptr;
    m_pressed = nullptr;
    m_pressButton = -1;
    m_panTarget = nullptr;
    m_panActive = false;
    m_flingWidget = nullptr;
    m_flingVX = m_flingVY = 0.0f;
    m_holdWidget = nullptr;
    m_holdArmed = false;
    m_pinchWidget = nullptr;
    m_pinchPrevDist = 0.0f;
}

void Kernel::setTheme(const Theme* theme) { m_theme = theme; invalidate(); }

float Kernel::scale() const {
    auto screen = currentScreen();
    return screen ? saneScale(screen->scale) : 1.0f;
}

void Kernel::setScale(float s) {
    auto screen = currentScreen();
    if (!screen) return;
    const float v = saneScale(s);
    if (screen->scale == v) return;
    screen->scale = v;
    invalidate();
}

float Kernel::logicalWidth() const {
    auto screen = currentScreen();
    return screen ? screen->width / scale() : 0.0f;
}

float Kernel::logicalHeight() const {
    auto screen = currentScreen();
    return screen ? screen->height / scale() : 0.0f;
}

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
            w->refreshVisualState();
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
        m_popup->refreshVisualState();
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
    invalidate();
}

void Kernel::bind(const std::string& name, std::function<void()> fn) {
    // Stored by name; resolved at click time against the widget's binding
    // (onclick=) first, then its id. Widgets parsed later are covered too.
    m_bindings[name] = std::move(fn);
}

// Rebuild the id -> widget index over the current screen: one depth-first
// pass in document order, first writer of a duplicated id wins (matching the
// scan this replaced). Cheap enough to redo on every tree change, rare
// enough that doing it lazily from the lookup is the right trade.
void Kernel::rebuildIdIndex() const {
    m_idIndex.clear();
    auto screen = currentScreen();
    if (!screen) { m_indexGen = m_treeGen; return; }
    forEachWidget(screen->widgets, [&](const Widget::Ptr& w) {
        if (!w->id().empty()) m_idIndex.emplace(w->id(), w);
    });
    m_indexGen = m_treeGen;
}

Widget* Kernel::lookupWidget(const std::string& id) const {
    if (id.empty() || !currentScreen()) return nullptr;
    if (m_indexGen != m_treeGen) rebuildIdIndex();
    auto it = m_idIndex.find(id);
    if (it == m_idIndex.end()) return nullptr;
    // Weak value: a widget a handler dropped from the tree expires here
    // instead of handing back a dangling pointer.
    if (auto sp = it->second.lock()) return sp.get();
    return nullptr;
}

Widget* Kernel::findWidget(const std::string& id) { return lookupWidget(id); }

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
    Widget* found = lookupWidget(id);
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
// Widgets speak one pointer: a position, a button, press and release edges.
// A touch host speaks a list of fingers, so the list is folded into that
// pointer here and nowhere else: hit-testing, capture, click chaining and the
// Lua input queries then work under a finger without any widget having to know
// what a finger is, and a second finger cannot steal the drag the first one
// owns.
void Kernel::applyTouchStream() {
    const float rawX = m_input.mouseX;
    const float rawY = m_input.mouseY;
    const bool rawDown = m_input.mouseDown[0];
    const int rawBtn = m_input.mouseButton;
    // Anything new from the host about the mouse since the previous frame. A
    // touch-only host never moves these fields, which is how the bridge learns
    // that a machine with both has handed the pointer back to the mouse.
    const bool mouseSpoke = !m_rawMouseKnown || rawX != m_rawMouseX ||
                            rawY != m_rawMouseY || rawDown != m_rawMouseDown ||
                            rawBtn != m_rawMouseButton;
    m_rawMouseX = rawX; m_rawMouseY = rawY; m_rawMouseDown = rawDown;
    m_rawMouseButton = rawBtn; m_rawMouseKnown = true;

    if (m_input.touchCount > 0) {
        const int n = std::clamp(m_input.touchCount, 1,
                                 InputState::kMaxTouchPoints);
        int pick = -1;
        for (int i = 0; i < n; ++i) {
            if (m_touchDown && m_input.touches[i].id == m_touchPrimary) pick = i;
        }
        // Either the first finger of a sequence, or the one that owns the
        // pointer lifted and a finger that stayed down carries on with the
        // same press -- the release edge belongs to the drag, not to the id.
        if (pick < 0) pick = 0;
        m_touchDown = true;
        m_touchParked = false;
        m_pointerIsTouch = true;
        m_touchPrimary = m_input.touches[pick].id;
        m_touchX = m_input.touches[pick].x;
        m_touchY = m_input.touches[pick].y;
        m_input.mouseX = m_touchX;
        m_input.mouseY = m_touchY;
        m_input.mouseDown[0] = true;
        m_input.mouseButton = Mouse_Left;
        return;
    }

    if (m_touchDown) {
        // The last finger left. The release has to land where it left, on the
        // widget that was dragged there, so this one frame reports that
        // position with the button up.
        m_touchDown = false;
        m_touchParked = true;
        m_touchPrimary = -1;
        m_touchCancelled = m_input.touchCancelled;
        m_input.mouseX = m_touchX;
        m_input.mouseY = m_touchY;
        m_input.mouseDown[0] = false;
        m_input.mouseButton = Mouse_Left;
        return;
    }

    if (m_touchParked && mouseSpoke) {
        m_touchParked = false;
        m_pointerIsTouch = false;   // the mouse holds the pointer again
        return;
    }
    if (m_touchParked) {
        // Nothing on the screen and no mouse news: there is no pointer. Saying
        // so keeps the widget under the last tap from staying drawn as hovered,
        // which is what a finger does to a `hover:` state.
        m_input.mouseX = -1.0f;
        m_input.mouseY = -1.0f;
        m_input.mouseDown[0] = false;
        m_input.mouseButton = -1;
    }
}

// A finger that has travelled past the slop was never pressing: it was pulling
// the content. The scrollable ancestor that claims the direction takes the drag
// over, the widget that had the capture is released without being clicked, and
// from here on every frame hands the *pixel* displacement to onPointerScroll in
// that widget's own scrollUnit() steps -- which is also what the flick's inertia
// runs on, so a scrollable widget scrolls under a finger without new code.
//
// Only a finger does this. A mouse drag is a selection, a slider, a scrollbar
// thumb, and the slop rule would take all of that away.
void Kernel::updatePan(const Offset2D& p, bool down, bool pressedEdge,
                       bool releasedEdge) {
    if (pressedEdge) {
        // A new press stops a scroll in flight, the way putting a finger down
        // on a list stops it mid-flick.
        m_flingWidget = nullptr;
        m_flingVX = m_flingVY = 0.0f;
        m_panStart = p;
        m_panLast = p;
        m_panActive = false;
        m_panTarget = nullptr;
        m_sampleHead = 0;
        m_sampleCount = 0;
    }

    if (down) {
        m_samples[m_sampleHead].x = p.x;
        m_samples[m_sampleHead].y = p.y;
        m_samples[m_sampleHead].t = m_input.time;
        m_sampleHead = (m_sampleHead + 1) % 8;
        if (m_sampleCount < 8) ++m_sampleCount;
    }

    if (!m_pointerIsTouch) return;

    if (down && !m_panActive) {
        if (m_pinchWidget) return;   // a pinch owns the fingers, not a pan
        const float dx = p.x - m_panStart.x, dy = p.y - m_panStart.y;
        if (std::fabs(dx) <= kPanSlop && std::fabs(dy) <= kPanSlop) return;
        // The pressed widget first, then every ancestor: the one that is
        // actually scrolled by this direction and is not busy with its own drag
        // wins.
        for (Widget* w = m_pressed ? m_pressed : m_captured; w; w = w->parent()) {
            if (w->scrollUnit() == 0.0f) continue;
            if (!w->wantsPan(dx, dy)) continue;
            m_panTarget = w;
            m_panActive = true;
            m_panLast = p;
            if (m_captured) {
                m_captured->onPointerUp(static_cast<MouseButton>(
                    m_pressButton >= 0 ? m_pressButton : 0));
            }
            // Released, not clicked: the press became a scroll.
            m_captured = nullptr;
            m_pressed = nullptr;
            m_pressButton = -1;
            break;
        }
        if (!m_panActive) return;   // nothing claims this drag: keep pressing
    }

    if (m_panActive && down) {
        const float dx = p.x - m_panLast.x, dy = p.y - m_panLast.y;
        m_panLast = p;
        const float unit = m_panTarget ? m_panTarget->scrollUnit() : 0.0f;
        if (unit != 0.0f && (dx != 0.0f || dy != 0.0f)) {
            m_panTarget->onPointerScroll(dx / unit, dy / unit);
            m_dirty = true;
        }
    }

    if (releasedEdge) {
        if (!m_panActive || !m_panTarget) {
            m_panActive = false;
            return;
        }
        m_panActive = false;
        // The velocity over the last fraction of a second, not over the whole
        // drag: a flick is judged by how the finger was moving when it left, and
        // a drag that slowed to a stop before lifting must not fly.
        const int newest = (m_sampleHead + 7) % 8;
        int first = newest;
        for (int i = 1; i < m_sampleCount; ++i) {
            const int j = (m_sampleHead + 8 - i) % 8;
            if (m_samples[newest].t - m_samples[j].t > 0.1f) break;
            first = j;
        }
        const FingerSample& a = m_samples[first];
        const FingerSample& b = m_samples[newest];
        const float span = b.t - a.t;
        m_flingWidget = nullptr;
        if (span > 1e-4f) {
            m_flingVX = (b.x - a.x) / span;
            m_flingVY = (b.y - a.y) / span;
            const float speed = std::sqrt(m_flingVX * m_flingVX +
                                          m_flingVY * m_flingVY);
            if (speed >= kFlingStart) {
                m_flingWidget = m_panTarget;
                m_dirty = true;
            }
        }
        m_panTarget = nullptr;
    }
}

void Kernel::advanceFling(float dt) {
    if (!m_flingWidget || dt <= 0.0f) return;
    const float unit = m_flingWidget->scrollUnit();
    if (unit == 0.0f) {
        m_flingWidget = nullptr;
        return;
    }
    // Exponential decay: fast at the start, easing into a stop, which is what a
    // flick feels like; a constant friction would keep it crawling for seconds.
    const float keep = std::exp(-dt / kFlingTau);
    m_flingVX *= keep;
    m_flingVY *= keep;
    if (std::fabs(m_flingVX) < kFlingStop && std::fabs(m_flingVY) < kFlingStop) {
        m_flingWidget = nullptr;
        m_flingVX = m_flingVY = 0.0f;
        return;
    }
    m_flingWidget->onPointerScroll(m_flingVX * dt / unit, m_flingVY * dt / unit);
    m_dirty = true;
}

// A finger that stays where it landed is not a press waiting to be released:
// past the hold delay it is a long-press, which is how a touch screen opens a
// context menu at all. A mouse is never armed -- a held button on a desktop is
// the first frame of a selection, and right-click already has this job -- and a
// finger that travels is a drag, which the pan above owns.
void Kernel::updateLongPress(const Offset2D& p, bool down, bool pressedEdge,
                             bool releasedEdge) {
    if (pressedEdge) {
        m_holdArmed = m_pointerIsTouch;
        m_holdWidget = m_pressed;
        m_holdAt = p;
        m_holdStart = m_input.time;
    }
    if (!m_holdArmed) return;
    if (!down || releasedEdge || m_panActive || m_pinchWidget) {
        m_holdArmed = false;
        return;
    }
    if (std::fabs(p.x - m_holdAt.x) > kPanSlop ||
        std::fabs(p.y - m_holdAt.y) > kPanSlop) {
        m_holdArmed = false;
        return;
    }
    if (m_input.time - m_holdStart < kHoldSeconds) return;
    m_holdArmed = false;
    fireLongPress(m_holdWidget, p);
}

void Kernel::fireLongPress(Widget* w, const Offset2D& p) {
    if (!w) return;
    if (m_debugEvents) addLog("longPress " + describe(w));
    if (m_captured) {
        m_captured->onPointerUp(static_cast<MouseButton>(
            m_pressButton >= 0 ? m_pressButton : 0));
    }
    // Released, not clicked: the press became the gesture, so letting go has to
    // click nothing -- the same rule that keeps a pan from also being a tap.
    m_captured = nullptr;
    m_pressed = nullptr;
    m_pressButton = -1;
    m_holdWidget = nullptr;
    m_dirty = true;

    // What a finger came here for: the nearest ancestor's <contextmenu>, opened
    // at the finger exactly where a right-click would have put it.
    for (Widget* t = w; t != nullptr; t = t->parent()) {
        if (t->contextMenuName().empty()) continue;
        if (auto* cm = dynamic_cast<ContextMenu*>(
                findWidget(t->contextMenuName())))
            cm->openAt(p.x, p.y);
        break;
    }

    Event e;
    e.kind = EventKind::LongPress;
    e.source = EventSource::Pointer;
    e.target = w;
    e.point = p;
    dispatchEvent(e);
}

// Two fingers on the screen are one gesture, not two pointers: the pair's
// distance is measured every frame and the ratio between frames goes to the
// widget that claims it. Claiming takes the press away from the tree, so the
// fingers can slide freely without also dragging, panning or clicking anything,
// and the pan is out of the way for as long as the pinch holds.
void Kernel::updatePinch(float sc) {
    const int n = m_input.touchCount > InputState::kMaxTouchPoints
                      ? InputState::kMaxTouchPoints
                      : m_input.touchCount;
    if (!m_pointerIsTouch || sc <= 0.0f || n < 2 || !m_touchDown) {
        m_pinchWidget = nullptr;
        m_pinchPrevDist = 0.0f;
        return;
    }
    // The primary finger and the one that joined it: with three down, the extra
    // one is noise, and the pair the user put down first is the gesture.
    int a = -1, b = -1;
    for (int i = 0; i < n; ++i) {
        if (m_input.touches[i].id == m_touchPrimary) a = i;
    }
    if (a < 0) a = 0;
    for (int i = 0; i < n; ++i) {
        if (i != a) { b = i; break; }
    }
    if (b < 0) {
        m_pinchWidget = nullptr;
        m_pinchPrevDist = 0.0f;
        return;
    }
    const Offset2D fa{m_input.touches[a].x / sc, m_input.touches[a].y / sc};
    const Offset2D fb{m_input.touches[b].x / sc, m_input.touches[b].y / sc};
    const Offset2D pivot{(fa.x + fb.x) * 0.5f, (fa.y + fb.y) * 0.5f};
    const float dx = fb.x - fa.x, dy = fb.y - fa.y;
    const float dist = std::sqrt(dx * dx + dy * dy);

    if (!m_pinchWidget) {
        // Ask the widget under the primary finger, then each of its ancestors,
        // exactly the way the pan asks. Nobody claiming is a normal answer: the
        // two fingers then stay whatever the one-finger machine makes them.
        Widget* from = m_pressed ? m_pressed
                     : (m_captured ? m_captured : m_hovered);
        for (Widget* w = from; w != nullptr; w = w->parent()) {
            if (!w->wantsPinch()) continue;
            m_pinchWidget = w;
            m_holdArmed = false;
            if (m_captured) {
                m_captured->onPointerUp(static_cast<MouseButton>(
                    m_pressButton >= 0 ? m_pressButton : 0));
            }
            m_captured = nullptr;
            m_pressed = nullptr;
            m_pressButton = -1;
            break;
        }
        if (!m_pinchWidget) return;
        m_pinchPrevDist = dist;   // the frame the pair formed is not a zoom
        return;
    }

    if (m_pinchPrevDist <= 0.0f || dist <= 0.0f) {
        m_pinchPrevDist = dist;
        return;
    }
    const float factor = dist / m_pinchPrevDist;
    m_pinchPrevDist = dist;
    if (factor != 1.0f) {
        m_pinchWidget->onPinch(factor, pivot);
        m_dirty = true;
    }
}

void Kernel::update() {
    applyTouchStream();
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
            a.lastChar != b.lastChar || a.touchCount != b.touchCount ||
            a.touchCancelled != b.touchCancelled ||
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
    // The tree is laid out in logical units; screen->width/height are device
    // pixels, so the scale divides them here and nowhere else.
    const float sc = scale();
    const float lw = screen->width / sc;
    const float lh = screen->height / sc;
    for (const auto& w : screen->widgets) {
        if (!w) continue;
        w->resolveGeometry(lw, lh);
        w->layout(lw, lh);
    }

    // The host reports the pointer in device pixels; widgets see logical.
    const Offset2D p{m_input.mouseX / sc, m_input.mouseY / sc};
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
    // Two fingers first: a pair that claims the gesture takes the press with it,
    // and the one-finger machine below must not also read these moves as a pan.
    updatePinch(sc);
    updatePan(p, down, pressedEdge, releasedEdge);
    updateLongPress(p, down, pressedEdge, releasedEdge);
    Widget* moveTarget = m_panActive
                             ? nullptr
                             : ((popupHit && m_popup) ? popupHit
                                                      : (m_captured ? m_captured
                                                                    : m_hovered));
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
        // A pointer the system took away is not a click. The widget still gets
        // its release, because a slider thumb or a text selection has to let go
        // either way.
        const bool taken = m_touchCancelled;
        m_touchCancelled = false;
        if (m_captured) {
            m_captured->onPointerUp(static_cast<MouseButton>(
                m_pressButton >= 0 ? m_pressButton : 0));
        }
        // Click = press and release happened on the same widget, with the
        // left button (a right-click belongs to the context menu).
        if (!taken && m_pressed && m_pressed == m_hovered &&
            m_pressButton != Mouse_Right)
            clickWidget(m_pressed, p);
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
        // Tab drives focus traversal (P4 #24) unless the focused widget
        // claims the key for itself (consumesTab). Shift+Tab goes backwards.
        if (k == Key_Tab && now && !m_prevKeys[k] &&
            !(m_focus && m_focus->consumesTab())) {
            focusNext((mods & Mod_Shift) != 0);
            m_prevKeys[k] = now;
            continue;
        }
        if (now && !m_prevKeys[k]) {
            if (m_focus) m_focus->onKeyDown(static_cast<KeyCode>(k), mods);
        } else if (now && m_prevKeys[k] && m_input.keyRepeat[k]) {
            if (m_focus) m_focus->onKeyDown(static_cast<KeyCode>(k), mods);
        } else if (!now && m_prevKeys[k]) {
            if (m_focus) m_focus->onKeyUp(static_cast<KeyCode>(k));
        }
        m_prevKeys[k] = now;
    }

    // A flick outlives the finger that started it: the inertia runs off this
    // clock, not off input, so it keeps scrolling on frames where nothing moved.
    // Last in the frame, after the input was read, so a hand landing on the
    // screen stops the scroll in the very frame it lands.
    advanceFling(dt);

    m_prevDown = down;
}

// ---- drawing --------------------------------------------------------------
const DrawList& Kernel::buildDrawList() {
    const auto t0 = std::chrono::steady_clock::now();
    auto screen = currentScreen();
    if (!screen) {
        m_drawList.clear();
        return m_drawList;
    }
    const float sc = scale();
    const float logicalW = screen->width / sc;
    const float logicalH = screen->height / sc;
    if (screen->width != m_builtW || screen->height != m_builtH ||
        sc != m_builtScale) {
        m_dirty = true;   // host resized the viewport or changed the dpi scale
    }
    if (!m_dirty) {
        m_stats.drawMs = std::chrono::duration<float, std::milli>(
                             std::chrono::steady_clock::now() - t0).count();
        return m_drawList;   // nothing changed: reuse last frame
    }

    Rasterizer& r = m_raster;
    // The one scale hand-off: the rasterizer multiplies logical geometry into
    // device pixels as it emits, and the font atlases are baked at that same
    // factor so text stays crisp instead of being stretched.
    r.setScale(sc);
    FontManager::instance().setScale(sc);
    r.reset();
    r.beginFrame(Rect{0, 0, logicalW, logicalH});
    for (const auto& w : screen->widgets) {
        if (!w) continue;
        // Containers (Column/Row/ScrollView) position their children first.
        w->resolveGeometry(logicalW, logicalH);
        w->layout(logicalW, logicalH);
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
        const float lineH = std::max(textLineHeight(kTipFontSize, "sans"), 1.0f);
        const float bw = tw + kPad * 2.0f;
        const float bh = lineH + kPad * 2.0f;
        // Pointer and canvas are both logical here; the rasterizer scales the
        // box into device pixels when it emits it.
        const float mx = m_input.mouseX / sc;
        const float my = m_input.mouseY / sc;
        float tx = mx + 14.0f;
        float ty = my + 18.0f;
        if (tx + bw > logicalW) tx = mx - bw - 6.0f;
        if (ty + bh > logicalH) ty = my - bh - 6.0f;
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
        float y = logicalH - 8.0f;
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
            const Rect box{logicalW - bw - 8.0f, y - bh, bw, bh};
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

    if (m_debug) drawDebugOverlay(r);

    r.endFrame();
    // Swap instead of move: the rasterizer keeps the previous frame's
    // reserved vectors and refills them without reallocating.
    m_drawList.swap(r.drawList());
    r.drawList().clear();
    m_dirty = false;
    m_builtW = screen->width;
    m_builtH = screen->height;
    m_builtScale = sc;
    {
        std::vector<Widget*> order;
        collectFocusable(screen->widgets, order);
        m_stats.tabStops = order.size();
        m_stats.widgets = 0;
        forEachWidget(screen->widgets,
                      [&](const Widget::Ptr&) { ++m_stats.widgets; });
    }
    m_stats.vertices = m_drawList.vertices.size();
    m_stats.indices = m_drawList.indices.size();
    m_stats.cmds = m_drawList.cmds.size();
    m_stats.drawMs = std::chrono::duration<float, std::milli>(
                         std::chrono::steady_clock::now() - t0).count();
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
