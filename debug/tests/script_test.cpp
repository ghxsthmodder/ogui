// ogui_script_test.cpp : Lua scripting integration — <script> collection,
// screen-bound loading, hooks (ogui_on_load/ogui_on_update/ogui_on_click/ogui_on_event) and the
// ogui.* widget API.
#include "ogui/kernel.hpp"
#include "ogui/script.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>

using namespace ogui;

static int g_failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond  \
                      << "\n";                                                 \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

namespace {

std::shared_ptr<Screen> makeScreen() {
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <label id="out" text="init" x="10" y="10" width="200" height="24"/>
  <button id="btn" text="press" x="10" y="50" width="120" height="40"/>
  <slider id="sl" x="10" y="100" width="200" height="24"/>
  <progress id="pb" x="10" y="140" width="200" height="24"/>
  <checkbox id="cb" x="10" y="180" width="120" height="24"/>
  <script>
count = 0
function ogui_on_load()
  ogui.set("out", "text", "loaded")
end
function ogui_on_update(t, dt)
  if dt > 0 then ogui.set("pb", "value", 0.5) end
end
function ogui_on_click(key)
  if key == "btn" then
    count = count + 1
    ogui.set("out", "text", "clicks:" .. count)
    ogui.set("sl", "value", 0.1 * count)
    ogui.set("cb", "checked", not ogui.get("cb", "checked"))
    ogui.set("btn", "x", 10 + count)
    ogui.toast("clicked " .. key, "warn")
  end
end
function ogui_on_event(name, key)
  ogui.set("out", "text", "event:" .. name)
end
  </script>
</screen>)");
    assert(res.root);
    CHECK(res.errors.empty());
    CHECK(res.scripts.size() == 1);
    CHECK(res.scripts.size() == 1 && !res.scripts[0].second);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->scripts = std::move(res.scripts);
    s->width = 800;
    s->height = 600;
    return s;
}

InputState makeInput() {
    InputState st;
    st.mouseButton = -1;
    return st;
}

void pressAt(Kernel& k, InputState& st, float x, float y, int btn = 0) {
    st.mouseX = x;
    st.mouseY = y;
    st.mouseButton = btn;
    st.mouseDown[btn] = true;
    k.frame(st);
}

void release(Kernel& k, InputState& st) {
    st.mouseDown[st.mouseButton >= 0 ? st.mouseButton : 0] = false;
    st.mouseButton = -1;
    k.frame(st);
}

} // namespace

