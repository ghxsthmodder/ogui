// src/script.cpp — Lua 5.4 scripting backend for ogui screens.
//
// The Kernel owns one ScriptHost; parsed <script> elements route here via
// XmlParser::setScriptHandler. All Lua entry points use lua_pcall, so
// script errors are captured into errors() + Kernel::addLog and never
// unwind into the engine.
#include "ogui/script.hpp"
#include "ogui/kernel.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"

#include <fstream>
#include <sstream>
#include <cstring>

#ifdef OGUI_HAS_LUA
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#endif

namespace ogui {

namespace {

constexpr size_t kMaxErrors = 32;

#ifdef OGUI_HAS_LUA
// Registry slot holding the ScriptHost* for this state.
const char kHostKey = 0;

ScriptHost* hostOf(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, &kHostKey);
    auto* h = static_cast<ScriptHost*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return h;
}

Kernel* kernelOf(lua_State* L) {
    ScriptHost* h = hostOf(L);
    return h ? h->kernel() : nullptr;
}

// ---- ogui.* bindings ------------------------------------------------------

int l_exists(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    lua_pushboolean(L, k && k->findWidget(id) != nullptr);
    return 1;
}

// Numeric value accessor pair helpers (Slider/ProgressBar/SpinBox share the
// float setValue/value API but have no common base).
bool numValueGet(Widget* w, double& out) {
    if (auto* s = dynamic_cast<Slider*>(w))       { out = s->value(); return true; }
    if (auto* p = dynamic_cast<ProgressBar*>(w))  { out = p->value(); return true; }
    if (auto* b = dynamic_cast<SpinBox*>(w))      { out = b->value(); return true; }
    return false;
}

bool numValueSet(Widget* w, double v) {
    if (auto* s = dynamic_cast<Slider*>(w))       { s->setValue(static_cast<float>(v)); return true; }
    if (auto* p = dynamic_cast<ProgressBar*>(w))  { p->setValue(static_cast<float>(v)); return true; }
    if (auto* b = dynamic_cast<SpinBox*>(w))      { b->setValue(static_cast<float>(v)); return true; }
    return false;
}

bool checkedGet(Widget* w, double& out) {
    if (auto* c = dynamic_cast<Checkbox*>(w))     { out = c->checked(); return true; }
    if (auto* r = dynamic_cast<RadioButton*>(w))  { out = r->checked(); return true; }
    if (auto* t = dynamic_cast<TreeItem*>(w))     { out = t->checked(); return true; }
    if (auto* s = dynamic_cast<ToggleSwitch*>(w)) { out = s->state(); return true; }
    return false;
}

bool checkedSet(Widget* w, bool v) {
    if (auto* c = dynamic_cast<Checkbox*>(w))     { c->setChecked(v); return true; }
    if (auto* r = dynamic_cast<RadioButton*>(w))  { r->setChecked(v); return true; }
    if (auto* t = dynamic_cast<TreeItem*>(w))     { t->setChecked(v); return true; }
    if (auto* s = dynamic_cast<ToggleSwitch*>(w)) { s->setState(v); return true; }
    return false;
}

// Geometry attrs move/resize the widget and pin its spec to absolute so
// the next layout pass keeps the script's values.
bool geomGet(Widget* w, const char* attr, double& out) {
    const Rect& b = w->bounds();
    if (!std::strcmp(attr, "x"))      { out = b.x; return true; }
    if (!std::strcmp(attr, "y"))      { out = b.y; return true; }
    if (!std::strcmp(attr, "width"))  { out = b.w; return true; }
    if (!std::strcmp(attr, "height")) { out = b.h; return true; }
    return false;
}

bool geomSet(Widget* w, const char* attr, double v, Kernel* k) {
    Rect b = w->bounds();
    Rect spec = w->geometrySpec();
    if (!std::strcmp(attr, "x"))           { b.x = spec.x = static_cast<float>(v); }
    else if (!std::strcmp(attr, "y"))      { b.y = spec.y = static_cast<float>(v); }
    else if (!std::strcmp(attr, "width"))  { b.w = spec.w = static_cast<float>(v); }
    else if (!std::strcmp(attr, "height")) { b.h = spec.h = static_cast<float>(v); }
    else return false;
    w->setGeometrySpec(spec, Widget::GeomAbsolute, Widget::GeomAbsolute,
                       Widget::GeomAbsolute, Widget::GeomAbsolute);
    w->setBounds(b);
    k->invalidate();
    return true;
}

int l_get(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w) { lua_pushnil(L); return 1; }

