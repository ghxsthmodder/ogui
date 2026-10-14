// ogui_kernel_test.cpp : input dispatch, focus, bindings and draw list.
#include "ogui/kernel.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/clipboard.hpp"
#include "ogui/font.hpp"
#include "ogui/host.hpp"
#include "ogui/imagemanager.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

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
  <window id="win" x="100" y="50" width="400" height="300" title="t">
    <!-- children are offset by the 24px title bar -->
    <button id="btn" text="Ok" x="20" y="20" width="120" height="40"/>
    <textinput id="inp" x="20" y="80" width="200" height="36"/>
  </window>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    return s;
}

InputState makeInput() {
    InputState st;
    st.mouseButton = -1;
    return st;
}

// Press at (x,y) for one frame.
void pressAt(Kernel& k, InputState& st, float x, float y, int btn = 0) {
    st.mouseX = x;
    st.mouseY = y;
    st.mouseButton = btn;
    st.mouseDown[btn] = true;
    k.frame(st);
}

// Release for one frame.
void releaseAt(Kernel& k, InputState& st) {
    st.mouseDown[st.mouseButton >= 0 ? st.mouseButton : 0] = false;
    st.mouseButton = -1;
    k.frame(st);
    st.mouseButton = 0;
}

void idle(Kernel& k, InputState& st) {
    st.mouseButton = -1;
    k.frame(st);
}

// Press and release `key` over two frames while `mod` stays held.
void tapKey(Kernel& k, InputState& st, int key, int mod = -1, int mod2 = -1) {
    if (mod >= 0) st.keyDown[mod] = true;
    if (mod2 >= 0) st.keyDown[mod2] = true;
    st.keyDown[key] = true;
    k.frame(st);
    st.keyDown[key] = false;
    k.frame(st);
    if (mod >= 0) st.keyDown[mod] = false;
    if (mod2 >= 0) st.keyDown[mod2] = false;
}

// Focuses the <textinput> of makeScreen() (abs origin 120,154; 200x36).
TextInput* focusInput(Kernel& k, InputState& st) {
    pressAt(k, st, 120 + 2, 154 + 10);
    releaseAt(k, st);
    return dynamic_cast<TextInput*>(k.focusedWidget());
}

} // namespace

static void testDrawList() {
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    const DrawList& dl = k.buildDrawList();
    CHECK(!dl.empty());
    CHECK(dl.cmds.size() >= 1);
    // bg + window body + title bar + buttons + textinput at minimum.
    CHECK(dl.vertices.size() >= 8 * 4);

    // All clips are inside the screen.
    for (const auto& c : dl.cmds) {
        CHECK(c.clip.w > 0 && c.clip.h > 0);
        CHECK(c.clip.x >= 0 && c.clip.y >= 0);
        CHECK(c.clip.x + c.clip.w <= 800.0f + 1.0f);
        CHECK(c.clip.y + c.clip.h <= 600.0f + 1.0f);
    }
}

static void testHitTestAndFocus() {
    Kernel k;
    k.init();
    auto screen = makeScreen();
    k.pushScreen(screen);

    InputState st = makeInput();
    idle(k, st);
    st.mouseX = 1;
    st.mouseY = 1;
    k.frame(st);
    // The bg panel spans the whole screen, so the pointer always hits
    // something; the initial idle frame just has the pointer at (0,0) which
    // is inside bg too. What matters is that the root itself is skipped.
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "bg");

    // Point inside the window but outside any child -> window.
    st.mouseX = 350;
    st.mouseY = 60;
    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "win");

    // Point on the button (window 100,50 + 24px title bar + btn 20,20).
    st.mouseX = 100 + 20 + 10;
    st.mouseY = 50 + 24 + 20 + 10;
    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "btn");

    // Far corner: only the full-screen bg panel reaches there.
    st.mouseX = 790;
    st.mouseY = 590;
    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "bg");
}

static void testClickBinding() {
    Kernel k;
    k.init();
    auto screen = makeScreen();
    k.pushScreen(screen);

    int clicks = 0;
    k.bind("btn", [&] { ++clicks; });

    InputState st = makeInput();
    idle(k, st);

    pressAt(k, st, 100 + 30, 50 + 24 + 30);   // over the button (below 24px title bar)
    CHECK(clicks == 0);                        // not yet: press only

    releaseAt(k, st);
    CHECK(clicks == 1);

    // Press outside, release over the button -> no click.
    idle(k, st);
    pressAt(k, st, 5, 5);
    st.mouseX = 100 + 30;
    st.mouseY = 50 + 24 + 30;
    k.frame(st);
    releaseAt(k, st);
    CHECK(clicks == 1);
}

static void testTextInput() {
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    InputState st = makeInput();
    idle(k, st);

    // Click the input (window 100,50 + inp 20,80).
    pressAt(k, st, 100 + 40, 50 + 24 + 80 + 10);
    releaseAt(k, st);
    Widget* w = k.focusedWidget();
    CHECK(w != nullptr);
    CHECK(w && w->id() == "inp");

    auto* inp = dynamic_cast<TextInput*>(w);
    CHECK(inp != nullptr);
    if (!inp) return;
    CHECK(inp->focused());

    // Type "hi".
    st.lastChar = 'h';
    k.frame(st);
    st.lastChar = 'i';
    k.frame(st);
    CHECK(inp->text() == "hi");

    // Backspace.
    st.lastChar = 0;
    st.keyDown[Key_Backspace] = true;
    k.frame(st);
    st.keyDown[Key_Backspace] = false;
    k.frame(st);
    CHECK(inp->text() == "h");

    // Clicking elsewhere moves focus away.
    pressAt(k, st, 5, 5);
    releaseAt(k, st);
    CHECK(!inp->focused());
}

static void testKeyboardRouting() {
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    InputState st = makeInput();
    idle(k, st);

    // No focus: chars go nowhere and must not crash.
    st.lastChar = 'x';
    k.frame(st);
    st.lastChar = 0;
    k.frame(st);

    // Focus the input, then type.
    pressAt(k, st, 100 + 40, 50 + 24 + 90);
    releaseAt(k, st);
    auto* inp = dynamic_cast<TextInput*>(k.focusedWidget());
    CHECK(inp != nullptr);
    if (!inp) return;

    for (char c : { 'a', 'b', 'c' }) {
        st.lastChar = static_cast<uint32_t>(c);
        k.frame(st);
    }
    CHECK(inp->text() == "abc");
}

static void testFindAndGetText() {
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    CHECK(k.findWidget("btn") != nullptr);
    CHECK(k.findWidget("nope") == nullptr);

    CHECK(k.setText("btn", "Novo"));
    auto* b = dynamic_cast<Button*>(k.findWidget("btn"));
    CHECK(b && b->text() == "Novo");
    CHECK(k.getText("btn") == "Novo");

    CHECK(!k.setText("nope", "x"));
}

static void testTextInputSelection() {
    Clipboard::instance().reset();   // in-memory default for the test
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    InputState st = makeInput();
    idle(k, st);

    TextInput* inp = focusInput(k, st);
    CHECK(inp != nullptr);
    if (!inp) return;

    inp->setText("gatinho");
    CHECK(inp->caret() == 7);
    CHECK(!inp->hasSelection());

    // Ctrl+A selects everything.
    tapKey(k, st, Key_A, Key_LCTRL);
    CHECK(inp->hasSelection());
    CHECK(inp->selectedText() == "gatinho");
    CHECK(inp->selectionStart() == 0);
    CHECK(inp->selectionEnd() == 7);

    // Ctrl+C copies the selection to the clipboard.
    tapKey(k, st, Key_C, Key_LCTRL);
    CHECK(Clipboard::instance().text() == "gatinho");

    // Shift+Left pulls the caret back one code point; the anchor stays.
    tapKey(k, st, Key_Left, Key_LSHIFT);
    CHECK(inp->selectedText() == "gatinh");
    CHECK(inp->selectionStart() == 0);
    CHECK(inp->selectionEnd() == 6);

    // Backspace removes the selection and parks the caret on its start.
    tapKey(k, st, Key_Backspace);
    CHECK(inp->text() == "o");
    CHECK(!inp->hasSelection());
    CHECK(inp->caret() == 0);

    // Ctrl+V pastes at the caret; here it sits on 0, so the 7 bytes land
    // in front of the "o" and the caret stops right after them.
    tapKey(k, st, Key_V, Key_LCTRL);
    CHECK(inp->text() == "gatinhoo");
    CHECK(inp->caret() == 7);

    // Ctrl+X cuts the selection back to the clipboard.
    tapKey(k, st, Key_A, Key_LCTRL);
    tapKey(k, st, Key_X, Key_LCTRL);
    CHECK(inp->text().empty());
    CHECK(Clipboard::instance().text() == "gatinhoo");
    CHECK(inp->caret() == 0);

    // Ctrl+V restores it, then an accented char goes to the end.
    tapKey(k, st, Key_V, Key_LCTRL);
    CHECK(inp->text() == "gatinhoo");
    CHECK(inp->caret() == 8);
    st.lastChar = 0xE7;   // ç (one code point, two UTF-8 bytes)
    k.frame(st);
    st.lastChar = 0;
    k.frame(st);
    CHECK(inp->text() == "gatinhoo\xC3\xA7");
    CHECK(inp->caret() == 10);

    // Left from the end lands on the 'ç' lead byte (8), never inside its
    // continuation bytes; forward delete then removes the whole code point.
    tapKey(k, st, Key_Left);
    CHECK(inp->caret() == 8);
    tapKey(k, st, Key_Delete);
    CHECK(inp->text() == "gatinhoo");

    // A combining mark belongs to the base in front of it, so the pair is
    // one editing unit: Left steps over both, and one Backspace erases both.
    st.lastChar = 'e';
    k.frame(st);
    st.lastChar = 0x301;   // combining acute accent
    k.frame(st);
    st.lastChar = 0;
    k.frame(st);
    CHECK(inp->text() == "gatinhoo\x65\xCC\x81");
    CHECK(inp->caret() == 11);
    tapKey(k, st, Key_Left);
    CHECK(inp->caret() == 8);
    tapKey(k, st, Key_Right);
    CHECK(inp->caret() == 11);
    tapKey(k, st, Key_Backspace);
    CHECK(inp->text() == "gatinhoo");
    CHECK(inp->caret() == 8);

    tapKey(k, st, Key_Home);
    CHECK(inp->caret() == 0);
    tapKey(k, st, Key_End);
    CHECK(inp->caret() == inp->text().size());

    // Clicking away drops focus.
    pressAt(k, st, 5, 5);
    releaseAt(k, st);
    CHECK(!inp->focused());
}

static void testTextInputClickAndDrag() {
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    InputState st = makeInput();
    idle(k, st);

    TextInput* inp = focusInput(k, st);
    CHECK(inp != nullptr);
    if (!inp) return;

    inp->setText("abc");

    // Clicking past the text puts the caret on the end.
    pressAt(k, st, 120 + 190, 154 + 18);
    releaseAt(k, st);
    CHECK(inp->caret() == 3);
    CHECK(!inp->hasSelection());

    // Clicking inside the left padding puts it back to the start.
    pressAt(k, st, 120 + 1, 154 + 18);
    releaseAt(k, st);
    CHECK(inp->caret() == 0);

    // Press and drag to the right selects the word.
    pressAt(k, st, 120 + 1, 154 + 18);
    st.mouseX = 120 + 190;
    k.frame(st);
    CHECK(inp->hasSelection());
    CHECK(inp->selectedText() == "abc");
    releaseAt(k, st);

    // A plain click clears it again.
    pressAt(k, st, 120 + 60, 154 + 18);
    releaseAt(k, st);
    CHECK(!inp->hasSelection());
}

static void testTextInputWordSelection() {
    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    InputState st = makeInput();
    idle(k, st);

    TextInput* inp = focusInput(k, st);
    CHECK(inp != nullptr);
    if (!inp) return;
    inp->setText("gato preto");

    const float wx = 120 + 7, wy = 154 + 18;   // inside the first word

    // A click outside the double-click window is a fresh single click.
    st.time += 1.0f;
    pressAt(k, st, wx, wy);
    releaseAt(k, st);
    CHECK(k.clickCount() == 1);
    CHECK(!inp->hasSelection());

    // Double-click selects the word under the pointer.
    pressAt(k, st, wx, wy);
    releaseAt(k, st);
    CHECK(k.clickCount() == 2);
    CHECK(inp->selectionStart() == 0);
    CHECK(inp->selectionEnd() == 4);
    CHECK(inp->selectedText() == "gato");

    // Triple-click selects everything.
    pressAt(k, st, wx, wy);
    releaseAt(k, st);
    CHECK(k.clickCount() == 3);
    CHECK(inp->selectionStart() == 0);
    CHECK(inp->selectionEnd() == 10);

    // Shift+click extends from the caret instead of resetting the anchor.
    st.time += 1.0f;
    pressAt(k, st, 120 + 190, wy);      // plain click past the end
    releaseAt(k, st);
    CHECK(inp->caret() == 10);
    CHECK(!inp->hasSelection());
    st.keyDown[Key_LSHIFT] = true;
    pressAt(k, st, wx, wy);
    releaseAt(k, st);
    st.keyDown[Key_LSHIFT] = false;
    CHECK(inp->selectedText() == "gato preto");

    // Ctrl+Left jumps a whole word; Ctrl+Shift+Left selects across it.
    st.time += 1.0f;
    pressAt(k, st, 120 + 190, wy);      // park the caret at the end
    releaseAt(k, st);
    CHECK(inp->caret() == 10);
    tapKey(k, st, Key_Left, Key_LCTRL);
    CHECK(inp->caret() == 5);
    CHECK(!inp->hasSelection());
    tapKey(k, st, Key_Left, Key_LCTRL, Key_LSHIFT);
    CHECK(inp->caret() == 0);
    CHECK(inp->selectionStart() == 0);
    CHECK(inp->selectionEnd() == 5);
    CHECK(inp->selectedText() == "gato ");
    tapKey(k, st, Key_Right, Key_LCTRL);   // collapse to end, then jump
    CHECK(inp->caret() == 10);

    // Double-click on the trailing separator falls back to the word before.
    st.time += 1.0f;
    pressAt(k, st, 120 + 190, wy);
    releaseAt(k, st);
    pressAt(k, st, 120 + 190, wy);
    releaseAt(k, st);
    CHECK(inp->selectedText() == "preto");
}

static void testTextInputSelectionDraw() {
    const Font* f = FontManager::instance().get("sans", 14);
    if (!f) {
        std::cout << "  (no system font; skipping selection draw checks)\n";
        return;
    }

    Kernel k;
    k.init();
    k.pushScreen(makeScreen());

    InputState st = makeInput();
    idle(k, st);
    TextInput* inp = focusInput(k, st);
    CHECK(inp != nullptr);
    if (!inp) return;

    // Selection in the middle of the text: the highlight must span exactly
    // the selected glyphs (it once measured the substring width from the
    // text origin, so it only appeared past the middle of the widget).
    inp->setText("abcdefghijklmnopqrstuvwxyz");
    inp->setSelection(10, 15);

    const DrawList& dl = k.buildDrawList();
    float x0 = 1e9f, x1 = -1e9f;
    int matched = 0;
    for (const Vertex& v : dl.vertices) {
        if (std::fabs(v.r - 68.0f / 255.0f) > 0.02f ||
            std::fabs(v.g - 136.0f / 255.0f) > 0.02f ||
            std::fabs(v.b - 1.0f) > 0.02f ||
            std::fabs(v.a - 90.0f / 255.0f) > 0.02f)
            continue;
        x0 = std::min(x0, v.x);
        x1 = std::max(x1, v.x);
        ++matched;
    }
    CHECK(matched == 4);
    if (matched == 4) {
        const char* t = "abcdefghijklmnopqrstuvwxyz";
        const float expectW = f->measureRange(t, 15) - f->measureRange(t, 10);
        CHECK(std::fabs((x1 - x0) - expectW) < 1.0f);
        CHECK(x0 >= 120.0f && x1 <= 320.0f + 1.0f);
    }
}

static void testAnchors() {
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <button id="tr" text="t" x="660" y="20" width="120" height="30" anchor="top,right"/>
  <panel id="all" x="100" y="100" width="400" height="100" background="#202020" anchor="l,r,t,b"/>
  <panel id="fixed" x="30" y="40" width="50" height="50" background="#303030"/>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);

    InputState st = makeInput();
    idle(k, st);

    // Find the anchored widgets by walking the root's children.
    Widget* tr = nullptr;
    Widget* all = nullptr;
    Widget* fixed = nullptr;
    for (const auto& c : res.root->children()) {
        if (c->id() == "tr") tr = c.get();
        if (c->id() == "all") all = c.get();
        if (c->id() == "fixed") fixed = c.get();
    }
    CHECK(tr && all && fixed);
    if (!tr || !all || !fixed) return;
    CHECK(tr->anchor() == (uint8_t)(Widget::AnchorTop | Widget::AnchorRight));

    // At the authored size the anchor is a no-op.
    Rect r = tr->bounds();
    CHECK(r.x == 660 && r.y == 20 && r.w == 120 && r.h == 30);

    s->width = 1000;
    s->height = 700;
    idle(k, st);

    // Margins captured at 800x600: right=20, top=20.
    r = tr->bounds();
    CHECK(r.x == 860 && r.y == 20 && r.w == 120 && r.h == 30);

    // Both edges of both axes: margins l=100 r=300 t=100 b=400 stretch.
    r = all->bounds();
    CHECK(r.x == 100 && r.y == 100 && r.w == 600 && r.h == 200);

    // Unanchored absolute widgets are untouched.
    r = fixed->bounds();
    CHECK(r.x == 30 && r.y == 40 && r.w == 50 && r.h == 50);
}

