// ogui_lifetime_test.cpp : the pointers the kernel and the widgets keep after
// a tree mutation. Every one of these is a widget that was removed while
// something still remembered it: a screen torn down in the wrong order, a
// tooltip still showing on the outgoing screen, a field inside a removed
// panel, a popup whose owner died, a tree row that vanished mid-animation.
// The host run proves the behaviour; AddressSanitizer is what proves the
// memory, because a dangling pointer usually reads whatever it used to hold.
#include "ogui/kernel.hpp"
#include "ogui/script.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

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

// A screen from a body of widget XML, with one optional Lua chunk. The kernel
// is the only owner of anything mk() returns unless the caller keeps a copy.
std::shared_ptr<Screen> mk(const std::string& id, const std::string& body,
                           const std::string& lua = "") {
    XmlParser p;
    auto res = p.parse("<screen id=\"" + id + "\">" + body +
                       (lua.empty() ? std::string()
                                    : "<script>" + lua + "</script>") +
                       "</screen>");
    assert(res.root);
    auto s = std::make_shared<Screen>(id);
    s->widgets.push_back(res.root);
    if (!lua.empty()) s->scripts.push_back({lua, false});
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

void runLua(Kernel& k, const std::string& chunk) {
    k.script().loadString(chunk, "lifetime");
    CHECK(k.script().errors().empty());
}

} // namespace

// ---- 1. teardown order: the outgoing screen outlives its own events -------
//
// popScreen()/switchScreen() used to drop the stack entry before clearing the
// pointer caches, so the Blur went to a widget the drop had just freed, and a
// host that wrote pushScreen(makeScreen()) had no owner left to keep it alive.
static void testScreenTeardownOrder() {
    Kernel k;
    k.init();
    const std::string field =
        "<textinput id=\"inp\" x=\"20\" y=\"20\" width=\"200\" height=\"30\"/>"
        "<label id=\"out\" text=\"idle\" x=\"20\" y=\"70\" width=\"300\" height=\"24\"/>";
    const std::string blur =
        "ogui.on(\"inp\", \"blur\", function() ogui.set(\"out\", \"text\", \"blurred\") end)";

    auto a = mk("A", field, blur);
    k.pushScreen(a);
    // B is handed over as a temporary: the kernel owns it outright.
    k.pushScreen(mk("B", field, blur));
    InputState st = makeInput();
    idle(k, st);
    pressAt(k, st, 30.0f, 30.0f);
    releaseAt(k, st);
    CHECK(k.focusedWidget() != nullptr);
    CHECK(k.focusedWidget()->id() == "inp");

    k.popScreen();
    CHECK(k.focusedWidget() == nullptr);
    idle(k, st);
    // Back on A, whose field can take focus again.
    pressAt(k, st, 30.0f, 30.0f);
    releaseAt(k, st);
    CHECK(k.focusedWidget() != nullptr);

    // A popup belongs to the screen that opened it: it is dismissed while that
    // screen is still there to hear about it.
    k.pushScreen(mk("C",
        "<combobox id=\"cb\" x=\"60\" y=\"60\" width=\"200\" height=\"28\" selected=\"0\">"
        "<option label=\"Baixo\" value=\"0\"/><option label=\"Medio\" value=\"1\"/>"
        "</combobox>"));
    CHECK(k.focusedWidget() == nullptr);
    idle(k, st);
    pressAt(k, st, 120.0f, 74.0f);
    releaseAt(k, st);
    CHECK(k.popupOpen());
    k.pushScreen(mk("D", field, blur));
    CHECK(!k.popupOpen());
    CHECK(k.focusedWidget() == nullptr);
    idle(k, st);

    k.switchScreen(mk("E", field, blur));
    CHECK(!k.popupOpen());
    idle(k, st);
    pressAt(k, st, 30.0f, 30.0f);
    releaseAt(k, st);
    CHECK(k.focusedWidget() != nullptr);
    k.switchScreen(mk("F", field, blur));
    CHECK(k.focusedWidget() == nullptr);
    idle(k, st);
}

