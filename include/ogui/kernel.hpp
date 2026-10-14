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
#include <thread>

namespace ogui {

class Widget;
class Kernel;
struct InputState;

// ------------------------------------------------------------------
// Input state
// ------------------------------------------------------------------
struct InputState {
    bool mouseDown[4] = {false, false, false, false};
    int mouseButton = -1;         // -1 = none
    float mouseX = 0.0f, mouseY = 0.0f;
    float scrollX = 0.0f, scrollY = 0.0f;
    bool keyDown[256] = {false};
    bool keyRepeat[256] = {false};
    uint32_t lastChar = 0;
    float time = 0.0f;            // seconds since start
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
    void invalidate() { m_dirty = true; }
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

    // Snapshot of the raw input the last frame was fed (mouse position,
    // button/key arrays). Used by the Lua ogui.* input queries.
    const InputState& input() const { return m_input; }

    // ---- lookup ----
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
    void clickWidget(Widget* w, const Offset2D& point = {});
    // Widget that currently holds the mouse press (null when nothing is
    // pressed); drives the "pressed" visual state.
    Widget* pressedWidget() const { return m_pressed; }
    // Drop cached pointers to a widget that is about to leave the tree
    // (script-driven ogui.remove). Call before the last shared_ptr dies.
    void forgetWidget(Widget* w) {
        if (m_focus == w) m_focus = nullptr;
        if (m_hovered == w) m_hovered = nullptr;
        if (m_captured == w) m_captured = nullptr;
        if (m_pressed == w) m_pressed = nullptr;
        if (m_clickTarget == w) m_clickTarget = nullptr;
    }
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
    void clearLogs() { m_logs.clear(); }
    const std::vector<std::string>& logs() const { return m_logs; }

private:
    void update();
    // Swap the ScriptHost to the current screen's scripts (clear + load).
    void reloadScreenScripts();
    // modifier bitmask derived from the current key state (see ModBits).
    uint32_t currentMods() const;
    void drawTree(Rasterizer& r, Widget* w, float ox, float oy) const;
    void advanceAnimations(float dt);
    // Widget the popup layer hit at the pointer (nullptr when closed/no hit).
    Widget* popupHitTest(const Offset2D& p) const;

    std::vector<std::shared_ptr<Screen>> m_screenStack;
    const Theme* m_theme = nullptr;

    InputState m_input;
    std::vector<std::string> m_logs;
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
    bool m_dirty = true;
    InputState m_prevInput{};
    float m_builtW = -1.0f, m_builtH = -1.0f;   // screen size at last build
    float m_builtScale = -1.0f;                 // dpi scale at last build

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
