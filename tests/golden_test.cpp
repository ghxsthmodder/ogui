// ogui_golden_test.cpp : golden-image test for the software renderer.
//
// Renders a fixed layout through Kernel -> DrawList -> SoftwareRenderer and
// compares the pixels against tests/golden/basic.png (dark theme) and
// tests/golden/basic_light.png (light theme). The neutral colors come from
// @tokens, so each theme's palette is pinned end to end.
//
//   OGUI_UPDATE_GOLDEN=1 ./ogui_golden_test     (re)generates the goldens
//
// The comparison is tolerant (a couple of units per channel, 1% of pixels)
// so different compilers' floating point rounding does not fail the build,
// while any real geometry/colour change still does. A golden generated with
// a different font file is skipped with a message instead of compared: font
// rasterization is the one input that legitimately differs between machines.
#include "ogui/kernel.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/font.hpp"
#include "ogui/software.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// stb is third-party; its designated-initializer style trips -Wextra.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
// stb_image is implemented inside libogui (src/imagemanager.cpp); this TU
// only needs the declarations to read the golden PNG.
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifndef OGUI_GOLDEN_DIR
#define OGUI_GOLDEN_DIR "tests/golden"
#endif

namespace {

constexpr int kW = 480;
constexpr int kH = 500;
constexpr int kSsaa = 2;

// Per-channel slack for cross-compiler FP rounding, and the share of pixels
// allowed to exceed it (antialiased edges move by a hair between builds).
constexpr int kMaxChannelDelta = 4;
constexpr double kMaxBadPixelRatio = 0.01;

const char* kLayout = R"XML(<screen id="main">
  <style name="primary" bg="#4488ff" color="#ffffff" radius="6"/>
  <style name="muted"   bg="#3a3a3a" color="#cccccc" radius="6"/>

  <panel id="bg" x="0" y="0" w="480" h="500" bg="@screen"/>

  <window id="win" x="16" y="16" w="448" h="244" title="golden" bg="@surface"
          border="@accent" borderwidth="1">
    <column id="col">
      <button id="b1" style="primary" text="Iniciar" x="12" y="12" w="150" h="38"
              tooltip="Iniciar"/>
      <button id="b2" style="muted"   text="Carregar" x="12" y="58" w="150" h="38"/>
      <label id="l1" text="alinhado ao centro" x="12" y="58" w="424" h="26"
             align="center" color="#66ee66"/>
      <textinput id="t1" x="12" y="92" w="424" h="36" placeholder="Digite algo..."
                 bg="@window" color="@text" border="@accent" radius="6"/>
      <label id="l2" wrap="true" x="12" y="136" w="260" h="0" color="#8fe"
             text="Uma frase bem longa que o rasterizador quebra em varias linhas dentro de uma caixa estreita."/>
    </column>
  </window>

  <group id="g1" x="16" y="268" w="448" h="80" bg="@surface"
         border="@accent" borderwidth="1">
    <checkbox id="c1" text="Opcao" x="12" y="10" w="150" h="22" checked="true"/>
    <separator id="s1" x="180" y="14" w="110" h="8" thickness="2" color="@borderStrong"/>
    <progress id="p1" x="12" y="46" w="200" h="16" value="0.65" color="#66ee66"/>
    <slider id="sl1" x="240" y="42" w="190" h="24" min="0" max="100" value="40"/>
  </group>

  <tabs id="tt" x="16" y="356" w="448" h="52" theight="24" bg="@window" active="0">
    <tab label="Pagina A">
      <label id="tl1" text="conteudo visivel" x="12" y="6" w="300" h="20" color="#66ee66"/>
    </tab>
    <tab label="Pagina B">
      <label id="tl2" text="NAO PODE APARECER" x="12" y="6" w="300" h="20" color="#ff4444"/>
    </tab>
  </tabs>

  <scrollview id="sv" x="16" y="416" w="448" h="72" bg="@window"
              border="@accent" borderwidth="1" scrolly="-40">
    <label id="sv1" text="linha rolavel um (escondida)" x="12" y="8"
           w="400" h="20" color="@text"/>
    <label id="sv2" text="linha rolavel dois (no topo)" x="12" y="40"
           w="400" h="20" color="#66ee66"/>
    <label id="sv3" text="linha rolavel tres (recortada)" x="12" y="100"
           w="400" h="20" color="#ff4444"/>
  </scrollview>
</screen>)XML";