static void testGrid() {
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <grid id="g" x="0" y="0" width="100%" height="600" columns="3" gap="8">
    <panel id="p1" width="100" height="50" background="#111111"/>
    <panel id="p2" width="100%" height="50" background="#222222"/>
    <panel id="p3" width="100" height="80" background="#333333"/>
    <panel id="p4" width="100" height="20" background="#444444"/>
  </grid>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);
    InputState st = makeInput();
    idle(k, st);

    auto findIn = [](Widget* w, const char* id) -> Widget* {
        for (const auto& c : w->children())
            if (c && c->id() == id) return c.get();
        return nullptr;
    };
    Widget* grid = res.root->children().empty() ? nullptr : res.root->children()[0].get();
    CHECK(grid && grid->id() == "g");
    if (!grid) return;

    // Three equal tracks over 800px with 2 gaps of 8: (800-16)/3 = 261.33.
    const float tw = (800.0f - 16.0f) / 3.0f;
    Widget* p1 = findIn(grid, "p1");
    Widget* p2 = findIn(grid, "p2");
    Widget* p3 = findIn(grid, "p3");
    Widget* p4 = findIn(grid, "p4");
    CHECK(p1 && p2 && p3 && p4);
    if (!p1 || !p2 || !p3 || !p4) return;

    Rect r = p1->bounds();
    CHECK(r.x == 0.0f && r.y == 0.0f && r.w == 100.0f && r.h == 50.0f);
    r = p2->bounds();
    CHECK(r.x == tw + 8.0f && std::fabs(r.w - tw) < 0.5f);
    r = p3->bounds();
    CHECK(r.x == 2.0f * (tw + 8.0f));

    // Row height is the tallest child (80), so p4 starts below it.
    r = p4->bounds();
    CHECK(r.x == 0.0f && r.y == 88.0f && r.h == 20.0f);

    // Resizing the screen recomputes the tracks (no drift on re-layout).
    s->width = 500;
    idle(k, st);
    idle(k, st);
    const float tw2 = (500.0f - 16.0f) / 3.0f;
    r = p2->bounds();
    CHECK(r.x == tw2 + 8.0f && std::fabs(r.w - tw2) < 0.5f);

    // Explicit track list: px + fr + %.
    XmlParser p2parser;
    auto res2 = p2parser.parse(R"(<screen id="s2">
  <grid id="g" x="0" y="0" width="800" height="100" columns="120 1fr 25%" gap="0" rowHeight="30">
    <panel id="a" background="#111111"/>
    <panel id="b" width="100%" background="#222222"/>
    <panel id="c" background="#333333"/>
  </grid>
</screen>)");
    assert(res2.root);
    auto s2 = std::make_shared<Screen>("s2");
    s2->widgets.push_back(res2.root);
    s2->width = 800;
    s2->height = 100;
    Kernel k2;
    k2.init();
    k2.pushScreen(s2);
    InputState st2 = makeInput();
    idle(k2, st2);
    Widget* g2 = res2.root->children()[0].get();
    Widget* b2 = findIn(g2, "b");
    CHECK(b2);
    if (b2) {
        r = b2->bounds();
        // leftover = 800 - 120 - 200 = 480 for the single fr track.
        CHECK(r.x == 120.0f && r.w == 480.0f && r.h == 30.0f);
    }
}

// Batch A spec widgets: accordion exclusivity, badge removal,
// searchinput clear button, modal open/close input semantics.
static void testBatchAWidgets() {
    Kernel k;
    k.init();

    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <accordion id="ac" x="20" y="20" width="300" height="200" gap="4">
    <section id="sec1" header="H1" expanded="true" headerHeight="24">
      <label id="l1" x="0" y="0" width="100%" height="18" text="A"/>
    </section>
    <section id="sec2" header="H2" headerHeight="24">
      <label id="l2" x="0" y="0" width="100%" height="18" text="B"/>
    </section>
  </accordion>
  <badge id="bd1" x="20" y="300" width="100" height="24" text="Tag" removable="true"/>
  <badge id="bd2" x="130" y="300" width="100" height="24" text="Keep" removable="true"/>
  <searchinput id="se" x="20" y="340" width="200" height="30" placeholder="Buscar..."/>
  <modal id="md" x="0" y="0" width="100%" height="100%" title="T" open="false"
         panelWidth="320" panelHeight="170">
    <button id="md_ok" x="40" y="112" width="110" height="30" text="OK"/>
  </modal>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    auto find = [&](const char* id) -> Widget* {
        Widget* out = nullptr;
        std::function<void(Widget*)> rec = [&](Widget* w) {
            if (out) return;
            if (w->id() == id) { out = w; return; }
            for (const auto& c : w->children()) rec(c.get());
        };
        rec(res.root.get());
        return out;
    };

    InputState st = makeInput();
    idle(k, st);

    // Accordion: exclusive collapse/hide of section content.
    auto* sec1 = dynamic_cast<AccordionSection*>(find("sec1"));
    auto* sec2 = dynamic_cast<AccordionSection*>(find("sec2"));
    CHECK(sec1 && sec2);
    if (sec1 && sec2) {
        CHECK(sec1->expanded() && !sec2->expanded());
        CHECK(find("l1")->isVisible() && !find("l2")->isVisible());
        const Rect s2b = sec2->bounds();   // parent-relative (accordion at 20,20)
        pressAt(k, st, 20.0f + s2b.x + 40.0f, 20.0f + s2b.y + 10.0f);
        releaseAt(k, st);
        idle(k, st);
        CHECK(sec2->expanded() && !sec1->expanded());
        CHECK(!find("l1")->isVisible() && find("l2")->isVisible());
    }

    // Badge: clicking the ✕ region hides only that badge.
    Widget* bd1 = find("bd1");
    CHECK(bd1);
    if (bd1) {
        const Rect b = bd1->bounds();
        pressAt(k, st, b.x + b.w - 10.0f, b.y + b.h * 0.5f);
        releaseAt(k, st);
        idle(k, st);
        CHECK(!bd1->isVisible());
        CHECK(find("bd2")->isVisible());
    }

    // SearchInput: the clear button empties the text without focusing.
    auto* se = dynamic_cast<SearchInput*>(find("se"));
    CHECK(se);
    if (se) {
        se->setText("ola");
        const Rect b = se->bounds();
        pressAt(k, st, b.x + b.w - 16.0f + 6.0f, b.y + b.h * 0.5f);
        releaseAt(k, st);
        idle(k, st);
        CHECK(se->text().empty());
    }

    // Modal: hidden by default, blocks input once open.
    auto* md = dynamic_cast<Modal*>(find("md"));
    CHECK(md);
    if (md) {
        CHECK(!md->isOpen() && !md->isVisible());
        int okHits = 0, bgHits = 0;
        k.bind("md_ok", [&] { ++okHits; });
        k.bind("bg", [&] { ++bgHits; });
        md->setOpen(true);
        idle(k, st);
        CHECK(md->isVisible());
        const Rect mb = md->bounds();
        const Rect panel{mb.x + (mb.w - 320.0f) * 0.5f,
                         mb.y + (mb.h - 170.0f) * 0.5f, 320.0f, 170.0f};
        CHECK(find("md_ok")->bounds().x == panel.x + 40.0f);
        // OK lives inside the panel and still works.
        const Rect okb = find("md_ok")->bounds();
        pressAt(k, st, okb.x + okb.w * 0.5f, okb.y + okb.h * 0.5f);
        releaseAt(k, st);
        CHECK(okHits == 1);
        // The backdrop absorbs presses over widgets below.
        pressAt(k, st, 400.0f, 300.0f);
        releaseAt(k, st);
        CHECK(bgHits == 0 && okHits == 1);
        md->setOpen(false);
        idle(k, st);
        pressAt(k, st, 400.0f, 300.0f);   // backdrop gone: hits bg panel
        releaseAt(k, st);
        CHECK(bgHits == 1);
    }
}

// Batch B: menu bar with submenu, right-click context menu, color picker.
static void testBatchBWidgets() {
    Kernel k;
    k.init();

    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <menubar id="mb" x="0" y="0" width="400" height="24">
    <menu label="Arquivo">
      <menuitem label="Novo" command="cmd_new"/>
      <menuitem label="Sombras">
        <menuitem label="SM" command="cmd_sm"/>
      </menuitem>
    </menu>
  </menubar>
  <panel id="tgt" x="50" y="100" width="200" height="100" background="#1c1d22" contextMenu="cm"/>
  <contextmenu id="cm">
    <menuitem label="Copiar" command="cmd_copy"/>
  </contextmenu>
  <colorpicker id="cp" x="50" y="300" width="110" height="28" color="#ff0000"/>
  <button id="bt" x="400" y="300" width="100" height="30" text="B"/>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    int news = 0, sms = 0, copies = 0, btns = 0;
    k.bind("cmd_new", [&] { ++news; });
    k.bind("cmd_sm", [&] { ++sms; });
    k.bind("cmd_copy", [&] { ++copies; });
    k.bind("bt", [&] { ++btns; });

    InputState st = makeInput();
    idle(k, st);

    auto* mb = dynamic_cast<MenuBar*>(k.findWidget("mb"));
    CHECK(mb);
    if (!mb) return;

    // Open "Arquivo": the popup lists Novo + Sombras.
    pressAt(k, st, 20.0f, 12.0f);
    releaseAt(k, st);
    CHECK(k.popupOpen());
    auto pop = std::dynamic_pointer_cast<MenuPopup>(k.popup());
    CHECK(pop && pop->rowCount() == 2);

    // Click "Novo" (first row under the bar): command fires, popup closes.
    pressAt(k, st, 60.0f, 24.0f + 12.0f);
    releaseAt(k, st);
    CHECK(news == 1);
    CHECK(!k.popupOpen());

    // Click "Sombras" (second row): submenu pushes, still open.
    pressAt(k, st, 20.0f, 12.0f);
    releaseAt(k, st);
    pop = std::dynamic_pointer_cast<MenuPopup>(k.popup());
    CHECK(pop && pop->rowCount() == 2);
    pressAt(k, st, 60.0f, 24.0f + 24.0f + 12.0f);
    releaseAt(k, st);
    CHECK(news == 1);
    pop = std::dynamic_pointer_cast<MenuPopup>(k.popup());
    CHECK(pop && pop->rowCount() == 2);   // back row + SM
    // "SM" is now the second row.
    pressAt(k, st, 60.0f, 24.0f + 24.0f + 12.0f);
    releaseAt(k, st);
    CHECK(sms == 1);
    CHECK(!k.popupOpen());

    // Right-click on the target panel opens the context menu.
    st.mouseX = 100.0f; st.mouseY = 150.0f;
    st.mouseButton = 1; st.mouseDown[1] = true;
    k.frame(st);
    st.mouseDown[1] = false; st.mouseButton = -1;
    k.frame(st);
    st.mouseButton = 0;
    CHECK(k.popupOpen());

    // Right-clicking a bound button must not fire its click binding.
    k.closePopup();
    st.mouseX = 450.0f; st.mouseY = 315.0f;
    st.mouseButton = 1; st.mouseDown[1] = true;
    k.frame(st);
    st.mouseDown[1] = false; st.mouseButton = -1;
    k.frame(st);
    st.mouseButton = 0;
    CHECK(btns == 0);

    // Context menu item click fires the command.
    st.mouseX = 100.0f; st.mouseY = 150.0f;
    st.mouseButton = 1; st.mouseDown[1] = true;
    k.frame(st);
    st.mouseDown[1] = false; st.mouseButton = -1;
    k.frame(st);
    st.mouseButton = 0;
    pressAt(k, st, 120.0f, 150.0f + 12.0f);   // "Copiar" row
    releaseAt(k, st);
    CHECK(copies == 1);
    CHECK(!k.popupOpen());

    // ColorPicker: open the popup and drag inside the SV square.
    auto* cp = dynamic_cast<ColorPicker*>(k.findWidget("cp"));
    CHECK(cp);
    if (cp) {
        CHECK(cp->colorHex() == "#ff0000");
        pressAt(k, st, 60.0f, 314.0f);
        releaseAt(k, st);
        CHECK(k.popupOpen());
        // Popup sits at (50, 330); SV square starts at (58, 338), 164x110.
        pressAt(k, st, 58.0f + 150.0f, 338.0f + 5.0f);   // high sat, high val
        releaseAt(k, st);
        CHECK(cp->colorHex() != "#ff0000");
        k.closePopup();
    }
}

static void testBatchCWidgets() {
    Kernel k;
    k.init();

    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <rampeditor id="re" x="100" y="100" width="200" height="26"
              stops="0:#000000 0.5:#ff3300 1:#ffffaa" bind="re_change"/>
  <curveeditor id="ce" x="100" y="200" width="200" height="100"
               points="0,0 0.33,1 0.66,0 1,1" bind="ce_change"/>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    int reHits = 0, ceHits = 0;
    k.bind("re_change", [&] { ++reHits; });
    k.bind("ce_change", [&] { ++ceHits; });

    InputState st = makeInput();
    idle(k, st);

    auto* re = dynamic_cast<RampEditor*>(k.findWidget("re"));
    auto* ce = dynamic_cast<CurveEditor*>(k.findWidget("ce"));
    CHECK(re && ce);
    if (!re || !ce) return;

    // --- RampEditor: parsed stops + sampled endpoints ---
    CHECK(re->stopCount() == 3);
    CHECK(re->sample(0.0f).r == 0 && re->sample(0.0f).b == 0);
    CHECK(re->sample(1.0f).r == 255 && re->sample(1.0f).b == 0xaa);
    // Midpoint between the two authored stops is a blend, not a pure stop.
    const Color mid = re->sample(0.25f);
    CHECK(mid.r > 0 && mid.r < 255);

    // Click empty bar (local x=50 -> pos 0.25) adds a stop and fires binding.
    pressAt(k, st, 150.0f, 113.0f);
    releaseAt(k, st);
    CHECK(re->stopCount() == 4);
    CHECK(reHits == 1);

    // Drag the far-right stop (pos 1.0 at x=300) left to ~0.9.
    pressAt(k, st, 300.0f, 113.0f);
    st.mouseX = 280.0f; st.mouseDown[0] = true; k.frame(st);   // move while held
    releaseAt(k, st);
    CHECK(re->stopCount() == 4);
    CHECK(re->stops().back().pos < 0.95f);
    CHECK(reHits >= 2);

    // --- CurveEditor: parsed endpoints + symmetric evaluation ---
    CHECK(ce->evaluate(0.0f) == 0.0f);
    CHECK(ce->evaluate(1.0f) == 1.0f);
    CHECK(std::fabs(ce->evaluate(0.5f) - 0.5f) < 0.01f);

    // Drag control point P1 (local 66,0 -> absolute 166,200) down to y=0.5.
    pressAt(k, st, 166.0f, 200.0f);
    st.mouseY = 250.0f; st.mouseDown[0] = true; k.frame(st);   // move while held
    releaseAt(k, st);
    CHECK(ceHits == 1);
    CHECK(ce->point(1).y < 0.9f);
    CHECK(ce->evaluate(0.5f) < 0.5f);   // lowered control drags the curve down
}

static void testLabelWrapAndColumnAutoHeight() {
    if (!FontManager::instance().get("sans", 14)) {
        std::cout << "  (no system font; skipping wrap checks)\n";
        return;
    }

    const std::string longText =
        "isto e um texto bem longo que precisa quebrar em varias linhas "
        "quando a caixa fica estreita";

    auto lab = std::make_shared<Label>();
    lab->setText(longText);
    lab->setWrap(true);

    const float hNarrow = lab->measureHeight(80.0f);
    const float hWide = lab->measureHeight(600.0f);
    CHECK(hNarrow > 0.0f);
    CHECK(hWide > 0.0f);
    CHECK(hWide < hNarrow);           // wider box -> fewer lines

    lab->setWrap(false);
    const float hOne = lab->measureHeight(80.0f);
    CHECK(hOne > 0.0f);
    CHECK(hOne < hNarrow);            // no wrap -> a single line
    lab->setWrap(true);

    // A Column gives a wrapped label with h=0 the height its text needs.
    auto col = std::make_shared<Column>();
    lab->setBounds(Rect{0.0f, 0.0f, 0.0f, 0.0f});
    col->addChild(lab);
    col->layout(200.0f, 0.0f);

    const Rect lb = lab->bounds();
    CHECK(lb.w == 200.0f);
    const float want = lab->measureHeight(lb.w);
    CHECK(want > 0.0f);
    CHECK(std::fabs(lb.h - want) < 1.0f);
    CHECK(col->bounds().h >= lb.h);
}