    double num = 0.0;
    if (!std::strcmp(attr, "text")) {
        lua_pushstring(L, k->getText(id).c_str());
    } else if (!std::strcmp(attr, "visible")) {
        lua_pushboolean(L, w->isVisible());
    } else if (!std::strcmp(attr, "checked")) {
        if (checkedGet(w, num)) lua_pushboolean(L, num != 0.0);
        else lua_pushnil(L);
    } else if (!std::strcmp(attr, "value")) {
        if (numValueGet(w, num)) lua_pushnumber(L, num);
        else lua_pushnil(L);
    } else if (geomGet(w, attr, num)) {
        lua_pushnumber(L, num);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

int l_set(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w) { lua_pushboolean(L, 0); return 1; }

    bool ok = false;
    if (!std::strcmp(attr, "text")) {
        size_t len = 0;
        const char* s = luaL_checklstring(L, 3, &len);
        ok = k->setText(id, std::string(s, len));
        if (ok) k->invalidate();
    } else if (!std::strcmp(attr, "visible")) {
        w->setVisible(lua_toboolean(L, 3) != 0);
        ok = true;
    } else if (!std::strcmp(attr, "checked")) {
        ok = checkedSet(w, lua_toboolean(L, 3) != 0);
        if (ok) k->invalidate();
    } else if (!std::strcmp(attr, "value")) {
        ok = numValueSet(w, luaL_checknumber(L, 3));
        if (ok) k->invalidate();
    } else {
        ok = geomSet(w, attr, luaL_checknumber(L, 3), k);
    }
    if (!ok)
        luaL_error(L, "ogui.set: widget \"%s\" has no attribute \"%s\"", id, attr);
    lua_pushboolean(L, 1);
    return 1;
}

int l_click(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    if (!k) { lua_pushboolean(L, 0); return 1; }
    // C++ bindings first, then the widget's own handler.
    bool ok = k->invokeBinding(id);
    if (!ok)
        if (Widget* w = k->findWidget(id)) { w->click(); ok = true; }
    lua_pushboolean(L, ok);
    return 1;
}

int l_time(lua_State* L) {
    Kernel* k = kernelOf(L);
    lua_pushnumber(L, k ? k->time() : 0.0);
    return 1;
}

int l_log(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    if (Kernel* k = kernelOf(L)) k->addLog(std::string(s, len));
    return 0;
}

int l_toast(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    const char* kind = luaL_optstring(L, 2, "info");
    if (Kernel* k = kernelOf(L)) {
        auto kt = Kernel::ToastKind::Info;
        if (!std::strcmp(kind, "warn")) kt = Kernel::ToastKind::Warn;
        else if (!std::strcmp(kind, "error")) kt = Kernel::ToastKind::Error;
        k->showToast(std::string(s, len), kt);
    }
    return 0;
}

const luaL_Reg oguiLib[] = {
    {"set",    l_set},
    {"get",    l_get},
    {"exists", l_exists},
    {"click",  l_click},
    {"time",   l_time},
    {"log",    l_log},
    {"toast",  l_toast},
    {nullptr, nullptr},
};

struct ReaderBuf { const char* data; size_t left; };

const char* chunkReader(lua_State*, void* ud, size_t* size) {
    auto* b = static_cast<ReaderBuf*>(ud);
    if (b->left == 0) { *size = 0; return nullptr; }
    *size = b->left;
    const char* p = b->data;
    b->left = 0;
    return p;
}
#endif // OGUI_HAS_LUA

} // namespace

