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
#include "ogui/xmlparser.hpp"
#include "ogui/style.hpp"

#include <fstream>
#include <sstream>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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
    w->clampToBounds();
    k->invalidate();
    return true;
}

// "selected": the current index of the choice widgets (list-like and
// option-cycling ones share the concept, not a base class).
bool selGet(Widget* w, int& out) {
    if (auto* c = dynamic_cast<ComboBox*>(w))    { out = c->selectedIndex(); return true; }
    if (auto* l = dynamic_cast<ListBox*>(w))     { out = l->selectedIndex(); return true; }
    if (auto* t = dynamic_cast<Table*>(w))       { out = t->selectedIndex(); return true; }
    if (auto* v = dynamic_cast<TreeView*>(w))    { out = v->selectedIndex(); return true; }
    if (auto* tt = dynamic_cast<TreeTable*>(w))  { out = tt->selectedIndex(); return true; }
    if (auto* o = dynamic_cast<OptionSpin*>(w))  { out = o->index(); return true; }
    return false;
}

bool selSet(Widget* w, int v) {
    if (auto* c = dynamic_cast<ComboBox*>(w))    { c->setSelectedIndex(v); return true; }
    if (auto* l = dynamic_cast<ListBox*>(w))     { l->setSelectedIndex(v); return true; }
    if (auto* t = dynamic_cast<Table*>(w))       { t->setSelectedIndex(v); return true; }
    if (auto* tv = dynamic_cast<TreeView*>(w))   { tv->setSelectedIndex(v); return true; }
    if (auto* tt = dynamic_cast<TreeTable*>(w))  { tt->setSelectedIndex(v); return true; }
    if (auto* o = dynamic_cast<OptionSpin*>(w))  { o->setIndex(v); return true; }
    return false;
}

// Numeric attributes the tween engine can drive.
bool attrGetNum(Widget* w, const std::string& attr, double& out) {
    if (attr == "value") return numValueGet(w, out);
    return geomGet(w, attr.c_str(), out);
}

bool attrSetNum(Widget* w, const std::string& attr, double v, Kernel* k) {
    if (attr == "value") {
        if (!numValueSet(w, v)) return false;
        k->invalidate();
        return true;
    }
    return geomSet(w, attr.c_str(), v, k);
}

