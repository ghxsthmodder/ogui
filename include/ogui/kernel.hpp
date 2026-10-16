#pragma once
#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/font.hpp"
#include "ogui/widget.hpp"
#include "ogui/drawlist.hpp"
#include "ogui/script.hpp"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <thread>

namespace ogui {

class Widget;
class Kernel;
struct InputState;

// ------------------------------------------------------------------
// Input state
// ------------------------------------------------------------------
// One finger on the screen. `id` is the host's pointer id -- Android's
// AMotionEvent pointer id, stable while that finger stays down -- and the
// position is in device pixels, like mouseX/mouseY.
struct TouchPoint {
    int id = 0;
    float x = 0.0f, y = 0.0f;
};

struct InputState {
    static constexpr int kMaxTouchPoints = 16;

    bool mouseDown[4] = {false, false, false, false};
    int mouseButton = -1;         // -1 = none
    float mouseX = 0.0f, mouseY = 0.0f;
    float scrollX = 0.0f, scrollY = 0.0f;
    bool keyDown[256] = {false};
    bool keyRepeat[256] = {false};
    uint32_t lastChar = 0;
    float time = 0.0f;            // seconds since start

    // Every pointer on the screen right now, in the order the host reports
    // them; a host without touch leaves touchCount at 0 and drives the mouse
    // fields, which is what a desktop does. The kernel folds this list into
    // the one pointer widgets see, so a widget does not have to know that
    // touch exists.
    TouchPoint touches[kMaxTouchPoints] = {};
    int touchCount = 0;
    // The system took the pointers away (Android's ACTION_CANCEL, a gesture the
    // OS claimed, an incoming call). Not a release: the drag ends and nothing
    // is clicked.
    bool touchCancelled = false;
};

// ------------------------------------------------------------------
// Input source (filled by the host app / game)
// ------------------------------------------------------------------
struct InputSource {
    using Callback = std::function<void(const InputState&)>;

    void setCallback(Callback cb) { m_cb = cb; }
    void send(const InputState& st) const { if (m_cb) m_cb(st); }

    std::function<void(const InputState&)> m_cb;
};

// ------------------------------------------------------------------
// Insets: the part of the window the system paints over
// ------------------------------------------------------------------
// An edge-to-edge phone hands the app its whole display and then draws a status
// bar, a navigation bar and, while a keyboard is up, the keyboard itself on top
// of whatever landed there. A layout is not wrong for having authored y="16" at
// the top of a window it was told is 2340 pixels tall; it is only in the wrong
// place. So the host reports the holes as four numbers in logical units and the
// tree is laid out in what is left: a percentage resolves against the safe
// width, and the whole tree moves into the safe rectangle when it is drawn and
// when a pointer is read against it. Widgets never see any of it -- they keep
// authoring from the corner of the area they were given, and the point a finger
// delivers arrives in the same coordinates, so a caret lands under the pixel
// that was tapped whether or not the window has bars.
//
// All four are >= 0 (a bar that would enlarge the canvas is a bug, not an
// inset) and 0 is what a desktop, a test and a phone with no bars say.
struct Insets {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
};

// ------------------------------------------------------------------
// Screen
// ------------------------------------------------------------------
struct Screen {
    std::string id;
    std::vector<std::shared_ptr<Widget>> widgets;
    Style style;
    float scale = 1.0f;
    float width = 800.0f;
    float height = 600.0f;

    // Lua <script> chunks collected at parse time ({payload, isFile}).
    // The Kernel clears its ScriptHost and runs these on every screen
    // change, so hooks and the ogui globals always belong to the
    // visible screen.
    std::vector<std::pair<std::string, bool>> scripts;

    Screen() = default;
    Screen(const std::string& id_) : id(id_) {}
};

// ------------------------------------------------------------------
// Kernel: controls a screen, input, theme & rasterizer
// ------------------------------------------------------------------
class Kernel {
public:
    Kernel();
    ~Kernel();

    // Must be called once before run().
    void init();

    // Run until requestQuit(). Each frame:
    //   1. deliver input to the screen
    //   2. update the widget tree (events, layout)
    //   3. build the draw list (drawTree) and hand it to the draw callback
    void run(const InputSource& input, const std::function<void()>& draw);