static void testNewWidgets() {
    Kernel k;
    k.init();

    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <checkbox id="cb" text="Ativar" x="20" y="20" width="160" height="24"/>
  <separator id="sep" x="20" y="60" width="300" height="8" thickness="2" color="#555555"/>
  <progress id="pb" x="20" y="80" width="300" height="18" value="0.25" color="#4488ff"/>
  <slider id="sl" x="20" y="110" width="300" height="24" min="0" max="100" value="50"/>
</screen>)");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto screen = std::make_shared<Screen>("s");
    screen->widgets.push_back(res.root);
    screen->width = 800;
    screen->height = 600;
    k.pushScreen(screen);

    auto* cb = dynamic_cast<Checkbox*>(k.findWidget("cb"));
    auto* sep = dynamic_cast<Separator*>(k.findWidget("sep"));
    auto* pb = dynamic_cast<ProgressBar*>(k.findWidget("pb"));
    auto* sl = dynamic_cast<Slider*>(k.findWidget("sl"));
    CHECK(cb && sep && pb && sl);
    if (!cb || !sep || !pb || !sl) return;

    // ---- XML attributes ---------------------------------------------------
    CHECK(cb->text() == "Ativar");
    CHECK(!cb->checked());
    CHECK(sep->thickness() == 2.0f);
    CHECK(sep->lineColor().r == 0x55 && sep->lineColor().g == 0x55);
    CHECK(pb->value() == 0.25f);
    CHECK(sl->minimum() == 0.0f && sl->maximum() == 100.0f);
    CHECK(sl->value() == 50.0f);

    // ProgressBar clamps into 0..1.
    pb->setValue(1.7f);
    CHECK(pb->value() == 1.0f);
    pb->setValue(-3.0f);
    CHECK(pb->value() == 0.0f);
    pb->setValue(0.25f);

    InputState st = makeInput();
    idle(k, st);

    // ---- checkbox: click toggles and notifies -----------------------------
    bool notified = false, got = false;
    cb->setOnChanged([&](bool v) { notified = true; got = v; });
    pressAt(k, st, 28, 32);
    releaseAt(k, st);
    CHECK(cb->checked());
    CHECK(notified && got);
    pressAt(k, st, 28, 32);
    releaseAt(k, st);
    CHECK(!cb->checked());
    CHECK(notified && !got);

    // ---- slider: press at an end, drag, clamp -----------------------------
    float changedTo = -1.0f;
    sl->setOnChanged([&](float v) { changedTo = v; });

    pressAt(k, st, 20 + 299.0f, 110 + 12.0f);   // right end -> max
    CHECK(changedTo == 100.0f);
    CHECK(sl->value() == 100.0f);

    st.mouseX = 20 + 150.0f;                     // drag to the middle
    st.mouseY = 110 + 12.0f;
    k.frame(st);
    CHECK(std::fabs(sl->value() - 50.0f) < 6.0f);
    releaseAt(k, st);

    pressAt(k, st, 20 + 0.0f, 110 + 12.0f);      // left end -> min
    CHECK(sl->value() == 0.0f);
    releaseAt(k, st);

    // ---- slider: keyboard (focus survived the last press) -----------------
    sl->setValue(50.0f);
    tapKey(k, st, Key_Left);
    CHECK(std::fabs(sl->value() - 45.0f) < 1e-4f);
    tapKey(k, st, Key_End);
    CHECK(sl->value() == 100.0f);
    tapKey(k, st, Key_Home);
    CHECK(sl->value() == 0.0f);

    // ---- everything reaches the draw list --------------------------------
    const DrawList& dl = k.buildDrawList();
    CHECK(!dl.empty());
    CHECK(dl.vertices.size() > 0);
}

static void testRadioButtonGroup() {
    Kernel k;
    k.init();

    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <radiobutton id="ra" text="A" x="20"  y="20" width="60" height="20" group="g1" checked="true"/>
  <radiobutton id="rb" text="B" x="100" y="20" width="60" height="20" group="g1"/>
  <radiobutton id="rc" text="C" x="180" y="20" width="60" height="20" group="g2"/>
</screen>)");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    idle(k, st);

    auto* ra = dynamic_cast<RadioButton*>(k.findWidget("ra"));
    auto* rb = dynamic_cast<RadioButton*>(k.findWidget("rb"));
    auto* rc = dynamic_cast<RadioButton*>(k.findWidget("rc"));
    CHECK(ra && rb && rc);
    if (!ra || !rb || !rc) return;
    CHECK(ra->checked() && !rb->checked() && !rc->checked());

    bool gotFalse = false;
    rb->setOnChanged([&](bool v) { gotFalse = !v; });

    // Click B: it takes over the group; A releases.
    pressAt(k, st, 130, 30);
    releaseAt(k, st);
    CHECK(rb->checked());
    CHECK(!ra->checked());
    CHECK(!gotFalse);                 // B only ever reports true

    // Clicking the selected radio again keeps it selected.
    pressAt(k, st, 130, 30);
    releaseAt(k, st);
    CHECK(rb->checked());

    // A different group is untouched.
    pressAt(k, st, 210, 30);
    releaseAt(k, st);
    CHECK(rc->checked());
    CHECK(rb->checked());             // g1 keeps its selection
}

static void testTable() {
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <table id="tb" x="20" y="20" width="400" height="120" rowHeight="22" headerHeight="24" selected="1">
    <th label="Nome" width="120"/>
    <th label="Idade" width="80"/>
    <th label="Cidade"/>
    <tr cells="Zoe;30;Rio"/>
    <tr cells="Ana;25;Sao Paulo"/>
    <tr cells="Bob;9;Curitiba"/>
  </table>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);

    InputState st = makeInput();
    k.frame(st);   // layout pass resolves <th>/<tr> children

    auto* tb = dynamic_cast<Table*>(k.findWidget("tb"));
    CHECK(tb != nullptr);
    if (!tb) return;
    CHECK(tb->columnCount() == 3);
    CHECK(tb->rowCount() == 3);
    CHECK(tb->cell(0, 0) == "Zoe");
    CHECK(tb->cell(2, 0) == "Bob");
    CHECK(tb->selectedIndex() == 1);   // deferred selected=1

    // ---- header click sorts by that column -------------------------------
    pressAt(k, st, 20 + 60, 20 + 12);    // "Nome" header cell
    releaseAt(k, st);
    CHECK(tb->sortColumn() == 0);
    CHECK(tb->sortAscending());
    CHECK(tb->cell(0, 0) == "Ana");      // Ana < Bob < Zoe
    CHECK(tb->cell(1, 0) == "Bob");
    CHECK(tb->cell(2, 0) == "Zoe");

    // Second click flips the direction.
    pressAt(k, st, 20 + 60, 20 + 12);
    releaseAt(k, st);
    CHECK(!tb->sortAscending());
    CHECK(tb->cell(0, 0) == "Zoe");

    // Numeric column sorts by value, not text (9 < 25 < 30).
    pressAt(k, st, 20 + 120 + 40, 20 + 12);
    releaseAt(k, st);
    CHECK(tb->sortColumn() == 1);
    CHECK(tb->cell(0, 0) == "Bob");      // 9 < 25 < 30 numerically
    CHECK(tb->cell(1, 0) == "Ana");
    CHECK(tb->cell(2, 0) == "Zoe");

    // ---- row click selects through the sort order ------------------------
    int selArg = -99;
    tb->setOnChanged([&](int i) { selArg = i; });
    pressAt(k, st, 20 + 60, 20 + 24 + 11);   // visible row 0 = Bob (orig 2)
    releaseAt(k, st);
    CHECK(tb->selectedIndex() == 2);
    CHECK(selArg == 2);

    // ---- keyboard (the header press focused the table) -------------------
    tapKey(k, st, Key_Down);
    CHECK(tb->selectedIndex() == 1);        // next visible row: Ana
    tapKey(k, st, Key_Up);
    CHECK(tb->selectedIndex() == 2);
    tapKey(k, st, Key_End);
    CHECK(tb->selectedIndex() == 0);        // last visible row: Zoe (orig 0)
    tapKey(k, st, Key_Home);
    CHECK(tb->selectedIndex() == 2);        // first visible row: Bob (orig 2)

    const DrawList& dl = k.buildDrawList();
    CHECK(!dl.empty());
}

static void testTableFeatures() {
    // Exercises the new Table knobs: <cell> children, per-column
    // editable/resizable/scrollingText, inline editing and header-edge resize.
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <table id="tb" x="20" y="20" width="400" height="120" rowHeight="22" headerHeight="24">
    <th label="Nome" width="120" editable="true"/>
    <th label="Idade" width="80" resizable="true" scrollingText="automatic"/>
    <th label="Cidade"/>
    <tr cells="Zoe;30;Rio"/>
    <tr cells="Ana;25;Sao Paulo"/>
    <tr>
      <cell editable="true">Bob</cell>
      <cell>9</cell>
      <cell scrollingText="onmouseenter">Curitiba</cell>
    </tr>
  </table>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);
    InputState st = makeInput();
    k.frame(st);   // layout resolves <th>/<tr>/<cell>

    auto* tb = dynamic_cast<Table*>(k.findWidget("tb"));
    CHECK(tb != nullptr);
    if (!tb) return;

    // ---- <cell> child rows parse alongside cells="a;b" rows --------------
    CHECK(tb->columnCount() == 3);
    CHECK(tb->rowCount() == 3);
    CHECK(tb->cell(0, 0) == "Zoe");
    CHECK(tb->cell(2, 0) == "Bob");        // from <cell>Bob</cell>
    CHECK(tb->cell(2, 2) == "Curitiba");

    // editable folds from <th editable> (col 0) and <cell editable> (col 0).
    CHECK(tb->cellEditable(0, 0));
    CHECK(!tb->cellEditable(0, 2));        // Cidade stays read-only

    // ---- inline editing: click the editable Nome cell of row 0 ----------
    const float cell0x = 20 + 60;          // inside col 0 (w=120)
    const float row0y  = 20 + 24 + 11;     // header (24) + half a row (22)
    pressAt(k, st, cell0x, row0y);
    releaseAt(k, st);
    CHECK(tb->selectedIndex() == 0);       // the click selected the row
    CHECK(tb->isEditing());
    CHECK(tb->editingText() == "Zoe");

    // Backspace the old value away, then type a replacement.
    for (int i = 0; i < 3; ++i) tapKey(k, st, Key_Backspace);
    CHECK(tb->editingText() == "");
    for (char c : std::string("Ivo")) { st.lastChar = c; k.frame(st); }
    st.lastChar = 0;
    CHECK(tb->editingText() == "Ivo");

    // Enter commits it into the model.
    tapKey(k, st, Key_Enter);
    CHECK(!tb->isEditing());
    CHECK(tb->cell(0, 0) == "Ivo");

    // Escape on a fresh edit cancels without writing.
    pressAt(k, st, cell0x, row0y);
    releaseAt(k, st);
    CHECK(tb->isEditing());
    for (char c : std::string("XXX")) { st.lastChar = c; k.frame(st); }
    st.lastChar = 0;
    tapKey(k, st, Key_Escape);
    CHECK(!tb->isEditing());
    CHECK(tb->cell(0, 0) == "Ivo");        // unchanged

    // ---- header-edge resize on the resizable "Idade" column --------------
    // Its right edge sits at Nome(120)+Idade(80)=200 from the table origin.
    const float edgeX = 20 + 200.0f;
    const float wBefore = tb->columnWidth(1);
    pressAt(k, st, edgeX, 20 + 12);         // grab the edge on the header
    // Drag +50px while the button stays down, then release.
    st.mouseX = edgeX + 50.0f;
    k.frame(st);
    releaseAt(k, st);
    CHECK(tb->columnWidth(1) > wBefore);    // the column grew

    // ---- scrollingText: advance frames so the marquee phase moves --------
    for (int i = 0; i < 6; ++i) { st.time += 0.03f; k.frame(st); }
    CHECK(!k.buildDrawList().empty());
}

static void testSimplePorts() {
    // SpinBox / RangeSlider / Splitter behaviour through the Kernel.
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <spinbox id="sp" x="100" y="50" width="140" height="28"
           min="0" max="10" value="3" step="1"/>
  <spinbox id="spw" x="100" y="90" width="140" height="28"
           min="1" max="12" value="12" step="1" wrap="true" prefix="Mes "/>
  <spinbox id="spd" x="100" y="130" width="140" height="28"
           min="0" max="20" value="12.5" step="0.5" decimals="1" suffix=" kg"/>
  <optionspin id="os" x="100" y="170" width="160" height="28"
              options="Baixo, Medio, Alto" value="Medio"/>
  <optionspin id="osn" x="100" y="210" width="200" height="28"
              options="A, B, C" value="B" wrap="false"/>
  <rangeslider id="rs" x="100" y="300" width="300" height="22"
               min="0" max="100" low="20" high="80"/>
  <splitter id="spt" x="400" y="200" width="300" height="150" split="0.5">
    <panel x="0" y="0" width="200" height="150" background="#1b2733"/>
    <panel x="0" y="0" width="200" height="150" background="#271b33"/>
  </splitter>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    Kernel k;
    k.init();
    k.pushScreen(s);
    InputState st = makeInput();
    idle(k, st);

    // ---- SpinBox ----------------------------------------------------------
    auto* sp = dynamic_cast<SpinBox*>(k.findWidget("sp"));
    auto* spw = dynamic_cast<SpinBox*>(k.findWidget("spw"));
    auto* spd = dynamic_cast<SpinBox*>(k.findWidget("spd"));
    CHECK(sp && spw && spd);
    if (!sp || !spw || !spd) return;
    CHECK(sp->value() == 3.0f);
    CHECK(spd->value() == 12.5f);        // decimals="1" parse
    float lastCb = -1.0f;
    sp->setOnChanged([&](float v) { lastCb = v; });

    // Buttons are b.h-wide squares at each end: centres at x+14 and x+w-14.
    pressAt(k, st, 100 + 140 - 14, 64);  // plus
    releaseAt(k, st);
    CHECK(sp->value() == 4.0f);
    CHECK(lastCb == 4.0f);
    pressAt(k, st, 100 + 14, 64);        // minus
    releaseAt(k, st);
    CHECK(sp->value() == 3.0f);

    // Keyboard while focused: Up/Right step, Down/Left unstep.
    tapKey(k, st, Key_Up);
    CHECK(sp->value() == 4.0f);
    tapKey(k, st, Key_Down);
    CHECK(sp->value() == 3.0f);

    // Clamped at the top without wrap...
    sp->setValue(10.0f);
    pressAt(k, st, 100 + 140 - 14, 64);
    releaseAt(k, st);
    CHECK(sp->value() == 10.0f);
    // ...while wrap="true" cycles max -> min.
    pressAt(k, st, 100 + 140 - 14, 104);
    releaseAt(k, st);
    CHECK(spw->value() == 1.0f);

    // ---- OptionSpin -------------------------------------------------------
    auto* os = dynamic_cast<OptionSpin*>(k.findWidget("os"));
    CHECK(os != nullptr);
    if (os) {
        CHECK(os->index() == 1);
        CHECK(os->value() == "Medio");
        // [ > ] steps forward and wraps past the last option.
        pressAt(k, st, 100 + 160 - 14, 170 + 14);
        releaseAt(k, st);
        CHECK(os->index() == 2 && os->value() == "Alto");
        pressAt(k, st, 100 + 160 - 14, 170 + 14);
        releaseAt(k, st);
        CHECK(os->index() == 0 && os->value() == "Baixo");
        // [ < ] steps back (wrapping the other way).
        pressAt(k, st, 100 + 14, 170 + 14);
        releaseAt(k, st);
        CHECK(os->index() == 2 && os->value() == "Alto");
        // Keys while focused.
        tapKey(k, st, Key_Right);
        CHECK(os->index() == 0);
        tapKey(k, st, Key_End);
        CHECK(os->value() == "Alto");
        // Non-wrapping variant clamps at the end.
        auto* osn = dynamic_cast<OptionSpin*>(k.findWidget("osn"));
        CHECK(osn != nullptr);
        if (osn) {
            CHECK(osn->index() == 1);
            pressAt(k, st, 100 + 200 - 14, 210 + 14);   // [ > ] steps to the end...
            releaseAt(k, st);
            CHECK(osn->index() == 2 && osn->value() == "C");
            pressAt(k, st, 100 + 200 - 14, 210 + 14);   // ...and clamps (wrap=false)
            releaseAt(k, st);
            CHECK(osn->index() == 2);
        }
    }

    // ---- RangeSlider ------------------------------------------------------
    auto* rs = dynamic_cast<RangeSlider*>(k.findWidget("rs"));
    CHECK(rs);
    if (!rs) return;
    CHECK(rs->low() == 20.0f && rs->high() == 80.0f);
    // pad = thumbWidth/2 = 6, span = 288; low thumb sits at 100+6+57.6.
    float cbLo = 0, cbHi = 0;
    rs->setOnChanged([&](float lo, float hi) { cbLo = lo; cbHi = hi; });
    pressAt(k, st, 164.0f, 311.0f);      // grab the low thumb
    st.mouseX = 250.0f;                  // local 150 -> value 50
    k.frame(st);
    releaseAt(k, st);
    CHECK(std::fabs(rs->low() - 50.0f) < 1.0f);
    CHECK(rs->high() == 80.0f);
    CHECK(std::fabs(cbLo - 50.0f) < 1.0f && cbHi == 80.0f);
    // Grabbing exactly on the high thumb and dragging it below low keeps
    // the pair ordered.
    pressAt(k, st, 100 + 6 + 288 * 0.8f, 311.0f);
    st.mouseX = 120.0f;
    k.frame(st);
    releaseAt(k, st);
    CHECK(rs->low() <= rs->high());
    // Keyboard: focused nudges use span/20 = 5 per step. Re-open a clean
    // range and grab the low thumb in place (a down without movement).
    rs->setLow(20.0f);
    rs->setHigh(80.0f);
    pressAt(k, st, 100 + 6 + 288 * 0.2f, 311.0f);
    releaseAt(k, st);
    const float loBefore = rs->low();
    tapKey(k, st, Key_Right);
    CHECK(rs->low() > loBefore);

    // ---- Splitter ---------------------------------------------------------
    auto* spt = dynamic_cast<Splitter*>(k.findWidget("spt"));
    CHECK(spt);
    if (!spt) return;
    CHECK(spt->children().size() == 2);
    // split=0.5 of 300 -> pane 150 wide, handle 6, rest 144. Children are
    // placed in the splitter's local space (origin 0,0), not parent-absolute.
    CHECK(spt->children()[0]->bounds().w == 150.0f);
    CHECK(spt->children()[0]->bounds().x == 0.0f);
    CHECK(spt->children()[1]->bounds().x == 150 + 6);
    CHECK(spt->children()[1]->bounds().w == 144.0f);
    CHECK(!k.buildDrawList().empty());
    // Drag the handle (band starts at local 150) to x=600 -> split ~2/3.
    pressAt(k, st, 553.0f, 275.0f);
    st.mouseX = 600.0f;
    k.frame(st);
    releaseAt(k, st);
    CHECK(std::fabs(spt->split() - 2.0f / 3.0f) < 0.02f);
    idle(k, st);
    // Regression: dragging the handle must move the children with it.
    CHECK(std::fabs(spt->children()[0]->bounds().w - 197.0f) < 2.0f);
    CHECK(std::fabs(spt->children()[1]->bounds().x - 203.0f) < 2.0f);
    CHECK(std::fabs(spt->children()[1]->bounds().w - 97.0f) < 2.0f);
}

