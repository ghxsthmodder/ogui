#include <fstream>
// ogui_screenshot.cpp : headless renderer + PNG dump.
//
// Renders the same layout as the demos straight from the Kernel's DrawList
// into an RGBA framebuffer using a small CPU rasterizer (barycentric fill,
// bilinear font-atlas sampling, GL-compatible src-alpha blending), then
// writes it to debug/screenshots/ with stb_image_write.
//
// This is a debugging aid: it shows exactly what GlBackend::render() would
// draw, without needing a window, a GL context or a display.
//
//   ./ogui_screenshot [out.png] [width] [height]
#include "ogui/kernel.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/widgets.hpp"
#include "ogui/drawlist.hpp"
#include "ogui/font.hpp"
#include "ogui/software.hpp"
#include "gallery.hpp"

// stb is third-party; its designated-initializer style trips -Wextra.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using ogui::DrawList;
using ogui::Rect;

// Bounds are parent-relative (hitTestTree accumulates them), so a host that
// wants to aim at a widget has to walk the chain first.
bool absBounds(const std::vector<ogui::Widget::Ptr>& kids, float ox, float oy,
               const std::string& id, ogui::Rect& out) {
    for (const auto& w : kids) {
        if (!w) continue;
        const ogui::Rect b = w->bounds();
        const float ax = ox + b.x, ay = oy + b.y;
        if (w->id() == id) {
            out = ogui::Rect{ax, ay, b.w, b.h};
            return true;
        }
        if (absBounds(w->children(), ax, ay, id, out)) return true;
    }
    return false;
}

// ---- input simulation ----------------------------------------------------
struct SimInput {
    ogui::InputState st;
    ogui::Kernel& k;