    // Single-step helpers for hosts that own their own loop.
    void frame(const InputState& input);

    void requestQuit() { m_quit = true; }
    bool quitRequested() const { return m_quit; }

    // ---- screen management ----
    void pushScreen(std::shared_ptr<Screen> screen);
    void popScreen();
    void switchScreen(std::shared_ptr<Screen> screen);

    std::shared_ptr<Screen> currentScreen() const {
        return m_screenStack.empty() ? nullptr : m_screenStack.back();
    }

    // ---- dpi / scale ----
    // `Screen::scale` is device pixels per logical unit. Everything a screen
    // authors (x/y/w/h, padding, font sizes), every widget bound, every
    // pointer position delivered to a widget and every text measurement lives
    // in those logical units; the draw list handed to the backends is in
    // device pixels. `Screen::width/height` keep the device size the host got
    // from the window, so logicalWidth()/logicalHeight() is the canvas the
    // tree actually lays out on. A non-positive scale is treated as 1.
    float scale() const;
    void setScale(float s);
    float logicalWidth() const;
    float logicalHeight() const;

    // ---- safe area ----
    // The insets are in logical units, the same canvas `logicalWidth()` reports
    // (a host that learned them in device pixels -- which is how Android
    // reports a bar -- divides by `scale()` on its way in). Setting them
    // invalidates, because the tree has to be laid out again for the new
    // rectangle, and a keyboard that comes and goes is the case where that
    // happens sixty times a second if the host is not careful.
    void setInsets(const Insets& in);
    Insets insets() const { return m_insets; }
    // Where the tree actually goes: the canvas minus the insets, with the
    // origin the tree is drawn at and a size that is never negative. Two insets
    // that together eat a dimension leave that dimension at 0 rather than
    // pretending the window is somewhere else, and the leading edge is the one
    // kept whole -- a status bar is a fact, while the trailing edge is the
    // keyboard, which is the half that grows.
    Rect safeRect() const;

    // ---- theme ----
    void setTheme(const Theme* theme);
    const Theme* theme() const { return m_theme; }

    // ---- query input ----
    void processInput(const InputState& st);

    // ---- events ----
    // Bind a handler to a widget id; fired on click (press+release on it).
    void bind(const std::string& id, std::function<void()> fn);
    // Fire a bound handler by name (menu commands route through this).
    // A name without a C++ binding also reaches the script's ogui_on_event
    // hook; the return value only reports the C++ binding (compat).
    bool invokeBinding(const std::string& name) {
        auto it = m_bindings.find(name);
        const bool ok = (it != m_bindings.end() && it->second);
        if (m_debugEvents) addLog("event " + name + (ok ? "" : " (unbound)"));
        if (ok) it->second();
        else m_script.notifyEvent(name, name);
        return ok;
    }
    // Set text on a widget by id (Label/Button/TextInput).
    bool setText(const std::string& id, const std::string& text);
    // Read text from a widget by id (TextInput/Label/Button).
    std::string getText(const std::string& id) const;

    // Depth-first traversal of the current screen's tree in draw order.
    // Lets widgets find related ones (e.g. radio group exclusivity)
    // without owning the tree.
    void eachWidget(const std::function<void(Widget*)>& fn) const;

    // ---- popups (dropdowns, menus) ----
    // A popup is a widget drawn above everything (tooltip aside) and given
    // priority when hit-testing. Its bounds are screen-absolute. A pointer
    // press outside it closes it and is swallowed. Managed by widgets like
    // ComboBox; usually called from a pointer handler.
    void openPopup(std::shared_ptr<Widget> popup) {
        m_popup = std::move(popup);
        invalidate();
    }
    void closePopup() {
        if (m_popup) m_popup->onPopupDismissed();
        m_popup.reset();
        invalidate();
    }
    bool popupOpen() const { return m_popup != nullptr; }
    std::shared_ptr<Widget> popup() const { return m_popup; }

    // ---- toast notifications ----
    // Bottom-right banners that expire on their own (3.5 s). Drawn above the
    // tree and the tooltip; they are never an input target.
    enum class ToastKind : uint8_t { Info = 0, Warn = 1, Error = 2 };
    void showToast(const std::string& text, ToastKind kind = ToastKind::Info) {
        m_toasts.push_back(ToastItem{text, static_cast<uint8_t>(kind), 0.0f});
        invalidate();
    }