static void testTabs() {
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <tabs id="tb" x="100" y="200" width="400" height="120" tabHeight="24" active="0">
    <tab label="Um">
      <label id="la" text="pagina um" x="10" y="10" width="200" height="20"/>
    </tab>
    <tab label="Dois">
      <label id="lb" text="pagina dois" x="10" y="10" width="200" height="20"/>
    </tab>
  </tabs>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);

    auto* tabs = dynamic_cast<Tabs*>(k.findWidget("tb"));
    auto* la = k.findWidget("la");
    auto* lb = k.findWidget("lb");
    CHECK(tabs != nullptr && la != nullptr && lb != nullptr);
    if (!tabs || !la || !lb) return;
    CHECK(tabs->active() == 0);

    InputState st = makeInput();
    k.frame(st);                        // first layout pass
    CHECK(la->isVisible());             // active page shown...
    CHECK(!lb->isVisible());            // ...inactive page hidden

    // Header cells tile the strip: find the x range of the second tab.
    float lx = -1.0f;
    for (float x = 0.0f; x < 400.0f; x += 1.0f) {
        if (tabs->tabAt(x) == 1) { lx = x; break; }
    }
    CHECK(lx >= 0.0f);
    CHECK(tabs->tabAt(-1.0f) == -1);
    CHECK(tabs->tabAt(1e6f) == -1);
    CHECK(tabs->tabCellWidth(0) > 0.0f);

    // Click the second header cell: press switches the page.
    pressAt(k, st, 100.0f + lx, 200.0f + 8.0f);
    releaseAt(k, st);
    CHECK(tabs->active() == 1);
    CHECK(!la->isVisible());
    CHECK(lb->isVisible());

    // Click the first cell to go back.
    pressAt(k, st, 100.0f + tabs->tabCellWidth(0) * 0.5f, 200.0f + 8.0f);
    releaseAt(k, st);
    CHECK(tabs->active() == 0);
    CHECK(la->isVisible());
    CHECK(!lb->isVisible());

    // The press focused the Tabs, so arrow keys page through them.
    tapKey(k, st, Key_Right);
    CHECK(tabs->active() == 1);
    tapKey(k, st, Key_Left);
    CHECK(tabs->active() == 0);
    tapKey(k, st, Key_End);
    CHECK(tabs->active() == 1);
    tapKey(k, st, Key_Home);
    CHECK(tabs->active() == 0);

    // Content clicks belong to the page widgets, never to the header:
    // the point sits over <label> of page 0 (and over the hidden page 1).
    pressAt(k, st, 100.0f + 50.0f, 200.0f + 24.0f + 20.0f);
    releaseAt(k, st);
    CHECK(tabs->active() == 0);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "la");

    // The hidden page cannot take input even at identical coordinates.
    st.mouseX = 100.0f + 50.0f;
    st.mouseY = 200.0f + 24.0f + 20.0f;
    k.frame(st);
    CHECK(k.hoveredWidget()->id() == "la");   // not "lb" (invisible)

    // Pages reach the draw list.
    const DrawList& dl = k.buildDrawList();
    CHECK(!dl.empty());
}

static void testTabsOverflow() {
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <tabs id="tb" x="100" y="200" width="160" height="120" tabHeight="24" active="0">
    <tab label="Alpha"><label id="p0" text="0" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Beta"><label id="p1" text="1" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Gamma"><label id="p2" text="2" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Delta"><label id="p3" text="3" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Epsilon"><label id="p4" text="4" x="8" y="8" width="30" height="16"/></tab>
  </tabs>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);

    auto* tabs = dynamic_cast<Tabs*>(k.findWidget("tb"));
    CHECK(tabs != nullptr);
    if (!tabs) return;

    InputState st = makeInput();
    idle(k, st);

    // Five labelled cells never fit 160px minus the "..." reserve.
    const int vis = tabs->visibleTabs(160.0f);
    CHECK(vis >= 1);
    CHECK(vis < 5);
    CHECK(tabs->visibleTabs(900.0f) == 5);

    // A hidden tab's region is inert until picked from the menu.
    float vx = 0.0f;
    for (int i = 0; i < vis; ++i) vx += tabs->tabCellWidth(i);
    CHECK(vx + 4.0f < 132.0f);   // inside a hidden cell, outside the button
    pressAt(k, st, 100.0f + vx + 4.0f, 212.0f);
    releaseAt(k, st);
    CHECK(tabs->active() == 0);

    // Click the "..." button at the right edge of the header strip.
    pressAt(k, st, 100.0f + 146.0f, 212.0f);
    releaseAt(k, st);
    CHECK(k.popupOpen());
    auto pop = std::dynamic_pointer_cast<MenuPopup>(k.popup());
    CHECK(pop && pop->rowCount() == 5 - vis);
    if (!pop) return;

    // First popup row is the first hidden tab (index `vis`).
    const Rect pb = pop->bounds();
    pressAt(k, st, pb.x + pb.w * 0.5f, pb.y + 12.0f);
    releaseAt(k, st);
    CHECK(tabs->active() == vis);
    CHECK(!k.popupOpen());
    const std::string pid = "p" + std::to_string(vis);
    if (Widget* page = k.findWidget(pid)) CHECK(page->isVisible());
}

static void testTooltip() {
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <button id="b" text="Oi" x="100" y="100" width="120" height="40" tooltip="dica"/>
  <button id="n" text="Sem" x="100" y="160" width="120" height="40"/>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);

    InputState st = makeInput();
    st.mouseX = 150.0f;
    st.mouseY = 120.0f;                 // over button "b"
    st.time = 0.0f;

    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "b");
    CHECK(!k.tooltipVisible());         // delay not reached
    const size_t noTip = k.drawList().vertices.size();

    k.frame(st);                        // no time, no motion -> still hidden
    CHECK(!k.tooltipVisible());

    st.time = 0.6f;                     // past the 0.5s default delay
    k.frame(st);
    CHECK(k.tooltipVisible());
    CHECK(k.drawList().vertices.size() > noTip);   // panel + glyphs appeared

    // Moving away hides it at once (the timer resets).
    st.mouseX = 400.0f;
    st.mouseY = 400.0f;
    k.frame(st);
    CHECK(!k.tooltipVisible());

    // A widget without a tooltip never shows one, however long it hovers.
    st.mouseX = 150.0f;
    st.mouseY = 180.0f;                 // over button "n"
    st.time = 2.0f;
    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "n");
    CHECK(!k.tooltipVisible());
    st.time = 3.0f;
    k.frame(st);
    CHECK(!k.tooltipVisible());
}

static void testTween() {
    // Linear: value == t * range mid-way.
    Tween t;
    t.start(0.0f, 10.0f, 1.0f, TweenEase::Linear);
    CHECK(t.running());
    CHECK(t.value() == 0.0f);
    t.update(0.5f);
    CHECK(t.value() > 4.9f && t.value() < 5.1f);
    t.update(0.5f);
    CHECK(!t.running());
    CHECK(t.value() == 10.0f);

    // An easing just shapes the curve; endpoints hold.
    Tween e;
    e.start(0.0f, 100.0f, 2.0f, TweenEase::EaseOutBack);
    e.update(0.5f);
    CHECK(tweenEaseValue(TweenEase::EaseOutCubic, 0.0f) == 0.0f);
    CHECK(tweenEaseValue(TweenEase::EaseOutCubic, 1.0f) == 1.0f);
    CHECK(e.value() > 0.0f && e.value() < 100.0f);
    e.finish();
    CHECK(!e.running());
    CHECK(e.value() == 100.0f);

    // Delay keeps the start value until it elapses.
    Tween d;
    d.start(0.0f, 1.0f, 1.0f, TweenEase::Linear, 2.0f);
    d.update(1.5f);
    CHECK(d.running());
    CHECK(d.value() == 0.0f);
    d.update(1.6f);   // past delay + duration (3.0s total)
    CHECK(!d.running());
    CHECK(d.value() > 0.99f);
}

static void testWidgetAnimateTo() {
    Kernel k;
    k.init();
    auto screen = makeScreen();
    k.pushScreen(screen);

    auto* lbl = dynamic_cast<TextInput*>(k.findWidget("inp")); // <textinput>
    CHECK(lbl != nullptr);
    if (!lbl) return;
    const Rect start = lbl->bounds();
    const Rect target{start.x, start.y + 80.0f, start.w, start.h};

    lbl->animateTo(target, 0.4f);
    {   // Rect has no operator==; compare members.
        const Rect b = lbl->bounds();
        CHECK(b.x == target.x && b.y == target.y &&
              b.w == target.w && b.h == target.h);
    }

    // Time advances the tween at each kernel.frame().
    InputState st = makeInput();
    st.time = 0.0f;
    idle(k, st);
    st.time = 0.2f;
    idle(k, st);
    const float mid = lbl->bounds().y;
    CHECK(mid > start.y && mid < target.y);   // half-way up a smooth curve

    st.time = 0.4f;
    idle(k, st);
    CHECK(lbl->bounds().y == target.y);       // settled
}

static void testComboBox() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <combobox id="cb" x="60" y="60" width="200" height="28" selected="0" tooltip="nivel">
    <option label="Baixo" value="0"/>
    <option label="Medio" value="1"/>
    <option label="Alto" value="2"/>
  </combobox>
  <button id="btn" text="Clicar" x="400" y="200" width="120" height="40" onClick="go"/>
</screen>)");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto screen = std::make_shared<Screen>("s");
    screen->widgets.push_back(res.root);
    screen->width = 800;
    screen->height = 600;
    k.pushScreen(screen);

    auto* box = dynamic_cast<ComboBox*>(k.findWidget("cb"));
    CHECK(box != nullptr);
    if (!box) return;

    int changed = -1;
    box->setOnChanged([&](int i) { changed = i; });

    InputState st = makeInput();
    idle(k, st);   // first frame: layout resolves the <option> children

    CHECK(box->count() == 3);
    CHECK(box->selectedIndex() == 0);
    CHECK(box->selectedLabel() == "Baixo");

    // Programmatic set doesn't fire the callback.
    box->setSelectedIndex(1);
    CHECK(changed == -1);

    // Open by clicking the box (60,60 200x28).
    pressAt(k, st, 120.0f, 74.0f);
    releaseAt(k, st);
    CHECK(box->popupOpen());
    CHECK(k.popupOpen());

    // The popup draws above the tree (its geometry lands in the draw list).
    const size_t before = k.buildDrawList().vertices.size();
    CHECK(before > 0u);
    CHECK(k.buildDrawList().vertices.size() >= before);

    // Press over row 2 (popup at 60,88 200x72; row 2 -> y148) selects it.
    pressAt(k, st, 160.0f, 147.0f);
    releaseAt(k, st);
    CHECK(box->selectedIndex() == 2);
    CHECK(box->selectedLabel() == "Alto");
    CHECK(changed == 2);
    CHECK(!box->popupOpen());
    CHECK(!k.popupOpen());

    // Keyboard: Down opens (combo still focused), Up moves the highlight to
    // "Medio", Enter commits it. Park the pointer off the dropdown first:
    // while it hovers a row, that row re-highlights every frame (mouse wins).
    st.mouseX = 0.0f;
    st.mouseY = 0.0f;
    st.keyDown[Key_Down] = true;
    k.frame(st);
    st.keyDown[Key_Down] = false;
    st.time = 0.1f;
    k.frame(st);
    CHECK(box->popupOpen());
    CHECK(box->popup() && box->popup()->hover() == 2);   // starts at selected
    st.keyDown[Key_Up] = true;
    st.time = 0.2f;
    k.frame(st);
    st.keyDown[Key_Up] = false;
    st.time = 0.3f;
    k.frame(st);
    CHECK(box->popup()->hover() == 1);
    st.keyDown[Key_Enter] = true;
    st.time = 0.4f;
    k.frame(st);
    st.keyDown[Key_Enter] = false;
    st.time = 0.5f;
    k.frame(st);
    CHECK(box->selectedLabel() == "Medio");
    CHECK(changed == 1);
    CHECK(!box->popupOpen());

    // Re-open, then a press on the button far away closes the popup and is
    // swallowed (the click must NOT reach the button).
    pressAt(k, st, 120.0f, 74.0f);
    releaseAt(k, st);
    CHECK(box->popupOpen());
    int clicks = 0;
    k.bind("go", [&] { ++clicks; });
    pressAt(k, st, 460.0f, 220.0f);
    CHECK(!box->popupOpen());
    releaseAt(k, st);
    CHECK(clicks == 0);
}

static void testScrollView() {
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <scrollview id="sv" x="100" y="100" width="200" height="150" background="#111111" border="#4488ff">
    <label id="top" text="topo" x="10" y="10" width="180" height="20"/>
    <button id="far" text="la embaixo" x="10" y="300" width="120" height="30"/>
  </scrollview>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;

    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;

    Kernel k;
    k.init();
    k.pushScreen(s);

    auto* sv = dynamic_cast<ScrollView*>(k.findWidget("sv"));
    CHECK(sv != nullptr);
    if (!sv) return;

    InputState st = makeInput();
    k.frame(st);

    // Children span y=10..330 inside a 150px viewport -> 180px of travel.
    CHECK(sv->scrollPosition().x == 0.0f);
    CHECK(sv->scrollPosition().y == 0.0f);
    CHECK(sv->contentBounds().y + sv->contentBounds().h == 330.0f);

    // The button sits below the viewport: the pointer cannot reach it.
    st.mouseX = 115.0f;
    st.mouseY = 415.0f;
    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "bg");

    k.buildDrawList();
    const size_t verticesTop = k.drawList().vertices.size();

    // One wheel notch over the view scrolls it (bubbles from the hovered
    // widget up to the ScrollView).
    st.mouseX = 150.0f;
    st.mouseY = 150.0f;
    st.scrollY = -1.0f;
    k.frame(st);
    st.scrollY = 0.0f;
    CHECK(sv->scrollPosition().y == -40.0f);

    // More notches clamp at the end of the content.
    for (int i = 0; i < 10; ++i) {
        st.scrollY = -1.0f;
        k.frame(st);
        st.scrollY = 0.0f;
    }
    CHECK(sv->scrollPosition().y == -180.0f);

    // The far button is now inside the viewport and takes the pointer...
    st.mouseX = 115.0f;
    st.mouseY = 235.0f;
    k.frame(st);
    CHECK(k.hoveredWidget() != nullptr);
    CHECK(k.hoveredWidget()->id() == "far");

    // ...and its geometry is no longer clipped out of the draw list.
    k.buildDrawList();
    CHECK(k.drawList().vertices.size() > verticesTop);

    // Scrolling back up moves the content down again.
    st.scrollY = 1.0f;
    k.frame(st);
    st.scrollY = 0.0f;
    CHECK(sv->scrollPosition().y == -140.0f);

    // Far past the top: clamped at 0.
    for (int i = 0; i < 20; ++i) {
        st.scrollY = 1.0f;
        k.frame(st);
        st.scrollY = 0.0f;
    }
    CHECK(sv->scrollPosition().y == 0.0f);
}

