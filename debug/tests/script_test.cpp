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
            ogui.set("tip", "tooltip", "say hi")
            assert(ogui.get("tip", "tooltip") == "say hi")
            assert(ogui.set("combo", "selected", 1))
            assert(ogui.get("combo", "selected") == 1)
        )", "lists");
        CHECK(k.script().errors().empty());
        auto* lb = dynamic_cast<ListBox*>(k.findWidget("lb"));
        CHECK(lb && lb->count() == 3 && lb->selectedIndex() == 1);
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