    // ---- drawing ----
    // Builds the draw list for the current screen. Frames with unchanged
    // input, no running animation and no popup/tooltip/toast activity reuse
    // the previous list; call invalidate() after mutating the tree from
    // outside the event flow (setText, host timers, screen edits).
    // invalidate() also marks the id index stale: every tree mutation in the
    // library goes through here, so the next lookup rebuilds the index once
    // instead of scanning the tree per call.
    void invalidate() { m_dirty = true; ++m_treeGen; }
    const DrawList& buildDrawList();
    const DrawList& drawList() const { return m_drawList; }

    // ---- scripting (Lua) ----
    // The screen's script host: <script> elements parsed with the parser's
    // script handler route to loadScriptFile()/loadScriptString(). Hooks
    // (ogui_on_load/ogui_on_update/ogui_on_click/ogui_on_event) fire from the frame loop; see
    // ogui/script.hpp for the API surface.
    ScriptHost& script() { return m_script; }
    bool loadScriptFile(const std::string& path) {
        return m_script.loadFile(path);
    }
    bool loadScriptString(const std::string& code, const std::string& name = "script") {
        return m_script.loadString(code, name);
    }

    // Seconds since the first frame (InputState::time mirror).
    float time() const { return m_input.time; }

    // What the last frame was fed, with the touch stream folded in: the mouse
    // fields are the pointer the tree actually saw, not what the host sent.
    // Used by the Lua ogui.* input queries.
    const InputState& input() const { return m_input; }

    // ---- lookup ----
    // Id lookup over the current screen. Backed by an id -> widget index that
    // is rebuilt lazily, once, after the tree or the screen changed -- the
    // script layer resolves dozens of ids per frame, and a linear scan per
    // lookup used to make that quadratic in the size of the tree.
    Widget* findWidget(const std::string& id);
    Widget* focusedWidget() const { return m_focus; }
    Widget* hoveredWidget() const { return m_hovered; }
    // Keyboard focus traversal (P4 #24): Tab/Shift+Tab cycle through the
    // visible+enabled focusable widgets of the current screen in document
    // order. focusNext returns false when there is nothing focusable.
    bool focusNext(bool backwards = false);
    // Move the keyboard focus to an exact widget (nullptr clears it); fires
    // the same focus/blur script events as a click would.
    void setFocusWidget(Widget* w);
    // Run a widget's click path: named binding (onclick=), then the id
    // binding, then a C++ handler, else the Lua ogui_on_click hook.
    // Mouse release and Enter/Space on a focused button both go through here.
    // The action lookup stays on the pressed widget; the click event itself is
    // then dispatched (see dispatchEvent), so ancestors hear about it too.
    void clickWidget(Widget* w, const Offset2D& point = {},
                     EventSource source = EventSource::Pointer);
    // Route a semantic event: visit the target, then every ancestor up to the
    // screen root, calling Widget::onEvent and firing that widget's Lua
    // listeners at each stop. A listener that returns false in Lua, or calls
    // Event::stopPropagation in C++, ends the walk there. Raw mouse, wheel and
    // key input never travels this path — it goes to the hit-tested or focused
    // widget only.
    void dispatchEvent(Event& e);
    // Widget that currently holds the mouse press (null when nothing is
    // pressed); drives the "pressed" visual state.
    Widget* pressedWidget() const { return m_pressed; }
    // Drop cached pointers to a widget that is about to leave the tree
    // (script-driven ogui.remove). Call before the last shared_ptr dies.
    // Covers the whole subtree: a panel that is removed takes the focused
    // field inside it, the row being flicked and the tooltip being shown.
    void forgetWidget(Widget* w);
    // Last pointer position in logical screen coordinates (for per-region
    // hover effects inside one widget, e.g. SpinBox buttons).
    Offset2D mousePos() const {
        const float sc = scale();
        return {m_input.mouseX / sc, m_input.mouseY / sc};
    }

