#pragma once
#include "ogui/tween.hpp"
#include <string>
#include <vector>

// Lua's own typedef lives at global scope; forward-declare it there so
// ogui:: declarations do not shadow it.
struct lua_State;

namespace ogui {

class Kernel;
class Widget;
struct Event;

// ------------------------------------------------------------------
// ScriptHost: embedded Lua 5.4 behaviour for parsed screens.
//
// The engine owns the loop; scripts only react. A screen declares
// behaviour with <script src="file.lua"/> (or inline <script>code
// </script>); XmlParser forwards it through setScriptHandler(), which
// hosts wire to Kernel::loadScriptFile()/loadScriptString(). Loading
// immediately runs the chunk and calls its ogui_on_load() hook.
//
// Lua hooks (global functions, all optional; later loads override). The
// ogui_on_ prefix is the naming convention: game code may share the Lua
// state, so bare names like on_update could collide with game globals.
//   ogui_on_load()            once, right after the chunk runs
//   ogui_on_show()            after the screen's chunks loaded, when it
//                             becomes the foreground screen
//   ogui_on_hide()            when the screen loses the foreground
//   ogui_on_unload()          right before its script state is destroyed
//                             (always after ogui_on_hide: screen state is
//                             not persisted across switches)
//   ogui_on_update(t, dt)     every frame (t = seconds since kernel start)
//   ogui_on_click(key)        left-click released on a widget whose C++
//                             binding/handler did not fire (onclick name,
//                             else id)
//   ogui_on_event(name, key)  an invokeBinding() name with no C++ binding
//
// API table `ogui` (all calls return nil/false when the id is unknown):
//   ogui.set(id, attr, v)    attr: text | visible | checked | value |
//                                    selected | tooltip |
//                                    x | y | width | height
//   ogui.get(id, attr)       same attrs; text/tooltip -> string, value/
//                            selected/geometry -> number, visible/checked
//                            -> boolean; unknown -> nil
//   ogui.exists(id)          -> bool
//   ogui.click(id)           fire the widget's binding/handler -> bool
//   ogui.time()              seconds since Kernel start
//   ogui.toast(msg[, kind])  kind: "info" (default) | "warn" | "error"
//   ogui.log(msg)            push to Kernel::addLog
//   ogui.mouse()             -> x, y          (last pointer position)
//   ogui.mouseDown([btn])    btn: 0|1|2 or "left"|"right"|"middle";
//                            no arg -> any button
//   ogui.key(name|code)      key down; name: "a".."z", "0".."9", "space",
//                            "enter", "escape", "tab", "backspace", "home",
//                            "end", "pageup", "pagedown", "insert",
//                            "delete", "left/right/up/down", "f1".."f12",
//                            "lshift/rshift/lctrl/rctrl/lalt/ralt/lgui/rgui"
//   ogui.mods()              modifier bitmask (Shift=1 Ctrl=2 Alt=4 Super=8)
//   ogui.focus()             focused widget id, or nil
//   ogui.hovered()           hovered widget id, or nil
//   ogui.clickCount()        presses at this spot in the double-click
//                            window (1 single, 2 double, ...)
//   ogui.items(id, table)    replace ListBox/OptionSpin entries with the
//                            Lua array of strings -> bool
//   ogui.tween(id, attr, to, dur[, ease[, delay]])
//                            animate a numeric attr (value | x | y |
//                            width | height) from its current value;
//                            ease: "linear" (default) | "out" | "inout" |
//                            "back". Engine-driven: runs even without
//                            ogui_on_update. A new tween on the same
//                            id+attr replaces the old one.
//   ogui.tweenStop([id[, attr]])  cancel matching tweens -> count
//   ogui.after(secs, fn)     one-shot timer -> id (advanced by the
//                            engine, fires outside any hook)
//   ogui.every(secs, fn)     repeating timer -> id (one fire per frame max)
//   ogui.cancel(id)          stop a timer -> bool
//   ogui.add(parentId, frag) parse a widget snippet and attach it under
//                            parentId -> first new id | nil, err
//   ogui.remove(id)          drop a widget (safe on the pressed one)
//   ogui.clear(id)           detach every child of a container -> count
//   ogui.children(id)        ordered array of child ids (nil for unknown id)
//   ogui.parent(id)          parent id, or nil at the screen root
//   ogui.index(id)           1-based position among siblings, or nil
//   ogui.on(id, event, fn)   attach a per-widget listener -> handle. Events
//                            currently fired: "click", "focus", "blur",
//                            "change" (user edits of a value/selection/text)
//                            and "submit" (Enter in a TextInput). Programmatic
//                            writes via ogui.set never fire them, so a listener
//                            that writes back cannot loop. The callback gets
//                            (id, info) with info.target, info.currentTarget,
//                            info.event, info.phase ("target" | "bubble"),
//                            info.source and info.x/y; a listener on a
//                            container id also hears its children's events as
//                            they bubble. Returning false stops the walk.
//   ogui.off(handle|id)      by handle -> bool; by widget id -> count of
//                            listeners dropped
//   ogui.bind(targetId, targetAttr, sourceId, sourceAttr[, twoWay])
//                            mirror source -> target now and on every source
//                            "change"/"submit"; twoWay adds the reverse
//                            mirror. Loop-safe: ogui.set is silent, so only
//                            user edits propagate. -> true
//   ogui.unbind(targetId[, targetAttr])  drop mirrors -> count removed
//   ogui.quit()              Kernel::requestQuit
//
// Runtime errors are captured (never thrown): recorded in errors() and
// logged to the kernel. The last 32 messages are kept.
// ------------------------------------------------------------------
class ScriptHost {
public:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    // False when ogui was built with OGUI_USE_LUA=OFF; every operation
    // then fails with a recorded error instead of running Lua.
    static bool available();

