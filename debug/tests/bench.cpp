// ogui_bench.cpp : what a frame costs, measured instead of assumed.
//
// The completion criteria ask for a number wherever a feature can reach the
// tree or the drawing, and the flow containers are exactly such a feature: they
// lay their own children out, a pass the plain ScrollView page never runs. The
// question is therefore not "is it fast" — that answer belongs to whichever
// machine ran it — but "does the thing that was added cost what the thing it
// wraps costs". A ratio answers that on any machine, so every assertion below
// is one; the absolute figures are printed for the record.
#include "ogui/kernel.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#ifndef OGUI_SOURCE_DIR
#define OGUI_SOURCE_DIR "."
#endif

using namespace ogui;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) return;
    std::cout << "bench: FAIL " << what << "\n";
    ++g_failures;
}

enum Kind { Page, Stack, Tile };

const char* kindName(Kind k) {
    return k == Page ? "scrollview" : k == Stack ? "vcontainer" : "hcontainer";
}

// A box of `children` cards on the phone's own canvas: 800 logical units wide
// (the design width the gallery is authored at, after fit-to-width) and 1600
// tall, which is what a 19.5:9 screen is left with once the bars are subtracted.
struct Rig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    std::shared_ptr<ScrollView> box;
    InputState st;

    void build(Kind kind, int children) {
        kernel.init();
        screen = std::make_shared<Screen>("bench");
        screen->width = 800.0f;
        screen->height = 1600.0f;
        screen->scale = 1.0f;
        box = kind == Page ? std::static_pointer_cast<ScrollView>(std::make_shared<ScrollView>())
              : kind == Stack
                    ? std::static_pointer_cast<ScrollView>(std::make_shared<ScrollableVerticalContainer>())
                    : std::static_pointer_cast<ScrollView>(std::make_shared<ScrollableHorizontalContainer>());
        box->setBounds(Rect{0, 70, 800, 400});
        for (int i = 0; i < children; ++i) {
            auto card = std::make_shared<Panel>();
            // A row of cards tiles across, a stack piles down, and a page keeps
            // every card where it was authored -- so the page is the only kind
            // whose positions have to be written out here, and it is the same
            // 35200-unit line either way. All three overflow the 400-unit
            // viewport, which is what makes all three pannable.
            card->setBounds(kind == Tile    ? Rect{0, 0, 120, 380}
                            : kind == Stack ? Rect{0, 0, 780, 44}
                                            : Rect{0, 44.0f * i, 780, 44});
            box->addChild(card);
        }
        screen->widgets.push_back(box);
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
        settle(30);
    }

    void touch(float x, float y) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 1;
        st.touches[0].id = 1;
        st.touches[0].x = x;
        st.touches[0].y = y;
        kernel.frame(st);
    }
    void lift() {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        kernel.frame(st);
    }
    // Frames with nothing happening, long enough for a fling to die out.
    void settle(int frames = 120) {
        lift();
        for (int i = 0; i < frames; ++i) {
            st.time += 1.0f / 60.0f;
            st.touchCount = 0;
            kernel.frame(st);
        }
    }
};