// Key names for ogui.key(): single letters/digits are handled directly.
int keyFromName(const char* s) {
    static const std::pair<const char*, int> kNames[] = {
        {"space", Key_Space},       {"enter", Key_Enter},
        {"escape", Key_Escape},     {"tab", Key_Tab},
        {"backspace", Key_Backspace}, {"delete", Key_Delete},
        {"insert", Key_Insert},     {"home", Key_Home},
        {"end", Key_End},           {"pageup", Key_PageUp},
        {"pagedown", Key_PageDown}, {"left", Key_Left},
        {"right", Key_Right},       {"up", Key_Up},
        {"down", Key_Down},         {"lshift", Key_LSHIFT},
        {"rshift", Key_RSHIFT},     {"lctrl", Key_LCTRL},
        {"rctrl", Key_RCTRL},       {"lalt", Key_LALT},
        {"ralt", Key_RALT},         {"lgui", Key_LGUI},
        {"rgui", Key_RGUI},         {"f1", Key_F1},   {"f2", Key_F2},
        {"f3", Key_F3},             {"f4", Key_F4},   {"f5", Key_F5},
        {"f6", Key_F6},             {"f7", Key_F7},   {"f8", Key_F8},
        {"f9", Key_F9},             {"f10", Key_F10}, {"f11", Key_F11},
        {"f12", Key_F12},
    };
    if (s[0] && !s[1]) {
        if (s[0] >= 'a' && s[0] <= 'z') return Key_A + (s[0] - 'a');
        if (s[0] >= '0' && s[0] <= '9') return Key_0 + (s[0] - '0');
    }
    for (const auto& [name, code] : kNames)
        if (!std::strcmp(s, name)) return code;
    return -1;
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
    } else if (!std::strcmp(attr, "enabled")) {
        lua_pushboolean(L, w->isEnabled());
    } else if (!std::strcmp(attr, "checked")) {
        if (checkedGet(w, num)) lua_pushboolean(L, num != 0.0);
        else lua_pushnil(L);
    } else if (!std::strcmp(attr, "value")) {
        if (numValueGet(w, num)) lua_pushnumber(L, num);
        else lua_pushnil(L);
    } else if (!std::strcmp(attr, "selected")) {
        int idx = 0;
        if (selGet(w, idx)) lua_pushnumber(L, idx);
        else lua_pushnil(L);
    } else if (!std::strcmp(attr, "tooltip")) {
        lua_pushstring(L, w->tooltip().c_str());
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
    } else if (!std::strcmp(attr, "enabled")) {
        w->setEnabled(lua_toboolean(L, 3) != 0);
        ok = true;
    } else if (!std::strcmp(attr, "checked")) {
        ok = checkedSet(w, lua_toboolean(L, 3) != 0);
        if (ok) k->invalidate();
    } else if (!std::strcmp(attr, "value")) {
        ok = numValueSet(w, luaL_checknumber(L, 3));
        if (ok) k->invalidate();
    } else if (!std::strcmp(attr, "selected")) {
        ok = selSet(w, static_cast<int>(luaL_checkinteger(L, 3)));
        if (ok) k->invalidate();
    } else if (!std::strcmp(attr, "tooltip")) {
        size_t len = 0;
        const char* s = luaL_checklstring(L, 3, &len);
        w->setTooltip(std::string(s, len));
        ok = true;
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

// ---- input / context queries ----------------------------------------------

int l_mouse(lua_State* L) {
    Kernel* k = kernelOf(L);
    const Offset2D p = k ? k->mousePos() : Offset2D{};
    lua_pushnumber(L, p.x);
    lua_pushnumber(L, p.y);
    return 2;
}

int l_mousedown(lua_State* L) {
    Kernel* k = kernelOf(L);
    if (!k) { lua_pushboolean(L, 0); return 1; }
    const InputState& in = k->input();
    int btn = -1;
    if (lua_isnumber(L, 1)) btn = static_cast<int>(lua_tointeger(L, 1));
    else if (lua_isstring(L, 1)) {
        const char* s = lua_tostring(L, 1);
        if (!std::strcmp(s, "left")) btn = Mouse_Left;
        else if (!std::strcmp(s, "right")) btn = Mouse_Right;
        else if (!std::strcmp(s, "middle")) btn = Mouse_Middle;
        else return luaL_error(L, "ogui.mouseDown: unknown button \"%s\"", s);
    }
    bool down = false;
    if (btn >= 0) down = btn < 4 && in.mouseDown[btn];
    else down = in.mouseDown[0] || in.mouseDown[1] ||
                in.mouseDown[2] || in.mouseDown[3];
    lua_pushboolean(L, down);
    return 1;
}

int l_key(lua_State* L) {
    Kernel* k = kernelOf(L);
    if (!k) { lua_pushboolean(L, 0); return 1; }
    int code = -1;
    if (lua_isnumber(L, 1)) code = static_cast<int>(lua_tointeger(L, 1));
    else code = keyFromName(luaL_checkstring(L, 1));
    if (code < 0 || code >= 256)
        return luaL_error(L, "ogui.key: unknown key");
    lua_pushboolean(L, k->input().keyDown[code]);
    return 1;
}

int l_mods(lua_State* L) {
    Kernel* k = kernelOf(L);
    lua_pushinteger(L, k ? static_cast<lua_Integer>(k->mods()) : 0);
    return 1;
}

// Push the id of `w`, or nil when there is no widget / no id.
void pushWidgetId(lua_State* L, Widget* w) {
    if (!w || w->id().empty()) lua_pushnil(L);
    else lua_pushstring(L, w->id().c_str());
}

int l_focus(lua_State* L) {
    Kernel* k = kernelOf(L);
    pushWidgetId(L, k ? k->focusedWidget() : nullptr);
    return 1;
}

int l_hovered(lua_State* L) {
    Kernel* k = kernelOf(L);
    pushWidgetId(L, k ? k->hoveredWidget() : nullptr);
    return 1;
}

int l_clickcount(lua_State* L) {
    Kernel* k = kernelOf(L);
    lua_pushinteger(L, k ? k->clickCount() : 0);
    return 1;
}

// ---- list content ----------------------------------------------------------

int l_items(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w) { lua_pushboolean(L, 0); return 1; }
    auto* lb = dynamic_cast<ListBox*>(w);
    auto* os = dynamic_cast<OptionSpin*>(w);
    if (!lb && !os)
        return luaL_error(L, "ogui.items: widget \"%s\" has no item list", id);
    std::vector<std::string> items;
    const lua_Integer n = luaL_len(L, 2);
    items.reserve(static_cast<size_t>(n));
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, 2, i);
        items.push_back(luaL_tolstring(L, -1, nullptr));  // also stringifies
        lua_pop(L, 2);   // the tolstring pushes its own copy
    }
    if (lb) lb->setItems(std::move(items));
    else os->setOptions(std::move(items));
    if (k) k->invalidate();
    lua_pushboolean(L, 1);
    return 1;
}