bool envEnabled(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr && *v != '\0' && std::strcmp(v, "0") != 0;
}

std::string joinFontPaths() {
    std::vector<std::string> paths;
    ogui::FontManager::instance().forEach([&](ogui::Font& f) {
        if (!f.loaded()) return;
        if (std::find(paths.begin(), paths.end(), f.path()) == paths.end())
            paths.push_back(f.path());
    });
    std::ostringstream os;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (i) os << ';';
        os << paths[i];
    }
    return os.str();
}

// Reads "fonts=a;b;c" (single line) out of the meta file.
std::string readMetaFonts(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("fonts=", 0) == 0) return line.substr(6);
    }
    return {};
}

void writeMeta(const std::string& path, int w, int h) {
    std::ofstream out(path);
    out << "size=" << w << "x" << h << "\n";
    out << "ssaa=" << kSsaa << "\n";
    out << "fonts=" << joinFontPaths() << "\n";
}

struct CompareResult {
    long differingPixels = 0;   // any channel moved
    long badPixels = 0;         // a channel moved more than kMaxChannelDelta
    int maxDelta = 0;
    bool sizeMismatch = false;
};

CompareResult compare(const uint8_t* got, int gw, int gh,
                      const uint8_t* ref, int rw, int rh) {
    CompareResult r;
    if (gw != rw || gh != rh) {
        r.sizeMismatch = true;
        return r;
    }
    const long total = static_cast<long>(gw) * gh;
    for (long i = 0; i < total; ++i) {
        int pixelMax = 0;
        for (int c = 0; c < 4; ++c)
            pixelMax = std::max(pixelMax,
                                std::abs(int(got[i * 4 + c]) - int(ref[i * 4 + c])));
        if (pixelMax == 0) continue;
        ++r.differingPixels;
        r.maxDelta = std::max(r.maxDelta, pixelMax);
        if (pixelMax > kMaxChannelDelta) ++r.badPixels;
    }
    (void)total;
    return r;
}

} // namespace

namespace {

// Build the scene under `theme`, drive input like a real host (hover the
// tooltip-bearing button until the tooltip appears) and render to `out`.
bool renderTheme(const ogui::Theme& theme, ogui::Framebuffer& out) {
    ogui::setActiveTheme(theme);
    ogui::XmlParser parser;
    auto res = parser.parse(kLayout, &ogui::activeTheme());
    if (!res.root) {
        for (const auto& e : res.errors) std::cerr << "xml: " << e << "\n";
        return false;
    }

    auto screen = std::make_shared<ogui::Screen>("main");
    screen->widgets.push_back(res.root);
    screen->width = static_cast<float>(kW);
    screen->height = static_cast<float>(kH);

    ogui::Kernel kernel;
    kernel.init();
    kernel.pushScreen(screen);

    // Focused input with a selection: covers the caret and highlight paths.
    if (auto* t = dynamic_cast<ogui::TextInput*>(kernel.findWidget("t1"))) {
        t->setText("selecao");
        t->setSelection(0, 4);
        t->onFocus(true);
    }

    ogui::InputState st;
    st.mouseButton = -1;
    st.time = 0.0f;
    kernel.frame(st);                       // first pass lays the tree out
    if (auto* b1w = kernel.findWidget("b1")) {
        const ogui::Offset2D o = b1w->absoluteOrigin();
        st.mouseX = o.x + b1w->bounds().w * 0.5f;
        st.mouseY = o.y + b1w->bounds().h * 0.5f;
    }
    kernel.frame(st);                       // pointer lands on the button
    st.time = 1.0f;                         // past the 0.5s tooltip delay
    kernel.frame(st);

    const ogui::DrawList& dl = kernel.drawList();
    if (dl.cmds.empty()) {
        std::cerr << "golden_test: empty draw list\n";
        return false;
    }
    if (!kernel.tooltipVisible()) {
        std::cerr << "golden_test: tooltip never became visible\n";
        return false;
    }

    ogui::SoftwareRenderer sr;
    sr.syncFonts();
    sr.syncImages();

    ogui::Framebuffer big;
    big.resize(kW * kSsaa, kH * kSsaa);
    sr.clear(big, ogui::palette().screen);
    sr.render(dl, big, kSsaa, nullptr);
    ogui::SoftwareRenderer::downsample(big, out, kSsaa);
    return true;
}

} // namespace