static void testTextArea() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <textarea id="ta" x="20" y="20" width="300" height="100" background="#111111" color="#cccccc" fontSize="14"/>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    auto* ta = dynamic_cast<TextArea*>(k.findWidget("ta"));
    CHECK(ta != nullptr);
    if (!ta) return;

    InputState st = makeInput();
    idle(k, st);

    // Wheel over an unfocused, never-clicked TextArea scrolls, and a long
    // paragraph soft-wraps into display lines (no Enter needed) so the
    // vertical wheel has something to scroll.
    ta->setText("alpha bravo charlie delta echo foxtrot golf hotel india "
                "juliet kilo lima mike november oscar papa quebec romeo "
                "sierra tango uniform victor whiskey xray yankee zulu "
                "alpha bravo charlie delta echo foxtrot golf hotel india "
                "juliet kilo lima mike november oscar papa quebec romeo "
                "sierra tango uniform victor whiskey xray yankee zulu");
    st.mouseX = 40.0f;
    st.mouseY = 40.0f;
    for (int i = 0; i < 4; ++i) {
        st.scrollY = -1.0f;
        k.frame(st);
        st.scrollY = 0.0f;
    }
    pressAt(k, st, 40, 30);
    releaseAt(k, st);
    CHECK(!ta->text().empty());
    CHECK(ta->caret() >= 40);   // top row shows a later word: wrap + wheel work
    CHECK(ta->focused());

    ta->setText("");

    st.lastChar = 'a';
    k.frame(st);
    st.lastChar = 0;
    k.frame(st);
    tapKey(k, st, Key_Enter);
    CHECK(ta->text() == "a\n");
    st.lastChar = 'b';
    k.frame(st);
    st.lastChar = 0;
    k.frame(st);
    CHECK(ta->text() == "a\nb");
    CHECK(ta->caret() == 3);

    // Line-wise Home/End/Up/Down.
    tapKey(k, st, Key_Home);
    CHECK(ta->caret() == 2);
    tapKey(k, st, Key_End);
    CHECK(ta->caret() == 3);
    tapKey(k, st, Key_Home);          // sticky column resets to 0
    tapKey(k, st, Key_Up);
    CHECK(ta->caret() == 0);
    tapKey(k, st, Key_Down);
    CHECK(ta->caret() == 2);

    // Backspace at the start of line 2 removes the newline, joining lines.
    tapKey(k, st, Key_Backspace);
    CHECK(ta->text() == "ab");
    CHECK(ta->caret() == 1);

    // Programmatic setText and kernel-level access.
    ta->setText("l1\nl2\nl3");
    CHECK(k.getText("ta") == "l1\nl2\nl3");
    tapKey(k, st, Key_A, Key_LCTRL);
    CHECK(ta->selectedText() == "l1\nl2\nl3");

    // Wheel scrolls (and must never crash or escape the content bounds).
    // Twelve notches down move the viewport past line 12: a click on the
    // top row must then land on a late letter (indexAtPoint folds in
    // m_scrollY) — proving the wheel actually scrolled.
    ta->setText("a\nb\nc\nd\ne\nf\ng\nh\ni\nj\nk\nl\nm\nn\no\np\nq\nr\ns\nt\nu\nv\nw\nx\ny\nz");
    st.mouseX = 40.0f;
    st.mouseY = 40.0f;
    for (int i = 0; i < 12; ++i) {
        st.scrollY = -1.0f;
        k.frame(st);
        st.scrollY = 0.0f;
    }
    pressAt(k, st, 40, 30);
    releaseAt(k, st);
    CHECK(ta->caret() >= 20);
    idle(k, st);
    CHECK(!k.buildDrawList().empty());

    // Vertical scrollbar: a click near the bottom of the track scrolls the
    // content down, so a later click near the top lands on a late line
    // (indexAtPoint folds m_scrollY into the hit line). The bar spans the
    // inner height at x = taX + w - kSbW - 2 .. that + kSbW (kSbW = 8).
    pressAt(k, st, 20 + 300 - 8 - 2 + 3, 20 + 100 - 6);   // track bottom
    releaseAt(k, st);
    pressAt(k, st, 40, 30);                                 // text near the top
    releaseAt(k, st);
    CHECK(ta->caret() >= 20);   // scrolled: the top row is now a late letter
    CHECK(!k.buildDrawList().empty());
}

static void testListBox() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <listbox id="lb" x="360" y="20" width="200" height="80" rowHeight="20" selected="0">
    <item label="Um"/>
    <item label="Dois"/>
    <item id="it3" label="Tres"/>
  </listbox>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    auto* lb = dynamic_cast<ListBox*>(k.findWidget("lb"));
    CHECK(lb != nullptr);
    if (!lb) return;

    InputState st = makeInput();
    idle(k, st);   // first layout resolves the <item> children

    CHECK(lb->count() == 3);
    CHECK(lb->selectedIndex() == 0);
    CHECK(lb->selectedLabel() == "Um");

    int got = -1;
    lb->setOnChanged([&](int i) { got = i; });

    // Click the third row (rows 20px tall from y=20).
    pressAt(k, st, 460, 20 + 2 * 20 + 10);
    releaseAt(k, st);
    CHECK(lb->selectedIndex() == 2);
    CHECK(got == 2);

    // The press focused the list: arrows walk the selection.
    tapKey(k, st, Key_Up);
    CHECK(lb->selectedIndex() == 1);
    tapKey(k, st, Key_Home);
    CHECK(lb->selectedIndex() == 0);
    tapKey(k, st, Key_End);
    CHECK(lb->selectedIndex() == 2);

    // Programmatic select must not notify.
    got = -1;
    lb->setSelectedIndex(1);
    CHECK(got == -1);

    CHECK(!k.buildDrawList().empty());
}

static void testVirtualList() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <virtuallist id="vl" x="20" y="20" width="200" height="60" rowHeight="20">
    <item label="a"/>
    <item label="b"/>
    <item label="c"/>
  </virtuallist>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);
    auto* vl = dynamic_cast<ListBox*>(k.findWidget("vl"));
    CHECK(vl != nullptr);
    if (!vl) return;
    InputState st = makeInput();
    idle(k, st);   // first layout materialises the <item> children
    CHECK(vl->count() == 3);

    int got = -1;
    vl->setSelectedIndex(2);        // "c" selected
    vl->setOnChanged([&](int i) { got = i; });

    // Append and mid-insert: the selection travels with its row.
    vl->insertItem(-1, "z");            // a b c z
    vl->insertItem(1, "b0");            // a b0 b c z
    std::string out;
    CHECK(vl->itemAt(1, out) && out == "b0");
    CHECK(vl->itemAt(4, out) && out == "z");
    CHECK(vl->selectedIndex() == 3 && got == -1);   // silent; row "c" moved 2->3

    // Change text in place.
    CHECK(vl->setItem(0, "a!"));
    CHECK(!vl->setItem(99, "x"));
    CHECK(vl->itemAt(0, out) && out == "a!");

    // Removing a row before the selection shifts it; removing the selected
    // row keeps the index, landing on the next entry ("z").
    vl->removeItem(2);                  // drops "b": a b0 c z
    CHECK(vl->count() == 4);
    CHECK(vl->selectedIndex() == 2 && got == -1);   // "c" slid to 2
    vl->removeItem(2);                  // drops the selected "c": a b0 z
    CHECK(vl->selectedIndex() == 2);    // now "z"
    CHECK(vl->itemAt(2, out) && out == "z");

    // Delta ops survive later layouts: parsed children are not re-collected.
    idle(k, st);
    CHECK(vl->count() == 3);

    // scrollIntoView on the last row must not crash or mis-clamp.
    vl->scrollIntoView(vl->count() - 1);
    vl->scrollIntoView(0);
    CHECK(!k.buildDrawList().empty());

    vl->clearItems();
    CHECK(vl->count() == 0 && vl->selectedIndex() == -1);
    idle(k, st);
    CHECK(vl->count() == 0);
}

static void testPropertyGrid() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <propertygrid id="pg" x="20" y="20" width="400" height="180" rowheight="24">
    <property name="visible" type="bool" value="true" category="Base"/>
    <property name="size" type="number" value="12"/>
    <property name="title" type="text" value="hi" readonly="true"/>
    <property name="mode" type="enum" value="a" options="a;b;c" category="Advanced"/>
    <property name="tint" type="color" value="#ff0000"/>
  </propertygrid>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);
    auto* pg = dynamic_cast<PropertyGrid*>(k.findWidget("pg"));
    CHECK(pg != nullptr);
    if (!pg) return;

    InputState st = makeInput();
    idle(k, st);   // first layout collects the <property> children
    CHECK(pg->count() == 5);
    CHECK(pg->indexOf("mode") == 3);
    CHECK(pg->valueOf("size") == "12");
    CHECK(!k.buildDrawList().empty());

    std::vector<std::pair<std::string, std::string>> changes;
    pg->setOnChanged([&](const std::string& n, const std::string& v) {
        changes.emplace_back(n, v);
    });

    // Row map (with group headers): vr0=Base, vr1=visible, vr2=size,
    // vr3=title, vr4=Advanced, vr5=mode, vr6=tint.
    // labelW = 400*0.4 = 160; grid abs origin is (20,20).

    // Clicking the bool value cell toggles and announces.
    pressAt(k, st, 20 + 200, 20 + 1 * 24 + 12);
    releaseAt(k, st);
    CHECK(pg->valueOf("visible") == "false");
    CHECK(pg->selectedIndex() == 0);
    CHECK(changes.size() == 1 && changes[0].first == "visible");

    // Clicking a label cell only selects (no edit, no announce).
    pressAt(k, st, 20 + 10, 20 + 2 * 24 + 12);
    releaseAt(k, st);
    CHECK(pg->selectedIndex() == 1);
    CHECK(changes.size() == 1);

    // Clicking the value cell opens inline edit; typing appends, Enter
    // commits and fires the callback.
    pressAt(k, st, 20 + 200, 20 + 2 * 24 + 12);
    releaseAt(k, st);
    st.lastChar = '5';
    k.frame(st);
    st.lastChar = 0;
    tapKey(k, st, Key_Enter);
    CHECK(pg->valueOf("size") == "125");
    CHECK(changes.size() == 2 && changes[1].first == "size" && changes[1].second == "125");

    // Invalid commit: value keeps, error shows on the row.
    pressAt(k, st, 20 + 200, 20 + 2 * 24 + 12);
    releaseAt(k, st);
    tapKey(k, st, Key_Backspace);
    tapKey(k, st, Key_Backspace);   // "1"
    st.lastChar = 'x';
    k.frame(st);
    st.lastChar = 0;
    tapKey(k, st, Key_Enter);
    CHECK(pg->valueOf("size") == "125");
    CHECK(pg->errorText() == "expected a number");
    CHECK(pg->errorIndex() == 1);

    // Escape cancels a fresh edit and clears the error.
    pressAt(k, st, 20 + 200, 20 + 2 * 24 + 12);
    releaseAt(k, st);
    tapKey(k, st, Key_Escape);
    CHECK(pg->errorText().empty());
    CHECK(pg->valueOf("size") == "125");

    // Read-only rows never enter edit mode: typing does nothing.
    pressAt(k, st, 20 + 200, 20 + 3 * 24 + 12);
    releaseAt(k, st);
    st.lastChar = 'Z';
    k.frame(st);
    st.lastChar = 0;
    tapKey(k, st, Key_Enter);
    CHECK(pg->valueOf("title") == "hi");

    // Enum: click cycles forward.
    pressAt(k, st, 20 + 200, 20 + 5 * 24 + 12);
    releaseAt(k, st);
    CHECK(pg->valueOf("mode") == "b");
    CHECK(changes.size() == 3 && changes[2].first == "mode");

    // Keyboard nav skips headers; Left/Right cycle the enum, Space too.
    pg->setSelectedIndex(2);        // title (vr3)
    tapKey(k, st, Key_Down);        // skips the vr4 header -> mode (vr5)
    CHECK(pg->selectedIndex() == 3);
    tapKey(k, st, Key_Right);       // b -> c
    CHECK(pg->valueOf("mode") == "c");
    tapKey(k, st, Key_Space);       // c wraps -> a
    CHECK(pg->valueOf("mode") == "a");
    tapKey(k, st, Key_Left);        // a wraps -> c
    CHECK(pg->valueOf("mode") == "c");
    tapKey(k, st, Key_End);         // last item: tint
    CHECK(pg->selectedIndex() == 4);
    tapKey(k, st, Key_Home);        // first item: visible (past the vr0 header)
    CHECK(pg->selectedIndex() == 0);

    // Programmatic sets are silent and validated.
    CHECK(pg->setValue("tint", "#00ff00c0"));
    CHECK(pg->valueOf("tint") == "#00ff00c0");
    CHECK(!pg->setValue("tint", "#zzzzzz"));
    CHECK(!pg->setValue("size", "NaN-ish"));
    CHECK(!pg->setValue("ghost", "1"));
    CHECK(!pg->setValue("mode", "z"));    // not an option
    CHECK(changes.size() == 6);           // only user edits announced

    // Scrolling moves through the rows.
    st.scrollY = -1.0f;
    k.frame(st);
    st.scrollY = 0.0f;
    idle(k, st);

    // C++ model edits.
    auto extra = std::make_shared<PropertyItem>();
    extra->setName("extra");
    extra->setKind(PropertyItem::Kind::Text);
    extra->setValue("e");
    pg->addItem(extra);
    CHECK(pg->count() == 6 && pg->indexOf("extra") == 5);
    CHECK(pg->removeItem("size"));
    CHECK(pg->indexOf("size") == -1 && pg->count() == 5);
    CHECK(!pg->removeItem("size"));
    pg->setSelectedIndex(99);
    CHECK(pg->selectedIndex() == -1);
    idle(k, st);
    CHECK(pg->count() == 5);   // re-layout does not re-collect
    pg->clear();
    CHECK(pg->count() == 0 && pg->valueOf("visible").empty());
}

static void testTreeTable() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <treetable id="tt" x="20" y="20" width="360" height="180" rowheight="22" headerheight="24">
    <th label="Name" width="160"/>
    <th label="Size" width="80"/>
    <th label="Kind"/>
    <tnode label="src" cells=";dir">
      <tnode label="main.cpp" cells="120;file"/>
      <tnode label="util.cpp" cells="80;file"/>
    </tnode>
    <tnode label="docs" cells=";dir" expanded="false">
      <tnode label="readme.md" cells="10;file"/>
    </tnode>
  </treetable>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);
    auto* tt = dynamic_cast<TreeTable*>(k.findWidget("tt"));
    CHECK(tt != nullptr);
    if (!tt) return;

    InputState st = makeInput();
    idle(k, st);   // first layout collects <th> columns and flattens nodes
    CHECK(tt->columnCount() == 3);
    CHECK(tt->columnLabel(1) == "Size");
    CHECK(tt->rowCount() == 4);   // src, main, util, docs (docs collapsed)
    CHECK(tt->cell(0, 0) == "src");
    CHECK(tt->cell(1, 1) == "120");
    CHECK(tt->cell(1, 2) == "file");
    CHECK(!tt->isExpanded(3));
    CHECK(!k.buildDrawList().empty());

    int changed = -99;
    tt->setOnChanged([&](int r) { changed = r; });

    // Click a data row (column 0, outside the triangle band) selects it.
    pressAt(k, st, 20 + 100, 20 + 24 + 1 * 22 + 11);
    releaseAt(k, st);
    CHECK(tt->selectedIndex() == 1);
    CHECK(changed == 1);

    // Silent programmatic select.
    changed = -99;
    tt->setSelectedIndex(0);
    CHECK(tt->selectedIndex() == 0 && changed == -99);

    // Clicking the triangle on src (depth 0, band [6,22)+origin) collapses
    // its children: rows shrink to src + docs.
    pressAt(k, st, 20 + 12, 20 + 24 + 0 * 22 + 11);
    releaseAt(k, st);
    CHECK(tt->rowCount() == 2);
    CHECK(!tt->isExpanded(0));
    pressAt(k, st, 20 + 12, 20 + 24 + 0 * 22 + 11);
    releaseAt(k, st);
    CHECK(tt->rowCount() == 4);

    // Keyboard: Right expands a collapsed node without firing change.
    tt->setSelectedIndex(3);          // docs
    changed = -99;
    tapKey(k, st, Key_Right);
    CHECK(tt->rowCount() == 5 && tt->isExpanded(3));
    CHECK(tt->selectedIndex() == 3 && changed == -99);
    tapKey(k, st, Key_Left);          // collapse again
    CHECK(tt->rowCount() == 4);
    tapKey(k, st, Key_Left);          // already collapsed -> go to parent
    CHECK(tt->selectedIndex() == 3);  // depth 0: no parent row, stays
    tt->setSelectedIndex(1);          // main.cpp (depth 1)
    tapKey(k, st, Key_Left);          // leaf -> select parent src
    CHECK(tt->selectedIndex() == 0 && changed == 0);
    tapKey(k, st, Key_End);
    CHECK(tt->selectedIndex() == 3);
    tapKey(k, st, Key_Home);
    CHECK(tt->selectedIndex() == 0);

    // Sorting by the Size header reorders siblings at every level; the
    // numeric-aware compare puts 80 before 120.
    pressAt(k, st, 20 + 180, 20 + 12);   // "Size" header (x in [160,240))
    releaseAt(k, st);
    CHECK(tt->sortColumn() == 1 && tt->sortAscending());
    CHECK(tt->cell(1, 1) == "80" && tt->cell(2, 1) == "120");
    pressAt(k, st, 20 + 180, 20 + 12);   // re-click flips direction
    releaseAt(k, st);
    CHECK(!tt->sortAscending());
    CHECK(tt->cell(1, 1) == "120" && tt->cell(2, 1) == "80");

    // Selection rides with its node across sorts.
    tt->setSelectedIndex(2);             // descending: row 2 = util.cpp (80)
    CHECK(tt->cell(2, 0) == "util.cpp");
    pressAt(k, st, 20 + 180, 20 + 12);   // flip back to ascending
    releaseAt(k, st);
    CHECK(tt->cell(1, 0) == "util.cpp" && tt->selectedIndex() == 1);

    // expandAll / collapseAll.
    tt->expandAll();
    CHECK(tt->rowCount() == 5);
    tt->collapseAll();
    CHECK(tt->rowCount() == 2);
    tt->expandAll();

    // Wheel scroll is clamped (content fits the viewport -> no movement).
    st.scrollY = 1.0f;
    k.frame(st);
    st.scrollY = 0.0f;
    idle(k, st);
    idle(k, st);
    CHECK(tt->rowCount() == 5);
}