// ogui.list(id, op, ...) — incremental (virtualised) row mutation for a
// ListBox/<virtuallist>. ops: count | get(i) | insert(i,text|-1=append) |
// remove(i) | set(i,text) | scroll(i). count/get return a value; the others
// return true on success, false when the index/widget is out of range.
int l_list(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* op = luaL_checkstring(L, 2);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    auto* lb = dynamic_cast<ListBox*>(w);
    if (!lb) return luaL_error(L, "ogui.list: widget \"%s\" is not a list", id);
    if (!std::strcmp(op, "count")) {
        lua_pushinteger(L, lb->count());
        return 1;
    }
    if (!std::strcmp(op, "get")) {
        const int i = static_cast<int>(luaL_checkinteger(L, 3));
        std::string out;
        if (lb->itemAt(i, out)) lua_pushlstring(L, out.c_str(), out.size());
        else lua_pushnil(L);
        return 1;
    }
    if (!std::strcmp(op, "insert")) {
        const int i = static_cast<int>(luaL_checkinteger(L, 3));
        size_t len = 0;
        const char* s = luaL_checklstring(L, 4, &len);
        lb->insertItem(i, std::string(s, len));
        lua_pushboolean(L, 1);
    } else if (!std::strcmp(op, "remove")) {
        const int i = static_cast<int>(luaL_checkinteger(L, 3));
        const int before = lb->count();
        lb->removeItem(i);
        lua_pushboolean(L, lb->count() < before);
    } else if (!std::strcmp(op, "set")) {
        const int i = static_cast<int>(luaL_checkinteger(L, 3));
        size_t len = 0;
        const char* s = luaL_checklstring(L, 4, &len);
        lua_pushboolean(L, lb->setItem(i, std::string(s, len)));
    } else if (!std::strcmp(op, "scroll")) {
        const int i = static_cast<int>(luaL_checkinteger(L, 3));
        lb->scrollIntoView(i);
        lua_pushboolean(L, 1);
    } else {
        return luaL_error(L, "ogui.list: unknown op \"%s\" "
                             "(count|get|insert|remove|set|scroll)", op);
    }
    k->invalidate();
    return 1;
}

