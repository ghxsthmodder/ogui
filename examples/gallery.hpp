#pragma once
// gallery.hpp : shared multi-screen loader for the ogui demos.
//
// The demo is a small "widget gallery": every available widget lives on one of
// several grouped screens, and a Prev/Title/Next bar (injected here, not baked
// into each XML file) cycles between them. All screens run inside a single
// process / demo.exe; navigation just swaps the Kernel's active screen.
//
// Each layout file authors its content starting at y >= 52 so the injected bar
// never covers it.
#include "ogui/kernel.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/widgets.hpp"

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace gallery {

// Ordered {title, path} manifest read from manifest.xml: <manifest> with one
// <screen title="..." path="..."/> per gallery group. Editing the file adds
// or removes screens without recompiling. Falls back to a built-in list when
// manifest.xml is missing or unusable, so the demo never opens empty.
inline std::vector<std::pair<std::string, std::string>> manifest(
        const std::string& file = "manifest.xml") {
    std::vector<std::pair<std::string, std::string>> list;
    std::ifstream f(file);
    if (f.good()) {
        std::stringstream ss;
        ss << f.rdbuf();
        ogui::XmlParser p;
        auto res = p.parse(ss.str());
        if (res.root) {
            for (const auto& c : res.root->children()) {
                if (!c) continue;
                const std::string* path = c->rawAttribute("path");
                if (!path) continue;
                const std::string* title = c->rawAttribute("title");
                list.emplace_back(title ? *title : *path, *path);
            }
        }
        if (!list.empty()) return list;
        std::cerr << "gallery: no <screen path=...> entries in " << file
                  << ", using built-in list\n";
    }
    return {
        {"Basico",     "layout_demo.xml"},
        {"Texto",      "layouts/layout_text.xml"},
        {"Selecao",    "layouts/layout_selection.xml"},
        {"Dados",      "layouts/layout_data.xml"},
        {"Containers", "layouts/layout_containers.xml"},
    };
}

// A Prev / Title / Next bar laid out as a Row across the top of a screen.
// Returns the title label so the caller can stamp the group name on it.
inline ogui::Label* addNavBar(const ogui::Widget::Ptr& root, float width) {
    auto bar = std::make_shared<ogui::Row>();
    bar->setId("navbar");
    bar->setBounds(ogui::Rect{10.0f, 10.0f, width - 20.0f, 32.0f});
    // x/y/h stay absolute (top-anchored); width tracks the window.
    bar->setGeometrySpec(ogui::Rect{1.25f, 10.0f, 97.5f, 32.0f},
                         ogui::Widget::GeomPercent, ogui::Widget::GeomAbsolute,
                         ogui::Widget::GeomPercent, ogui::Widget::GeomAbsolute);

    auto prev = std::make_shared<ogui::Button>();
    prev->setId("nav_prev");
    prev->setText("< Voltar");
    prev->setBounds(ogui::Rect{0.0f, 0.0f, 120.0f, 32.0f});
    prev->setBinding("navPrev");
    prev->setBgColor(ogui::hexColor("#333"));
    prev->setTextColor(ogui::hexColor("#eee"));
    prev->setRadius(6.0f);

    auto title = std::make_shared<ogui::Label>();
    title->setId("nav_title");
    title->setBounds(ogui::Rect{0.0f, 0.0f, 420.0f, 32.0f});
    title->setTextColor(ogui::hexColor("#8fe"));
    title->setFontSize(16);

    auto next = std::make_shared<ogui::Button>();
    next->setId("nav_next");
    next->setText("Avancar >");
    next->setBounds(ogui::Rect{0.0f, 0.0f, 120.0f, 32.0f});
    next->setBinding("navNext");
    next->setBgColor(ogui::hexColor("#333"));
    next->setTextColor(ogui::hexColor("#eee"));
    next->setRadius(6.0f);

    bar->addChild(prev);
    bar->addChild(title);
    bar->addChild(next);
    root->addChild(bar);
    return title.get();
}

// Parse every manifest entry into a Screen (with the nav bar injected). Files
// that fail to load are reported and skipped, so a missing group never takes
// the whole demo down.
struct Gallery {
    std::vector<std::string> titles;
    std::vector<std::shared_ptr<ogui::Screen>> screens;
};

inline Gallery loadAll(float width, float height) {
    Gallery g;
    ogui::XmlParser parser;
    for (const auto& [title, path] : manifest()) {
        std::ifstream f(path);
        if (!f.good()) {
            std::cerr << "gallery: layout not found: " << path << "\n";
            continue;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        auto res = parser.parse(ss.str());
        if (!res.root) {
            std::cerr << "gallery: parse failed: " << path << "\n";
            for (const auto& e : res.errors) std::cerr << "  - " << e << "\n";
            continue;
        }
        addNavBar(res.root, width);
        auto screen = std::make_shared<ogui::Screen>(title);
        screen->width = width;
        screen->height = height;
        screen->widgets.push_back(res.root);
        g.titles.push_back(title);
        g.screens.push_back(screen);
    }
    return g;
}

// Wire the nav bar: bind navPrev/navNext once (bindings key off the widget id,
// which is identical on every screen) and switch the active screen. `g` and
// `index` must outlive the kernel. `start` selects the initial screen.
inline void install(ogui::Kernel& k, Gallery& g, int& index, int start = 0) {
    const int n = static_cast<int>(g.screens.size());
    index = (n > 0) ? (start < 0 ? 0 : (start >= n ? n - 1 : start)) : 0;
    if (g.screens.empty()) return;

    auto show = [&k, &g, &index] {
        k.switchScreen(g.screens[static_cast<size_t>(index)]);
        std::ostringstream os;
        os << (index + 1) << "/" << g.screens.size() << "   "
           << g.titles[static_cast<size_t>(index)];
        k.setText("nav_title", os.str());
    };

    k.bind("navPrev", [&index, n, show] {
        index = (index + n - 1) % n;
        show();
    });
    k.bind("navNext", [&index, n, show] {
        index = (index + 1) % n;
        show();
    });

    k.pushScreen(g.screens[static_cast<size_t>(index)]);
    show();
}

} // namespace gallery