// ---- 2. the tooltip keeps its widget alive or loses it ---------------------
//
// tooltipVisible() reads the hovered widget twice a frame and again while the
// tip is drawn; a widget removed by a script left both of those in front of
// freed memory.
static void testTooltipAfterRemove() {
    Kernel k;
    k.init();
    k.setTooltipDelay(0.1f);
    k.pushScreen(mk("tip",
        "<panel id=\"bg\" x=\"0\" y=\"0\" width=\"800\" height=\"600\"/>"
        "<button id=\"b1\" text=\"hover\" x=\"200\" y=\"200\" width=\"120\" height=\"40\" "
        "tooltip=\"some tip\"/>",
        "function ogui_on_click(key) if key == \"b1\" then ogui.remove(\"b1\") end end"));

    InputState st = makeInput();
    st.mouseX = 240.0f;
    st.mouseY = 220.0f;
    idle(k, st);
    st.time = 1.0f;
    idle(k, st);
    st.time = 2.0f;
    idle(k, st);
    CHECK(k.tooltipVisible());

    // Clicking the widget that is showing its tooltip removes it; the draw at
    // the end of that same frame still has the tooltip up.
    pressAt(k, st, 240.0f, 220.0f);
    releaseAt(k, st);
    CHECK(k.findWidget("b1") == nullptr);
    CHECK(!k.tooltipVisible());
    idle(k, st);
    k.buildDrawList();

    // The same removal through the property-free script path, without a click.
    st.mouseX = 240.0f;
    st.mouseY = 220.0f;
    k.pushScreen(mk("tip2",
        "<panel id=\"bg\" x=\"0\" y=\"0\" width=\"800\" height=\"600\"/>"
        "<button id=\"b2\" text=\"gone\" x=\"200\" y=\"200\" width=\"120\" height=\"40\" "
        "tooltip=\"other tip\"/>"));
    idle(k, st);
    st.time = 3.0f;
    idle(k, st);
    st.time = 4.0f;
    idle(k, st);
    CHECK(k.tooltipVisible());
    runLua(k, "assert(ogui.remove(\"b2\"))");
    CHECK(!k.tooltipVisible());
    k.buildDrawList();
    idle(k, st);
}

// ---- 3. forgetWidget covers the whole subtree -----------------------------
//
// The kernel caches a handful of widgets by pointer; removing a panel takes
// every one of them that lives inside it.
static void testForgetDescendants() {
    Kernel k;
    k.init();
    k.pushScreen(mk("nest",
        "<panel id=\"bg\" x=\"0\" y=\"0\" width=\"800\" height=\"600\"/>"
        "<panel id=\"box\" x=\"0\" y=\"0\" width=\"400\" height=\"300\">"
        "  <textinput id=\"inp\" x=\"20\" y=\"20\" width=\"200\" height=\"30\" "
        "tooltip=\"field tip\"/>"
        "  <slider id=\"sl\" x=\"20\" y=\"80\" width=\"200\" height=\"24\"/>"
        "</panel>"));
    InputState st = makeInput();
    idle(k, st);

    // Focus and a tooltip on the field inside the panel.
    pressAt(k, st, 30.0f, 30.0f);
    releaseAt(k, st);
    CHECK(k.focusedWidget() != nullptr);
    st.time = 1.0f;
    idle(k, st);
    st.time = 2.0f;
    idle(k, st);

    runLua(k, "assert(ogui.remove(\"box\"))");
    // Without the descent these two still point into the panel's freed children.
    CHECK(k.focusedWidget() == nullptr);
    CHECK(!k.tooltipVisible());
    idle(k, st);
    k.buildDrawList();
}

// A press held on a descendant, then the ancestor goes: the release frame
// calls onPointerUp on the widget the removal freed.
static void testForgetPressedDescendant() {
    Kernel k;
    k.init();
    k.pushScreen(mk("nest2",
        "<panel id=\"bg\" x=\"0\" y=\"0\" width=\"800\" height=\"600\"/>"
        "<panel id=\"box\" x=\"0\" y=\"0\" width=\"400\" height=\"300\">"
        "  <slider id=\"sl\" x=\"20\" y=\"80\" width=\"200\" height=\"24\"/>"
        "</panel>"));
    InputState st = makeInput();
    idle(k, st);
    pressAt(k, st, 120.0f, 102.0f);   // hold the slider down
    runLua(k, "assert(ogui.remove(\"box\"))");
    releaseAt(k, st);
    CHECK(k.focusedWidget() == nullptr);
    idle(k, st);
    k.buildDrawList();
}