    // ---- pointer context (read inside onPointerDown) ----
    // Presses at the same spot within the double-click window count up:
    // 1 = single, 2 = double, 3 = triple. Modifier bits (see ModBits).
    int clickCount() const { return m_clickCount; }
    uint32_t mods() const { return currentMods(); }

    // ---- tooltip ----
    // True while the hover tooltip panel should be on screen.
    bool tooltipVisible() const;
    void setTooltipDelay(float seconds) { m_tipDelay = seconds; }
    float tooltipDelay() const { return m_tipDelay; }

    // ---- debug ----
    void addLog(const std::string& msg);
    // Forgetting the log also forgets which duplicated ids were already
    // reported, so a host that clears the console can be told again.
    void clearLogs() { m_logs.clear(); m_duplicateSeen.clear(); }
    const std::vector<std::string>& logs() const { return m_logs; }

    // An `id` that names more than one widget on the current screen. The
    // lookup takes the first in document order, so `shadowed` is the widget
    // that name never reaches — invisible while the id still resolves to
    // *something*. Each duplicated id is reported into logs() the first time
    // the index meets it; this is the same fact with the widgets in it, for a
    // host or a tool that would rather point than read text.
    struct DuplicateId {
        std::string id;
        Widget* first = nullptr;    // what findWidget(id) hands back
        Widget* shadowed = nullptr; // what the author also wrote
    };
    std::vector<DuplicateId> duplicateIds() const;

    // ---- inspection (P4 #27) ----
    // The current screen's tree as one line per widget, in draw order:
    //   Button btn_start 10,10 200x50  focusable binding=onStart
    // Bounds are absolute (parent offsets folded in) and in logical units, so
    // they can be read straight back into the XML. Empty with no screen.
    std::string dumpTree() const;
    // Position of a widget in the Tab order, -1 when it is not a tab stop.
    int tabOrder(const Widget* w) const;
    // Short "<Type> <id>" label for a widget ("none" for a null pointer);
    // what the event log prints.
    static std::string describe(const Widget* w);
    // The widget's C++ class name without the namespace ("Button").
    static std::string typeName(const Widget* w);

    // Overlay bits drawn on top of the tree by buildDrawList().
    enum DebugFlag : uint32_t {
        DebugBounds = 1u << 0,   // outline every widget rect
        DebugLabels = 1u << 1,   // type + id + size on each widget
        DebugFocus  = 1u << 2,   // tab-order number, ring on the focused one
        DebugHover  = 1u << 3,   // the widget that would take the next click
    };
    void setDebugOverlay(uint32_t flags) { m_debug = flags; invalidate(); }
    uint32_t debugOverlay() const { return m_debug; }
    void toggleDebugOverlay(uint32_t flag) {
        setDebugOverlay(m_debug ^ flag);
    }

    // What the last frame cost and how much it drew.
    struct FrameStats {
        float updateMs = 0.0f;      // input + events + layout
        float drawMs = 0.0f;        // draw list build (0 when reused)
        size_t widgets = 0;         // widgets visited in the last build
        size_t vertices = 0, indices = 0, cmds = 0;
        size_t tabStops = 0;
    };
    const FrameStats& stats() const { return m_stats; }

