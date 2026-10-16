// Scrollbar drag test: presses on the bars scroll the content instead of
// selecting rows (kernel_test-style frame driving), and scrollableVertical/
// HorizontalContainer stack their children along one axis and lock the other.
#include "ogui/kernel.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"
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

    // ---- ScrollableVerticalContainer: stacks children, scrolls only y ----
    {
        auto sv = std::make_shared<ScrollableVerticalContainer>();
        sv->setId("svc");
        sv->setBounds({0, 0, 200, 100});
        std::vector<std::shared_ptr<Widget>> kids;
        for (int i = 0; i < 10; ++i) {
            auto p = std::make_shared<Panel>();
            p->setBounds({0, 0, 300, 30});   // wider than the viewport: the
            sv->addChild(p);                 // locked axis has room to move
            kids.push_back(p);
        }
        show(k, sv);
        k.frame(st);

        for (int i = 1; i < 10; ++i)
            CHECK(kids[i]->bounds().y > kids[i - 1]->bounds().y + 29.0f);
        CHECK(kids[5]->bounds().x == kids[0]->bounds().x);
        CHECK(sv->contentBounds().h > 300.0f);
        CHECK(sv->contentBounds().w > 200.0f);

        // The cross axis is locked, whichever way the content is moved.
        sv->setScroll(-40.0f, -50.0f);
        CHECK(sv->scrollPosition().x == 0.0f);
        CHECK(sv->scrollPosition().y == -50.0f);
        CHECK(sv->childOffset().x == 0.0f);
        sv->scrollBy(-60.0f, 0.0f);
        CHECK(sv->scrollPosition().x == 0.0f);
        sv->setScroll(0.0f, -200.0f);
        sv->onPointerScroll(-1.0f, -1.0f);   // three notches further down
        CHECK(sv->scrollPosition().x == 0.0f);
        CHECK(sv->scrollPosition().y <= -200.0f);

        // Bar on the right edge: the empty track at the bottom jumps there.
        pressAt(k, st, 194, 95);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y <= -150.0f);
        CHECK(sv->scrollPosition().x == 0.0f);
        pressAt(k, st, 194, 60);
        moveWhileDown(k, st, 194, 4);        // drag the thumb back to the top
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y > -10.0f);

        // A press in the body, away from the bar, scrolls nothing.
        const float yBefore = sv->scrollPosition().y;
        pressAt(k, st, 100, 50);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y == yBefore);
    }

    // ---- ScrollableHorizontalContainer: tiles children, scrolls only x ----
    {
        auto sv = std::make_shared<ScrollableHorizontalContainer>();
        sv->setId("shc");
        sv->setBounds({0, 0, 200, 100});
        std::vector<std::shared_ptr<Widget>> kids;
        for (int i = 0; i < 40; ++i) {
            auto p = std::make_shared<Panel>();
            p->setBounds({0, 0, 30, 140});  // taller than the viewport: the
            sv->addChild(p);                // locked axis has room to move
            kids.push_back(p);
        }
        show(k, sv);
        k.frame(st);

        for (int i = 1; i < 40; ++i)
            CHECK(kids[i]->bounds().x > kids[i - 1]->bounds().x + 29.0f);
        CHECK(kids[20]->bounds().y == kids[0]->bounds().y);
        CHECK(sv->contentBounds().w > 1000.0f);
        CHECK(sv->contentBounds().h > 100.0f);

        sv->setScroll(-50.0f, -30.0f);
        CHECK(sv->scrollPosition().y == 0.0f);
        CHECK(sv->scrollPosition().x == -50.0f);
        sv->scrollBy(0.0f, -35.0f);
        CHECK(sv->scrollPosition().y == 0.0f);
        sv->setScroll(-50.0f, 0.0f);
        sv->onPointerScroll(-1.0f, -1.0f);
        CHECK(sv->scrollPosition().y == 0.0f);
        CHECK(sv->scrollPosition().x <= -80.0f);

        // Bar along the bottom edge: an empty track in the middle jumps the
        // thumb there, not to the end of the line.
        sv->setScroll(0.0f, 0.0f);
        pressAt(k, st, 100, 94);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().x < -450.0f);
        CHECK(sv->scrollPosition().x > -700.0f);
        CHECK(sv->scrollPosition().y == 0.0f);

        // Its right end is the end of the line.
        pressAt(k, st, 195, 94);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().x <= -1100.0f);
        CHECK(sv->scrollPosition().y == 0.0f);

        // The right edge is not a bar here: the vertical bar the plain
        // ScrollView puts there does not exist for this flow.
        const float xBefore = sv->scrollPosition().x;
        pressAt(k, st, 196, 40);
        releaseAt(k, st);
        CHECK(sv->scrollPosition().x == xBefore);
        CHECK(sv->scrollPosition().y == 0.0f);

        pressAt(k, st, 180, 94);
        moveWhileDown(k, st, 4, 94);         // drag the thumb back to the left
        releaseAt(k, st);
        CHECK(sv->scrollPosition().x > -20.0f);
    }

    // ---- A bar shorter than the minimum thumb: it fills the track ----
    {
        auto sv = std::make_shared<ScrollableVerticalContainer>();
        sv->setId("svtiny");
        sv->setBounds({0, 0, 200, 12});      // track 8px, thumb floor 16px
        for (int i = 0; i < 10; ++i) {
            auto p = std::make_shared<Panel>();
            p->setBounds({0, 0, 200, 30});
            sv->addChild(p);
        }
        show(k, sv);
        k.frame(st);                         // drew a clamp(lo>hi) until the
        CHECK(sv->contentBounds().h > 12.0f);  // thumb floor learned to shrink
        sv->onPointerScroll(0.0f, -1.0f);    // the wheel still scrolls
        CHECK(sv->scrollPosition().y < 0.0f);
        pressAt(k, st, 194, 6);              // no drag span: nothing moves
        releaseAt(k, st);
        CHECK(sv->scrollPosition().y < 0.0f);

        auto hs = std::make_shared<ScrollableHorizontalContainer>();
        hs->setId("shtiny");
        hs->setBounds({0, 40, 12, 100});
        for (int i = 0; i < 10; ++i) {
            auto p = std::make_shared<Panel>();
            p->setBounds({0, 0, 30, 100});
            hs->addChild(p);
        }
        show(k, hs);
        k.frame(st);
        CHECK(hs->contentBounds().w > 12.0f);
        hs->onPointerScroll(-1.0f, 0.0f);
        CHECK(hs->scrollPosition().x < 0.0f);
        pressAt(k, st, 6, 40 + 94);
        releaseAt(k, st);
        CHECK(hs->scrollPosition().x < 0.0f);
    }

    // ---- Both bars are hit-tested in local space ----
    {
        auto sv = std::make_shared<ScrollableHorizontalContainer>();
        sv->setId("shcofs");
        sv->setBounds({50, 40, 200, 100});
        for (int i = 0; i < 10; ++i) {
            auto p = std::make_shared<Panel>();
            p->setBounds({0, 0, 30, 100});
            sv->addChild(p);
        }
        show(k, sv);
        k.frame(st);

        pressAt(k, st, 50 + 195, 40 + 94);   // bottom-right of the offset bar
        releaseAt(k, st);
        CHECK(sv->scrollPosition().x <= -100.0f);

        auto vc = std::make_shared<ScrollableVerticalContainer>();
        vc->setId("svcofs");
        vc->setBounds({50, 200, 200, 100});
        for (int i = 0; i < 10; ++i) {
            auto p = std::make_shared<Panel>();
            p->setBounds({0, 0, 200, 30});
            vc->addChild(p);
        }
        show(k, vc);
        k.frame(st);

        pressAt(k, st, 50 + 194, 200 + 95);  // bottom of the offset bar
        releaseAt(k, st);
        CHECK(vc->scrollPosition().y <= -150.0f);
    }

    // ---- The tags build flow containers, not plain ScrollViews ----
    {
        XmlParser parser;
        auto res = parser.parse(R"(<screen id="flows">
  <scrollableverticalcontainer id="svc" x="0" y="0" width="200" height="100"
                               scrollstep="20" gap="20">
    <panel id="a" width="200" height="30"/>
    <panel id="b" width="200" height="30"/>
    <panel id="c" width="200" height="30"/>
    <panel id="g" width="200" height="30"/>
  </scrollableverticalcontainer>
  <scrollablehorizontalcontainer id="shc" x="300" y="0" width="200" height="100">
    <panel id="d" width="30" height="100"/>
    <panel id="e" width="30" height="100"/>
    <panel id="f" width="30" height="100"/>
    <panel id="h" width="30" height="100"/>
    <panel id="i" width="30" height="100"/>
    <panel id="j" width="30" height="100"/>
    <panel id="l" width="30" height="100"/>
    <panel id="m" width="30" height="100"/>
  </scrollablehorizontalcontainer>
</screen>)");
        CHECK(res.root != nullptr);
        CHECK(res.errors.empty());
        std::shared_ptr<ScrollableVerticalContainer> v;
        std::shared_ptr<ScrollableHorizontalContainer> h;
        if (res.root) {
            for (const auto& w : res.root->children()) {
                if (w->id() == "svc") v = std::dynamic_pointer_cast<ScrollableVerticalContainer>(w);
                if (w->id() == "shc") h = std::dynamic_pointer_cast<ScrollableHorizontalContainer>(w);
            }
        }
        CHECK(v != nullptr);
        CHECK(h != nullptr);
        if (v && h) {
            CHECK(v->scrollStep() == 20.0f);   // the ScrollView attributes carry
            auto s = std::make_shared<Screen>("flows");
            s->widgets.push_back(v);
            s->widgets.push_back(h);
            s->width = 800; s->height = 600;
            k.pushScreen(s);
            k.frame(st);
            CHECK(v->contentBounds().h > 100.0f);
            CHECK(v->contentBounds().w <= 200.0f);
            CHECK(h->contentBounds().w > 200.0f);
            CHECK(h->contentBounds().h <= 100.0f);
            const auto& vk = v->children();
            const auto& hk = h->children();
            CHECK(vk.size() == 4 && hk.size() == 8);
            if (vk.size() == 4 && hk.size() == 8) {
                CHECK(vk[3]->bounds().y > vk[0]->bounds().y);
                CHECK(hk[7]->bounds().x > hk[0]->bounds().x);
                CHECK(vk[3]->bounds().x == vk[0]->bounds().x);
                CHECK(hk[7]->bounds().y == hk[0]->bounds().y);
                // The style gap separates the stacked children.
                CHECK(vk[1]->bounds().y - (vk[0]->bounds().y + vk[0]->bounds().h)
                          == 20.0f);
            }
        }
    }

    std::cout << (failures ? "BAR TEST FAILED" : "bar test OK") << "\n";
    return failures ? 1 : 0;
}
