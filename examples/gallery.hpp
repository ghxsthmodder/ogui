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

#include <algorithm>
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

// Applies <manifest theme="dark|light|claro|escuro"> once at startup; screens
// re-parsed later (theme toggle) keep whatever theme is active then.
inline void applyManifestTheme(const std::string& file = "manifest.xml") {
    static bool applied = false;
    if (applied) return;
    applied = true;
    std::ifstream f(file);
    if (!f.good()) return;
    std::stringstream ss;
    ss << f.rdbuf();
    ogui::XmlParser p;
    auto res = p.parse(ss.str());
    if (!res.root) return;
    const std::string* t = res.root->rawAttribute("theme");
    if (!t || t->empty()) return;
    if (*t == "light" || *t == "claro") ogui::setActiveTheme(ogui::lightTheme());
    else if (*t == "dark" || *t == "escuro") ogui::setActiveTheme(ogui::darkTheme());
    else {
        // Any other value names a <theme> stylesheet file layered over dark.
        ogui::XmlParser tp;
        auto tr = tp.parseThemeFile(*t, ogui::darkTheme());
        if (!tr.errors.empty()) {
            for (const auto& e : tr.errors)
                std::cerr << "gallery: theme " << *t << ": " << e << "\n";
            std::cerr << "gallery: falling back to dark\n";
            return;
        }
        ogui::setActiveTheme(tr.theme);
    }
}

// A Prev / Title / Next bar laid out as a Row across the top of a screen.
// Returns the title label so the caller can stamp the group name on it.
inline ogui::Label* addNavBar(const ogui::Widget::Ptr& root, float width,
                              bool light) {
    auto bar = std::make_shared<ogui::Row>();
    bar->setId("navbar");
    bar->setBounds(ogui::Rect{10.0f, 10.0f, width - 20.0f, 32.0f});
    // Fully absolute: the screen may be laid out at the design minimum while
    // the host window is narrower, and the bar must stay inside the visible
    // edge rather than stretch across the clamped layout width.
    bar->setGeometrySpec(ogui::Rect{10.0f, 10.0f, width - 20.0f, 32.0f},
                         ogui::Widget::GeomAbsolute, ogui::Widget::GeomAbsolute,
                         ogui::Widget::GeomAbsolute, ogui::Widget::GeomAbsolute);

    auto prev = std::make_shared<ogui::Button>();
    prev->setId("nav_prev");
    prev->setText("< Voltar");
    prev->setBounds(ogui::Rect{0.0f, 0.0f, 120.0f, 32.0f});
    prev->setBinding("navPrev");
    prev->setBgColor(ogui::palette().surfaceHi);
    prev->setTextColor(ogui::palette().textBright);
    prev->setRadius(6.0f);

    auto title = std::make_shared<ogui::Label>();
    title->setId("nav_title");
    title->setBounds(ogui::Rect{0.0f, 0.0f, 420.0f, 32.0f});
    title->setTextColor(ogui::palette().text);
    title->setFontSize(16);

    auto next = std::make_shared<ogui::Button>();
    next->setId("nav_next");
    next->setText("Avancar >");
    next->setBounds(ogui::Rect{0.0f, 0.0f, 120.0f, 32.0f});
    next->setBinding("navNext");
    next->setBgColor(ogui::palette().surfaceHi);
    next->setTextColor(ogui::palette().textBright);
    next->setRadius(6.0f);

    auto theme = std::make_shared<ogui::ToggleSwitch>();
    theme->setId("nav_theme");
    theme->setBounds(ogui::Rect{0.0f, 0.0f, 110.0f, 32.0f});
    theme->setText("Tema claro");
    theme->setState(light);
    theme->setBinding("navTheme");

    bar->addChild(prev);
    bar->addChild(title);
    bar->addChild(next);
    bar->addChild(theme);
    root->addChild(bar);
    return title.get();
}

