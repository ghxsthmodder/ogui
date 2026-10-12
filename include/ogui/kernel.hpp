#pragma once
#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/font.hpp"
#include "ogui/widget.hpp"
#include "ogui/drawlist.hpp"
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

    // ---- theme ----
    void setTheme(const Theme* theme);
    const Theme* theme() const { return m_theme; }

    // ---- query input ----
    void processInput(const InputState& st);

    // ---- events ----
    // Bind a handler to a widget id; fired on click (press+release on it).
    void bind(const std::string& id, std::function<void()> fn);
    // Fire a bound handler by name (menu commands route through this).
    bool invokeBinding(const std::string& name) {
        auto it = m_bindings.find(name);
        if (it == m_bindings.end() || !it->second) return false;
        it->second();
        return true;
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
    void openPopup(std::shared_ptr<Widget> popup);
    void closePopup() {
        if (m_popup) m_popup->onPopupDismissed();
        m_popup.reset();
    }
    bool popupOpen() const { return m_popup != nullptr; }
    std::shared_ptr<Widget> popup() const { return m_popup; }

    // ---- toast notifications ----
    // Bottom-right banners that expire on their own (3.5 s). Drawn above the
    // tree and the tooltip; they are never an input target.
    enum class ToastKind : uint8_t { Info = 0, Warn = 1, Error = 2 };
    void showToast(const std::string& text, ToastKind kind = ToastKind::Info) {
        m_toasts.push_back(ToastItem{text, static_cast<uint8_t>(kind), 0.0f});
    }

    // ---- drawing ----
    // Builds the draw list for the current screen (idempotent per frame).
    const DrawList& buildDrawList();
    const DrawList& drawList() const { return m_drawList; }

    // ---- lookup ----
    Widget* findWidget(const std::string& id);
    Widget* focusedWidget() const { return m_focus; }
    Widget* hoveredWidget() const { return m_hovered; }
    // Last pointer position in screen coordinates (for per-region hover
    // effects inside one widget, e.g. SpinBox buttons).
    Offset2D mousePos() const { return {m_input.mouseX, m_input.mouseY}; }

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
    const std::vector<std::string>& logs() const { return m_logs; }

private:
    void update();
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
    DrawList m_drawList;

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