ScriptHost::ScriptHost() = default;
ScriptHost::~ScriptHost() { clear(); }

bool ScriptHost::available() {
#ifdef OGUI_HAS_LUA
    return true;
#else
    return false;
#endif
}

void ScriptHost::recordError(const std::string& msg) {
    m_errors.push_back(msg);
    if (m_errors.size() > kMaxErrors) m_errors.erase(m_errors.begin());
    if (m_kernel) m_kernel->addLog("script: " + msg);
}

void ScriptHost::ensureState() {
#ifdef OGUI_HAS_LUA
    if (m_L) return;
    m_L = luaL_newstate();
    if (!m_L) { recordError("cannot open Lua state"); return; }
    luaL_openlibs(m_L);
    lua_pushlightuserdata(m_L, this);
    lua_rawsetp(m_L, LUA_REGISTRYINDEX, &kHostKey);
    luaL_newlib(m_L, oguiLib);
    lua_setglobal(m_L, "ogui");
#endif
}

bool ScriptHost::loadString(const std::string& code, const std::string& name,
                            std::string* error) {
#ifndef OGUI_HAS_LUA
    (void)code; (void)name;
    const std::string msg = "ogui built without Lua (OGUI_USE_LUA=OFF)";
    recordError(msg);
    if (error) *error = msg;
    return false;
#else
    ensureState();
    if (!m_L) return false;
    ReaderBuf buf{code.c_str(), code.size()};
    std::string chunkName = name.empty() ? std::string("=script") : "@" + name;
    int st = lua_load(m_L, chunkReader, &buf, chunkName.c_str(), nullptr);
    if (st != LUA_OK) {
        std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "load error";
        lua_pop(m_L, 1);
        recordError(msg);
        if (error) *error = msg;
        return false;
    }
    st = lua_pcall(m_L, 0, 0, 0);
    if (st != LUA_OK) {
        std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "runtime error";
        lua_pop(m_L, 1);
        recordError(msg);
        if (error) *error = msg;
        return false;
    }
    notifyLoad();
    if (m_kernel) m_kernel->invalidate();
    return true;
#endif
}

bool ScriptHost::loadFile(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        const std::string msg = "script: cannot open " + path;
        recordError(msg);
        if (error) *error = msg;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadString(ss.str(), path, error);
}

void ScriptHost::clear() {
#ifdef OGUI_HAS_LUA
    if (m_L) { lua_close(m_L); m_L = nullptr; }
#endif
    m_errors.clear();
}

template <typename Push>
bool ScriptHost::callHook(const char* name, Push push) {
#ifdef OGUI_HAS_LUA
    if (!m_L) return true;   // nothing loaded yet: silently no-op
    lua_getglobal(m_L, name);
    if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return true; }
    push(m_L);
    const int nargs = static_cast<int>(lua_gettop(m_L)) - 1;
    if (lua_pcall(m_L, nargs, 0, 0) != LUA_OK) {
        std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "error";
        lua_pop(m_L, 1);
        recordError(std::string(name) + ": " + msg);
        return false;
    }
#else
    (void)name; (void)push;
#endif
    return true;
}

void ScriptHost::notifyLoad() {
    callHook("on_load", [](lua_State*) {});
}

void ScriptHost::notifyUpdate(float t, float dt) {
    callHook("on_update", [t, dt](lua_State* L) {
        lua_pushnumber(L, t);
        lua_pushnumber(L, dt);
    });
}

void ScriptHost::notifyClick(const std::string& key) {
    callHook("on_click", [&key](lua_State* L) { lua_pushstring(L, key.c_str()); });
}

void ScriptHost::notifyEvent(const std::string& name, const std::string& key) {
    callHook("on_event", [&name, &key](lua_State* L) {
        lua_pushstring(L, name.c_str());
        lua_pushstring(L, key.c_str());
    });
}

} // namespace ogui