// ogui.prop(id, op, ...) — PropertyGrid model access.
//   count            -> number of items
//   get(name)        -> value string (nil when the name is unknown)
//   set(name, value) -> bool; programmatic, silent, validated per kind
//   add(name, type, value[, group[, options]]) -> bool; creates the item
//                  (type: bool|number|text|enum|color, options ";"-separated)
//   remove(name)     -> bool
//   list()           -> table {name = value, ...}
//   selected / select(i)
int l_prop(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* op = luaL_checkstring(L, 2);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    auto* pg = dynamic_cast<PropertyGrid*>(w);
    if (!pg)
        return luaL_error(L, "ogui.prop: widget \"%s\" is not a property grid", id);
    if (!std::strcmp(op, "count")) {
        lua_pushinteger(L, pg->count());
        return 1;
    }
    if (!std::strcmp(op, "get")) {
        const char* name = luaL_checkstring(L, 3);
        if (pg->indexOf(name) < 0) lua_pushnil(L);
        else {
            const std::string v = pg->valueOf(name);
            lua_pushlstring(L, v.c_str(), v.size());
        }
        return 1;
    }
    if (!std::strcmp(op, "list")) {
        lua_newtable(L);
        for (int i = 0; i < pg->count(); ++i) {
            const PropertyItem* it = pg->itemAt(i);
            if (!it) continue;
            lua_pushlstring(L, it->value().c_str(), it->value().size());
            lua_setfield(L, -2, it->name().c_str());
        }
        return 1;
    }
    if (!std::strcmp(op, "set")) {
        size_t len = 0;
        const char* v = luaL_checklstring(L, 4, &len);
        lua_pushboolean(L, pg->setValue(luaL_checkstring(L, 3),
                                        std::string(v, len)));
    } else if (!std::strcmp(op, "add")) {
        auto item = std::make_shared<PropertyItem>();
        item->setName(luaL_checkstring(L, 3));
        item->setKind(PropertyItem::kindFromName(luaL_optstring(L, 4, "text")));
        item->setValue(luaL_optstring(L, 5, ""));
        item->setGroup(luaL_optstring(L, 6, ""));
        if (!lua_isnoneornil(L, 7)) {
            const std::string opts = luaL_checkstring(L, 7);
            std::vector<std::string> list;
            size_t start = 0;
            while (start <= opts.size()) {
                const size_t semi = opts.find(';', start);
                if (semi == std::string::npos) {
                    if (start < opts.size()) list.push_back(opts.substr(start));
                    break;
                }
                if (semi > start) list.push_back(opts.substr(start, semi - start));
                start = semi + 1;
            }
            item->setOptions(list);
        }
        pg->addItem(item);
        lua_pushboolean(L, 1);
    } else if (!std::strcmp(op, "remove")) {
        lua_pushboolean(L, pg->removeItem(luaL_checkstring(L, 3)));
    } else if (!std::strcmp(op, "selected")) {
        lua_pushinteger(L, pg->selectedIndex());
        return 1;
    } else if (!std::strcmp(op, "select")) {
        pg->setSelectedIndex(static_cast<int>(luaL_checkinteger(L, 3)));
        lua_pushboolean(L, 1);
    } else {
        return luaL_error(L, "ogui.prop: unknown op \"%s\" "
                             "(count|get|set|add|remove|list|selected|select)",
                          op);
    }
    k->invalidate();
    return 1;
}

// ---- tweens ----------------------------------------------------------------

int l_tween(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    const float to = static_cast<float>(luaL_checknumber(L, 3));
    const float dur = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    const char* ease = luaL_optstring(L, 5, "linear");
    const float delay = static_cast<float>(luaL_optnumber(L, 6, 0.0));
    auto* h = hostOf(L);
    TweenEase e = TweenEase::Linear;
    if (!std::strcmp(ease, "out")) e = TweenEase::EaseOutCubic;
    else if (!std::strcmp(ease, "inout")) e = TweenEase::EaseInOutCubic;
    else if (!std::strcmp(ease, "back")) e = TweenEase::EaseOutBack;
    else if (std::strcmp(ease, "linear"))
        return luaL_error(L, "ogui.tween: unknown ease \"%s\" "
                             "(linear|out|inout|back)", ease);
    lua_pushboolean(L, h && h->startTween(id, attr, to, dur, e, delay));
    return 1;
}

int l_tweenstop(lua_State* L) {
    const char* id = luaL_optstring(L, 1, "");
    const char* attr = luaL_optstring(L, 2, "");
    auto* h = hostOf(L);
    lua_pushinteger(L, h ? h->stopTween(id, attr) : 0);
    return 1;
}

// ---- timers ----------------------------------------------------------------

int addTimer(lua_State* L, bool repeat) {
    const float secs = static_cast<float>(luaL_checknumber(L, 1));
    luaL_checktype(L, 2, LUA_TFUNCTION);
    auto* h = hostOf(L);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);   // consumes arg 2
    const int id = h ? h->addTimer(secs, repeat, ref) : -1;
    if (id <= 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, id);
    }
    return 1;
}

int l_after(lua_State* L)  { return addTimer(L, false); }
int l_every(lua_State* L)  { return addTimer(L, true); }