    // Kernel the scripts drive (used by every ogui.* call). The Kernel
    // binds itself on init; hosts may rebind for shared states.
    void bindKernel(Kernel* kernel) { m_kernel = kernel; }
    Kernel* kernel() const { return m_kernel; }

    // Run a chunk in the shared globals. On failure the error is recorded
    // and (when the state exists) previous scripts keep working; returns
    // false and fills *error when given.
    bool loadString(const std::string& code, const std::string& name,
                    std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);

    // Close the Lua state and drop all hooks/scripts.
    void clear();

    // ---- engine -> script (no-ops when the hook is not a function) ----
    void notifyLoad();
    void notifyShow();
    void notifyHide();
    void notifyUnload();
    void notifyUpdate(float t, float dt);
    void notifyClick(const std::string& key);
    void notifyEvent(const std::string& name, const std::string& key);

    // Captured load/runtime errors, oldest first (capped at 32).
    const std::vector<std::string>& errors() const { return m_errors; }

    // ---- script-driven tweens (ogui.tween/ogui.tweenStop) ----
    // Animate one numeric attribute of one widget; started from Lua,
    // advanced every frame by notifyUpdate (independent of hooks).
    bool startTween(const std::string& id, const std::string& attr,
                    float to, float dur, TweenEase ease, float delay);
    int stopTween(const std::string& id = "", const std::string& attr = "");
    size_t tweenCount() const { return m_tweens.size(); }

    // ---- script-driven timers (ogui.after/ogui.every/ogui.cancel) ----
    // Callbacks live in the Lua registry; they advance with the frame
    // clock and die with clear() (screen swaps never leak timers).
    size_t timerCount() const { return m_timers.size(); }

    // Timer bookkeeping used by the bindings: `ref` is a LUA_REGISTRYINDEX
    // reference to the callback (ownership moves to the host on success).
    // addTimer returns the new id, or -1 when invalid/no state.
    int addTimer(float interval, bool repeat, int ref);
    bool cancelTimer(int id);

    // ---- per-widget event listeners (ogui.on/ogui.off) ----
    // Callbacks live in the Lua registry; they die with clear() and are
    // dropped when the widget they target is removed via ogui.remove/clear.
    int addListener(const std::string& widgetId, const std::string& event, int ref);
    bool removeListener(int handle);
    int removeAllListeners(const std::string& widgetId);
    // Fire the listeners registered for one widget during an event walk. The
    // callback gets (id, info) and returns `false` to stop propagation, which
    // this reports back as true; anything else lets the walk continue.
    bool fireListener(const std::string& widgetId, const std::string& event,
                      const Event& e);
    size_t listenerCount() const { return m_listeners.size(); }

    // Raw state for hosts extending the bindings (nullptr before any
    // load or without a Lua build). The ogui table lives in its globals.
    lua_State* state() { return m_L; }

private:
    struct TweenJob {
        std::string id;
        std::string attr;
        Tween tw;
    };
    struct TimerJob {
        int id = 0;
        float interval = 0.0f;
        float elapsed = 0.0f;
        bool repeat = false;
        int ref = -1;           // LUA_REGISTRYINDEX reference to the callback
    };
    struct Listener {
        int handle = 0;
        std::string widgetId;
        std::string event;
        int ref = -1;           // LUA_REGISTRYINDEX reference to the callback
    };

    void ensureState();
    void recordError(const std::string& msg);
    void advanceTweens(float dt);
    void advanceTimers(float dt);
    // Calls the global function `name` if it is a function; push() adds
    // the arguments. Errors are recorded, never thrown.
    template <typename Push>
    bool callHook(const char* name, Push push);

    lua_State* m_L = nullptr;
    Kernel* m_kernel = nullptr;
    std::vector<std::string> m_errors;
    std::vector<TweenJob> m_tweens;
    std::vector<TimerJob> m_timers;
    int m_nextTimerId = 1;
    std::vector<Listener> m_listeners;
    int m_nextListenerHandle = 1;
};

} // namespace ogui