// Parse every manifest entry into a Screen (with the nav bar injected). Files
// that fail to load are reported and skipped, so a missing group never takes
// the whole demo down.
struct Gallery {
    std::vector<std::string> titles;
    std::vector<std::shared_ptr<ogui::Screen>> screens;
    bool light = false;  // current theme, kept across re-parses
};

// setActiveTheme() copies the preset, so identity must be checked by palette
// value, not pointer.
inline bool activeThemeIsLight() {
    const ogui::Color& a = ogui::activeTheme().palette.screen;
    const ogui::Color& b = ogui::lightTheme().palette.screen;
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

inline Gallery loadAll(float width, float height) {
    applyManifestTheme();
    // The layouts mix fixed-px rows with %-height boxes; below roughly this
    // design size the percentage rows slide up into the boxes above them.
    // Keep the layout at its minimum and let a smaller host clip the edges.
    const float contentW = std::max(width, 800.0f);
    const float contentH = std::max(height, 600.0f);
    Gallery g;
    g.light = activeThemeIsLight();
    ogui::XmlParser parser;
    for (const auto& [title, path] : manifest()) {
        std::ifstream f(path);
        if (!f.good()) {
            std::cerr << "gallery: layout not found: " << path << "\n";
            continue;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        auto res = parser.parse(ss.str(), &ogui::activeTheme());
        if (!res.root) {
            std::cerr << "gallery: parse failed: " << path << "\n";
            for (const auto& e : res.errors) std::cerr << "  - " << e << "\n";
            continue;
        }
        addNavBar(res.root, width, g.light);
        auto screen = std::make_shared<ogui::Screen>(title);
        screen->width = contentW;
        screen->height = contentH;
        screen->widgets.push_back(res.root);
        g.titles.push_back(title);
        g.screens.push_back(screen);
    }
    return g;
}

// Wire the nav bar: bind navPrev/navNext once (bindings key off the widget id,
// which is identical on every screen) and switch the active screen. `g` and
// `index` must outlive the kernel. `start` selects the initial screen.
// `width`/`height` size the re-parse done by the navTheme toggle.
inline void install(ogui::Kernel& k, Gallery& g, int& index, int start = 0,
                    float width = 800.0f, float height = 600.0f) {
    const int n = static_cast<int>(g.screens.size());
    index = (n > 0) ? (start < 0 ? 0 : (start >= n ? n - 1 : start)) : 0;
    if (g.screens.empty()) return;

    // show() must live on the heap: the bound lambdas capture it by value and
    // outlive install(), so a stack lambda would dangle.
    auto show = std::make_shared<std::function<void()>>([&k, &g, &index] {
        k.switchScreen(g.screens[static_cast<size_t>(index)]);
        std::ostringstream os;
        os << (index + 1) << "/" << g.screens.size() << "   "
           << g.titles[static_cast<size_t>(index)];
        k.setText("nav_title", os.str());
    });

    k.bind("navPrev", [&index, n, show] {
        index = (index + n - 1) % n;
        (*show)();
    });
    k.bind("navNext", [&index, n, show] {
        index = (index + 1) % n;
        (*show)();
    });

    // Dark <-> light: install the preset, re-parse every layout so @tokens
    // and defaultStyle re-resolve, then re-show the current screen. The old
    // trees are parked in a graveyard because the clicked toggle is still
    // mid-dispatch inside one of them.
    k.bind("navTheme", [&k, &g, &index, show, width, height] {
        const bool light = !g.light;
        ogui::setActiveTheme(light ? ogui::lightTheme() : ogui::darkTheme());
        g.light = light;
        Gallery ng = loadAll(width, height);
        if (ng.screens.empty()) return;
        static std::vector<std::shared_ptr<ogui::Screen>> graveyard;
        graveyard = std::move(g.screens);  // previous round's trees freed here
        g.titles = std::move(ng.titles);
        g.screens = std::move(ng.screens);
        index = std::clamp(index, 0, static_cast<int>(g.screens.size()) - 1);
        (*show)();
        k.showToast(light ? "Tema claro." : "Tema escuro.",
                    ogui::Kernel::ToastKind::Info);
    });

    // Widgets screen: modal open/close and toast banners.
    k.bind("btn_modal", [&k] {
        if (auto* m = dynamic_cast<ogui::Modal*>(k.findWidget("md_confirm")))
            m->setOpen(true);
    });
    k.bind("btn_cancel", [&k] {
        if (auto* m = dynamic_cast<ogui::Modal*>(k.findWidget("md_confirm")))
            m->setOpen(false);
    });
    k.bind("btn_ok", [&k] {
        if (auto* m = dynamic_cast<ogui::Modal*>(k.findWidget("md_confirm")))
            m->setOpen(false);
        k.showToast("Shaders regravaidos.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("btn_toast", [&k] {
        k.showToast("Texturas carregadas (12).", ogui::Kernel::ToastKind::Warn);
    });

    // Widgets screen: menu commands and context menu.
    k.bind("file_new", [&k] {
        k.showToast("Cena nova criada.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("file_open", [&k] {
        k.showToast("Abrir cena: use o seletor de arquivo.",
                    ogui::Kernel::ToastKind::Info);
    });
    k.bind("file_export", [&k] {
        k.showToast("Cena exportada.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("render_quality", [&k] {
        k.showToast("Qualidade: Alta.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("shadow_sm", [&k] {
        k.showToast("Sombras: shadow maps.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("shadow_rt", [&k] {
        k.showToast("Sombras: ray tracing.", ogui::Kernel::ToastKind::Warn);
    });
    k.bind("help_about", [&k] {
        k.showToast("ogui: XML + OpenGL 3.3.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("cm_clear", [&k] {
        k.setText("se_busca", "");
    });
    k.bind("cm_add", [&k] {
        if (auto* b = dynamic_cast<ogui::Badge*>(k.findWidget("bg_inst"))) {
            b->setVisible(true);
            b->setCount(b->count() + 1);
        }
    });
    k.bind("cm_props", [&k] {
        k.showToast("Propriedades do formulario.", ogui::Kernel::ToastKind::Info);
    });
    k.bind("cp_change", [&k] {
        if (auto* cp = dynamic_cast<ogui::ColorPicker*>(k.findWidget("cp_tint")))
            k.setText("lbl_cp", "Cor: " + cp->colorHex());
    });
    k.bind("pp_change", [&k] {
        if (auto* pp = dynamic_cast<ogui::PathPicker*>(k.findWidget("pp_mesh")))
            k.setText("lbl_cp", "Arquivo: " + pp->value());
    });
    k.bind("re_change", [&k] {
        if (auto* re = dynamic_cast<ogui::RampEditor*>(k.findWidget("re_ramp")))
            k.showToast("Rampa: " + std::to_string(re->stopCount()) + " stops.",
                        ogui::Kernel::ToastKind::Info);
    });
    k.bind("ce_change", [&k] {
        if (auto* ce = dynamic_cast<ogui::CurveEditor*>(k.findWidget("ce_curve")))
            k.showToast("Curva em t=0.5: " + std::to_string(
                            static_cast<int>(ce->evaluate(0.5f) * 100.0f)) + "%",
                        ogui::Kernel::ToastKind::Info);
    });
    k.bind("ts_change", [&k] {
        if (auto* ts = dynamic_cast<ogui::ToggleSwitch*>(k.findWidget("ts_vsync")))
            k.showToast(std::string("VSync: ") + (ts->state() ? "ligado" : "desligado"),
                        ogui::Kernel::ToastKind::Info);
    });
    k.bind("tree_change", [&k] {
        if (auto* tr = dynamic_cast<ogui::TreeView*>(k.findWidget("tr_files")))
            k.setText("lbl_tree", std::to_string(tr->checkedCount()) +
                                  " itens marcados");
    });

    k.pushScreen(g.screens[static_cast<size_t>(index)]);
    (*show)();
}

} // namespace gallery