int l_cancel(lua_State* L) {
    const int id = static_cast<int>(luaL_checkinteger(L, 1));
    auto* h = hostOf(L);
    lua_pushboolean(L, h && h->cancelTimer(id));
    return 1;
}

int l_quit(lua_State* L) {
    if (Kernel* k = kernelOf(L)) k->requestQuit();
    return 0;
}

// ---- dynamic widgets --------------------------------------------------------

// Recursive kernel hand-off so a just-added subtree can be reached (findWidget,
// hover/focus APIs) before the next frame's walk assigns it.
void adoptKernel(Widget* w, Kernel* k) {
    w->setKernel(k);
    for (const auto& c : w->children())
        if (c) adoptKernel(c.get(), k);
}

// ogui.add(parentId, xmlFragment) -> first new id | nil, err
int l_add(lua_State* L) {
    const char* parentId = luaL_checkstring(L, 1);
    size_t fragLen = 0;
    const char* frag = luaL_checklstring(L, 2, &fragLen);
    Kernel* k = kernelOf(L);
    if (!k) { lua_pushnil(L); lua_pushliteral(L, "no kernel"); return 2; }
    Widget* parent = k->findWidget(parentId);
    if (!parent) {
        lua_pushnil(L);
        lua_pushstring(L, ("unknown parent: " + std::string(parentId)).c_str());
        return 2;
    }

    XmlParser p;
    XmlParser::ParseResult res = p.parse("<screen>" + std::string(frag, fragLen) + "</screen>",
                                          &activeTheme());
    if (!res.errors.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, res.errors.front().c_str());
        return 2;
    }
    if (!res.root) { lua_pushnil(L); lua_pushliteral(L, "empty fragment"); return 2; }

    std::vector<Widget::Ptr> kids = res.root->takeChildren();
    std::string firstId;
    for (auto& c : kids) {
        if (c && firstId.empty() && !c->id().empty()) firstId = c->id();
        if (c) adoptKernel(c.get(), k);
        parent->addChild(std::move(c));
    }
    k->invalidate();
    lua_pushstring(L, firstId.c_str());
    return 1;
}

// ogui.remove(id) -> true | nil, err
int l_remove(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w) {
        lua_pushnil(L);
        lua_pushstring(L, ("unknown widget: " + std::string(id)).c_str());
        return 2;
    }
    Widget* parent = w->parent();
    if (!parent) {
        lua_pushnil(L);
        lua_pushliteral(L, "cannot remove parentless widget");
        return 2;
    }
    // Nulls m_focus/m_hovered/m_captured/m_pressed if this widget is one of them
    // (e.g. a handler removing the button it was pressed on), so the shared_ptr
    // erase below cannot leave the kernel pointing at freed memory.
    k->forgetWidget(w);
    if (ScriptHost* h = hostOf(L)) h->removeAllListeners(id);
    parent->removeChild(w);
    k->invalidate();
    lua_pushboolean(L, 1);
    return 1;
}

// ogui.clear(id) -> count of removed children
int l_clear(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w) { lua_pushinteger(L, 0); return 1; }
    ScriptHost* h = hostOf(L);
    std::vector<Widget::Ptr> kids = w->takeChildren();
    for (const auto& c : kids)
        if (c) {
            k->forgetWidget(c.get());
            if (h && !c->id().empty()) h->removeAllListeners(c->id());
        }
    k->invalidate();
    lua_pushinteger(L, static_cast<int>(kids.size()));
    return 1;
}

// ---- tree navigation -------------------------------------------------------

// ogui.children(id) -> array of child ids (top level of a screen returns nil)
int l_children(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w) { lua_pushnil(L); return 1; }
    lua_newtable(L);
    int n = 0;
    for (const auto& c : w->children()) {
        if (c) {
            lua_pushinteger(L, ++n);
            lua_pushstring(L, c->id().c_str());
            lua_settable(L, -3);
        }
    }
    return 1;
}

// ogui.parent(id) -> parent id, or nil at the screen root / unknown id
int l_parent(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w || !w->parent()) { lua_pushnil(L); return 1; }
    lua_pushstring(L, w->parent()->id().c_str());
    return 1;
}