static void testTreeView() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <tree id="tr" x="360" y="120" width="240" height="120" rowHeight="20" indent="14">
    <treeitem label="Raiz">
      <treeitem label="Filho A"/>
      <treeitem label="Filho B"/>
    </treeitem>
    <treeitem label="Outra">
      <treeitem label="Neto"/>
    </treeitem>
  </tree>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    auto* tr = dynamic_cast<TreeView*>(k.findWidget("tr"));
    CHECK(tr != nullptr);
    if (!tr) return;

    InputState st = makeInput();
    idle(k, st);   // first layout flattens the tree

    CHECK(tr->rowCount() == 5);      // Raiz, A, B, Outra, Neto
    CHECK(tr->selectedIndex() == -1);
    // Auto-height: the box hugs its 5 rows (5 * 20) instead of the authored
    // 120, so a collapsed tree can shrink to just its top-level labels.
    CHECK(tr->bounds().h == 100.0f);

    int got = -1;
    tr->setOnChanged([&](int i) { got = i; });

    // Click a row's text selects it.
    pressAt(k, st, 360 + 40, 120 + 20 + 10);   // row 1: Filho A
    releaseAt(k, st);
    CHECK(tr->selectedIndex() == 1);
    CHECK(tr->selectedLabel() == "Filho A");
    CHECK(got == 1);

    // Click the toggle triangle of row 0 collapses "Raiz".
    pressAt(k, st, 360 + 7, 120 + 10);
    releaseAt(k, st);
    CHECK(tr->rowCount() == 3);      // the collapsed children vanish
    CHECK(tr->selectedIndex() == -1);  // the selected row disappeared

    // Click it again to re-expand.
    pressAt(k, st, 360 + 7, 120 + 10);
    releaseAt(k, st);
    CHECK(tr->rowCount() == 5);

    // Keyboard: select row 0 by click, then Down / Left (jump to parent).
    pressAt(k, st, 360 + 40, 120 + 10);
    releaseAt(k, st);
    CHECK(tr->selectedIndex() == 0);
    tapKey(k, st, Key_Down);
    CHECK(tr->selectedIndex() == 1);
    tapKey(k, st, Key_Left);          // leaf -> select parent (row 0)
    CHECK(tr->selectedIndex() == 0);
    tapKey(k, st, Key_Space);         // toggle row 0 (collapse)
    CHECK(tr->rowCount() == 3);

    // Compression animation: advancing frames while the tween runs must keep
    // the tree renderable (onTick drives it, onDraw lerps the rows) and the
    // layout must settle back to the full count once re-expanded.
    for (int i = 0; i < 8; ++i) {
        st.time += 0.03f;
        k.frame(st);
        CHECK(!k.buildDrawList().empty());
    }
    CHECK(tr->rowCount() == 3);
    // The height tween settled with the rows: 3 * 20 = 60.
    CHECK(tr->bounds().h == 60.0f);
    pressAt(k, st, 360 + 7, 120 + 10);   // expand again
    releaseAt(k, st);
    CHECK(tr->rowCount() == 5);
    for (int i = 0; i < 8; ++i) {
        st.time += 0.03f;
        k.frame(st);
        CHECK(!k.buildDrawList().empty());
    }
    CHECK(tr->rowCount() == 5);

    CHECK(!k.buildDrawList().empty());
}

static void testToggleAndFileTree() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600"/>
  <toggleswitch id="ts" x="40" y="40" width="150" height="24" checked="true"
                text="VSync" bind="ts_test"/>
  <tree id="tr" x="360" y="120" width="240" height="120" rowHeight="22" indent="14"
        autoHeight="false" icons="true" checks="true" guides="true"
        bind="tree_test">
    <treeitem label="Pasta" folder="true" expanded="true" checked="true">
      <treeitem label="a.txt" file="true"/>
      <treeitem label="b.txt" file="true" checked="true"/>
    </treeitem>
  </tree>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    int tsHits = 0, treeHits = 0;
    bool lastState = true;
    k.bind("ts_test", [&] { ++tsHits; });
    k.bind("tree_test", [&] { ++treeHits; });

    InputState st = makeInput();
    idle(k, st);

    // --- ToggleSwitch ---
    auto* ts = dynamic_cast<ToggleSwitch*>(k.findWidget("ts"));
    CHECK(ts != nullptr);
    if (ts) {
        CHECK(ts->state() == true);          // parsed checked="true"
        pressAt(k, st, 60.0f, 52.0f);        // click the pill
        releaseAt(k, st);
        CHECK(ts->state() == false);         // toggled
        CHECK(tsHits == 1);                  // binding fired once
        lastState = ts->state();
        pressAt(k, st, 60.0f, 52.0f);
        releaseAt(k, st);
        CHECK(ts->state() == true);
        CHECK(tsHits == 2);
    }
    (void)lastState;

    // --- File tree: parsed checks + checkbox click toggles + fires binding ---
    auto* tr = dynamic_cast<TreeView*>(k.findWidget("tr"));
    CHECK(tr != nullptr);
    if (!tr) return;
    CHECK(tr->rowCount() == 3);              // Pasta, a.txt, b.txt
    CHECK(tr->checkedCount() == 2);          // Pasta + b.txt from XML

    // a.txt is row 1 (depth 1): its checkbox centre in absolute pixels.
    // contentX(1) = 14 + 14 = 28 local; box 13px -> centre ~34.5; row y ~33.
    pressAt(k, st, 360.0f + 34.5f, 120.0f + 33.0f);
    releaseAt(k, st);
    CHECK(tr->checkedCount() == 3);          // a.txt now checked
    CHECK(treeHits == 1);

    // Click it again to uncheck.
    pressAt(k, st, 360.0f + 34.5f, 120.0f + 33.0f);
    releaseAt(k, st);
    CHECK(tr->checkedCount() == 2);
    CHECK(treeHits == 2);

    // A click on the row body (not the checkbox) selects it. The kernel fires
    // the widget's binding on any click, so treeHits advances but the checked
    // set is unchanged.
    pressAt(k, st, 360.0f + 120.0f, 120.0f + 33.0f);
    releaseAt(k, st);
    CHECK(tr->selectedIndex() == 1);
    CHECK(tr->checkedCount() == 2);          // unchanged
    CHECK(treeHits == 3);                    // generic click fired the binding

    CHECK(!k.buildDrawList().empty());
}

void testHost() {
    // ogui::Host is a buildable, movable-free RAII type on every platform.
    Host::Settings s;
    s.title = "test";
    s.width = 320;
    s.height = 240;
    Host host(s);
    CHECK(!host.isOpen());

    // The Win32/WGL implementation is the only native backend today; any
    // other platform must fail cleanly instead of crashing.
#if defined(_WIN32)
    (void)host; // opening a real window during the test run is left to the demo
#else
    CHECK(!host.open());
    CHECK(!host.lastError().empty());
    CHECK(!host.isOpen());
    host.setClearColor(0.1f, 0.2f, 0.3f); // no-op state setter must not assert
    host.close();                         // idempotent
#endif
}

static void testImage() {
    Kernel k;
    k.init();
    const std::string png = OGUI_ASSETS_DIR;
    XmlParser p;
    std::string xml = std::string(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#101010"/>
  <image id="fit" source=")XML");
    xml += png + R"XML(/checker.png" x="10" y="60" width="64" height="64"/>
  <image id="tile" source=")XML";
    xml += png + R"XML(/checker.png" x="100" y="60" width="200" height="90" tile="true"/>
  <image id="miss" source=")XML";
    xml += png + R"XML(/nope.png" x="400" y="60" width="64" height="64"/>
</screen>)XML";
    auto res = p.parse(xml);
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    auto* fit = dynamic_cast<Image*>(k.findWidget("fit"));
    auto* tile = dynamic_cast<Image*>(k.findWidget("tile"));
    CHECK(fit && !fit->imagePath().empty());
    CHECK(tile && tile->tile());

    const DrawList& dl = k.buildDrawList();

    // The one decoded file owns a single positive slot.
    int texId = 0;
    ImageManager::instance().forEach([&](const ImageData& im) { texId = im.id; });
    CHECK(texId > 0);
    CHECK(ImageManager::instance().ownsSlot(texId));

    // fit = 1 quad; tile = ceil(200/64) * ceil(90/64) = 4*2 = 8 quads.
    // Same-texture quads batch into one cmd, so count triangles instead.
    int quads = 0;
    for (const auto& c : dl.cmds) {
        if (c.texId == texId) quads += static_cast<int>(c.indexCount / 6);
        // No command may reference a texture nobody registered: positive
        // ids in the list are ImageManager slots (fonts are negative).
        if (c.texId > 0) CHECK(ImageManager::instance().ownsSlot(c.texId));
    }
    CHECK(quads == 9);
    // The missing file must not emit textured geometry of its own.
    CHECK(dl.cmds.size() >= 1);
}

static void testRelativeGeometry() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="bg" x="0" y="0" width="100%" height="100%" background="#101010"/>
  <panel id="rel" x="10%" y="20%" width="50%" height="fill" background="#222"/>
  <panel id="abs" x="12" y="150" width="64" height="32" background="#333"/>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    k.frame(st);

    auto* bg = dynamic_cast<Panel*>(k.findWidget("bg"));
    auto* rel = dynamic_cast<Panel*>(k.findWidget("rel"));
    auto* ab = dynamic_cast<Panel*>(k.findWidget("abs"));
    CHECK(bg && rel && ab);
    CHECK(std::fabs(bg->bounds().w - 800.0f) < 0.01f);
    CHECK(std::fabs(bg->bounds().h - 600.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().x - 80.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().y - 120.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().w - 400.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().h - 600.0f) < 0.01f);   // fill = parent h
    CHECK(ab->bounds().x == 12.0f && ab->bounds().w == 64.0f);

    // Resize (the maximize path): percent/fill reflow, absolute survives.
    s->width = 1000;
    s->height = 800;
    k.frame(st);
    CHECK(std::fabs(bg->bounds().w - 1000.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().x - 100.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().y - 160.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().w - 500.0f) < 0.01f);
    CHECK(std::fabs(rel->bounds().h - 800.0f) < 0.01f);
    CHECK(ab->bounds().x == 12.0f && ab->bounds().w == 64.0f);

    // Percent children inside a Window resolve against the inner area
    // (below the title bar) and keep the title offset.
    Kernel k2;
    k2.init();
    auto res2 = p.parse(R"XML(<screen id="s">
  <window id="win" x="0" y="0" width="100%" height="100%" title="t">
    <panel id="pw" x="0" y="0" width="100%" height="50%" background="#222"/>
  </window>
</screen>)XML");
    assert(res2.root);
    auto s2 = std::make_shared<Screen>("s");
    s2->widgets.push_back(res2.root);
    s2->width = 800;
    s2->height = 600;
    k2.pushScreen(s2);
    k2.frame(st);
    auto* win = dynamic_cast<Window*>(k2.findWidget("win"));
    auto* pw = dynamic_cast<Panel*>(k2.findWidget("pw"));
    CHECK(win && pw);
    CHECK(std::fabs(win->bounds().w - 800.0f) < 0.01f);
    const float th = win->titleHeight();
    CHECK(std::fabs(pw->bounds().w - 800.0f) < 0.01f);
    CHECK(std::fabs(pw->bounds().h - (600.0f - th) * 0.5f) < 0.01f);
    CHECK(std::fabs(pw->bounds().y - th) < 0.01f);
}

static void testSizeLimits() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="cap" x="0" y="0" width="100%" height="100%" maxwidth="500" maxheight="300" background="#101010"/>
  <panel id="floor" x="0" y="0" width="10%" height="10%" minwidth="200" minheight="150" background="#222"/>
  <panel id="both" x="0" y="0" width="80" height="900" minwidth="100" maxwidth="400" background="#333"/>
  <panel id="seed" width="600" height="40" maxwidth="300" minheight="60" background="#444"/>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    k.frame(st);

    auto* cap = dynamic_cast<Panel*>(k.findWidget("cap"));
    auto* floor = dynamic_cast<Panel*>(k.findWidget("floor"));
    auto* both = dynamic_cast<Panel*>(k.findWidget("both"));
    auto* seed = dynamic_cast<Panel*>(k.findWidget("seed"));
    CHECK(cap && floor && both && seed);
    // Percent/fill sizes clamp to the ceiling.
    CHECK(std::fabs(cap->bounds().w - 500.0f) < 0.01f);
    CHECK(std::fabs(cap->bounds().h - 300.0f) < 0.01f);
    CHECK(std::fabs(floor->bounds().w - 200.0f) < 0.01f);   // 80 -> min 200
    CHECK(std::fabs(floor->bounds().h - 150.0f) < 0.01f);
    CHECK(std::fabs(both->bounds().w - 100.0f) < 0.01f);    // 80 -> min 100
    // Absolute seed sizes clamp at parse time, before the first layout.
    CHECK(std::fabs(seed->bounds().w - 300.0f) < 0.01f);    // 600 -> max 300
    CHECK(std::fabs(seed->bounds().h - 60.0f) < 0.01f);     // 40 -> min 60

    // gap="..." on column/row overrides the default 4px child spacing.
    Kernel k2;
    k2.init();
    auto res2 = p.parse(R"XML(<screen id="s">
  <column id="col" x="0" y="0" width="200" height="0" gap="20">
    <panel id="p1" width="200" height="30" background="#111"/>
    <panel id="p2" width="200" height="30" background="#222"/>
  </column>
  <row id="rw" x="0" y="400" width="0" height="40" gap="10" padding="5">
    <panel id="q1" width="50" height="40" background="#333"/>
    <panel id="q2" width="50" height="40" background="#444"/>
  </row>
</screen>)XML");
    assert(res2.root);
    auto s2 = std::make_shared<Screen>("s");
    s2->widgets.push_back(res2.root);
    s2->width = 800;
    s2->height = 600;
    k2.pushScreen(s2);
    InputState st2 = makeInput();
    k2.frame(st2);
    auto* p1 = dynamic_cast<Panel*>(k2.findWidget("p1"));
    auto* p2 = dynamic_cast<Panel*>(k2.findWidget("p2"));
    auto* q1 = dynamic_cast<Panel*>(k2.findWidget("q1"));
    auto* q2 = dynamic_cast<Panel*>(k2.findWidget("q2"));
    CHECK(p1 && p2 && q1 && q2);
    CHECK(p1->bounds().y == 0.0f);
    CHECK(p2->bounds().y == 50.0f);          // 30 + gap 20
    CHECK(q1->bounds().x == 5.0f);           // padding 5
    CHECK(q2->bounds().x == 65.0f);          // 5 + 50 + gap 10

    // align="center|right" places column children horizontally;
    // valign="middle|bottom" places shorter row children vertically.
    Kernel k3;
    k3.init();
    auto res3 = p.parse(R"XML(<screen id="s">
  <column id="ac" x="0" y="0" width="200" height="0" align="center">
    <panel id="n1" width="60" height="10" background="#111"/>
  </column>
  <column id="ar" x="300" y="0" width="200" height="0" align="right">
    <panel id="n2" width="60" height="10" background="#222"/>
  </column>
  <row id="vm" x="0" y="400" width="0" height="40" valign="middle">
    <panel id="t1" width="30" height="40" background="#333"/>
    <panel id="t2" width="30" height="20" background="#444"/>
  </row>
</screen>)XML");
    assert(res3.root);
    auto s3 = std::make_shared<Screen>("s");
    s3->widgets.push_back(res3.root);
    s3->width = 800;
    s3->height = 600;
    k3.pushScreen(s3);
    InputState st3 = makeInput();
    k3.frame(st3);
    auto* n1 = dynamic_cast<Panel*>(k3.findWidget("n1"));
    auto* n2 = dynamic_cast<Panel*>(k3.findWidget("n2"));
    auto* t1 = dynamic_cast<Panel*>(k3.findWidget("t1"));
    auto* t2 = dynamic_cast<Panel*>(k3.findWidget("t2"));
    CHECK(n1 && n2 && t1 && t2);
    // Columns stretch to their parent's width, so alignment centers
    // within that (the authored column width is a floor, not a cap).
    CHECK(std::fabs(n1->bounds().x - 370.0f) < 0.01f);      // (800-60)/2
    CHECK(std::fabs(n2->bounds().x - 740.0f) < 0.01f);      // 800-60
    CHECK(t1->bounds().y == 0.0f);                          // tallest stays
    CHECK(std::fabs(t2->bounds().y - 10.0f) < 0.01f);       // (40-20)/2

    // weight="N": children split the leftover height (column) or width
    // (row) proportionally; fixed children keep their authored size.
    Kernel k4;
    k4.init();
    auto res4 = p.parse(R"XML(<screen id="s">
  <column id="fc" x="0" y="0" width="200" height="0" gap="10" padding="5">
    <panel id="f1" width="200" height="100" background="#111"/>
    <panel id="f2" width="200" height="0" weight="1" background="#222"/>
    <panel id="f3" width="200" height="0" weight="3" background="#333"/>
  </column>
  <row id="fr" x="0" y="500" width="400" height="40" padding="0">
    <panel id="r1" width="92" height="40" background="#444"/>
    <panel id="r2" width="0" height="40" weight="1" background="#555"/>
    <panel id="r3" width="0" height="40" weight="2" background="#666"/>
  </row>
</screen>)XML");
    assert(res4.root);
    auto s4 = std::make_shared<Screen>("s");
    s4->widgets.push_back(res4.root);
    s4->width = 800;
    s4->height = 600;
    k4.pushScreen(s4);
    InputState st4 = makeInput();
    k4.frame(st4);
    auto* f2 = dynamic_cast<Panel*>(k4.findWidget("f2"));
    auto* f3 = dynamic_cast<Panel*>(k4.findWidget("f3"));
    auto* r2 = dynamic_cast<Panel*>(k4.findWidget("r2"));
    auto* r3 = dynamic_cast<Panel*>(k4.findWidget("r3"));
    CHECK(f2 && f3 && r2 && r3);
    // Column: avail = 600 - 10(pad) - 100 - 20(two gaps) = 470; split 1:3.
    CHECK(std::fabs(f2->bounds().h - 117.5f) < 0.01f);
    CHECK(std::fabs(f3->bounds().h - 352.5f) < 0.01f);
    // Row: default gap 4; avail = 400 - 92 - 8 = 300; split 1:2.
    CHECK(std::fabs(r2->bounds().w - 100.0f) < 0.01f);
    CHECK(std::fabs(r3->bounds().w - 200.0f) < 0.01f);
}

