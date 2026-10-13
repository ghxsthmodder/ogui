// Scrollbar drag test: presses on the bars scroll the content instead of
// selecting rows (kernel_test-style frame driving).
#include "ogui/kernel.hpp"
#include "ogui/widgets.hpp"
#include <cassert>
#include <iostream>

using namespace ogui;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __LINE__ << "  " #cond "\n";              \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static InputState makeInput() {
    InputState st;
    st.mouseButton = -1;
    return st;
}

static void pressAt(Kernel& k, InputState& st, float x, float y, int btn = 0) {
    st.mouseX = x; st.mouseY = y; st.mouseButton = btn; st.mouseDown[btn] = true;
    k.frame(st);
}
static void moveWhileDown(Kernel& k, InputState& st, float x, float y) {
    st.mouseX = x; st.mouseY = y;
    k.frame(st);
}
static void releaseAt(Kernel& k, InputState& st) {
    st.mouseDown[0] = false; st.mouseButton = -1;
    k.frame(st);
}

static std::shared_ptr<Screen> show(Kernel& k, std::shared_ptr<Widget> w) {
    auto s = std::make_shared<Screen>("s");
    s->widgets.push_back(w);
    s->width = 800; s->height = 600;
    k.pushScreen(s);
    return s;
}

int main() {
    Kernel k;
    k.init();
    InputState st = makeInput();

    // ---- ListBox: 100 rows of 24px inside a 100px viewport ----
    {
        auto lb = std::make_shared<ListBox>();
        lb->setId("lb");
        lb->setBounds({0, 0, 200, 100});
        std::vector<std::string> items;
        for (int i = 0; i < 100; ++i) items.push_back("item " + std::to_string(i));
        lb->setItems(items);
        show(k, lb);
        k.frame(st);

        // Press on the empty bottom track: jumps, does NOT select a row.
        pressAt(k, st, 194, 95);
        releaseAt(k, st);
        CHECK(lb->selectedIndex() == -1);

        // A click in the middle now hits a late row -> scroll jumped.
        pressAt(k, st, 100, 50);
        releaseAt(k, st);
        CHECK(lb->selectedIndex() > 60);

        // Grab the thumb at the (now scrolled) top? thumb near bottom; drag up.
        pressAt(k, st, 194, 60);            // may be thumb or track, both fine
        moveWhileDown(k, st, 194, 5);       // drag to the very top
        releaseAt(k, st);
        pressAt(k, st, 100, 50);
        releaseAt(k, st);
        CHECK(lb->selectedIndex() >= 0 && lb->selectedIndex() < 10);
    }

    // ---- Table: 100 rows, header 24px, viewport 120px ----
    {
        auto tb = std::make_shared<Table>();
        tb->setId("tb");
        tb->setBounds({0, 0, 300, 120});
        tb->setColumns({{"N", 0, false, false, CellScroll::None}});
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < 100; ++i) rows.push_back({"row " + std::to_string(i)});
        tb->setRows(rows);
        show(k, tb);
        k.frame(st);

        pressAt(k, st, 294, 115);           // bottom of the bar area
        releaseAt(k, st);
        CHECK(tb->selectedIndex() == -1);
        pressAt(k, st, 100, 70);            // middle of body
        releaseAt(k, st);
        CHECK(tb->selectedIndex() > 60);
    }

    // ---- TreeView: fixed height with many rows (autoHeight off) ----
    {
        auto tv = std::make_shared<TreeView>();
        tv->setId("tv");
        tv->setBounds({0, 0, 200, 120});
        tv->setAutoHeight(false);
        for (int i = 0; i < 50; ++i) {
            auto node = std::make_shared<TreeItem>();
            node->setLabel("node " + std::to_string(i));
            tv->addChild(node);
        }
        show(k, tv);
        k.frame(st);

        pressAt(k, st, 194, 115);           // jump to bottom
        releaseAt(k, st);
        CHECK(tv->selectedIndex() == -1);
        pressAt(k, st, 100, 70);
        releaseAt(k, st);
        CHECK(tv->selectedIndex() > 30);
    }

    std::cout << (failures ? "BAR TEST FAILED" : "bar test OK") << "\n";
    return failures ? 1 : 0;
}