// ogui.index(id) -> 1-based position among siblings, or nil
int l_index(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    Kernel* k = kernelOf(L);
    Widget* w = k ? k->findWidget(id) : nullptr;
    if (!w || !w->parent()) { lua_pushnil(L); return 1; }
    int pos = 0;
    for (const auto& c : w->parent()->children()) {
        ++pos;
        if (c.get() == w) { lua_pushinteger(L, pos); return 1; }
    }
    lua_pushnil(L);
    return 1;
}

// ---- per-widget event listeners --------------------------------------------

// ogui.on(id, event, fn) -> handle | nil
int l_on(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* event = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    ScriptHost* h = hostOf(L);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);   // consumes arg 3
    const int handle = h ? h->addListener(id, event, ref) : -1;
    if (handle <= 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, handle);
    }
    return 1;
}

// ogui.off(handle) -> bool;  ogui.off(id) -> count removed
int l_off(lua_State* L) {
    ScriptHost* h = hostOf(L);
    if (!h) { lua_pushboolean(L, 0); return 1; }
    if (lua_type(L, 1) == LUA_TNUMBER) {
        lua_pushboolean(L, h->removeListener(static_cast<int>(lua_tointeger(L, 1))));
    } else {
        const char* id = luaL_checkstring(L, 1);
        lua_pushinteger(L, h->removeAllListeners(id));
    }
    return 1;
}

const luaL_Reg oguiLib[] = {
    {"set",        l_set},
    {"get",        l_get},
    {"exists",     l_exists},
    {"click",      l_click},
    {"time",       l_time},
    {"log",        l_log},
    {"toast",      l_toast},
    {"mouse",      l_mouse},
    {"mouseDown",  l_mousedown},
    {"key",        l_key},
    {"mods",       l_mods},
    {"focus",      l_focus},
    {"hovered",    l_hovered},
    {"clickCount", l_clickcount},
    {"items",      l_items},
    {"list",       l_list},
    {"prop",       l_prop},
    {"tween",      l_tween},
    {"tweenStop",  l_tweenstop},
    {"after",      l_after},
    {"every",      l_every},
    {"cancel",     l_cancel},
    {"quit",       l_quit},
    {"add",        l_add},
    {"remove",     l_remove},
    {"clear",      l_clear},
    {"children",   l_children},
    {"parent",     l_parent},
    {"index",      l_index},
    {"on",         l_on},
    {"off",        l_off},
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

// The data-binding layer, layered on top of the C `ogui` table so the
// reactive model lives where the widgets do and needs no C++ bookkeeping.
//   ogui.bind(targetId, targetAttr, sourceId, sourceAttr[, bidirectional])
//   ogui.unbind(targetId[, targetAttr])
// A bind mirrors source -> target immediately and, from then on, whenever
// the source reports a change (or submits). Because programmatic writes
// (ogui.set) are silent and only user interaction fires the "change" event,
// mirrors cannot loop; a two-way bind adds the reverse mirror.
static const char* kBootstrap = R"BOOTSRC(
local bindings = {}    -- { {targetId,targetAttr,sourceId,sourceAttr}, ... }
local watched  = {}    -- sourceId -> true once its listeners are attached
local guard    = nil   -- target id currently being written (blocks echo)

local function fire(sourceId)
  for _, b in ipairs(bindings) do
    if b.sourceId == sourceId and guard ~= b.targetId then
      guard = b.targetId
      ogui.set(b.targetId, b.targetAttr, ogui.get(sourceId, b.sourceAttr))
      guard = nil
    end
  end
end

local function watch(sourceId)
  if watched[sourceId] then return end
  watched[sourceId] = true
  ogui.on(sourceId, "change", function() fire(sourceId) end)
  ogui.on(sourceId, "submit", function() fire(sourceId) end)
end

function ogui.bind(targetId, targetAttr, sourceId, sourceAttr, bidirectional)
  targetAttr = targetAttr or "value"
  sourceAttr = sourceAttr or "value"
  table.insert(bindings, { targetId = targetId, targetAttr = targetAttr,
                           sourceId = sourceId, sourceAttr = sourceAttr })
  guard = targetId
  ogui.set(targetId, targetAttr, ogui.get(sourceId, sourceAttr))
  guard = nil
  watch(sourceId)
  if bidirectional then
    table.insert(bindings, { targetId = sourceId, targetAttr = sourceAttr,
                             sourceId = targetId, sourceAttr = targetAttr })
    watch(targetId)
  end
  return true
end

function ogui.unbind(targetId, targetAttr)
  local removed = 0
  for i = #bindings, 1, -1 do
    local b = bindings[i]
    if b.targetId == targetId and (not targetAttr or b.targetAttr == targetAttr) then
      table.remove(bindings, i)
      removed = removed + 1
    end
  end
  return removed
end
)BOOTSRC";

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
    // Layer the pure-Lua binding helpers onto the ogui table.
    if (luaL_loadstring(m_L, kBootstrap) == LUA_OK) {
        if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
            std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "error";
            lua_pop(m_L, 1);
            recordError("bootstrap: " + msg);
        }
    } else {
        lua_pop(m_L, 1);
        recordError("bootstrap: cannot compile");
    }
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
    m_tweens.clear();
    m_timers.clear();   // registry refs die with the state in clear()
    m_listeners.clear();
}