static void testConsoleWidget() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <console id="log" x="10" y="10" width="400" height="100" maxlines="3"/>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    k.frame(st);
    auto* cs = dynamic_cast<Console*>(k.findWidget("log"));
    CHECK(cs != nullptr);
    if (!cs) return;
    CHECK(cs->lineCount() == 0);

    k.addLog("first");
    k.addLog("second");
    k.frame(st);
    CHECK(cs->lineCount() == 2);
    CHECK(cs->line(0) == "second");   // newest first
    CHECK(cs->line(1) == "first");

    // maxLines caps the view to the newest entries.
    k.addLog("third");
    k.addLog("fourth");
    k.addLog("fifth");
    k.frame(st);
    CHECK(cs->lineCount() == 3);
    CHECK(cs->line(0) == "fifth");

    // Wheel scrolls history; drawing stays clipped (no crash, no change
    // to the mirrored content).
    st.mouseX = 100.0f; st.mouseY = 50.0f;
    st.scrollY = 1.0f; k.frame(st);
    st.scrollY = 0.0f; k.frame(st);
    CHECK(cs->lineCount() == 3);

    // Clear drops the mirrored lines and the kernel history.
    cs->clear();
    k.frame(st);
    CHECK(cs->lineCount() == 0);
    CHECK(k.logs().empty());
}

static void testTabsScrolling() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <tabs id="tb" x="100" y="200" width="160" height="120" tabHeight="24" scrolling="true">
    <tab label="Alpha"><label id="p0" text="0" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Beta"><label id="p1" text="1" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Gamma"><label id="p2" text="2" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Delta"><label id="p3" text="3" x="8" y="8" width="30" height="16"/></tab>
    <tab label="Epsilon"><label id="p4" text="4" x="8" y="8" width="30" height="16"/></tab>
  </tabs>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    auto* tabs = dynamic_cast<Tabs*>(k.findWidget("tb"));
    CHECK(tabs != nullptr);
    if (!tabs) return;
    InputState st = makeInput();
    idle(k, st);

    float total = 0.0f;
    for (int i = 0; i < 5; ++i) total += tabs->tabCellWidth(i);
    CHECK(total > 160.0f);              // the strip needs scrolling
    CHECK(tabs->stripOffset() == 0.0f);

    // Wheel over the header scrolls the strip right.
    st.mouseX = 200.0f; st.mouseY = 212.0f;
    st.scrollY = -1.0f; k.frame(st);
    st.scrollY = 0.0f; k.frame(st);
    CHECK(tabs->stripOffset() > 0.0f);

    // Drag the header left advances the strip by the drag distance.
    const float sx0 = tabs->stripOffset();
    pressAt(k, st, 240.0f, 212.0f);
    st.mouseX = 200.0f; k.frame(st);
    releaseAt(k, st);
    CHECK(std::fabs(tabs->stripOffset() - (sx0 + 40.0f)) < 0.01f);

    // A long drag clamps at the strip end (total width - header width).
    pressAt(k, st, 250.0f, 212.0f);
    st.mouseX = 50.0f; k.frame(st);
    releaseAt(k, st);
    CHECK(std::fabs(tabs->stripOffset() - (total - 160.0f)) < 0.01f);

    // Selecting the first tab reveals it: the strip scrolls back to 0.
    tabs->setActive(0);
    k.frame(st);
    CHECK(tabs->stripOffset() == 0.0f);
    CHECK(tabs->active() == 0);

    // Selecting the last tab scrolls it fully into view.
    tabs->setActive(4);
    k.frame(st);
    CHECK(tabs->stripOffset() >= total - 160.0f - 0.01f);
    CHECK(tabs->active() == 4);
}

static void testVisualStates() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <button id="b" x="10" y="10" width="100" height="30"
          background="#101010" color="#eeeeee" radius="2"
          hover:background="#202020" hover:radius="8"
          pressed:background="#303030" disabled:background="#050505"/>
  <checkbox id="c" x="10" y="60" width="120" height="20" checked="true"
            color="#aaaaaa" checked:color="#ff0000"/>
  <textinput id="t" x="10" y="90" width="100" height="20"
             background="#111111" border="#333333" focus:border="#00ff00"/>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);
    InputState st = makeInput();
    st.mouseX = 700.0f;
    st.mouseY = 700.0f;
    k.frame(st);

    auto* b = dynamic_cast<Button*>(k.findWidget("b"));
    auto* c = dynamic_cast<Checkbox*>(k.findWidget("c"));
    auto* t = dynamic_cast<TextInput*>(k.findWidget("t"));
    CHECK(b && c && t);

    // Pointer away: the plain (normal) values apply.
    CHECK(b->visualColorNow("background")->r == 0x10);
    CHECK(*b->visualFloatNow("radius") == 2.0f);

    // Hover swaps the color and the float override in.
    st.mouseX = 50.0f;
    st.mouseY = 20.0f;
    k.frame(st);
    k.frame(st);
    CHECK(b->visualColorNow("background")->r == 0x20);
    CHECK(*b->visualFloatNow("radius") == 8.0f);

    // Pressing (still over) takes the pressed overlay, priority over hover.
    pressAt(k, st, 50.0f, 20.0f);
    st.mouseDown[0] = true;   // keep the button held for a second frame
    k.frame(st);
    CHECK(b->visualColorNow("background")->r == 0x30);
    releaseAt(k, st);
    k.frame(st);
    CHECK(b->visualColorNow("background")->r == 0x20);   // back to hover

    // Disabled wins over every pointer state.
    b->setEnabled(false);
    k.frame(st);
    CHECK(b->visualColorNow("background")->r == 0x05);
    b->setEnabled(true);
    k.frame(st);
    k.frame(st);   // hover is recomputed at the end of each frame
    CHECK(b->visualColorNow("background")->r == 0x20);

    // Pointer leaves: base again.
    st.mouseX = 700.0f;
    st.mouseY = 700.0f;
    k.frame(st);
    k.frame(st);
    CHECK(b->visualColorNow("background")->r == 0x10);

    // Checked state is polled from the widget itself.
    CHECK(c->visualColorNow("color")->r == 0xff);
    c->setChecked(false);
    k.frame(st);
    CHECK(c->visualColorNow("color")->r == 0xaa);

    // Focusing the text field applies the focus:border overlay.
    pressAt(k, st, 50.0f, 95.0f);
    releaseAt(k, st);
    k.frame(st);
    CHECK(k.focusedWidget() == t);
    CHECK(t->visualColorNow("border")->g == 0xff);

    // Overlays without a plain base attribute are dropped with a warning.
    auto res2 = p.parse(R"XML(<screen id="s2">
  <button id="x" hover:background="#202020"/>
</screen>)XML");
    CHECK(!res2.warnings.empty());
    auto s2 = std::make_shared<Screen>("s2");
    if (res2.root) s2->widgets.push_back(res2.root);
    Kernel k2;
    k2.init();
    k2.pushScreen(s2);
    InputState st2 = makeInput();
    k2.frame(st2);
    auto* x = dynamic_cast<Button*>(k2.findWidget("x"));
    CHECK(x && x->visualColorNow("background") == nullptr);
}

static void testVisualTransitions() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <button id="b" x="10" y="10" width="100" height="30"
          background="#000000" hover:background="#008000" transition="0.2"/>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);
    InputState st = makeInput();
    st.time = 0.0f;
    st.mouseX = 700.0f;
    st.mouseY = 700.0f;
    k.frame(st);
    auto* b = dynamic_cast<Button*>(k.findWidget("b"));
    CHECK(b);
    CHECK(b->visualColorNow("background")->g == 0x00);

    // Hover at t=0.1 (recorded at the end of that frame); at t=0.2 the
    // transition starts and its first dt=0.1 covers half of the 0.2s span.
    st.mouseX = 50.0f;
    st.mouseY = 20.0f;
    st.time = 0.1f;
    k.frame(st);
    st.time = 0.2f;
    k.frame(st);
    CHECK(b->visualColorNow("background")->g == 0x40);
    CHECK(b->isAnimating());

    // Another 0.2s later it settles on the target.
    st.time = 0.4f;
    k.frame(st);
    CHECK(b->visualColorNow("background")->g == 0x80);
    CHECK(!b->isAnimating());
}

static void testRowWrap() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <row id="rw" x="0" y="0" width="120" height="0" gap="4" wrap="true" padding="0" valign="middle">
    <panel id="a" width="50" height="20" background="#111"/>
    <panel id="b" width="50" height="30" background="#222"/>
    <panel id="c" width="50" height="20" background="#333"/>
  </row>
</screen>)XML");
    assert(res.root);
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);
    InputState st = makeInput();
    k.frame(st);
    auto* rw = dynamic_cast<Row*>(k.findWidget("rw"));
    auto* a = dynamic_cast<Panel*>(k.findWidget("a"));
    auto* b = dynamic_cast<Panel*>(k.findWidget("b"));
    auto* c = dynamic_cast<Panel*>(k.findWidget("c"));
    CHECK(rw && a && b && c);
    // a and b share line 0 (50+4+50=104 <= 120); c wraps to line 1, whose
    // top sits under the tallest member of line 0 (30) plus the gap.
    // valign="middle" centres each child inside its own line band.
    CHECK(a->bounds().x == 0.0f && a->bounds().y == 5.0f);
    CHECK(b->bounds().x == 54.0f && b->bounds().y == 0.0f);
    CHECK(c->bounds().x == 0.0f && c->bounds().y == 34.0f);
    // The row box is the widest used line and the stacked heights.
    CHECK(rw->bounds().w == 104.0f);  // 50 + gap 4 + 50
    CHECK(rw->bounds().h == 54.0f);   // 30 + gap 4 + 20 (trailing gap dropped)
}

static void testDpiScale() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <button id="b" x="10" y="10" width="100" height="24" text="Ok"/>
  <label id="half" x="0" y="300" width="50%" height="20" text="h"/>
  <panel id="full" x="0" y="0" width="fill" height="10" background="#111"/>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 1600;      // device pixels, what the host knows
    s->height = 900;
    s->scale = 2.0f;      // -> an 800x450 logical canvas
    k.pushScreen(s);
    CHECK(k.scale() == 2.0f);
    CHECK(k.logicalWidth() == 800.0f);
    CHECK(k.logicalHeight() == 450.0f);

    InputState st = makeInput();
    idle(k, st);
    auto* b = k.findWidget("b");
    auto* half = k.findWidget("half");
    auto* full = k.findWidget("full");
    CHECK(b && half && full);
    if (!b || !half || !full) return;

    // Layout happens in logical units: authored px stay px, percentages
    // resolve against the logical canvas.
    CHECK(b->bounds().x == 10.0f && b->bounds().w == 100.0f);
    CHECK(half->bounds().w == 400.0f);
    CHECK(full->bounds().w == 800.0f);

    // The pointer arrives in device pixels and is divided once, so a press at
    // device (40,40) lands on the logical (20,20) inside the button.
    pressAt(k, st, 40.0f, 40.0f);
    CHECK(k.hoveredWidget() == b);
    CHECK(k.focusedWidget() == b);
    releaseAt(k, st);
    CHECK(k.mousePos().x == 20.0f && k.mousePos().y == 20.0f);
    pressAt(k, st, 300.0f, 300.0f);    // logical (150,150): outside
    CHECK(k.hoveredWidget() != b);
    releaseAt(k, st);

    // The draw list leaves in device pixels: nothing is drawn beyond the
    // logical canvas times the scale.
    idle(k, st);
    float maxX = 0.0f, maxY = 0.0f;
    for (const auto& v : k.drawList().vertices) {
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
    }
    CHECK(maxX <= 1600.0f + 0.01f);
    CHECK(maxY <= 900.0f + 0.01f);
    CHECK(maxX > 800.0f);   // the fill panel spans the whole device width
    CHECK(FontManager::instance().scale() == 2.0f);

    // A scale change re-lays out and re-flags the frame as dirty.
    k.setScale(1.0f);
    idle(k, st);
    CHECK(k.scale() == 1.0f);
    CHECK(half->bounds().w == 800.0f);   // 50% of the device-width canvas
    CHECK(b->bounds().w == 100.0f);
    CHECK(FontManager::instance().scale() == 1.0f);
    pressAt(k, st, 20.0f, 20.0f);        // now logical == device
    CHECK(k.hoveredWidget() == b);
    releaseAt(k, st);

    // A bogus scale value degrades to 1 instead of dividing by zero.
    s->scale = 0.0f;
    k.invalidate();
    idle(k, st);
    CHECK(k.scale() == 1.0f);
    CHECK(k.logicalWidth() == 1600.0f);
}