    // Route clicks, bindings and focus changes through addLog() so the
    // Console widget shows the event flow. Off by default: logging every
    // event would flood the history and the draw list.
    void setDebugEvents(bool on) { m_debugEvents = on; }
    bool debugEvents() const { return m_debugEvents; }

private:
    void update();
    // Fold InputState::touches into the one pointer the tree sees, and keep a
    // host that reports both honest about which one moved.
    void applyTouchStream();
    // The finger-pan state machine, called from update() with the pointer in
    // logical units and the edges the frame produced.
    void updatePan(const Offset2D& p, bool down, bool pressedEdge, bool releasedEdge);
    // Carry the velocity a flick left behind, decaying, through onPointerScroll.
    void advanceFling(float dt);
    // The hold state machine (see m_holdArmed) and the gesture it delivers.
    void updateLongPress(const Offset2D& p, bool down, bool pressedEdge,
                         bool releasedEdge);
    void fireLongPress(Widget* w, const Offset2D& p);
    // The two-finger scale, fed straight from InputState::touches. Takes the
    // dpi scale because the host reports raw fingers in device pixels.
    void updatePinch(float sc);
    // Forget every pointer cache when the screen changes: the tree they were
    // taken from may be gone with it.
    void dropPointerState();
    // The screen-change form of that: the open popup is dismissed first, so
    // its owner's callback still runs against a live tree. The caller has to
    // hold the outgoing screen across it, because dropping the focus announces
    // a Blur to the widget losing it and that widget lives in that screen.
    void dropScreenState();
    // Swap the ScriptHost to the current screen's scripts (clear + load).
    void reloadScreenScripts();
    // modifier bitmask derived from the current key state (see ModBits).
    uint32_t currentMods() const;
    void drawTree(Rasterizer& r, Widget* w, float ox, float oy) const;
    // Lay the current screen's tree out in the safe rectangle. Both passes --
    // the one that makes hit-testing honest before the first draw and the one
    // inside buildDrawList() -- go through here, because a tree laid out twice
    // with two different canvases is a tree whose second half of the frame is
    // in the wrong place.
    void layoutScreen();
    void advanceAnimations(float dt);
    // Widget the popup layer hit at the pointer (nullptr when closed/no hit).
    Widget* popupHitTest(const Offset2D& p) const;
    // Debug overlay: rects, labels, tab numbers and the hover target.
    void drawDebugOverlay(Rasterizer& r) const;

    std::vector<std::shared_ptr<Screen>> m_screenStack;
    const Theme* m_theme = nullptr;

    InputState m_input;
    // Mutable because a duplicated id is discovered by a const lookup, which is
    // the only place the tree is walked cheaply; see reportDuplicates().
    mutable std::vector<std::string> m_logs;
    bool m_quit = false;

    // event state
    Widget* m_focus = nullptr;
    Widget* m_hovered = nullptr;
    Widget* m_captured = nullptr;      // pointer owner between press/release
    Widget* m_pressed = nullptr;       // widget that received onPointerDown
    Widget* m_clickTarget = nullptr;   // widget inside clickWidget(), nulled
                                       // by forgetWidget() if it is removed
    int m_pressButton = -1;
    bool m_prevDown = false;
    uint32_t m_prevChar = 0;
    bool m_prevKeys[256] = {false};

    // multi-click detection (double/triple press at the same spot)
    int m_clickCount = 0;
    int m_lastClickBtn = -1;
    float m_lastClickTime = 0.0f;
    float m_lastClickX = 0.0f;
    float m_lastClickY = 0.0f;

    // touch -> pointer bridge (see applyTouchStream)
    int m_touchPrimary = -1;           // the id that owns the pointer now
    bool m_touchDown = false;          // a finger is on the screen
    bool m_touchParked = false;        // the finger left; the mouse has not spoken
    bool m_touchCancelled = false;     // it left because the system took it
    bool m_pointerIsTouch = false;     // the pointer the tree sees is a finger
    float m_touchX = 0.0f, m_touchY = 0.0f;
    float m_rawMouseX = 0.0f, m_rawMouseY = 0.0f;
    bool m_rawMouseDown = false;
    int m_rawMouseButton = -1;
    bool m_rawMouseKnown = false;

    // ---- finger pan + the inertia it leaves behind ----
    // A press that travels past kPanSlop is a drag of the content, not a press:
    // the scrollable ancestor that answers wantsPan takes over, the captured
    // widget is released without clicking, and every frame after that hands the
    // finger's own displacement to onPointerScroll in scrollUnit() steps. On
    // release the recent samples give the flick its velocity, and the same
    // channel keeps carrying it, decaying, until it is slow enough to stop.
    static constexpr float kPanSlop = 10.0f;        // logical units
    static constexpr float kFlingStart = 120.0f;    // logical units/second
    static constexpr float kFlingStop = 30.0f;      // logical units/second
    static constexpr float kFlingTau = 0.32f;       // seconds to decay to 1/e
    Widget* m_panTarget = nullptr;
    Offset2D m_panStart{};        // where the press began
    Offset2D m_panLast{};         // the previous frame's finger position
    bool m_panActive = false;
    struct FingerSample { float x = 0.0f, y = 0.0f, t = 0.0f; };
    FingerSample m_samples[8];    // the last fractions of a second of travel
    int m_sampleHead = 0;
    int m_sampleCount = 0;
    Widget* m_flingWidget = nullptr;
    float m_flingVX = 0.0f, m_flingVY = 0.0f;