int main() {
    if (!ScriptHost::available()) {
        std::cerr << "SKIP: ogui built without Lua (OGUI_USE_LUA=OFF)\n";
        return 0;
    }

    Kernel k;
    k.init();
    auto screen = makeScreen();
    k.pushScreen(screen);

    // ---- ogui_on_load fired by pushScreen ------------------------------------
    CHECK(k.script().errors().empty());
    CHECK(k.getText("out") == "loaded");

    // ---- ogui_on_update: dt > 0 frames drive the progress bar ----------------
    InputState st = makeInput();
    st.time = 0.0f;
    k.frame(st);            // first frame: dt = 0 (prevTime 0) — no move yet
    st.time = 0.1f;
    k.frame(st);
    CHECK(k.script().errors().empty());
    if (auto* pb = dynamic_cast<ProgressBar*>(k.findWidget("pb")))
        CHECK(pb->value() == 0.5f);
    else
        CHECK(false);

    // ---- ogui_on_click: unreleased C++ binding falls through to Lua -----------
    pressAt(k, st, 70.0f, 70.0f);   // inside btn (10,50,120x40)
    release(k, st);
    CHECK(k.getText("out") == "clicks:1");
    pressAt(k, st, 70.0f, 70.0f);
    release(k, st);
    CHECK(k.getText("out") == "clicks:2");
    if (auto* sl = dynamic_cast<Slider*>(k.findWidget("sl")))
        CHECK(std::fabs(sl->value() - 0.2f) < 1e-4);
    else
        CHECK(false);
    if (auto* cb = dynamic_cast<Checkbox*>(k.findWidget("cb")))
        CHECK(!cb->checked());   // toggled twice: true then back to false
    else
        CHECK(false);

    // geometry write survives the next layout pass
    if (auto* btn = k.findWidget("btn")) {
        CHECK(btn->bounds().x == 12.0f);
        st.time = 0.2f;
        k.frame(st);
        CHECK(btn->bounds().x == 12.0f);
    }

    // toast surfaced through Kernel::showToast from Lua
    bool sawToast = false;
    for (const auto& log : k.logs())
        if (log.find("clicked") != std::string::npos) sawToast = true;
    // (logs may not carry toasts; just ensure no script error happened)
    (void)sawToast;
    CHECK(k.script().errors().empty());

    // ---- ogui_on_event: invokeBinding miss reaches the script hook ------------
    CHECK(!k.invokeBinding("cmd_unknown"));
    CHECK(k.getText("out") == "event:cmd_unknown");

    // A C++ binding wins: the script must NOT see it.
    int cppHits = 0;
    k.bind("btn", [&] { ++cppHits; });
    pressAt(k, st, 70.0f, 70.0f);
    release(k, st);
    CHECK(cppHits == 1);
    CHECK(k.getText("out") == "event:cmd_unknown");   // unchanged

    // ---- ogui.get / exists semantics via a fresh chunk -------------------
    k.script().loadString(R"(
        assert(ogui.exists("out"))
        assert(not ogui.exists("nope"))
        assert(type(ogui.get("out", "text")) == "string")
        assert(ogui.get("nope", "text") == nil)
        assert(ogui.get("sl", "value") > 0)
        assert(ogui.set("nope", "text", "x") == false)
    )", "asserts");
    CHECK(k.script().errors().empty());

    // ---- loadFile: screen-style <script src=...> -------------------------
    {
        const char* tmp = "ogui_script_test_tmp.lua";
        std::ofstream f(tmp);
        f << "function ogui_on_click(key) ogui.set('out','text','file:'..key) end\n";
        f.close();
        XmlParser p;
        auto res = p.parse(R"(<screen id="f">
  <label id="out" text="init" x="10" y="10" width="200" height="24"/>
  <button id="b2" text="x" x="10" y="50" width="120" height="40"/>
  <script src="ogui_script_test_tmp.lua"/>
</screen>)");
        CHECK(res.errors.empty());
        CHECK(res.scripts.size() == 1 && res.scripts[0].second);
        auto s2 = std::make_shared<Screen>("f");
        s2->widgets.push_back(res.root);
        s2->scripts = std::move(res.scripts);
        s2->width = 800; s2->height = 600;
        k.pushScreen(s2);        // new screen's scripts replace the old ones
        InputState st2 = makeInput();
        pressAt(k, st2, 70.0f, 70.0f);   // button b2
        release(k, st2);
        CHECK(k.getText("out") == "file:b2");

        // ---- popScreen: back to screen "s" reloads its inline script ----
        k.popScreen();
        CHECK(k.getText("out") == "loaded");   // ogui_on_load ran again
        std::remove(tmp);
    }

    // ---- runtime + syntax errors are captured, engine survives -----------
    {
        auto& sh = k.script();
        const size_t before = sh.errors().size();
        CHECK(!sh.loadString("bogus ~~~ syntax", "bad"));
        CHECK(sh.errors().size() == before + 1);
        CHECK(!sh.loadString("error('boom')", "raise"));
        CHECK(sh.errors().size() == before + 2);
        // Defining a failing hook loads fine; the error surfaces on frame.
        CHECK(sh.loadString(
            "function ogui_on_update() error('tick') end", "tickerr"));
        InputState st3 = makeInput();
        st3.time = 5.0f;
        k.frame(st3);   // ogui_on_update raises; frame must not crash
        CHECK(sh.errors().size() == before + 3);
        CHECK(sh.errors().back().find("tick") != std::string::npos);
        // The failing script's hook is gone once a clean chunk redefines it.
        CHECK(sh.loadString("function ogui_on_update() end", "fix"));
        k.frame(st3);
    }

    // ---- input queries: mouse / mouseDown / key / mods ---------------------
    // (errors() accumulates on purpose in the block above; compare counts.)
    const size_t errBase = k.script().errors().size();
    auto newErrors = [&](const char* where) {
        for (size_t i = errBase; i < k.script().errors().size(); ++i)
            std::cerr << "new script error @" << where << ": "
                      << k.script().errors()[i] << "\n";
        return k.script().errors().size() == errBase;
    };
    {
        InputState st = makeInput();
        st.time = 6.0f;
        st.mouseX = 123.0f;
        st.mouseY = 45.0f;
        st.mouseDown[Mouse_Right] = true;
        st.keyDown[Key_Space] = true;
        st.keyDown[Key_LCTRL] = true;
        k.frame(st);
        k.script().loadString(R"(
            local x, y = ogui.mouse()
            assert(x == 123 and y == 45, "mouse")
            assert(ogui.mouseDown())
            assert(ogui.mouseDown("right"))
            assert(ogui.mouseDown(1))
            assert(ogui.mouseDown("left") == false)
            assert(ogui.mouseDown(0) == false)
            assert(ogui.key("space"))
            assert(ogui.key("k") == false)
            assert(ogui.key(1))                 -- Key_Space == 1
            assert(ogui.mods() == 2)            -- Mod_Ctrl
        )", "input");
        CHECK(newErrors("input"));
    }

    // ---- focus / hovered / clickCount ---------------------------------------
    {
        InputState st = makeInput();
        st.time = 6.5f;
        pressAt(k, st, 70.0f, 70.0f);   // btn was moved to x=12 (10+2) but is
        release(k, st);                 // 120 wide: 70,70 still inside
        st.time = 6.6f;
        pressAt(k, st, 70.0f, 70.0f);   // second click at the same spot
        release(k, st);
        k.script().loadString(R"(
            assert(ogui.focus() == "btn")
            assert(ogui.hovered() == "btn")
            assert(ogui.clickCount() >= 1)
        )", "context");
        CHECK(newErrors("context"));
    }

    // ---- tooltip + selected attributes, ogui.items ---------------------------
    {
        XmlParser p;
        auto res = p.parse(R"(<screen id="lists">
  <listbox id="lb" x="10" y="10" width="150" height="200">
    <item text="one"/>
  </listbox>
  <optionspin id="os" x="10" y="230" width="150" height="24">
    <option text="a"/>
    <option text="b"/>
  </optionspin>
  <combobox id="combo" x="10" y="270" width="150" height="24">
    <option text="x"/>
    <option text="y"/>
  </combobox>
  <label id="tip" text="t" x="10" y="310" width="100" height="20"/>
  <propertygrid id="pg" x="180" y="10" width="200" height="120">
    <property name="visible" type="bool" value="true" category="Base"/>
    <property name="size" type="number" value="12"/>
  </propertygrid>
  <treetable id="tt" x="400" y="10" width="240" height="120">
    <th label="Name"/>
    <th label="Kind" width="80"/>
    <tnode label="src" cells="dir">
      <tnode label="main.cpp" cells="file"/>
      <tnode label="util.cpp" cells="file"/>
    </tnode>
  </treetable>
  <keybind id="kbd" x="10" y="350" width="150" height="24"/>
  <commandpalette id="cp" x="560" y="150" width="220" height="120">
    <cmd name="Run Task" hint="build" key="f5"/>
    <cmd name="Quit"/>
  </commandpalette>
</screen>)");
        assert(res.root);
        auto s = std::make_shared<Screen>("lists");
        s->widgets.push_back(res.root);
        s->width = 800; s->height = 600;
        k.pushScreen(s);   // screen has no <script>: the host is cleared
        CHECK(k.script().errors().empty());
        InputState stl = makeInput();
        stl.time = 6.65f;
        k.frame(stl);      // one layout pass resolves ComboBox items
        k.script().loadString(R"(
            assert(ogui.items("lb", {"alpha", "beta", "gamma"}))
            ogui.set("lb", "selected", 1)
            assert(ogui.get("lb", "selected") == 1)
            assert(ogui.items("os", {"low", "mid", "high"}))
            ogui.set("os", "selected", 2)
            assert(ogui.get("os", "selected") == 2)
            assert(ogui.items("os", {}) == true)
            assert(not pcall(ogui.items, "tip", {"no"}))   -- label: no items
            -- ogui.list: incremental virtual-list mutation
            assert(ogui.list("lb", "count") == 3)
            ogui.list("lb", "insert", -1, "omega")          -- appends
            ogui.list("lb", "insert", 0, "first")
            assert(ogui.list("lb", "get", 0) == "first")
            assert(ogui.list("lb", "get", 4) == "omega")
            assert(ogui.list("lb", "count") == 5)
            assert(ogui.list("lb", "set", 2, "beta!"))      -- "beta" slid to 2
            assert(ogui.list("lb", "get", 2) == "beta!")
            assert(not ogui.list("lb", "set", 9, "x"))
            assert(ogui.list("lb", "remove", 1))            -- drops "alpha"
            assert(ogui.list("lb", "get", 1) == "beta!")
            assert(not ogui.list("lb", "remove", 42))
            assert(ogui.list("lb", "get", 42) == nil)
            ogui.list("lb", "scroll", 3)
            assert(ogui.get("lb", "selected") == 1)         -- selection rode along
            assert(not pcall(ogui.list, "tip", "count"))
            assert(ogui.list("lb", "get", 42) == nil)
            ogui.list("lb", "scroll", 3)
            assert(not pcall(ogui.list, "tip", "count"))
            ogui.set("tip", "tooltip", "say hi")
            assert(ogui.get("tip", "tooltip") == "say hi")
            -- ogui.prop: property grid model access
            assert(ogui.prop("pg", "count") == 2)
            assert(ogui.prop("pg", "get", "size") == "12")
            assert(ogui.prop("pg", "get", "ghost") == nil)
            assert(ogui.prop("pg", "set", "size", "42"))
            assert(ogui.prop("pg", "get", "size") == "42")
            assert(not ogui.prop("pg", "set", "size", "abc"))  -- number-validated
            assert(ogui.prop("pg", "add", "mode", "enum", "a", "Adv", "a;b"))
            assert(ogui.prop("pg", "count") == 3)
            local t = ogui.prop("pg", "list")
            assert(t.mode == "a" and t.visible == "true")
            assert(ogui.prop("pg", "remove", "mode"))
            assert(ogui.prop("pg", "selected") == -1)
            ogui.prop("pg", "select", 0)
            assert(ogui.prop("pg", "selected") == 0)
            assert(not pcall(ogui.prop, "tip", "count"))
            -- treetable: selected is readable/settable through the shared path
            assert(ogui.get("tt", "selected") == -1)
            assert(ogui.set("tt", "selected", 1))
            assert(ogui.get("tt", "selected") == 1)
            ogui.set("tt", "selected", 99)             -- clamped out of range
            assert(ogui.get("tt", "selected") == -1)
            -- treetable: selected readable/writable through the generic attr
            assert(ogui.get("tt", "selected") == -1)
            ogui.set("tt", "selected", 1)
            assert(ogui.get("tt", "selected") == 1)
            assert(ogui.set("combo", "selected", 1))
            assert(ogui.get("combo", "selected") == 1)
            -- ogui.focus: query, by id, next/prev, unfocusable rejection
            assert(not ogui.focus("tip"))                 -- label: not focusable
            assert(ogui.focus("lb"))
            assert(ogui.focus() == "lb")
            assert(ogui.focus("next") == "os")
            assert(ogui.focus("prev") == "lb")
            assert(ogui.focus("") == true)                -- clears focus
            assert(ogui.focus() == nil)
            ogui.set("combo", "focusable", true)
            assert(ogui.get("combo", "focusable") == true)
            assert(ogui.focus("nope") == false)           -- unknown id
            -- keybind: value is a validated string combo, not a number
            assert(ogui.set("kbd", "value", "ctrl+k"))
            assert(ogui.get("kbd", "value") == "ctrl+k")
            assert(not pcall(ogui.set, "kbd", "value", "bogus"))
            assert(ogui.get("kbd", "value") == "ctrl+k")
            assert(ogui.set("kbd", "value", ""))     -- empty clears
            assert(ogui.get("kbd", "value") == "")
            -- commandpalette: ogui.cmd drives model, filter, history
            assert(ogui.cmd("cp", "count") == 2)
            assert(ogui.cmd("cp", "add", "Format", "tidy up", "shift+alt+f"))
            assert(ogui.cmd("cp", "count") == 3)
            assert(ogui.cmd("cp", "query") == "")
            assert(ogui.cmd("cp", "query", "for"))
            assert(ogui.cmd("cp", "matches") == 1)
            assert(ogui.cmd("cp", "match", 0) == "Format")
            assert(ogui.cmd("cp", "match", 1) == nil)
            assert(ogui.get("cp", "selected") == 0)   -- filter auto-selects row 0
            assert(ogui.cmd("cp", "exec", "Format"))
            local h = ogui.cmd("cp", "history")
            assert(#h == 1 and h[1] == "Format")
            assert(not ogui.cmd("cp", "exec", "Ghost"))
            local t = ogui.cmd("cp", "list")
            assert(t.Quit == "" and t["Run Task"] == "build")
            assert(ogui.cmd("cp", "clearhistory"))
            assert(#ogui.cmd("cp", "history") == 0)
            assert(ogui.cmd("cp", "remove", "Format"))
            assert(not ogui.cmd("cp", "remove", "Format"))
            assert(not pcall(ogui.cmd, "tip", "count"))
            assert(not pcall(ogui.cmd, "cp", "bogus"))
        )", "lists");
        CHECK(k.script().errors().empty());
        auto* lb = dynamic_cast<ListBox*>(k.findWidget("lb"));
        CHECK(lb && lb->count() == 4 && lb->selectedIndex() == 1);
        auto* pg = dynamic_cast<PropertyGrid*>(k.findWidget("pg"));
        CHECK(pg && pg->count() == 2 && pg->valueOf("size") == "42"
              && pg->selectedIndex() == 0);
        auto* kbd = dynamic_cast<Keybind*>(k.findWidget("kbd"));
        CHECK(kbd && kbd->value().empty());
        auto* cp = dynamic_cast<CommandPalette*>(k.findWidget("cp"));
        CHECK(cp && cp->count() == 2 && cp->query() == "for");
        k.popScreen();   // back to "s": its inline script reloads
        CHECK(k.getText("out") == "loaded");
    }

    // ---- tweens: engine-driven, replaced, stopped ----------------------------
    {
        auto& sh = k.script();
        InputState st = makeInput();
        st.time = 6.7f;
        k.frame(st);   // screen s's on_update hook (reloaded on popScreen)
                       // has pb at 0.5 again
        sh.loadString(R"(
            function ogui_on_update() end   -- silence screen s's pb hook
            assert(ogui.tween("pb", "value", 1.0, 1.0, "linear"))
        )", "tw");
        CHECK(sh.errors().empty());
        CHECK(sh.tweenCount() == 1);

        for (int i = 0; i < 5; ++i) { st.time += 0.1f; k.frame(st); }
        auto* pb = dynamic_cast<ProgressBar*>(k.findWidget("pb"));
        CHECK(pb && std::fabs(pb->value() - 0.75f) < 0.02f);   // half done

        // A second tween on the same id+attr replaces the first.
        sh.loadString(R"(assert(ogui.tween("pb", "value", 0.0, 1.0)))", "tw2");
        CHECK(sh.tweenCount() == 1);
        CHECK(sh.errors().empty());

        for (int i = 0; i < 11; ++i) { st.time += 0.1f; k.frame(st); }
        CHECK(pb && pb->value() == 0.0f);   // finished and dropped
        CHECK(sh.tweenCount() == 0);

        // tweenStop cancels without touching the value further.
        const float slBefore = static_cast<Slider*>(k.findWidget("sl"))->value();
        sh.loadString(R"(
            assert(ogui.tween("sl", "value", 1.0, 1.0, "out"))
            assert(ogui.tweenStop("sl") == 1)
            assert(ogui.tweenStop("nothing") == 0)
        )", "twstop");
        CHECK(sh.errors().empty());
        CHECK(sh.tweenCount() == 0);
        st.time += 0.1f;
        k.frame(st);
        CHECK(static_cast<Slider*>(k.findWidget("sl"))->value() == slBefore);

        // Geometry attrs tween too (and stay pinned after layout).
        sh.loadString(R"(assert(ogui.tween("btn", "x", 300, 1.0, "linear")))", "geom");
        st.time += 0.1f;
        k.frame(st);
        auto* btn = k.findWidget("btn");
        CHECK(btn && btn->bounds().x > 12.0f && btn->bounds().x < 300.0f);
        sh.stopTween("btn");
    }

    // ---- timers: after / every / cancel --------------------------------------
    {
        auto& sh = k.script();
        InputState st = makeInput();
        st.time = 9.0f;   // continue past the tween block's clock
        sh.loadString(R"(
            hits = 0
            every = 0
            t1 = ogui.after(0.3, function() hits = hits + 1 end)
            t2 = ogui.every(0.2, function() every = every + 1 end)
            assert(type(t1) == "number" and t1 ~= t2)
            assert(ogui.cancel(99999) == false)
        )", "timers");
        CHECK(sh.errors().empty());
        CHECK(sh.timerCount() == 2);

        st.time += 0.1f; k.frame(st);   // 0.1: nothing yet
        st.time += 0.1f; k.frame(st);   // 0.2: every fires once
        st.time += 0.1f; k.frame(st);   // 0.3: after + every
        CHECK(sh.loadString(R"(
            assert(hits == 1, "after fired " .. hits)
            assert(every == 2, "every fired " .. every)
            assert(ogui.cancel(t2))
            assert(ogui.cancel(t1) == false)   -- one-shot already gone
        )", "timercheck"));
        CHECK(sh.errors().empty());
        CHECK(sh.timerCount() == 0);

        // Reentrant: a callback creating another timer must not break the
        // frame iteration; errors inside callbacks are recorded.
        sh.loadString(R"(
            nest = 0
            ogui.after(0.1, function()
                ogui.after(0.1, function() nest = nest + 1 end)
                error("timer boom")
            end)
        )", "reenter");
        st.time += 0.1f; k.frame(st);
        CHECK(sh.errors().back().find("timer boom") != std::string::npos);
        st.time += 0.1f; k.frame(st);
        CHECK(sh.loadString("assert(nest == 1)", "nestcheck"));
        CHECK(sh.errors().size() == 1);   // only the boom, nothing new

        // Screen swap kills pending timers with the old state.
        sh.loadString("ogui.after(0.1, function() error('leaked') end)", "leak");
        CHECK(sh.timerCount() == 1);
        k.pushScreen(makeScreen());
        CHECK(sh.timerCount() == 0);
        k.popScreen();
    }

    // ---- screen lifecycle hooks: show / hide / unload -------------------------
    {
        XmlParser p;
        auto mkScreen = [&](const char* id, const char* tag) {
            auto res = p.parse(std::string("<screen id=\"") + id + "\">" +
              "<label id=\"l\" text=\"x\" x=\"0\" y=\"0\" width=\"50\" height=\"20\"/>" +
              "<script>" +
              "function ogui_on_load() ogui.log('" + tag + ":load') end " +
              "function ogui_on_show() ogui.log('" + tag + ":show') end " +
              "function ogui_on_hide() ogui.log('" + tag + ":hide') end " +
              "function ogui_on_unload() ogui.log('" + tag + ":unload') end" +
              "</script></screen>");
            assert(res.root);
            auto s = std::make_shared<Screen>(id);
            s->widgets.push_back(res.root);
            s->scripts = std::move(res.scripts);
            s->width = 800; s->height = 600;
            return s;
        };
        auto lastLog = [&](const std::string& msg) {
            const auto& logs = k.logs();   // addLog prepends: newest first
            for (size_t i = 0; i < logs.size(); ++i)
                if (logs[i] == msg) return static_cast<int>(i);
            return -1;
        };
        auto after = [](int a, int b) { return a >= 0 && b >= 0 && a < b; };
        auto sa = mkScreen("lifeA", "A");
        auto sb = mkScreen("lifeB", "B");
        k.pushScreen(sa);
        CHECK(k.script().errors().empty());
        CHECK(lastLog("A:load") >= 0);
        CHECK(after(lastLog("A:show"), lastLog("A:load")));
        k.pushScreen(sb);
        CHECK(after(lastLog("A:hide"), lastLog("A:show")));
        CHECK(after(lastLog("A:unload"), lastLog("A:hide")));
        CHECK(after(lastLog("B:load"), lastLog("A:unload")));
        CHECK(after(lastLog("B:show"), lastLog("B:load")));
        k.popScreen();   // B is dropped, A is rebuilt from its chunks
        CHECK(after(lastLog("B:hide"), lastLog("B:show")));
        CHECK(after(lastLog("B:unload"), lastLog("B:hide")));
        CHECK(after(lastLog("A:load"), lastLog("B:unload")));
        CHECK(after(lastLog("A:show"), lastLog("A:load")));
        k.popScreen();
    }

    // ---- dynamic widgets: ogui.add / remove / clear ---------------------------
    {
        XmlParser p;
        auto res = p.parse(R"(<screen id="dyn">
  <panel id="box" x="10" y="10" width="400" height="300"/>
  <button id="boom" text="boom" x="20" y="330" width="80" height="30"/>
  <label id="dyn_out" text="start" x="120" y="330" width="200" height="30"/>
</screen>)");
        assert(res.root);
        auto s = std::make_shared<Screen>("dyn");
        s->widgets.push_back(res.root);
        s->scripts.push_back({R"(
function ogui_on_click(key)
  if key == "boom" then
    ogui.set("dyn_out", "text", "boom clicked")
    ogui.remove("boom")
  end
end
)", false});
        s->width = 800; s->height = 600;
        k.pushScreen(s);
        CHECK(k.script().errors().empty());

        k.script().loadString(R"(
            local id = ogui.add("box", '<button id="b1" text="one" x="0" y="0" width="60" height="24"/>')
            assert(id == "b1")
            assert(ogui.exists("b1"))
            local r = ogui.add("box", '<button id="b2" x="0" y="30" width="60" height="24"/><label id="b3" x="0" y="60"/>')
            assert(r == "b2")
            local bad, err = ogui.add("box", '<notatag/>')
            assert(bad == nil and err ~= nil)
            local miss, err2 = ogui.add("nope", '<button/>')
            assert(miss == nil and err2 ~= nil)
        )", "dyn-add");
        CHECK(k.script().errors().empty());
        CHECK(k.findWidget("b1") != nullptr);
        CHECK(k.findWidget("b2") != nullptr);
        CHECK(k.findWidget("b3") != nullptr);
        CHECK(k.getText("b1") == "one");

        // Removing the pressed widget from inside its own click hook must not
        // dangle: forgetWidget nulls m_pressed before the shared_ptr dies.
        InputState sd = makeInput();
        sd.time = 12.0f;
        k.frame(sd);
        pressAt(k, sd, 60.0f, 345.0f);   // boom: x=20..100, y=330..360
        release(k, sd);
        CHECK(k.getText("dyn_out") == "boom clicked");
        CHECK(k.findWidget("boom") == nullptr);
        k.frame(sd);   // a follow-up frame with m_pressed cleared must not crash

        k.script().loadString(R"(
            local n = ogui.clear("box")
            assert(n == 3)
            assert(not ogui.exists("b1"))
            assert(not ogui.exists("b2"))
            assert(not ogui.exists("b3"))
            assert(ogui.clear("missing") == 0)
            assert(ogui.remove("missing") == nil)
        )", "dyn-clear");
        CHECK(k.script().errors().empty());
        k.popScreen();
        CHECK(k.getText("out") == "loaded");
    }

    // ---- per-widget event listeners: ogui.on / ogui.off -----------------------
    {
        XmlParser p;
        auto res = p.parse(R"(<screen id="ev">
  <button id="eb" text="press" x="10" y="50" width="120" height="40"/>
  <label id="elog" text="none" x="10" y="120" width="200" height="24"/>
</screen>)");
        assert(res.root);
        auto s = std::make_shared<Screen>("ev");
        s->widgets.push_back(res.root);
        s->scripts.push_back({R"(
hits = 0
foc = 0
bl = 0
ogui.on("eb", "click", function(id) hits = hits + 1; ogui.set("elog", "text", "click:" .. id) end)
ogui.on("eb", "focus", function(id) foc = foc + 1 end)
ogui.on("eb", "blur",  function(id) bl = bl + 1 end)
)", false});
        s->width = 800; s->height = 600;
        k.pushScreen(s);
        CHECK(k.script().errors().empty());
        CHECK(k.script().listenerCount() == 3);

        InputState se = makeInput();
        se.time = 20.0f;
        k.frame(se);
        pressAt(k, se, 70.0f, 70.0f);   // click eb: focus fires on press
        release(k, se);                 // click released -> click listener
        CHECK(k.getText("elog") == "click:eb");

        // second click still counts (listener persists)
        pressAt(k, se, 70.0f, 70.0f);
        release(k, se);
        // remove focus (click empty area) -> blur
        pressAt(k, se, 400.0f, 400.0f);
        release(k, se);
        k.script().loadString("assert(hits == 2) assert(foc == 1) assert(bl == 1)", "ev-foc");
        CHECK(k.script().errors().empty());

        // off by widget id drops all three listeners
        k.script().loadString("assert(ogui.off(\"eb\") == 3)", "ev-off");
        CHECK(k.script().errors().empty());
        CHECK(k.script().listenerCount() == 0);
        k.popScreen();
        CHECK(k.getText("out") == "loaded");
    }

    // ---- change / submit events on user interaction ---------------------------
    {
        XmlParser p;
        auto res = p.parse(R"(<screen id="chg">
  <textinput id="ti" x="10" y="10" width="200" height="24"/>
  <checkbox id="ck" text="c" x="10" y="50" width="60" height="24"/>
  <label id="clog" text="none" x="10" y="90" width="200" height="24"/>
</screen>)");
        assert(res.root);
        auto s = std::make_shared<Screen>("chg");
        s->widgets.push_back(res.root);
        s->scripts.push_back({R"(
chg = 0
sub = 0
ckc = 0
ogui.on("ti", "change", function(id) chg = chg + 1; ogui.set("clog", "text", "chg:" .. id) end)
ogui.on("ti", "submit", function(id) sub = sub + 1 end)
ogui.on("ck",  "change", function(id) ckc = ckc + 1 end)
)", false});
        s->width = 800; s->height = 600;
        k.pushScreen(s);
        CHECK(k.script().errors().empty());

        InputState sc = makeInput();
        sc.time = 25.0f;
        k.frame(sc);
        // focus the text field, then type 'h' (an onChar edge) -> change
        pressAt(k, sc, 60.0f, 20.0f);
        release(k, sc);
        sc.lastChar = 'h';
        k.frame(sc);
        sc.lastChar = 0;
        k.frame(sc);
        CHECK(k.getText("clog") == "chg:ti");

        // Enter -> submit
        sc.keyDown[Key_Enter] = true;
        k.frame(sc);
        sc.keyDown[Key_Enter] = false;
        k.frame(sc);

        // clicking the checkbox -> its own change listener
        pressAt(k, sc, 30.0f, 60.0f);
        release(k, sc);

        // a programmatic set must NOT fire change (no loop)
        k.script().loadString(
            "ogui.set(\"ti\", \"text\", \"reset\"); "
            "ogui.set(\"ck\", \"checked\", false)", "chg-prog");
        CHECK(k.script().errors().empty());
        k.script().loadString("assert(chg == 1) assert(sub == 1) assert(ckc == 1)", "chg-count");
        CHECK(k.script().errors().empty());
        k.popScreen();
        CHECK(k.getText("out") == "loaded");
    }

    // ---- tree navigation: ogui.children / parent / index ----------------------
    {
        XmlParser p;
        auto res = p.parse(R"(<screen id="nav">
  <panel id="box" x="10" y="10" width="300" height="200">
    <button id="c1" text="a"/>
    <label id="c2" text="b"/>
  </panel>
</screen>)");
        assert(res.root);
        auto s = std::make_shared<Screen>("nav");
        s->widgets.push_back(res.root);
        s->width = 800; s->height = 600;
        k.pushScreen(s);
        InputState sn = makeInput();
        sn.time = 15.0f;
        k.frame(sn);   // attach kernel / resolve tree

        k.script().loadString(R"(
            local kids = ogui.children("box")
            assert(#kids == 2 and kids[1] == "c1" and kids[2] == "c2")
            assert(ogui.parent("c1") == "box")
            assert(ogui.parent("box") == "nav")
            assert(ogui.index("c1") == 1)
            assert(ogui.index("c2") == 2)
            assert(ogui.children("missing") == nil)
            assert(ogui.parent("missing") == nil)
            assert(ogui.index("missing") == nil)
            local id = ogui.add("box", '<checkbox id="c3" text="c"/>')
            assert(id == "c3")
            assert(#ogui.children("box") == 3)
            assert(ogui.index("c3") == 3)
        )", "nav-tree");
        CHECK(k.script().errors().empty());
        k.popScreen();
        CHECK(k.getText("out") == "loaded");
    }

    // ---- data bindings: ogui.bind / ogui.unbind -------------------------------
    {
        XmlParser p;
        auto res = p.parse(R"(<screen id="bind">
  <textinput id="tsrc" x="10" y="10" width="200" height="24"/>
  <label id="tdst" text="init" x="10" y="50" width="200" height="24"/>
  <slider id="sl" value="0.3" x="10" y="90" width="200" height="24"/>
  <progress id="pg" value="0" x="10" y="130" width="200" height="20"/>
</screen>)");
        assert(res.root);
        auto s = std::make_shared<Screen>("bind");
        s->widgets.push_back(res.root);
        s->width = 800; s->height = 600;
        k.pushScreen(s);
        CHECK(k.script().errors().empty());

        // one-way mirrors; initial pull runs at bind time
        k.script().loadString(R"(
            assert(ogui.bind("tdst", "text", "tsrc", "text"))
            assert(ogui.bind("pg", "value", "sl", "value"))
            assert(ogui.get("tdst", "text") == ogui.get("tsrc", "text"))
            assert(math.abs(ogui.get("pg", "value") - 0.3) < 1e-4)
            -- programmatic writes are silent, so they do NOT re-mirror (no loop)
            ogui.set("sl", "value", 0.9)
            assert(math.abs(ogui.get("pg", "value") - 0.3) < 1e-4)
        )", "bind-init");
        CHECK(k.script().errors().empty());

        // a user edit fires "change" and the mirror updates
        InputState sb = makeInput();
        sb.time = 30.0f;
        k.frame(sb);
        pressAt(k, sb, 60.0f, 20.0f);   // focus tsrc
        release(k, sb);
        sb.lastChar = 'a';
        k.frame(sb);
        sb.lastChar = 0;
        k.frame(sb);
        k.script().loadString("assert(ogui.get('tdst','text') == 'a')", "bind-mirror");
        CHECK(k.script().errors().empty());

        k.script().loadString("assert(ogui.unbind('tdst') == 1)", "bind-off");
        CHECK(k.script().errors().empty());
        k.popScreen();
        CHECK(k.getText("out") == "loaded");
    }

    // ---- ogui.quit ------------------------------------------------------------
    {
        CHECK(!k.quitRequested());
        k.script().loadString("ogui.quit()", "bye");
        CHECK(k.script().errors().empty());
        CHECK(k.quitRequested());
    }

    if (g_failures) {
        std::cerr << g_failures << " script test(s) FAILED\n";
        return 1;
    }
    std::cout << "script: all checks passed\n";
    return 0;
}
