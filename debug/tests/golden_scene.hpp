// ogui_golden_scene.hpp : the scene every pixel test renders.
//
// The layout, the theme and the input it is driven with live here so a pixel
// difference can only come from the renderer, never from a different scene.
// Both consumers are pixel tests: golden_test (SoftwareRenderer, compared
// against debug/tests/golden/*.png) and gl_test (GlBackend on a real GL/GLES
// context, compared against the SoftwareRenderer frame itself).
//
// If this scene changes, the goldens must be regenerated
// (OGUI_UPDATE_GOLDEN=1 ./ogui_golden_test).
#pragma once

#include "ogui/kernel.hpp"
#include "ogui/style.hpp"
#include "ogui/xmlparser.hpp"

#include <iostream>
#include <memory>
#include <string>

namespace oguitest {

inline constexpr int kSceneW = 480;
inline constexpr int kSceneH = 500;

inline const char* goldenLayout() {
    return R"XML(<screen id="main">
  <style name="primary" background="#4488ff" color="#ffffff" radius="6"/>
  <style name="muted"   background="#3a3a3a" color="#cccccc" radius="6"/>

  <panel id="bg" x="0" y="0" width="480" height="500" background="@screen"/>

  <window id="win" x="16" y="16" width="448" height="244" title="golden" background="@surface"
          border="@accent" borderWidth="1">
    <column id="col">
      <button id="b1" style="primary" text="Iniciar" x="12" y="12" width="150" height="38"
              tooltip="Iniciar"/>
      <button id="b2" style="muted"   text="Carregar" x="12" y="58" width="150" height="38"/>
      <label id="l1" text="alinhado ao centro" x="12" y="58" width="424" height="26"
             align="center" color="#66ee66"/>
      <textinput id="t1" x="12" y="92" width="424" height="36" placeholder="Digite algo..."
                 background="@window" color="@text" border="@accent" radius="6"/>
      <label id="l2" wrap="true" x="12" y="136" width="260" height="0" color="#8fe"
             text="Uma frase bem longa que o rasterizador quebra em varias linhas dentro de uma caixa estreita."/>
    </column>
  </window>

  <group id="g1" x="16" y="268" width="448" height="80" background="@surface"
         border="@accent" borderWidth="1">
    <checkbox id="c1" text="Opcao" x="12" y="10" width="150" height="22" checked="true"/>
    <separator id="s1" x="180" y="14" width="110" height="8" thickness="2" color="@borderStrong"/>
    <progress id="p1" x="12" y="46" width="200" height="16" value="0.65" color="#66ee66"/>
    <slider id="sl1" x="240" y="42" width="190" height="24" min="0" max="100" value="40"/>
  </group>

  <tabs id="tt" x="16" y="356" width="448" height="52" tabHeight="24" background="@window" active="0">
    <tab label="Pagina A">
      <label id="tl1" text="conteudo visivel" x="12" y="6" width="300" height="20" color="#66ee66"/>
    </tab>
    <tab label="Pagina B">
      <label id="tl2" text="NAO PODE APARECER" x="12" y="6" width="300" height="20" color="#ff4444"/>
    </tab>
  </tabs>

  <scrollview id="sv" x="16" y="416" width="448" height="72" background="@window"
              border="@accent" borderWidth="1" scrollY="-40">
    <label id="sv1" text="linha rolavel um (escondida)" x="12" y="8"
           width="400" height="20" color="@text"/>
    <label id="sv2" text="linha rolavel dois (no topo)" x="12" y="40"
           width="400" height="20" color="#66ee66"/>
    <label id="sv3" text="linha rolavel tres (recortada)" x="12" y="100"
           width="400" height="20" color="#ff4444"/>
  </scrollview>
</screen>)XML";
}

// Builds the screen under `theme` and drives it like a real host: frames to lay
// the tree out, a hover on b1 long enough for the tooltip to appear, a focused
// text input holding a selection (caret + highlight paths).
class GoldenScene {
public:
    bool build(const ogui::Theme& theme) {
        ogui::setActiveTheme(theme);
        ogui::XmlParser parser;
        auto res = parser.parse(goldenLayout(), &ogui::activeTheme());
        if (!res.root) {
            for (const auto& e : res.errors) std::cerr << "xml: " << e << "\n";
            return false;
        }

        screen = std::make_shared<ogui::Screen>("main");
        screen->widgets.push_back(res.root);
        screen->width = static_cast<float>(kSceneW);
        screen->height = static_cast<float>(kSceneH);

        kernel.init();
        kernel.pushScreen(screen);

        if (auto* t = dynamic_cast<ogui::TextInput*>(kernel.findWidget("t1"))) {
            t->setText("selecao");
            t->setSelection(0, 4);
            t->onFocus(true);
        }

        ogui::InputState st;
        st.mouseButton = -1;
        st.time = 0.0f;
        kernel.frame(st);                       // first pass lays the tree out
        if (auto* b1 = kernel.findWidget("b1")) {
            const ogui::Offset2D o = b1->absoluteOrigin();
            st.mouseX = o.x + b1->bounds().w * 0.5f;
            st.mouseY = o.y + b1->bounds().h * 0.5f;
        }
        kernel.frame(st);                       // pointer lands on the button
        st.time = 1.0f;                         // past the 0.5s tooltip delay
        kernel.frame(st);

        if (kernel.drawList().cmds.empty()) {
            std::cerr << "scene: empty draw list\n";
            return false;
        }
        if (!kernel.tooltipVisible()) {
            std::cerr << "scene: tooltip never became visible\n";
            return false;
        }
        return true;
    }

    const ogui::DrawList& drawList() { return kernel.drawList(); }

    ogui::Kernel kernel;
    std::shared_ptr<ogui::Screen> screen;
};

} // namespace oguitest