// ---- 4. a popup outlives the widget that opened it ------------------------
//
// The kernel holds the floating layer as well as the owner does, so a removed
// combo leaves the popup on screen holding callbacks against the dead widget.
static void testPopupOutlivesOwner() {
    Kernel k;
    k.init();
    k.pushScreen(mk("pop",
        "<panel id=\"bg\" x=\"0\" y=\"0\" width=\"800\" height=\"600\"/>"
        "<combobox id=\"cb\" x=\"60\" y=\"60\" width=\"200\" height=\"28\" selected=\"0\">"
        "  <option label=\"Baixo\" value=\"0\"/>"
        "  <option label=\"Medio\" value=\"1\"/>"
        "  <option label=\"Alto\" value=\"2\"/>"
        "</combobox>"));
    InputState st = makeInput();
    idle(k, st);
    pressAt(k, st, 120.0f, 74.0f);
    releaseAt(k, st);
    CHECK(k.popupOpen());

    runLua(k, "assert(ogui.remove(\"cb\"))");
    // The layer is the kernel's, so it stays up; only its owner is gone.
    CHECK(k.popupOpen());
    // A row press reaches callbacks of the widget that no longer exists.
    pressAt(k, st, 160.0f, 100.0f);
    releaseAt(k, st);
    k.buildDrawList();
    // An outside press still dismisses it, which is how it leaves.
    pressAt(k, st, 500.0f, 500.0f);
    releaseAt(k, st);
    CHECK(!k.popupOpen());
    idle(k, st);
}

// ---- 5. the tree animation holds its rows weakly --------------------------
//
// A collapse tweens for 0.18s and draws the rows that vanished; a script that
// deletes one of them during the tween used to be read back by onDraw.
static void testTreeRowsOutliveNodes() {
    Kernel k;
    k.init();
    k.pushScreen(mk("tree",
        "<panel id=\"bg\" x=\"0\" y=\"0\" width=\"800\" height=\"600\"/>"
        "<tree id=\"tr\" x=\"360\" y=\"120\" width=\"240\" height=\"120\" "
        "rowHeight=\"20\" indent=\"14\">"
        "  <treeitem id=\"root\" label=\"Raiz\" expanded=\"true\">"
        "    <treeitem id=\"kid1\" label=\"Filho A\"/>"
        "    <treeitem id=\"kid2\" label=\"Filho B\"/>"
        "  </treeitem>"
        "  <treeitem id=\"other\" label=\"Outra\">"
        "    <treeitem id=\"gkid\" label=\"Neto\"/>"
        "  </treeitem>"
        "</tree>"));

    auto* tr = dynamic_cast<TreeView*>(k.findWidget("tr"));
    CHECK(tr != nullptr);
    if (!tr) return;
    InputState st = makeInput();
    idle(k, st);
    CHECK(tr->rowCount() == 5);

    // Collapse "Raiz": rows A and B vanish and slide away over the tween.
    pressAt(k, st, 367.0f, 130.0f);
    releaseAt(k, st);
    CHECK(tr->rowCount() == 3);
    CHECK(tr->isAnimating());

    runLua(k, "assert(ogui.remove(\"kid1\"))");
    CHECK(k.findWidget("kid1") == nullptr);
    // Draw while the vanished rows are still being interpolated (0.18s).
    for (int i = 0; i < 5; ++i) {
        st.time += 0.02f;
        k.frame(st);
        CHECK(!k.buildDrawList().empty());
    }
    CHECK(tr->isAnimating());
    for (int i = 0; i < 7; ++i) {
        st.time += 0.02f;
        k.frame(st);
        CHECK(!k.buildDrawList().empty());
    }
    CHECK(!tr->isAnimating());
    CHECK(tr->rowCount() == 3);

    // Expand again with the surviving node: the tree is still a live layout.
    pressAt(k, st, 367.0f, 130.0f);
    releaseAt(k, st);
    CHECK(tr->rowCount() == 4);
    for (int i = 0; i < 6; ++i) {
        st.time += 0.02f;
        k.frame(st);
        CHECK(!k.buildDrawList().empty());
    }
    CHECK(tr->rowCount() == 4);
}

int main() {
    testScreenTeardownOrder();
    testTooltipAfterRemove();
    testForgetDescendants();
    testForgetPressedDescendant();
    testPopupOutlivesOwner();
    testTreeRowsOutliveNodes();

    if (g_failures == 0)
        std::cout << "lifetime_test: all checks passed (0 failures)\n";
    else
        std::cout << g_failures << " failure(s)\n";
    return g_failures ? 1 : 0;
}
