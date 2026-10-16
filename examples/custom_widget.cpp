// ogui_custom_widget.cpp : add a widget to ogui without touching the library.
//
// The XML parser builds elements from one process-wide map, `widgetRegistry()`
// (include/ogui/widgets.hpp): tag name -> factory. It is a plain
// std::map<std::string, WidgetFactory> that the library fills lazily on first
// use, so a host can add (or replace) a tag before parsing anything.
//
// This example registers <led>: a round indicator that takes its colour from
// `color` (which the parser folds into Style::textColor), pulses while lit and
// toggles on click. It then drives it exactly like a built-in widget — parse,
// push, frame, click — and checks the result, so the file doubles as its own
// test (`ctest` runs it as `custom-widget`).
#include "ogui/kernel.hpp"
#include "ogui/properties.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/widgets.hpp"
#include "ogui/rasterizer.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";        \
            ++g_failures;                                                \
        }                                                                \
    } while (false)

// ---------------------------------------------------------------------------
// 1. the widget: derive from ogui::Widget and override what you need.
//    onDraw() is what makes it visible; the pointer/key handlers, onTick()
//    (every frame) and layout() are optional.
// ---------------------------------------------------------------------------
class Led : public ogui::Widget {
public:
    // A click toggles it, so it is a control: declaring the default here puts
    // it in the Tab ring for every host, whether the widget came from the tag
    // registry or from `std::make_shared<Led>()`.
    bool focusableByDefault() const override { return true; }

    void setOn(bool on) { m_on = on; }
    bool on() const { return m_on; }

    void onTick(float dt) override {
        Widget::onTick(dt);
        if (m_on) m_phase += dt * 4.0f;      // breathe while lit
    }

    void onPointerUp(ogui::MouseButton btn) override {
        if (btn == ogui::Mouse_Left) m_on = !m_on;
    }

    void onDraw(ogui::Rasterizer& r) const override {
        const ogui::Rect b = bounds();
        const float d = std::min(b.w, b.h);
        const ogui::Rect circle{b.x + (b.w - d) * 0.5f,
                                b.y + (b.h - d) * 0.5f, d, d};
        const ogui::Color base = style().textColor;   // <- color="@accent"
        if (!m_on) {
            r.drawRectBorder(circle, ogui::withAlpha(base, 120), 1.0f, d * 0.5f);
            return;
        }
        const float pulse = 0.65f + 0.35f * std::sin(m_phase);
        r.drawRoundedRect(circle,
                          ogui::withAlpha(base, static_cast<uint8_t>(255 * pulse)),
                          d * 0.5f);
    }

private:
    bool m_on = false;
    float m_phase = 0.0f;
};

// ---------------------------------------------------------------------------
// 2. the factory. Signature: (inherited Style, authored attributes) -> Ptr.
//    The attribute map is the hook for custom fields: the parser hands it to
//    the factory *before* anything else runs, and only later copies it onto the
//    widget as raw attributes. Returning any Widget-derived shared_ptr is
//    enough — ids, geometry, styles and children are applied for you.
// ---------------------------------------------------------------------------
std::shared_ptr<ogui::Widget> makeLed(const ogui::Style&,
                                      const std::map<std::string, std::string>& attrs) {
    auto led = std::make_shared<Led>();
    auto on = attrs.find("on");
    if (on != attrs.end()) {
        bool v = false;
        ogui::parseBool(on->second, v);   // the registry's true/1/yes/on
        led->setOn(v);
    }
    return led;   // focusable by the class's own focusableByDefault()
}

} // namespace

int main() {
    using namespace ogui;

    // 3. one call, before parsing. Replacing a built-in tag works the same way.
    widgetRegistry()["led"] = &makeLed;

    XmlParser parser;
    auto res = parser.parse(R"XML(<screen id="leds">
  <label id="ttl" text="Status" x="16" y="12" width="120" height="20"/>
  <led id="power" on="true" x="16" y="40" width="24" height="24" color="@accent"/>
  <led id="warn" on="false" x="52" y="40" width="24" height="24" color="@warn"/>
</screen>)XML");

    CHECK(res.root != nullptr);
    if (!res.errors.empty()) {
        for (const auto& e : res.errors) std::cerr << "  parse error: " << e << '\n';
    }
    CHECK(res.errors.empty());
    if (!res.root) { std::cerr << "custom widget: FAILED\n"; return 1; }

    auto screen = std::make_shared<Screen>("leds");
    screen->widgets.push_back(res.root);
    screen->width = 300.0f;
    screen->height = 120.0f;

    Kernel kernel;
    kernel.init();
    kernel.pushScreen(screen);

    InputState in;
    in.time = 0.0f;
    kernel.frame(in);

    auto* power = dynamic_cast<Led*>(kernel.findWidget("power"));
    auto* warn = dynamic_cast<Led*>(kernel.findWidget("warn"));
    CHECK(power && power->on());        // the raw `on` attribute was read
    CHECK(warn && !warn->on());

    // It behaves like any other widget: geometry resolved, draw list emitted,
    // and the debug overlay names it by its class with no extra plumbing.
    const DrawList& dl = kernel.drawList();
    CHECK(!dl.vertices.empty());
    CHECK(kernel.dumpTree().find("Led power 16,40 24x24") != std::string::npos);
    CHECK(kernel.tabOrder(warn) >= 0);  // focusable through the factory

    // Click the dark one: the normal pointer path reaches onPointerUp.
    in.mouseX = 64.0f;
    in.mouseY = 52.0f;
    in.mouseDown[0] = true;
    in.mouseButton = 0;
    in.time += 0.1f;
    kernel.frame(in);
    in.mouseDown[0] = false;
    in.mouseButton = -1;
    in.time += 0.1f;
    kernel.frame(in);
    CHECK(warn && warn->on());

    std::printf("custom widget: %s (%zu vertices, %zu widgets)\n",
                g_failures ? "FAILED" : "ok", dl.vertices.size(),
                kernel.stats().widgets);
    return g_failures ? 1 : 0;
}
