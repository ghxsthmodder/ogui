// ogui_demo.cpp : headless usage example for ogui.
//
// ogui gives you two ways to drive a screen:
//
//   * Bring your own window/context (Win32+WGL, GLFW, SDL, your engine...):
//     feed ogui::InputState into the Kernel every frame and hand the
//     Kernel's DrawList to ogui::GlBackend::render().
//   * Let the library own it: ogui::Host creates the window, the context and
//     the event loop (see examples/demo_win32.cpp).
//
// This demo is the headless case: it runs without a display, parses the
// sample layout, runs one input + layout + draw frame, and reports what would
// be rendered. The windowed version lives in examples/demo_win32.cpp.
#include "ogui/kernel.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/widgets.hpp"
#include "ogui/glbackend.hpp"
#include <iostream>
#include <fstream>
#include <string>


int main() {
    // Bounds are parent-relative: walk the chain to absolute screen space.
    auto absBounds = [](auto&& self, const std::vector<ogui::Widget::Ptr>& kids,
                        float ox, float oy, const std::string& id)
        -> ogui::Rect {
        for (const auto& w : kids) {
            if (!w) continue;
            const ogui::Rect b = w->bounds();
            const ogui::Rect abs{ox + b.x, oy + b.y, b.w, b.h};
            if (w->id() == id) return abs;
            const ogui::Rect r = self(self, w->children(), abs.x, abs.y, id);
            if (r.w > 0.0f) return r;
        }
        return ogui::Rect{};
    };

    // ---- 1. parse the layout XML ----------------------------------------
    ogui::XmlParser parser;
    ogui::XmlParser::ParseResult res;
    {
        std::ifstream f("layout_demo.xml");
        if (!f.good()) {
            std::cerr << "Layout definition not found! "
                         "Create a layout for rendering screen.\n";
            return 1;
        }
        std::string data((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
        res = parser.parse(data);
    }
    if (!res.root) {
        std::cerr << "XML parse error:\n";
        for (const auto& e : res.errors) std::cerr << "  - " << e << '\n';
        return 1;
    }
    for (const auto& w : res.warnings) std::cerr << "warning: " << w << '\n';
    for (const auto& e : res.errors)   std::cerr << "error: "   << e << '\n';

    auto screen = std::make_shared<ogui::Screen>("main");
    screen->widgets.push_back(res.root);
    screen->width = 800.0f;
    screen->height = 600.0f;

    // ---- 2. kernel + handlers -------------------------------------------
    ogui::Kernel kernel;
    kernel.init();
    kernel.pushScreen(screen);

    int starts = 0, loads = 0;
    kernel.bind("onStart", [&] {
        ++starts;
        kernel.setText("lbl_status", "Iniciar pressionado");
        kernel.addLog("onStart");
    });
    kernel.bind("onLoad", [&] {
        ++loads;
        kernel.setText("lbl_status", "Carregar pressionado");
        kernel.addLog("onLoad");
    });

    // The slider drives the progress bar through its value callback.
    if (auto* sl = dynamic_cast<ogui::Slider*>(kernel.findWidget("sl_demo")))
        if (auto* pb = dynamic_cast<ogui::ProgressBar*>(kernel.findWidget("pb_demo")))
            sl->setOnChanged([pb](float v) { pb->setValue(v / 100.0f); });

    // ---- 3. one input + layout + draw frame ------------------------------
    ogui::InputState input;
    input.mouseButton = -1;
    kernel.processInput(input);
    kernel.frame(input);              // hover frame (establishes layout)

    // Aim at btn_start only after the first layout pass placed everything.
    const ogui::Rect btn = absBounds(absBounds, screen->widgets, 0.0f, 0.0f, "btn_start");
    input.mouseX = btn.x + btn.w * 0.5f;
    input.mouseY = btn.y + btn.h * 0.5f;
    kernel.frame(input);

    input.time = 0.4f;                // advance the tooltip hover clock
    input.mouseDown[0] = true;        // press
    input.mouseButton = 0;
    kernel.frame(input);

    input.time = 0.8f;                // past the 0.5s tooltip delay
    input.mouseDown[0] = false;       // release -> click
    input.mouseButton = -1;
    kernel.frame(input);

    const ogui::DrawList& dl = kernel.buildDrawList();
    std::cout << "tooltip: " << (kernel.tooltipVisible() ? "shown" : "hidden")
              << "\n";

    std::cout << "layout: " << res.root->children().size() << " top-level widgets\n";
    std::cout << "drawlist: " << dl.vertices.size() << " vertices, "
              << dl.indices.size() / 3 << " triangles, "
              << dl.cmds.size() << " batch(es)\n";
    for (const auto& c : dl.cmds) {
        std::cout << "  batch tex=" << c.texId
                  << " clip=(" << c.clip.x << "," << c.clip.y
                  << " " << c.clip.w << "x" << c.clip.h << ")"
                  << " indices=" << c.indexCount << "\n";
    }
    std::cout << "clicks: onStart=" << starts << " onLoad=" << loads << "\n";
    std::cout << "status: " << kernel.getText("lbl_status") << "\n";

    // Font resolution (no GPU here, but the manager must find a system font).
    if (const ogui::Font* f = ogui::FontManager::instance().get("sans", 22)) {
        std::cout << "font: " << f->path() << " atlas "
                  << f->atlasWidth() << "x" << f->atlasHeight()
                  << " measure('Boas-vindas!')=" << f->measure("Boas-vindas!")
                  << "\n";
    } else {
        std::cout << "font: none found (text will be skipped)\n";
    }

    // The GlBackend would be initialized here on a host with a GL context:
    //   ogui::GlBackend backend;         // after the context is current
    //   backend.init();
    //   backend.render(kernel.drawList(), fbw, fbh);
    // render() uploads the font atlases first, so text works from frame one.

    bool ok = starts == 1 && loads == 0 && !dl.empty();
    std::cout << (ok ? "demo: OK\n" : "demo: FAILED\n");
    return ok ? 0 : 1;
}
