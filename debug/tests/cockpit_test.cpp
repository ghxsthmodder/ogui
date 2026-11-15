// Cockpit widgets: <steeringwheel>, <pedal> and <speedometer>.
//
// The interesting parts are the parts a frame drives: the wheel turning under
// a dragged pointer and springing back, a pedal travelling while it is held
// and reading 1 the moment it is pressed, and a gauge whose property round
// trip is what the Lua script in the cockpit screen relies on.
#include "ogui/kernel.hpp"
#include "ogui/properties.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#define CK_CHDIR(p) _chdir(p)
#else
#include <unistd.h>
#define CK_CHDIR(p) chdir(p)
#endif

namespace {

int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";         \
            ++g_failures;                                                \
        }                                                                \
    } while (false)
#define CHECK_MSG(cond, msg)                                             \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL " << __LINE__ << ": " #cond " (" << (msg) \
                      << ")\n";                                          \
            ++g_failures;                                                \
        }                                                                \
    } while (false)

void frame(ogui::Kernel& kernel, ogui::InputState& in, float dt) {
    in.time += dt;
    kernel.frame(in);
}

const char* kLayout = R"XML(<screen id="cockpit">
  <speedometer id="speedo" x="20" y="20" width="180" height="180"
               min="0" max="60" value="12" redline="45" unit="km/h"/>
  <steeringwheel id="steer" x="100" y="60" width="200" height="200"
                 maxangle="180" label="VOLANTE"/>
  <pedal id="accel" x="380" y="60" width="80" height="120" label="ACELERADOR"/>
</screen>)XML";

}  // namespace