int main() {
    const std::string dir = OGUI_GOLDEN_DIR;
    const bool update = envEnabled("OGUI_UPDATE_GOLDEN");

    struct Case { const char* base; const ogui::Theme* theme; };
    const Case cases[] = {
        {"basic", &ogui::darkTheme()},
        {"basic_light", &ogui::lightTheme()},
    };

    // Without a system font the scene has no text at all: nothing to compare.
    if (!ogui::FontManager::instance().get("sans", 14)) {
        std::cout << "golden_test: SKIP - no font available for family \"sans\"\n";
        return 0;
    }

    // Render every theme first: font paths are only known once the scenes
    // have resolved their text.
    std::vector<ogui::Framebuffer> rendered;
    for (const Case& c : cases) {
        ogui::Framebuffer out;
        if (!renderTheme(*c.theme, out)) return 1;
        rendered.push_back(std::move(out));
    }
    const std::string fontsNow = joinFontPaths();

    int rc = 0;
    for (size_t i = 0; i < std::size(cases); ++i) {
        const std::string pngPath = dir + "/" + cases[i].base + ".png";
        const std::string metaPath = dir + "/" + cases[i].base + ".meta";
        const ogui::Framebuffer& out = rendered[i];

        if (update) {
            if (!stbi_write_png(pngPath.c_str(), out.w, out.h, 4, out.data(),
                                out.w * 4)) {
                std::cerr << "golden_test: cannot write " << pngPath << "\n";
                return 1;
            }
            writeMeta(metaPath, out.w, out.h);
            std::cout << "golden_test: wrote " << pngPath << " ("
                      << out.w << "x" << out.h << ")\n";
            continue;
        }

        const std::string fontsWas = readMetaFonts(metaPath);
        if (!fontsWas.empty() && fontsWas != fontsNow) {
            std::cout << "golden_test: SKIP " << cases[i].base
                      << " - golden was rendered with fonts [" << fontsWas
                      << "] but this machine resolved [" << fontsNow
                      << "]. Regenerate with OGUI_UPDATE_GOLDEN=1 on this machine.\n";
            continue;
        }

        int rw = 0, rh = 0, rn = 0;
        unsigned char* ref = stbi_load(pngPath.c_str(), &rw, &rh, &rn, 4);
        if (!ref) {
            std::cerr << "golden_test: missing or unreadable " << pngPath
                      << "\n  generate it with: OGUI_UPDATE_GOLDEN=1 ./ogui_golden_test\n";
            return 1;
        }

        const CompareResult r = compare(out.data(), out.w, out.h, ref, rw, rh);
        stbi_image_free(ref);

        const long total = static_cast<long>(out.w) * out.h;
        if (r.sizeMismatch) {
            std::cerr << "golden_test: " << cases[i].base
                      << ": size mismatch, got " << out.w << "x" << out.h
                      << " golden " << rw << "x" << rh << "\n";
            return 1;
        }
        if (r.badPixels > static_cast<long>(kMaxBadPixelRatio * total)) {
            const std::string actualPath =
                dir + "/" + cases[i].base + ".actual.png";
            stbi_write_png(actualPath.c_str(), out.w, out.h, 4, out.data(),
                           out.w * 4);
            std::cerr << "golden_test: FAIL " << cases[i].base << " - "
                      << r.badPixels << "/" << total
                      << " pixels differ by more than " << kMaxChannelDelta
                      << " (max delta " << r.maxDelta << ", differing "
                      << r.differingPixels << ")\n  actual: " << actualPath
                      << "\n  regenerate with: OGUI_UPDATE_GOLDEN=1 ./ogui_golden_test\n";
            rc = 1;
            continue;
        }
        std::cout << "golden_test: OK " << cases[i].base << " ("
                  << r.differingPixels << "/" << total
                  << " pixels nudged, max delta " << r.maxDelta << ")\n";
    }
    return rc;
}