bool ScriptHost::startTween(const std::string& id, const std::string& attr,
                            float to, float dur, TweenEase ease, float delay) {
#ifdef OGUI_HAS_LUA
    if (!m_kernel) return false;
    Widget* w = m_kernel->findWidget(id);
    if (!w) return false;
    double from = 0.0;
    if (!attrGetNum(w, attr, from)) return false;
    for (auto it = m_tweens.begin(); it != m_tweens.end(); ++it)
        if (it->id == id && it->attr == attr) { m_tweens.erase(it); break; }
    TweenJob job;
    job.id = id;
    job.attr = attr;
    job.tw.start(static_cast<float>(from), to, dur, ease, delay);
    m_tweens.push_back(std::move(job));
    return true;
#else
    (void)id; (void)attr; (void)to; (void)dur; (void)ease; (void)delay;
    return false;
#endif
}

int ScriptHost::stopTween(const std::string& id, const std::string& attr) {
    const size_t before = m_tweens.size();
    for (auto it = m_tweens.begin(); it != m_tweens.end(); )
        it = (id.empty() || it->id == id) && (attr.empty() || it->attr == attr)
                 ? m_tweens.erase(it) : std::next(it);
    return static_cast<int>(before - m_tweens.size());
}

void ScriptHost::advanceTweens(float dt) {
#ifdef OGUI_HAS_LUA
    if (m_tweens.empty()) return;
    for (auto it = m_tweens.begin(); it != m_tweens.end(); ) {
        it->tw.update(dt);
        Widget* w = m_kernel ? m_kernel->findWidget(it->id) : nullptr;
        const bool alive = w && attrSetNum(w, it->attr, it->tw.value(), m_kernel);
        if (!alive || !it->tw.running()) it = m_tweens.erase(it);
        else ++it;
    }
#else
    (void)dt;
#endif
}

int ScriptHost::addTimer(float interval, bool repeat, int ref) {
#ifdef OGUI_HAS_LUA
    if (!m_L || interval <= 0.0f || ref == LUA_NOREF) return -1;
    TimerJob job;
    job.id = m_nextTimerId++;
    job.interval = interval;
    job.repeat = repeat;
    job.ref = ref;
    m_timers.push_back(job);
    return job.id;
#else
    (void)interval; (void)repeat; (void)ref;
    return -1;
#endif
}

bool ScriptHost::cancelTimer(int id) {
    for (auto it = m_timers.begin(); it != m_timers.end(); ++it)
        if (it->id == id) {
#ifdef OGUI_HAS_LUA
            if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, it->ref);
#endif
            m_timers.erase(it);
            return true;
        }
    return false;
}