    void frame() { k.frame(st); }
    void hover(float x, float y) { st.mouseX = x; st.mouseY = y; }
    void click(float x, float y) {
        hover(x, y);
        st.mouseDown[0] = true;
        st.mouseButton = 0;
        k.frame(st);
        st.mouseDown[0] = false;
        st.mouseButton = -1;
        k.frame(st);
        st.mouseButton = 0;
    }
    // Delivers each UTF-8 code point as one character (a byte-wise loop
    // would split "ç" into two bogus input events).
    void type(const char* s) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
        while (*p) {
            st.lastChar = ogui::utf8Next(p);
            k.frame(st);
        }
        st.lastChar = 0;
        k.frame(st);
    }
};

} // namespace

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "debug/screenshots/ogui_debug.png";
    const int outW = argc > 2 ? std::atoi(argv[2]) : 800;
    const int outH = argc > 3 ? std::atoi(argv[3]) : 600;
    const int ssaa = 2;

    // ---- 1. gallery + kernel ---------------------------------------------
    // An optional 4th arg selects which grouped screen to render (default 0).
    int screenIndex = argc > 4 ? std::atoi(argv[4]) : 0;
    gallery::Gallery g = gallery::loadAll(static_cast<float>(outW),
                                          static_cast<float>(outH));
    if (g.screens.empty()) {
        std::fprintf(stderr, "screenshot: no layouts found "
                             "(need layout_demo.xml + layouts/)\n");
        return 1;
    }
    screenIndex = std::clamp(screenIndex, 0, static_cast<int>(g.screens.size()) - 1);
    auto screen = g.screens[static_cast<size_t>(screenIndex)];

    ogui::Kernel kernel;
    kernel.init();
    int navIndex = 0;
    gallery::install(kernel, g, navIndex, screenIndex);

    kernel.bind("onStart", [&] {
        kernel.setText("lbl_status", "Iniciar pressionado!");
        kernel.addLog("onStart");
    });
    kernel.bind("onLoad", [&] {
        kernel.setText("lbl_status", "Carregar pressionado!");
        kernel.addLog("onLoad");
    });

    // The combobox lives on the selection screen; only wire/interact with it
    // when that screen is the active one (findWidget searches the top screen).
    auto* cmb = dynamic_cast<ogui::ComboBox*>(kernel.findWidget("cmb_fps"));
    if (cmb) {
        cmb->setOnChanged([&](int) {
            kernel.setText("lbl_status",
                           "Fps escolhido: " + cmb->selectedLabel());
        });
    }
    const bool isBasics = (navIndex == 0);

    SimInput sim{ {}, kernel };

    // ---- 2. drive some interaction so the shot has state -----------------
    // The layout sits directly on the 800x600 screen now (ogui::Host owns
    // the window); widgets are aimed at by their real bounds.
    sim.hover(1.0f, 1.0f);
    sim.frame();
    sim.click(760.0f, 580.0f);          // dead zone: no-op click
    // Hit widgets by their real bounds: hard-coded offsets drifted when the
    // layout changed and silently missed the input (typed text never showed).
    auto clickWidget = [&](const char* id) {
        ogui::Rect b;
        if (!absBounds(screen->widgets, 0.0f, 0.0f, id, b)) {
            std::fprintf(stderr, "screenshot: widget %s not found\n", id);
            return;
        }
        sim.click(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        std::fprintf(stderr, "screenshot: clicked %s at (%.0f,%.0f) focus=%s\n",
                     id, b.x + b.w * 0.5f, b.y + b.h * 0.5f,
                     kernel.focusedWidget() ? "set" : "none");
    };
    if (isBasics) {
        clickWidget("btn_start");                  // -> onStart
        clickWidget("txt_log");                    // -> focus
        sim.type("ogui 0.1 \xC3\xA7\xC3\xA3");     // text + accents via UTF-8
        sim.hover(330.0f, 120.0f);                 // leave hovering btn_load
    }

    // Caret by click + drag selection: press past the text (caret on the
    // end), keep the button down and sweep left (selects the whole line).
    {
        ogui::Rect ib;
        if (isBasics && absBounds(screen->widgets, 0.0f, 0.0f, "txt_log", ib)) {
            const float y = ib.y + ib.h * 0.5f;
            sim.hover(ib.x + ib.w - 4.0f, y);
            sim.st.mouseDown[0] = true;
            sim.st.mouseButton = 0;
            sim.frame();
            sim.hover(ib.x + 4.0f, y);
            sim.frame();
            sim.st.mouseDown[0] = false;
            sim.st.mouseButton = -1;
            sim.frame();
            sim.st.mouseButton = 0;
            auto* inp = dynamic_cast<ogui::TextInput*>(kernel.focusedWidget());
            std::fprintf(stderr,
                         "screenshot: selection [%zu,%zu) text=[%s] caret=%zu\n",
                         inp ? inp->selectionStart() : 0,
                         inp ? inp->selectionEnd() : 0,
                         inp ? inp->text().c_str() : "",
                         inp ? inp->caret() : 0);
            // Keep the selection for the shot (caret sits on its start).
        }
    }

    // Word-wrapped label (analysis only; not part of the demo layout).
    if (isBasics) {
        auto lab = std::make_shared<ogui::Label>();
        lab->setText("Um rotulo comprido que precisa quebrar em varias linhas "
                     "dentro de uma caixa estreita desenhada pelo rasterizador.");
        lab->setWrap(true);
        lab->setFontFamily("sans");
        lab->setFontSize(14);
        lab->setTextColor(ogui::hexColor("#8fe"));
        const float lx = 470.0f, ly = outH - 90.0f, lw = 300.0f;
        lab->setBounds(ogui::Rect{lx, ly, lw, lab->measureHeight(lw)});
        screen->widgets.push_back(lab);
        std::fprintf(stderr, "screenshot: wrapped label height=%.1f\n",
                     lab->bounds().h);
    }

    // ComboBox LAST: pick "Fps: 120" (row 3 of the dropdown) through the
    // popup, then re-open it and hover the same row so the shot freezes it
    // open -- nothing after this block can swallow the popup.
    if (cmb) {
        ogui::Rect cb2;
        if (absBounds(screen->widgets, 0.0f, 0.0f, "cmb_fps", cb2)) {
            const float cx = cb2.x + cb2.w * 0.5f;
            const float row3y = cb2.y + cb2.h + 24.0f * 2.5f;  // 3rd option
            sim.click(cx, cb2.y + cb2.h * 0.5f);               // open
            sim.click(cx, row3y);                              // commit row
            std::fprintf(stderr,
                         "screenshot: combobox sel=%d label=%s status=%s\n",
                         cmb->selectedIndex(), cmb->selectedLabel().c_str(),
                         kernel.getText("lbl_status").c_str());
            sim.click(cx, cb2.y + cb2.h * 0.5f);               // open again
            sim.hover(cx, row3y);                              // highlight row
            sim.frame();
            // Let the dropdown's open-in fade finish (0.12s) so the shot
            // shows it fully opaque over the highlighted row.
            for (int i = 0; i < 6; ++i) {
                sim.st.time += 0.03f;
                sim.frame();
            }
        }
    }

    // ---- 3. build the draw list ------------------------------------------
    // Fonts resolve lazily during the first draw; every atlas then carries a
    // stable slot id. Register those slots the way GlBackend::syncFonts()
    // registers them as GL textures (the CPU rasterizer reads them directly).
    kernel.buildDrawList();

    ogui::SoftwareRenderer sr;
    sr.syncFonts();
    sr.syncImages();
    ogui::FontManager::instance().forEach([&](ogui::Font& f) {
        if (!f.loaded() || !f.atlas()) return;
        std::printf("font tex %d: %s (%dx%d)\n", f.atlasTexture(), f.path().c_str(),
                    f.atlasWidth(), f.atlasHeight());
    });

    const DrawList& dl = kernel.buildDrawList();

    // ---- 4. rasterize ----------------------------------------------------
    // Clear first, like the GL host does: blending then lands on an opaque
    // backdrop and the PNG needs no alpha flattening.
    ogui::Framebuffer big, out;
    big.resize(outW * ssaa, outH * ssaa);
    sr.clear(big, ogui::Color{16, 16, 16, 255});
    std::vector<int> missing;
    sr.render(dl, big, ssaa, &missing);
    ogui::SoftwareRenderer::downsample(big, out, ssaa);
    for (int texId : missing) {
        std::fprintf(stderr, "screenshot: no texture registered for texId=%d "
                     "(glyphs fall back to the white 1x1)\n", texId);
    }

    // ---- 5. write + report ------------------------------------------------
    const char* slash = std::strrchr(outPath.c_str(), '/');
    if (slash && slash != outPath.c_str()) {
        std::string dir(outPath.c_str(), slash);
        std::string cmd = "mkdir -p \"" + dir + "\"";
        if (std::system(cmd.c_str()) != 0) {
            std::fprintf(stderr, "warning: could not create %s\n", dir.c_str());
        }
    }

    if (!stbi_write_png(outPath.c_str(), out.w, out.h, 4, out.data(), out.w * 4)) {
        std::fprintf(stderr, "failed to write %s\n", outPath.c_str());
        return 1;
    }

    // Sanity: the bg panel must cover the whole frame.
    const uint8_t* corner = out.at(2, 2);
    const bool bgOk = corner[0] > 8 && corner[0] < 40;

    std::printf("screenshot: %s (%dx%d, ssaa %dx)\n", outPath.c_str(), out.w,
                out.h, ssaa);
    std::printf("drawlist: %zu vertices, %zu indices, %zu cmds, textures %zu\n",
                dl.vertices.size(), dl.indices.size(), dl.cmds.size(),
                sr.textures().size());
    std::printf("status: \"%s\"\n", kernel.getText("lbl_status").c_str());
    std::printf("sanity: bg corner %s\n", bgOk ? "ok" : "WRONG");
    return bgOk ? 0 : 1;
}