    // ---- the two gestures a finger holds instead of moving ----
    // Held still past kHoldSeconds, a press stops being a press waiting for a
    // release and becomes a long-press: the widget's <contextmenu> opens at the
    // finger, the way a right-click opens it, "longPress" is routed, and the
    // press is consumed so letting go clicks nothing. Travelling past the pan
    // slop first disarms it, because a drag is a drag, and a mouse is never
    // armed at all -- a held button on a desktop is the start of a selection.
    static constexpr float kHoldSeconds = 0.5f;
    Widget* m_holdWidget = nullptr;
    Offset2D m_holdAt{};          // where the finger landed, logical units
    float m_holdStart = 0.0f;     // input clock at that moment
    bool m_holdArmed = false;

    // Two fingers: the pair's distance ratio between frames, to whoever claims
    // it. Armed by the second finger, so one finger never pinches, and finished
    // the moment the count falls back below two -- the surviving finger holds no
    // press, so it cannot click on its way out either.
    Widget* m_pinchWidget = nullptr;
    float m_pinchPrevDist = 0.0f;

    // tooltip hover timing
    Widget* m_tipTarget = nullptr;
    float m_tipTimer = 0.0f;
    float m_tipDelay = 0.5f;
    float m_tipPrevTime = 0.0f;
    float m_tipMouseX = 0.0f;
    float m_tipMouseY = 0.0f;

    std::map<std::string, std::function<void()>> m_bindings;
    ScriptHost m_script;        // Lua hooks + ogui.* API (see script.hpp)
    DrawList m_drawList;
    Rasterizer m_raster;          // persistent: keeps buffers between frames

    // redraw bookkeeping (see invalidate/buildDrawList)
    mutable bool m_dirty = true;   // mutable with m_logs: same reason
    size_t m_treeGen = 1;      // bumped by invalidate(): what the index lags

    // id -> widget over the current screen, rebuilt lazily the first lookup
    // after m_treeGen moves. Weak values mean a widget freed by a screen swap
    // can never be handed back as a live pointer.
    mutable std::unordered_map<std::string, std::weak_ptr<Widget>> m_idIndex;
    mutable size_t m_indexGen = 0;
    void rebuildIdIndex() const;
    // The walk that fills the index already knows when a name was taken, so
    // duplicated ids are found there rather than by a second pass. Weak for
    // the same reason the index is: a widget that leaves the tree drops out of
    // the answer instead of lingering as a pointer.
    struct DuplicateRecord {
        std::string id;
        std::weak_ptr<Widget> first;
        std::weak_ptr<Widget> shadowed;
    };
    mutable std::vector<DuplicateRecord> m_duplicates;
    // (id, widget) pairs already put in logs(). Keyed by the shadowed widget
    // rather than by the name so a second collision elsewhere is still new
    // news, while the same one reported by every rebuild is not: the index is
    // rebuilt after every tree change, and a duplicate is a fact about the
    // layout, not an event. `clearLogs()` forgets them, which is how a host
    // asks to be told again.
    mutable std::set<std::pair<std::string, const Widget*>> m_duplicateSeen;
    void reportDuplicates() const;
    Widget* lookupWidget(const std::string& id) const;
    InputState m_prevInput{};
    float m_builtW = -1.0f, m_builtH = -1.0f;   // screen size at last build
    float m_builtScale = -1.0f;                 // dpi scale at last build
    Insets m_insets;                            // see safeRect()
    uint32_t m_debug = 0;                       // DebugFlag bits
    bool m_debugEvents = false;                 // event flow into addLog()
    FrameStats m_stats{};

    // popup layer
    std::shared_ptr<Widget> m_popup;

    // tooltip fade-in timed from when it becomes visible
    float m_tipFade = 0.0f;

    // toast banners (see showToast)
    struct ToastItem {
        std::string text;
        uint8_t kind;
        float age;
    };
    std::vector<ToastItem> m_toasts;

    // frame clock (animations)
    float m_prevTime = 0.0f;
};

} // namespace ogui
