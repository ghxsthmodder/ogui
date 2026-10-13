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

    // ---- ListBox at a non-zero origin: the bar hit region must follow
    // the widget (local-space regression) ----
    {
        auto lb = std::make_shared<ListBox>();
        lb->setId("lbofs");
        lb->setBounds({30, 20, 200, 100});
        std::vector<std::string> items;
        for (int i = 0; i < 100; ++i) items.push_back("item " + std::to_string(i));
        lb->setItems(items);
        show(k, lb);
        k.frame(st);

        pressAt(k, st, 30 + 194, 20 + 95);  // bottom of the offset bar
        releaseAt(k, st);
        CHECK(lb->selectedIndex() == -1);   // the bar took the press
        pressAt(k, st, 30 + 100, 20 + 50);  // body click hits a late row
        releaseAt(k, st);
        CHECK(lb->selectedIndex() > 60);
    }

    // ---- ScrollView: 300px page in a 100px viewport ----
    {
        auto sv = std::make_shared<ScrollView>();
        sv->setId("sv");
        sv->setBounds({0, 0, 200, 100});
        auto page = std::make_shared<Panel>();
        page->setBounds({0, 0, 200, 300});
        sv->addChild(page);
        show(k, sv);
        k.frame(st);
        CHECK(sv->scrollPosition().y == 0.0f);

        // Press the empty bottom track: jumps near the end (claimsPoint
        // keeps the press off the child page underneath).
        pressAt(k, st, 194, 95);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y <= -190.0f);

        // A press inside the content scrolls nothing.
        pressAt(k, st, 100, 50);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y <= -190.0f);

        // Thumb sits at the bottom now; press the empty track at the top.
        pressAt(k, st, 194, 10);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y > -20.0f);

        // Grab the thumb at the top and drag it down.
        pressAt(k, st, 194, 5);
        moveWhileDown(k, st, 194, 95);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y <= -190.0f);
    }

    // ---- ScrollView away from the parent origin ----
    {
        auto sv = std::make_shared<ScrollView>();
        sv->setId("svofs");
        sv->setBounds({50, 40, 200, 100});
        auto page = std::make_shared<Panel>();
        page->setBounds({0, 0, 200, 300});
        sv->addChild(page);
        show(k, sv);
        k.frame(st);

        pressAt(k, st, 50 + 194, 40 + 95);  // bottom of the offset bar
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y <= -190.0f);
    }

    // ---- Window: overflowing body scrolls from the bar under the title ----
    {
        auto win = std::make_shared<Window>();
        win->setId("win");
        win->setTitle("W");
        win->setBounds({20, 30, 300, 140});   // title 24, body 116
        auto page = std::make_shared<Panel>();
        page->setBounds({0, 0, 300, 400});    // overflow = 424 - 140 = 284
        win->addChild(page);
        show(k, win);
        k.frame(st);
        CHECK(win->childOffset().y == 0.0f);

        pressAt(k, st, 20 + 294, 30 + 135);   // bottom of the body track
        releaseAt(k, st);
        CHECK(win->childOffset().y < -200.0f);

        pressAt(k, st, 20 + 294, 30 + 40);    // top of the track: jump back
        releaseAt(k, st);
        CHECK(win->childOffset().y > -20.0f);

        // A press in the body away from the bar must NOT scroll.
        pressAt(k, st, 20 + 150, 30 + 70);
        releaseAt(k, st);
        CHECK(win->childOffset().y > -20.0f);
    }

    // ---- TextArea: a bar press scrolls, it must not select text ----
    {
        auto ta = std::make_shared<TextArea>();
        ta->setId("ta");
        ta->setBounds({10, 20, 220, 120});
        std::string text;
        for (int i = 0; i < 40; ++i) text += "linha " + std::to_string(i) + "\n";
        ta->setText(text);
        show(k, ta);
        k.frame(st);
        const size_t before = ta->caret();

        pressAt(k, st, 10 + 212, 20 + 110);   // bottom of the offset bar
        releaseAt(k, st);
        CHECK(ta->caret() == before);         // the bar took the press
        CHECK(!ta->hasSelection());

        // Body drag still selects normally (bar did not swallow it).
        pressAt(k, st, 10 + 40, 20 + 60);
        moveWhileDown(k, st, 10 + 120, 20 + 60);
        releaseAt(k, st);
        CHECK(ta->hasSelection());
    }

    // ---- ComboBox popup: clamped to the screen, bar scrolls the list ----
    {
        auto cb = std::make_shared<ComboBox>();
        cb->setId("cb");
        cb->setBounds({10, 520, 150, 24});
        for (int i = 0; i < 100; ++i) {
            auto opt = std::make_shared<ComboOption>();
            opt->setLabel("opt " + std::to_string(i));
            cb->addChild(opt);
        }
        show(k, cb);
        k.frame(st);

        pressAt(k, st, 80, 530);             // open the dropdown
        releaseAt(k, st);
        auto* pp = cb->popup();
        CHECK(pp != nullptr);
        if (pp) {
            const Rect pb = pp->bounds();
            CHECK(pb.h <= 588.0f);           // clamped: 600 - 2*6 margin
            CHECK(pb.y >= 0.0f && pb.y + pb.h <= 600.0f);   // fully visible

            // Press the empty track at the bottom: jumps the list to the end.
            pressAt(k, st, pb.x + pb.w - 4, pb.y + pb.h - 4);
            releaseAt(k, st);
            CHECK(pp->scroll() > 100.0f);

            // Reopen (scroll resets), then a row click near the bottom of
            // the list requires the jump first: picks a late option.
            pressAt(k, st, 80, 530);
            releaseAt(k, st);
            pressAt(k, st, pb.x + pb.w - 4, pb.y + pb.h - 4);   // jump to end
            releaseAt(k, st);
            pressAt(k, st, pb.x + 20, pb.y + 20);               // top row
            releaseAt(k, st);
            CHECK(cb->selectedIndex() >= 70);
        }
    }

    std::cout << (failures ? "BAR TEST FAILED" : "bar test OK") << "\n";
    return failures ? 1 : 0;
}