void ScriptHost::advanceTimers(float dt) {
#ifdef OGUI_HAS_LUA
    if (!m_L || m_timers.empty()) return;
    // Copy the due callbacks out before calling: a callback may legally
    // cancel or create timers, which must not invalidate this iteration.
    struct Due { int ref; bool repeat; };
    std::vector<Due> due;
    for (auto it = m_timers.begin(); it != m_timers.end(); ) {
        it->elapsed += dt;
        if (it->elapsed >= it->interval) {
            due.push_back({it->ref, it->repeat});
            it->elapsed = 0.0f;   // one fire per frame max
            if (!it->repeat) { m_timers.erase(it); continue; }
        }
        ++it;
    }
    for (const Due& d : due) {
        lua_rawgeti(m_L, LUA_REGISTRYINDEX, d.ref);
        if (!d.repeat) luaL_unref(m_L, LUA_REGISTRYINDEX, d.ref);
        if (lua_pcall(m_L, 0, 0, 0) != LUA_OK) {
            std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "error";
            lua_pop(m_L, 1);
            recordError("timer: " + msg);
        }
    }
#else
    (void)dt;
#endif
}

// ---- per-widget event listeners --------------------------------------------

int ScriptHost::addListener(const std::string& widgetId, const std::string& event, int ref) {
#ifdef OGUI_HAS_LUA
    if (!m_L || ref == LUA_NOREF || widgetId.empty() || event.empty()) return -1;
    Listener l;
    l.handle = m_nextListenerHandle++;
    l.widgetId = widgetId;
    l.event = event;
    l.ref = ref;
    m_listeners.push_back(l);
    return l.handle;
#else
    (void)widgetId; (void)event; (void)ref;
    return -1;
#endif
}

bool ScriptHost::removeListener(int handle) {
    for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it)
        if (it->handle == handle) {
#ifdef OGUI_HAS_LUA
            if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, it->ref);
#endif
            m_listeners.erase(it);
            return true;
        }
    return false;
}

int ScriptHost::removeAllListeners(const std::string& widgetId) {
    int n = 0;
    for (auto it = m_listeners.begin(); it != m_listeners.end(); ) {
        if (it->widgetId == widgetId) {
#ifdef OGUI_HAS_LUA
            if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, it->ref);
#endif
            it = m_listeners.erase(it);
            ++n;
        } else ++it;
    }
    return n;
}

void ScriptHost::fireEvent(const std::string& widgetId, const std::string& event) {
#ifdef OGUI_HAS_LUA
    if (!m_L || m_listeners.empty()) return;
    // Copy the matching callbacks out before calling: a handler may legally
    // ogui.off() (or even ogui.remove the widget), which must not invalidate
    // this iteration.
    std::vector<int> refs;
    for (const Listener& l : m_listeners)
        if (l.widgetId == widgetId && l.event == event) refs.push_back(l.ref);
    for (int ref : refs) {
        lua_rawgeti(m_L, LUA_REGISTRYINDEX, ref);
        lua_pushstring(m_L, widgetId.c_str());
        if (lua_pcall(m_L, 1, 0, 0) != LUA_OK) {
            std::string msg = lua_tostring(m_L, -1) ? lua_tostring(m_L, -1) : "error";
            lua_pop(m_L, 1);
            recordError("event " + event + ": " + msg);
        }
    }
#else
    (void)widgetId; (void)event;
#endif
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
    callHook("ogui_on_load", [](lua_State*) {});
}

void ScriptHost::notifyShow() {
    callHook("ogui_on_show", [](lua_State*) {});
}

void ScriptHost::notifyHide() {
    callHook("ogui_on_hide", [](lua_State*) {});
}

void ScriptHost::notifyUnload() {
    callHook("ogui_on_unload", [](lua_State*) {});
}

void ScriptHost::notifyUpdate(float t, float dt) {
    advanceTweens(dt);   // engine-side: run even with no ogui_on_update hook
    advanceTimers(dt);
    callHook("ogui_on_update", [t, dt](lua_State* L) {
        lua_pushnumber(L, t);
        lua_pushnumber(L, dt);
    });
}

void ScriptHost::notifyClick(const std::string& key) {
    callHook("ogui_on_click", [&key](lua_State* L) { lua_pushstring(L, key.c_str()); });
    fireEvent(key, "click");
}

void ScriptHost::notifyEvent(const std::string& name, const std::string& key) {
    callHook("ogui_on_event", [&name, &key](lua_State* L) {
        lua_pushstring(L, name.c_str());
        lua_pushstring(L, key.c_str());
    });
}

} // namespace ogui