int main() {
    using namespace ogui;

    XmlParser parser;
    auto res = parser.parse(kLayout);
    CHECK(res.root != nullptr);
    for (const auto& e : res.errors) std::cerr << "  parse error: " << e << '\n';
    CHECK(res.errors.empty());
    if (!res.root) return 1;

    auto screen = std::make_shared<Screen>("cockpit");
    screen->widgets.push_back(res.root);
    screen->width = 500.0f;
    screen->height = 260.0f;

    Kernel kernel;
    kernel.init();
    kernel.pushScreen(screen);

    InputState in;
    in.time = 0.0f;
    frame(kernel, in, 0.016f);

    auto* speedo = dynamic_cast<Speedometer*>(kernel.findWidget("speedo"));
    auto* wheel = dynamic_cast<SteeringWheel*>(kernel.findWidget("steer"));
    auto* pedal = dynamic_cast<Pedal*>(kernel.findWidget("accel"));
    CHECK(speedo && wheel && pedal);
    if (!speedo || !wheel || !pedal) return 1;

    // ---- authored attributes land in the class, not just the style ----
    CHECK(speedo->minimum() == 0.0f && speedo->maximum() == 60.0f);
    CHECK(speedo->value() == 12.0f);
    CHECK(speedo->redline() == 45.0f);
    CHECK(speedo->unit() == "km/h");
    CHECK(wheel->maxAngle() == 180.0f);
    CHECK(wheel->label() == "VOLANTE");
    CHECK(wheel->autoCenter());
    CHECK(pedal->value() == 0.0f);
    CHECK(pedal->label() == "ACELERADOR");

    // Every control draws: the gauge geometry, the rim and the plate all
    // reach the draw list.
    CHECK(kernel.drawList().vertices.size() > 50);

    // ---- the speedometer clamps and reports through the registry ----
    speedo->setValue(120.0f);
    CHECK(speedo->value() == 60.0f);
    std::string error;
    CHECK(setWidgetProperty(*speedo, "value", "33.5", &error));
    CHECK(std::fabs(speedo->value() - 33.5f) < 0.01f);
    CHECK(widgetProperty(*speedo, "value") == "33.5");
    CHECK(!setWidgetProperty(*speedo, "value", "fast", &error));
    CHECK(setWidgetProperty(*speedo, "redline", "50", &error));
    CHECK(speedo->redline() == 50.0f);
    speedo->setRange(0.0f, 100.0f);
    CHECK(speedo->redline() == 50.0f);   // an explicit redline survives a range change

    // ---- the pedal reads 1 on press, eases the plate, and returns ----
    in.mouseX = 420.0f;
    in.mouseY = 120.0f;
    in.mouseDown[0] = true;
    in.mouseButton = 0;
    frame(kernel, in, 0.1f);
    CHECK(pedal->value() == 1.0f);
    CHECK(pedal->pressed());
    CHECK_MSG(pedal->isAnimating(), "the plate should still be travelling");
    for (int i = 0; i < 10; ++i) frame(kernel, in, 0.05f);
    CHECK_MSG(!pedal->isAnimating(), "the plate should have reached full travel");

    in.mouseDown[0] = false;
    in.mouseButton = -1;
    frame(kernel, in, 0.05f);
    CHECK(pedal->value() == 0.0f);
    CHECK(!pedal->pressed());
    CHECK(widgetProperty(*pedal, "value") == "0");

    // A script can drive intermediate travel directly (analog axis input).
    pedal->setValue(0.4f);
    CHECK(widgetProperty(*pedal, "value") == "0.4");
    pedal->setValue(0.0f);

    // ---- the wheel turns with the drag and springs back to centre ----
    // Centre is (200, 160): grab the rim on the right, drag to the top.
    in.mouseX = 300.0f;
    in.mouseY = 160.0f;
    in.mouseDown[0] = true;
    in.mouseButton = 0;
    frame(kernel, in, 0.05f);
    in.mouseX = 200.0f;
    in.mouseY = 60.0f;
    frame(kernel, in, 0.05f);
    CHECK_MSG(std::fabs(wheel->angle() + 90.0f) < 1.0f,
              "angle " + std::to_string(wheel->angle()));

    // Keep dragging left: the shortest arc carries it to full left lock.
    in.mouseX = 100.0f;
    in.mouseY = 160.0f;
    frame(kernel, in, 0.05f);
    CHECK_MSG(wheel->angle() == -180.0f,
              "angle " + std::to_string(wheel->angle()));

    // The normalized value a vehicle model consumes.
    CHECK(wheel->steering() == -1.0f);
    CHECK(widgetProperty(*wheel, "angle") == "-180");

    in.mouseDown[0] = false;
    in.mouseButton = -1;
    for (int i = 0; i < 20; ++i) frame(kernel, in, 0.05f);
    CHECK_MSG(wheel->angle() == 0.0f,
              "auto-centre left the wheel at " + std::to_string(wheel->angle()));
    CHECK(!wheel->isAnimating());

    // Keyboard: arrow keys steer, Home recentres.
    wheel->onKeyDown(Key_Right, 0);
    CHECK(wheel->angle() > 0.0f);
    wheel->onKeyDown(Key_Home, 0);
    CHECK(wheel->angle() == 0.0f);

    // Programmatic writes stay clamped and notify nobody.
    CHECK(setWidgetProperty(*wheel, "angle", "900", &error));
    CHECK(wheel->angle() == 180.0f);
    CHECK(setWidgetProperty(*wheel, "autocenter", "false", &error));
    CHECK(!wheel->autoCenter());
    CHECK(setWidgetProperty(*wheel, "maxangle", "90", &error));
    CHECK_MSG(std::fabs(wheel->angle()) <= 90.0f, "maxAngle re-clamps the angle");

    // ---- tags are first-class elements with a property table ----
    for (const char* tag : {"steeringwheel", "pedal", "speedometer"}) {
        auto it = widgetRegistry().find(tag);
        CHECK_MSG(it != widgetRegistry().end(), tag);
        if (it == widgetRegistry().end()) continue;
        Widget::Ptr created = it->second(Style{}, {});
        CHECK(PropertyRegistry::instance().declares(typeid(*created)));
        CHECK(PropertyRegistry::instance().find(*created, "value") != nullptr ||
              PropertyRegistry::instance().find(*created, "angle") != nullptr);
    }

    // ---- the shipped cockpit screen: its Lua drives the gauge ----
    // The screen's script integrates a speed out of the two pedals and writes
    // it back through ogui.set, so a held accelerator has to move the needle.
    // The layout pulls its stylesheet in by a cwd-relative path, the way the
    // gallery resolves it, so the test stands where the gallery would.
    CK_CHDIR(OGUI_SOURCE_DIR);
    XmlParser gallery;
    auto shipped = gallery.parseFile(
        std::string(OGUI_SOURCE_DIR) + "/layouts/layout_cockpit.xml");
    CHECK_MSG(shipped.root != nullptr, "layout_cockpit.xml did not parse");
    for (const auto& e : shipped.errors) std::cerr << "  cockpit error: " << e << '\n';
    CHECK(shipped.errors.empty());
    if (!shipped.root) return 1;

    CHECK_MSG(shipped.scripts.size() == 1,
              "layout_cockpit.xml exposed " +
                  std::to_string(shipped.scripts.size()) + " script(s)");
    auto cockpitScreen = std::make_shared<Screen>("cockpit_screen");
    cockpitScreen->widgets.push_back(shipped.root);
    cockpitScreen->scripts = std::move(shipped.scripts);   // as the gallery does
    cockpitScreen->width = 1228.0f;
    cockpitScreen->height = 700.0f;

    Kernel hud;
    hud.init();
    hud.pushScreen(cockpitScreen);

    InputState hin;
    hin.time = 0.0f;
    frame(hud, hin, 0.1f);
    auto* gauge = dynamic_cast<Speedometer*>(hud.findWidget("speedo"));
    auto* throttle = dynamic_cast<Pedal*>(hud.findWidget("accel"));
    auto* steer2 = dynamic_cast<SteeringWheel*>(hud.findWidget("steer"));
    CHECK(gauge && throttle && steer2);
    if (!gauge || !throttle || !steer2) return 1;
    CHECK(gauge->value() == 0.0f);
    CHECK_MSG(hud.script().errors().empty(), "the cockpit script raised");

    // Hold the accelerator pedal for a while. Its rect is 866,386 104x230.
    hin.mouseX = 918.0f;
    hin.mouseY = 501.0f;
    hin.mouseDown[0] = true;
    hin.mouseButton = 0;
    for (int i = 0; i < 15; ++i) frame(hud, hin, 0.1f);
    CHECK(throttle->pressed());
    CHECK_MSG(gauge->value() > 3.0f,
              "the Lua speed model left the gauge at " +
                  std::to_string(gauge->value()));
    CHECK(hud.findWidget("gear")->property("text") != "N");

    // Releasing the pedal stops the acceleration; the model's rolling
    // resistance then brings the reading down a little every second.
    const float cruise = gauge->value();
    hin.mouseDown[0] = false;
    hin.mouseButton = -1;
    for (int i = 0; i < 30; ++i) frame(hud, hin, 0.1f);
    CHECK_MSG(gauge->value() < cruise - 1.0f && gauge->value() >= 0.0f,
              "the gauge did not coast back down: " +
                  std::to_string(gauge->value()));

    std::printf("cockpit: %s (%zu vertices)\n",
                g_failures ? "FAILED" : "ok", kernel.drawList().vertices.size());
    return g_failures ? 1 : 0;
}