double usNow() {
    return std::chrono::duration<double, std::micro>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// A finger drag: press inside the box, then move diagonally, because a finger
// never travels along one axis and a drag that only offers one cannot show what
// the layout costs on the other.
double panUs(Rig& r, int frames) {
    const double t0 = usNow();
    r.touch(400, 250);
    for (int i = 1; i <= frames; ++i) r.touch(400.0f - 6.0f * i, 250.0f - 3.0f * i);
    r.lift();
    return (usNow() - t0) / (frames + 1);
}

double idleUs(Rig& r, int frames) {
    r.settle();
    const double t0 = usNow();
    for (int i = 0; i < frames; ++i) {
        r.st.time += 1.0f / 60.0f;
        r.st.touchCount = 0;
        r.kernel.frame(r.st);
    }
    return (usNow() - t0) / frames;
}

struct Sample {
    double panUs = 0.0, idleUs = 0.0;
    float updateMs = 0.0f, drawMs = 0.0f;
    size_t widgets = 0, vertices = 0;
};

Sample measure(Kind kind, int children, int frames = 180) {
    Rig r;
    r.build(kind, children);
    Sample s;
    s.panUs = panUs(r, frames);
    s.idleUs = idleUs(r, frames);
    s.updateMs = r.kernel.stats().updateMs;
    s.drawMs = r.kernel.stats().drawMs;
    s.widgets = r.kernel.stats().widgets;
    s.vertices = r.kernel.stats().vertices;
    std::printf("  %-11s %5d cards   pan %7.1f us/frame   idle %7.1f us/frame"
                "   update %.3f ms   draw %.3f ms   %6zu widgets   %7zu verts\n",
                kindName(kind), children, s.panUs, s.idleUs, s.updateMs, s.drawMs,
                s.widgets, s.vertices);
    std::fflush(stdout);
    return s;
}

// ---- The shipped screen, not a synthetic one ----

void shippedScreen() {
    XmlParser parser;
    auto res = parser.parseFile(std::string(OGUI_SOURCE_DIR) + "/layouts/layout_containers.xml");
    if (!res.root) {
        std::cout << "bench: FAIL layouts/layout_containers.xml did not parse\n";
        ++g_failures;
        return;
    }
    auto screen = std::make_shared<Screen>("Containers");
    screen->width = 800.0f;
    screen->height = 1600.0f;
    screen->scale = 1.0f;
    screen->widgets.push_back(res.root);
    screen->scripts = std::move(res.scripts);

    Kernel k;
    k.init();
    k.pushScreen(screen);
    InputState st;
    st.mouseButton = -1;
    st.touchCount = 0;
    for (int i = 0; i < 30; ++i) {
        st.time += 1.0f / 60.0f;
        k.frame(st);
    }
    const double t0 = usNow();
    for (int i = 0; i < 180; ++i) {
        st.time += 1.0f / 60.0f;
        k.frame(st);
    }
    const double idle = (usNow() - t0) / 180;
    std::printf("  %-11s %5s          idle %7.1f us/frame   update %.3f ms"
                "   draw %.3f ms   %6zu widgets   %7zu verts\n",
                "layout", "(all)", idle, k.stats().updateMs, k.stats().drawMs,
                k.stats().widgets, k.stats().vertices);
    std::fflush(stdout);
    check(idle > 0.0, "the shipped screen was measured");
}

}  // namespace

int main() {
    std::printf("bench: one box, N panels, 800x1600 canvas, 180-frame drags\n");
    const Sample page = measure(Page, 800);
    const Sample stack = measure(Stack, 800);
    const Sample row = measure(Tile, 800);

    // A container that lays its children out costs a constant multiple of the
    // page that does not: the flow is a running offset over the same walk, so
    // anything super-constant here would mean it re-does work per child pair.
    check(stack.panUs < page.panUs * 3.0 + 1.0,
          "a stack pans within 3x of the page it replaces");
    check(row.panUs < page.panUs * 3.0 + 1.0,
          "a row pans within 3x of the page it replaces");

    std::printf("bench: the row, growing the line\n");
    const Sample small = measure(Tile, 200);
    const Sample big = measure(Tile, 3200);
    // 16x the cards. Linear is 16x the time; quadratic would be 256x, and the
    // 40x ceiling is where a quadratic term would already have shown itself on
    // any machine, however slow.
    check(big.panUs < small.panUs * 40.0,
          "16x the cards costs under 40x the frame, not 256x");

    std::printf("bench: the shipped gallery screen\n");
    shippedScreen();

    // A frame that changes nothing must re-tessellate nothing. `drawMs` is the
    // library's own number for the build, and on the reused path it is the time
    // of the early return, so what is asserted is the ratio between a frame that
    // rebuilt and one that did not.
    Rig r;
    r.build(Tile, 800);
    panUs(r, 60);
    r.settle();
    for (int i = 0; i < 30; ++i) {
        r.st.time += 1.0f / 60.0f;
        r.st.touchCount = 0;
        r.kernel.frame(r.st);
    }
    const float reused = r.kernel.stats().drawMs;
    r.kernel.invalidate();
    r.st.time += 1.0f / 60.0f;
    r.kernel.frame(r.st);
    const float rebuilt = r.kernel.stats().drawMs;
    std::printf("  draw list: idle frame %.4f ms, after invalidate() %.4f ms\n",
                reused, rebuilt);
    check(reused * 10.0f < rebuilt,
          "an idle frame reuses the draw list instead of rebuilding it");

    // Scrolling moves geometry; it must not add any. A card entering or leaving
    // the viewport is four vertices, so the count may differ by a few quads --
    // what it may not do is grow with how far the line has been dragged, or
    // with how long the line is.
    Rig q;
    q.build(Tile, 800);
    const size_t before = q.kernel.stats().vertices;
    panUs(q, 120);
    std::printf("  draw list across a pan: %zu -> %zu vertices\n", before,
                q.kernel.stats().vertices);
    check(q.kernel.stats().vertices <= before + 32,
          "a pan issues the geometry the viewport shows, not the whole line");
    check(big.vertices <= small.vertices * 3 / 2,
          "16x the cards does not 16x the draw list");

    std::cout << (g_failures ? "bench FAILED" : "bench OK") << "\n";
    return g_failures ? 1 : 0;
}
