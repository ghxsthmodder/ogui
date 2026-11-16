// ogui_responsive_test.cpp : every gallery screen must fit the canvas it is laid
// out on.
//
// The gallery is a fixed 800x600 design, and the same screens are shown on a
// desktop window, on a phone in portrait and on a phone in landscape. What the
// host does is swap the canvas: the layout code cannot reflow a fixed design, so
// the least it owes the user is that nothing is authored outside the canvas --
// a widget whose box leaves the screen is content that gets cut off, and that
// silently survived in three screens (a 1228x700 game menu, a 1200x800 cockpit
// and a widgets grid taller than the window).
//
// The audit walks each screen's laid-out tree and reports a widget whose bounds
// escape the canvas. Subtrees under a container that clips its children are
// skipped: content inside a scroll view or a scrollable container is meant to
// extend past the viewport, and the clip is exactly what makes that legal.
//
// Canvases checked: the desktop demo's 800x600, and the two logical canvases the
// Android fit rule produces for a 1080x2340 phone -- portrait and landscape.
#include "gallery.hpp"
#include "ogui/androidhost.hpp"
#include "ogui/kernel.hpp"
#include "ogui/widgets.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace {

int g_failures = 0;

void fail(const std::string& what) {
    std::cerr << "FAIL " << what << "\n";
    ++g_failures;
}

struct Canvas {
    const char* name;
    float w, h;
};

bool inside(const ogui::Rect& r, const ogui::Rect& outer) {
    return r.x >= outer.x - 0.5f && r.y >= outer.y - 0.5f &&
           r.x + r.w <= outer.x + outer.w + 0.5f &&
           r.y + r.h <= outer.y + outer.h + 0.5f;
}

ogui::Rect intersect(const ogui::Rect& a, const ogui::Rect& b) {
    const float x0 = a.x > b.x ? a.x : b.x;
    const float y0 = a.y > b.y ? a.y : b.y;
    const float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return ogui::Rect{x0, y0, x1 > x0 ? x1 - x0 : 0.0f, y1 > y0 ? y1 - y0 : 0.0f};
}

// Lays `screen` out at c.w x c.h and reports every visible widget whose bounds
// leave that canvas. When `rotateTo` is given the same screen is handed a second
// canvas after the first layout, which is what a rotation does to a running app
// -- and the only way to exercise the edge anchors, whose margins are captured
// on a layout pass and held across later resizes.
int offCanvasWidgets(std::shared_ptr<ogui::Screen> screen, const Canvas& c,
                     std::string* first, const Canvas* rotateTo = nullptr) {
    screen->width = c.w;
    screen->height = c.h;
    screen->scale = 1.0f;

    ogui::Kernel kernel;
    kernel.init();
    kernel.pushScreen(screen);
    ogui::InputState input{};
    kernel.frame(input);

    if (rotateTo) {
        screen->width = rotateTo->w;
        screen->height = rotateTo->h;
        input.time += 0.1f;
        kernel.frame(input);
    }

    const ogui::Rect canvas{0, 0, kernel.logicalWidth(), kernel.logicalHeight()};
    int off = 0;

    struct Item {
        ogui::Widget* w;
        float ax, ay;
        ogui::Rect clip;
    };
    std::vector<Item> stack;
    for (auto& root : screen->widgets)
        if (root) stack.push_back({root.get(), 0.0f, 0.0f, canvas});

    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();
        ogui::Widget* w = item.w;
        const ogui::Rect b = w->bounds();
        const float ax = item.ax + b.x, ay = item.ay + b.y;
        const ogui::Rect abs{ax, ay, b.w, b.h};

        if (w->style().isVisible && b.w > 0.5f && b.h > 0.5f && !inside(abs, canvas)) {
            ++off;
            if (first && first->size() < 200) {
                char buf[192];
                std::snprintf(buf, sizeof buf, "%s %.0f,%.0f %.0fx%.0f; ",
                              w->id().empty() ? "(anon)" : w->id().c_str(), ax, ay,
                              b.w, b.h);
                *first += buf;
            }
        }

        ogui::Rect clipOut;
        const bool clips = w->clipsChildren(clipOut);
        ogui::Rect clip = item.clip;
        if (clips) {
            clip = intersect(clip, ogui::Rect{ax + clipOut.x, ay + clipOut.y,
                                              clipOut.w, clipOut.h});
        }
        // Content inside a container that clips and that itself sits on the
        // canvas is scrolled or cropped on purpose, not a layout bug.
        if (clips && inside(clip, canvas)) continue;
        for (auto& child : w->children())
            if (child) stack.push_back({child.get(), ax, ay, clip});
    }
    return off;
}

} // namespace