static void testKeyboardNavigation() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <textinput id="a" x="10" y="10" width="100" height="22"/>
  <label id="lbl" x="10" y="40" width="80" height="20" text="Name" accessible="full name"/>
  <button id="b" x="10" y="70" width="80" height="24"/>
  <checkbox id="c" x="10" y="100" width="80" height="20"/>
  <slider id="sl" x="10" y="130" width="100" height="20"/>
  <button id="d" x="10" y="160" width="80" height="24" enabled="false"/>
  <panel id="p" x="10" y="190" width="60" height="60" focusable="true"/>
  <keybind id="kb" x="10" y="220" width="120" height="24"/>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    idle(k, st);
    auto* a = k.findWidget("a");
    auto* lbl = k.findWidget("lbl");
    auto* b = k.findWidget("b");
    auto* c = k.findWidget("c");
    auto* sl = k.findWidget("sl");
    auto* pn = k.findWidget("p");
    auto* kb = dynamic_cast<Keybind*>(k.findWidget("kb"));
    CHECK(a && lbl && b && c && sl && pn && kb);
    if (!a || !b || !c || !sl || !pn || !kb) return;

    CHECK(a->isFocusable());                       // interactive factory opt-in
    CHECK(!lbl->isFocusable());                    // display-only stays out
    CHECK(pn->isFocusable());                      // focusable="true" opts in
    CHECK(lbl->accessibleName() == "full name");

    // Cycle through document order, skipping labels and disabled widgets.
    CHECK(k.focusNext());
    CHECK(k.focusedWidget() == a);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == b);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == c);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == sl);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == pn);   // "d" skipped
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == kb);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == a);    // wraps forward
    CHECK(k.focusNext(true)); CHECK(k.focusedWidget() == kb);  // wraps back

    // Disabled and hidden widgets drop out of the ring; a focus that stops
    // being valid restarts from the first stop.
    sl->setEnabled(false);
    k.setFocusWidget(c);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == pn);
    sl->setEnabled(true);
    k.setFocusWidget(pn);
    pn->setVisible(false);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == a);
    k.setFocusWidget(kb);
    CHECK(k.focusNext()); CHECK(k.focusedWidget() == a);    // pn no longer a stop
    pn->setVisible(true);

    // The Tab key itself is consumed by the kernel for traversal.
    k.setFocusWidget(a);
    tapKey(k, st, Key_Tab);
    CHECK(k.focusedWidget() == b);
    tapKey(k, st, Key_Tab, Key_LSHIFT);             // Shift+Tab backwards
    CHECK(k.focusedWidget() == a);

    // Enter/Space activate a focused button through the mouse click path.
    int clicks = 0;
    k.bind("go", [&clicks] { ++clicks; });
    b->setBinding("go");
    k.setFocusWidget(b);
    tapKey(k, st, Key_Space);
    CHECK(clicks == 1);
    tapKey(k, st, Key_Enter);
    CHECK(clicks == 2);
    tapKey(k, st, Key_Tab);                      // Tab still traverses
    CHECK(clicks == 2);
    CHECK(k.focusedWidget() == c);

    // A capturing Keybind claims Tab so it can be bound as a key.
    pressAt(k, st, 30.0f, 232.0f);
    releaseAt(k, st);
    CHECK(kb->capturing());
    tapKey(k, st, Key_Tab);
    CHECK(!kb->capturing());
    CHECK(kb->value() == "tab");
    CHECK(k.focusedWidget() == kb);                 // focus did not move
}

static void testInspection() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <panel id="root" x="10" y="10" width="200" height="120">
    <button id="ok" x="4" y="4" width="80" height="24" text="OK" tooltip="apply"/>
    <label id="hint" x="4" y="40" width="120" height="16" text="hint"/>
  </panel>
  <slider id="vol" x="230" y="10" width="100" height="20"/>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    idle(k, st);
    Widget* ok = k.findWidget("ok");
    Widget* hint = k.findWidget("hint");
    Widget* vol = k.findWidget("vol");
    CHECK(ok && hint && vol);
    if (!ok || !hint || !vol) return;

    // ---- dump ----
    const std::string dump = k.dumpTree();
    CHECK(std::count(dump.begin(), dump.end(), '\n') == 5);  // + 4 children
    CHECK(dump.find("Panel root") != std::string::npos);
    CHECK(dump.find("Button ok") != std::string::npos);
    CHECK(dump.find("Slider vol") != std::string::npos);
    // Nested bounds are reported twice: parent-relative and absolute.
    CHECK(dump.find("4,4 80x24 (14,14)") != std::string::npos);
    CHECK(dump.find("tooltip=apply") != std::string::npos);
    CHECK(dump.find("tab=0") != std::string::npos);
    CHECK(dump.find("tab=1") != std::string::npos);
    CHECK(dump.find("focusable") != std::string::npos);
    CHECK(k.tabOrder(ok) == 0);
    CHECK(k.tabOrder(vol) == 1);
    CHECK(k.tabOrder(hint) == -1);         // a label is not a tab stop
    CHECK(Kernel::typeName(ok) == "Button");
    CHECK(Kernel::describe(vol) == "Slider vol");
    CHECK(Kernel::describe(nullptr) == "none");
    // Focus shows up in the dump as soon as it moves.
    k.setFocusWidget(vol);
    CHECK(k.dumpTree().find("Slider vol 230,10 100x20") != std::string::npos);
    CHECK(k.dumpTree().find("focused") != std::string::npos);
    k.setFocusWidget(nullptr);

    // ---- overlay + stats ----
    const size_t plain = k.buildDrawList().vertices.size();
    CHECK(k.stats().widgets == 5);
    CHECK(k.stats().tabStops == 2);
    CHECK(k.stats().vertices == plain);
    CHECK(k.stats().cmds == k.drawList().cmds.size());
    CHECK(k.stats().updateMs >= 0.0f);

    k.setDebugOverlay(Kernel::DebugBounds | Kernel::DebugLabels |
                      Kernel::DebugFocus | Kernel::DebugHover);
    CHECK(k.debugOverlay() ==
          static_cast<uint32_t>(Kernel::DebugBounds | Kernel::DebugLabels |
                                Kernel::DebugFocus | Kernel::DebugHover));
    const DrawList& on = k.buildDrawList();
    CHECK(on.vertices.size() > plain);     // the overlay added geometry
    for (const auto& c : on.cmds) {
        CHECK(c.indexCount > 0);
        CHECK(c.clip.w > 0 && c.clip.h > 0);
    }
    k.toggleDebugOverlay(Kernel::DebugLabels);
    CHECK(k.debugOverlay() == static_cast<uint32_t>(Kernel::DebugBounds |
                                                    Kernel::DebugFocus |
                                                    Kernel::DebugHover));
    k.setDebugOverlay(0);
    CHECK(k.buildDrawList().vertices.size() == plain);

    // The overlay is decoration: the pointer still reaches the widget under it.
    pressAt(k, st, 240.0f, 20.0f);
    releaseAt(k, st);
    CHECK(k.hoveredWidget() == vol);

    // ---- event log ----
    k.setDebugEvents(true);
    CHECK(k.debugEvents());
    int fired = 0;
    k.bind("go", [&fired] { ++fired; });
    k.clearLogs();
    k.invokeBinding("go");
    k.invokeBinding("nope");
    ok->setBinding("go");
    k.clickWidget(ok);
    k.setFocusWidget(ok);
    CHECK(fired == 2);
    const auto has = [&](const char* needle) {
        for (const auto& l : k.logs())
            if (l.find(needle) != std::string::npos) return true;
        return false;
    };
    CHECK(has("event go"));
    CHECK(has("event nope (unbound)"));
    CHECK(has("click Button ok -> go"));
    CHECK(has("focus Button ok"));
    // Off again: the history stops growing.
    k.setDebugEvents(false);
    const size_t n = k.logs().size();
    k.clickWidget(ok);
    CHECK(k.logs().size() == n);
}

static void testCommandPalette() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <commandpalette id="cp" x="20" y="20" width="300" height="120"
                  placeholder="Search...">
    <cmd name="New File" hint="create" key="ctrl+n"/>
    <cmd name="Open File" hint="load from disk" key="ctrl+o"/>
    <cmd name="Save" hint="write to disk" key="ctrl+s"/>
    <cmd label="Close Editor"/>
  </commandpalette>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    idle(k, st);
    auto* cp = dynamic_cast<CommandPalette*>(k.findWidget("cp"));
    CHECK(cp != nullptr);
    if (!cp) return;
    CHECK(cp->count() == 4);
    CHECK(cp->name(0) == "New File");
    CHECK(cp->hint(0) == "create");
    CHECK(cp->key(0) == "ctrl+n");
    CHECK(cp->name(3) == "Close Editor");
    CHECK(!k.buildDrawList().empty());

    // Filtering: name hits rank above hint-only hits, authored order kept.
    cp->setQuery("file");
    CHECK(cp->matchCount() == 2);          // New File, Open File (names)
    cp->setQuery("disk");                  // hint-only matches
    CHECK(cp->matchCount() == 2);
    CHECK(cp->match(0) == "Open File");
    CHECK(cp->match(1) == "Save");
    CHECK(cp->selectedIndex() == 0);
    CHECK(cp->query() == "disk");

    // Programmatic exec is silent but runs and records history.
    std::vector<std::string> runs;
    cp->setOnRun([&](const std::string& n) { runs.push_back(n); });
    CHECK(cp->exec("Save"));
    CHECK(runs.size() == 1 && runs[0] == "Save");
    CHECK(cp->historyCount() == 1 && cp->history(0) == "Save");
    CHECK(!cp->exec("Ghost"));
    cp->setQuery("save");
    CHECK(cp->matchCount() == 1);
    CHECK(cp->exec("Save"));               // history dedupes to the front
    CHECK(cp->historyCount() == 1);
    cp->setQuery("x");
    CHECK(cp->match(9) == std::string());

    // Typing over the focused palette filters live; Enter runs the
    // selection; Backspace edits the query.
    pressAt(k, st, 40.0f, 30.0f);          // header click focuses the widget
    releaseAt(k, st);
    cp->setQuery("");
    st.lastChar = 's';
    k.frame(st);
    st.lastChar = 0;
    CHECK(cp->query() == "s");
    CHECK(cp->matchCount() == 3);          // names: Save, Close Editor; hint: Open File
    CHECK(cp->match(0) == "Save");
    tapKey(k, st, Key_Enter);              // runs row 0
    CHECK(runs.size() == 3 && runs[2] == "Save");
    CHECK(cp->history(0) == "Save");

    tapKey(k, st, Key_Backspace);          // query back to empty
    CHECK(cp->query().empty());
    CHECK(cp->matchCount() == 4);

    // Up/Down walk the filtered rows without running anything.
    tapKey(k, st, Key_Down);
    CHECK(cp->selectedIndex() == 1);
    tapKey(k, st, Key_Up);
    tapKey(k, st, Key_Up);                 // clamps at 0
    CHECK(cp->selectedIndex() == 0);
    tapKey(k, st, Key_End);
    CHECK(cp->selectedIndex() == 3);
    tapKey(k, st, Key_Home);
    CHECK(cp->selectedIndex() == 0);
    CHECK(runs.size() == 3);

    // Clicking a row runs it (rows sit under the 26px header, 22px tall).
    pressAt(k, st, 60.0f, 20.0f + 26.0f + 22.0f + 11.0f);
    releaseAt(k, st);
    CHECK(runs.size() == 4 && runs[3] == "Open File");
    CHECK(cp->historyCount() == 2 && cp->history(0) == "Open File");

    // Re-running the newest command dedupes: size stays 2, order flips.
    cp->exec("Save");
    CHECK(cp->historyCount() == 2 && cp->history(0) == "Save");
}

static void testOnDemandWidgets() {
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"XML(<screen id="s">
  <statusbar id="sb" x="0" y="580" width="800" height="20"
              left="Ready" center="doc.txt" right="3 items"/>
  <breadcrumb id="bc" x="20" y="20" width="400" height="20">
    <crumb label="src"/>
    <crumb label="gui"/>
    <crumb label="main.cpp"/>
  </breadcrumb>
  <keybind id="kb" x="20" y="60" width="160" height="28" value="ctrl+k"/>
</screen>)XML");
    CHECK(res.root != nullptr);
    if (!res.root) return;
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(res.root);
    s->width = 800;
    s->height = 600;
    k.pushScreen(s);

    InputState st = makeInput();
    idle(k, st);
    CHECK(!k.buildDrawList().empty());

    // ---- StatusBar ----
    auto* sb = dynamic_cast<StatusBar*>(k.findWidget("sb"));
    CHECK(sb != nullptr);
    if (sb) {
        CHECK(sb->left() == "Ready");
        CHECK(sb->center() == "doc.txt");
        CHECK(sb->right() == "3 items");
        sb->setText("Busy");            // text= alias writes the left cell
        CHECK(sb->left() == "Busy");
    }

    // ---- Breadcrumb ----
    auto* bc = dynamic_cast<Breadcrumb*>(k.findWidget("bc"));
    CHECK(bc != nullptr);
    if (!bc) return;
    CHECK(bc->segments().size() == 3);
    CHECK(bc->segments()[2] == "main.cpp");
    CHECK(bc->current() == -1);          // -1: last segment shown as current
    std::vector<int> sel;
    bc->setOnSelected([&](int i) { sel.push_back(i); });

    // Click inside the first segment (rects start at the widget origin, and
    // every segment is at least ~15px wide, so +2 is a guaranteed hit).
    pressAt(k, st, 22.0f, 30.0f);
    releaseAt(k, st);
    CHECK(bc->current() == 0);
    CHECK(sel.size() == 1 && sel[0] == 0);

    // Miss right of the last segment: state and callbacks untouched.
    pressAt(k, st, 390.0f, 30.0f);
    releaseAt(k, st);
    CHECK(bc->current() == 0);
    CHECK(sel.size() == 1);

    bc->setCurrent(2);                   // silent
    CHECK(bc->current() == 2);
    CHECK(sel.size() == 1);
    bc->setCurrent(99);                  // out of range clamps to -1
    CHECK(bc->current() == -1);

    // ---- Keybind ----
    auto* kb = dynamic_cast<Keybind*>(k.findWidget("kb"));
    CHECK(kb != nullptr);
    if (!kb) return;
    CHECK(kb->value() == "ctrl+k");

    // Static round-trips.
    CHECK(Keybind::format(Key_K, Mod_Ctrl) == "ctrl+k");
    CHECK(Keybind::format(Key_F5, Mod_Ctrl | Mod_Alt | Mod_Shift | Mod_Super)
          == "ctrl+alt+shift+super+f5");
    {
        KeyCode key = Key_None; uint32_t mods = Mod_None;
        CHECK(Keybind::parse("CTRL+ALT+F5", key, mods));
        CHECK(key == Key_F5 && mods == (Mod_Ctrl | Mod_Alt));
        CHECK(Keybind::parse("", key, mods));      // empty == cleared
        CHECK(key == Key_None && mods == Mod_None);
        CHECK(!Keybind::parse("ctrl+", key, mods)); // no real key
        CHECK(!Keybind::parse("bogus", key, mods));
    }
    CHECK(!kb->setValue("ctrl+"));       // rejected, keeps old value
    CHECK(kb->value() == "ctrl+k");

    std::vector<std::string> kbdChanges;
    kb->setOnChanged([&](const std::string& v) { kbdChanges.push_back(v); });
    CHECK(kb->setValue("f12"));          // programmatic sets stay silent
    CHECK(kbdChanges.empty());

    // Click arms capture, then ctrl+s is recorded and announced.
    pressAt(k, st, 30.0f, 74.0f);
    releaseAt(k, st);
    tapKey(k, st, Key_S, Key_LCTRL);
    CHECK(kb->value() == "ctrl+s");
    CHECK(kbdChanges.size() == 1 && kbdChanges[0] == "ctrl+s");

    // Escape cancels capture without touching the value.
    pressAt(k, st, 30.0f, 74.0f);
    releaseAt(k, st);
    tapKey(k, st, Key_Escape);
    CHECK(kb->value() == "ctrl+s");
    CHECK(kbdChanges.size() == 1);

    // Backspace clears the binding and announces the empty combo.
    pressAt(k, st, 30.0f, 74.0f);
    releaseAt(k, st);
    tapKey(k, st, Key_Backspace);
    CHECK(kb->value().empty());
    CHECK(kbdChanges.size() == 2 && kbdChanges[1].empty());

    // A stray key while not capturing only arms capture (swallowed).
    tapKey(k, st, Key_D, Key_LALT);
    CHECK(kb->value().empty());
    CHECK(kbdChanges.size() == 2);
    tapKey(k, st, Key_D, Key_LALT);      // now the second key is recorded
    CHECK(kb->value() == "alt+d");
    CHECK(kbdChanges.size() == 3);
}

int main() {
    testDrawList();
    testHitTestAndFocus();
    testClickBinding();
    testTextInput();
    testKeyboardRouting();
    testTextInputSelection();
    testTextInputClickAndDrag();
    testTextInputWordSelection();
    testTextInputSelectionDraw();
    testAnchors();
    testGrid();
    testBatchAWidgets();
    testBatchBWidgets();
    testBatchCWidgets();
    testLabelWrapAndColumnAutoHeight();
    testNewWidgets();
    testRadioButtonGroup();
    testFindAndGetText();
    testTabs();
    testTabsOverflow();
    testTooltip();
    testScrollView();
    testTween();
    testWidgetAnimateTo();
    testComboBox();
    testTextArea();
    testListBox();
    testTreeView();
    testToggleAndFileTree();
    testTable();
    testTableFeatures();
    testSimplePorts();
    testImage();
    testRelativeGeometry();
    testSizeLimits();
    testTabsScrolling();
    testConsoleWidget();
    testVisualStates();
    testVisualTransitions();
    testRowWrap();
    testVirtualList();
    testPropertyGrid();
    testTreeTable();
    testOnDemandWidgets();
    testCommandPalette();
    testKeyboardNavigation();
    testDpiScale();
    testInspection();
    testHost();

    if (g_failures) {
        std::cerr << "kernel_test: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "kernel_test: OK\n";
    return 0;
}
