#pragma once
#include <string>
#include <vector>

// Lua's own typedef lives at global scope; forward-declare it there so
// ogui:: declarations do not shadow it.
struct lua_State;

namespace ogui {

class Kernel;
class Widget;

// ------------------------------------------------------------------
// ScriptHost: embedded Lua 5.4 behaviour for parsed screens.
//
// The engine owns the loop; scripts only react. A screen declares
// behaviour with <script src="file.lua"/> (or inline <script>code
// </script>); XmlParser forwards it through setScriptHandler(), which
// hosts wire to Kernel::loadScriptFile()/loadScriptString(). Loading
// immediately runs the chunk and calls its on_load() hook.
//
// Lua hooks (global functions, all optional; later loads override):
//   on_load()            once, right after the chunk runs
//   on_update(t, dt)     every frame (t = seconds since kernel start)
//   on_click(key)        left-click released on a widget whose C++
//                        binding/handler did not fire (onclick name,
//                        else id)
//   on_event(name, key)  an invokeBinding() name with no C++ binding
//
// API table `ogui` (all calls return nil/false when the id is unknown):
//   ogui.set(id, attr, v)    attr: text | visible | checked | value |
//                                    x | y | width | height
//   ogui.get(id, attr)       same attrs; text -> string, value -> number
//   ogui.exists(id)          -> bool
//   ogui.click(id)           fire the widget's binding/handler -> bool
//   ogui.time()              seconds since Kernel start
//   ogui.toast(msg[, kind])  kind: "info" (default) | "warn" | "error"
//   ogui.log(msg)            push to Kernel::addLog
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
    void notifyUpdate(float t, float dt);
    void notifyClick(const std::string& key);
    void notifyEvent(const std::string& name, const std::string& key);

    // Captured load/runtime errors, oldest first (capped at 32).
    const std::vector<std::string>& errors() const { return m_errors; }

    // Raw state for hosts extending the bindings (nullptr before any
    // load or without a Lua build). The ogui table lives in its globals.
    lua_State* state() { return m_L; }

private:
    void ensureState();
    void recordError(const std::string& msg);
    // Calls the global function `name` if it is a function; push() adds
    // the arguments. Errors are recorded, never thrown.
    template <typename Push>
    bool callHook(const char* name, Push push);

    lua_State* m_L = nullptr;
    Kernel* m_kernel = nullptr;
    std::vector<std::string> m_errors;
};

} // namespace ogui