int main() {
#ifdef _WIN32
    _chdir(OGUI_SOURCE_DIR);
#else
    chdir(OGUI_SOURCE_DIR);
#endif
    const float dpi = 2.8125f;   // 450 dpi, the phone this was first run on
    const float designW = 800.0f, designH = 600.0f;
    const std::vector<Canvas> canvases = {
        {"desktop 800x600", 800.0f, 600.0f},
        // What fitToDesignScale() gives that phone: portrait keeps the design
        // width, landscape keeps the design height (and gains width).
        {"portrait", 1080.0f / ogui::fitToDesignScale(1080.0f, 2340.0f, designW, designH, dpi),
         2340.0f / ogui::fitToDesignScale(1080.0f, 2340.0f, designW, designH, dpi)},
        {"landscape", 2340.0f / ogui::fitToDesignScale(2340.0f, 1080.0f, designW, designH, dpi),
         1080.0f / ogui::fitToDesignScale(2340.0f, 1080.0f, designW, designH, dpi)},
    };

    for (const Canvas& c : canvases) {
        gallery::Gallery g = gallery::loadAll(designW, designH);
        if (g.screens.empty()) {
            fail("the gallery loaded no screens");
            break;
        }
        for (size_t i = 0; i < g.screens.size(); ++i) {
            std::string first;
            const int off = offCanvasWidgets(g.screens[i], c, &first);
            if (off) {
                fail(std::string(c.name) + ": " + g.titles[i] + " has " +
                     std::to_string(off) + " widget(s) outside the canvas: " + first);
            }
        }
    }

    // Rotations. A freshly parsed screen captures its anchor margins on the
    // canvas it is first laid out on, so the audit above never moves anything;
    // these lay out at the design size first and then hand the screen the
    // landscape canvas a phone gives it (1300x600) and the portrait one
    // (800x1733). Anything anchored to the edge lands outside the canvas here if
    // the anchors are wrong, which is exactly the bug this file is for.
    const std::vector<Canvas> rotations = {
        {"landscape", 1300.0f, 600.0f},
        {"portrait", 800.0f, 1733.0f},
    };
    for (const Canvas& to : rotations) {
        gallery::Gallery g = gallery::loadAll(designW, designH);
        for (size_t i = 0; i < g.screens.size(); ++i) {
            std::string first;
            const int off = offCanvasWidgets(g.screens[i],
                                             Canvas{"design", 800.0f, 600.0f},
                                             &first, &to);
            if (off) {
                fail(std::string(to.name) + " (after a rotation): " +
                     g.titles[i] + " has " + std::to_string(off) +
                     " widget(s) outside the canvas: " + first);
            }
        }
    }

    // The point of the anchors, in pixels: on the landscape canvas the 3D
    // viewport has to reach the right margin and the cockpit's engine column has
    // to sit against it. A layout left at its authored 800 would pass the audit
    // and still leave 500 units of screen unused.
    {
        gallery::Gallery g = gallery::loadAll(designW, designH);
        auto screen = g.screens[9];  // "3D" in manifest.xml
        ogui::Kernel kernel;
        kernel.init();
        kernel.pushScreen(screen);
        screen->width = 800.0f;
        screen->height = 600.0f;
        screen->scale = 1.0f;
        ogui::InputState input{};
        kernel.frame(input);
        screen->width = 1300.0f;
        input.time += 0.1f;
        kernel.frame(input);
        auto* view = kernel.findWidget("view3d");
        if (!view) {
            fail("the 3D screen lost its viewport");
        } else if (view->bounds().w < 1200.0f) {
            fail("the 3D viewport did not grow into a landscape canvas: width " +
                 std::to_string(view->bounds().w));
        }

        // The cockpit's engine column is anchored to the right edge, so its
        // right margin (30 units at 800) has to hold at 1300.
        auto hud = g.screens[8];  // "Cockpit"
        ogui::Kernel k2;
        k2.init();
        k2.pushScreen(hud);
        hud->width = 800.0f;
        hud->height = 600.0f;
        hud->scale = 1.0f;
        ogui::InputState in2{};
        k2.frame(in2);
        hud->width = 1300.0f;
        in2.time += 0.1f;
        k2.frame(in2);
        auto* gear = k2.findWidget("gear");
        if (!gear) {
            fail("the cockpit lost its gear label");
        } else {
            const float right = gear->bounds().x + gear->bounds().w;
            if (right < 1250.0f) {
                fail("the cockpit's engine column did not follow the right edge: "
                     "gear ends at " + std::to_string(right));
            }
        }
    }

    if (g_failures == 0) {
        std::cout << "responsive: every screen fits the desktop and phone canvases,"
                     " before and after a rotation\n";
        return 0;
    }
    std::cerr << "responsive: " << g_failures << " failure(s)\n";
    return 1;
}
