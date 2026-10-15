// ogui_touch_test.cpp : the touch stream folded into the kernel's pointer.
//
// A touch host reports fingers, not a cursor; Kernel::applyTouchStream turns
// that list into the single pointer every widget already speaks. What this
// checks is the bridging contract: a tap is a click, the drag stays with the
// finger that started it, a pointer the system took away does not click, and a
// host that only owns a mouse is affected by none of this.
#include "ogui/kernel.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace ogui;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) return;
    std::cout << "touch_test: FAIL " << what << "\n";
    ++g_failures;
}

// A button that records what the pointer layer did to it.
class Probe : public Button {
public:
    int downs = 0, ups = 0, moves = 0, clicks = 0;
    float lastX = -1.0f, lastY = -1.0f;   // logical, from the last callback

    Probe() { setClickHandler([this] { ++clicks; }); }

    void onPointerDown(MouseButton b, const Offset2D& p) override {
        ++downs;
        lastX = p.x;
        lastY = p.y;
        Button::onPointerDown(b, p);
    }
    void onPointerUp(MouseButton b) override {
        ++ups;
        Button::onPointerUp(b);
    }
    void onPointerMove(const Offset2D& p) override {
        ++moves;
        lastX = p.x;
        lastY = p.y;
        Button::onPointerMove(p);
    }
};

struct Rig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    Probe* a = nullptr;   // (50,50,200,100), centre (150,100)
    Probe* b = nullptr;   // (50,300,200,100), centre (150,350)
    InputState st;
    float scale = 1.0f;

    void build() {
        kernel.init();
        screen = std::make_shared<Screen>("main");
        screen->width = 800.0f * scale;
        screen->height = 600.0f * scale;
        screen->scale = scale;
        auto wa = std::make_shared<Probe>();
        auto wb = std::make_shared<Probe>();
        a = wa.get();
        b = wb.get();
        a->setBounds(Rect{50, 50, 200, 100});
        b->setBounds(Rect{50, 300, 200, 100});
        a->setText("A");
        b->setText("B");
        screen->widgets.push_back(wa);
        screen->widgets.push_back(wb);
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
    }

    // One frame of the given fingers. Ids are the host's pointer ids; the
    // positions are device pixels, the way a host reports them.
    void fingers(std::vector<TouchPoint> pts, bool cancelled = false) {
        st.touchCount = 0;
        for (auto& t : pts) st.touches[st.touchCount++] = t;
        st.touchCancelled = cancelled;
        kernel.frame(st);
    }
    void none(bool cancelled = false) { fingers({}, cancelled); }

    static TouchPoint pt(int id, float x, float y) {
        TouchPoint t;
        t.id = id;
        t.x = x;
        t.y = y;
        return t;
    }
};

// ---- tests ---------------------------------------------------------------

void tapIsAClick() {
    Rig r;
    r.build();
    r.fingers({Rig::pt(1, 150, 100)});
    check(r.a->downs == 1, "a finger down presses the widget under it");
    check(r.a->clicks == 0, "the press alone does not click");
    check(r.kernel.hoveredWidget() == r.a, "the finger hovers what it touches");

    r.none();
    check(r.a->ups == 1, "the last finger lifting releases the widget");
    check(r.a->clicks == 1, "press and release on one widget is a click");

    // Parked frames after the release must not click again.
    r.none();
    r.none();
    check(r.a->clicks == 1 && r.a->ups == 1, "lifting ends the gesture once");
}

// The pointer a widget sees is logical; the finger a host reports is device.
void positionsAreScaled() {
    Rig r;
    r.scale = 2.0f;
    r.build();
    r.fingers({Rig::pt(1, 300, 200)});   // device = logical (150,100)
    check(r.a->downs == 1, "a scaled finger still lands on the button");
    check(r.a->lastX == 150.0f && r.a->lastY == 100.0f,
          "the widget is handed logical coordinates");
    r.none();
    check(r.a->clicks == 1, "a scaled tap clicks");
}

// A drag that leaves the widget must keep feeding it, and must not click.
void dragKeepsCaptureAndDoesNotClick() {
    Rig r;
    r.build();
    r.fingers({Rig::pt(1, 150, 100)});
    r.fingers({Rig::pt(1, 400, 500)});   // far outside A and B
    check(r.a->moves >= 1 && r.a->lastX == 400.0f,
          "the captured widget keeps receiving the drag past its edge");
    check(r.b->downs == 0, "a drag over a second widget does not press it");
    r.none();
    check(r.a->clicks == 0 && r.b->clicks == 0,
          "releasing somewhere else is a drag, not a click");
    check(r.a->ups == 1, "the captured widget is still the one released");
}

// Multi-touch: the second finger must not hijack the first one's press, and
// the press must survive the first finger lifting.
void secondFingerDoesNotSteal() {
    Rig r;
    r.build();
    r.fingers({Rig::pt(1, 150, 100)});           // A owns the pointer
    r.fingers({Rig::pt(1, 150, 110), Rig::pt(2, 150, 350)});
    check(r.a->downs == 1, "a second finger does not press again");
    check(r.b->downs == 0, "nor steals the press by landing on another widget");
    check(r.a->lastY == 110.0f, "the pointer follows the finger that owns it");

    // The owner lifts while the other is still down: the drag continues.
    r.fingers({Rig::pt(2, 200, 360)});
    check(r.a->ups == 0, "the press survives the owning finger lifting");
    check(r.a->lastX == 200.0f && r.a->lastY == 360.0f,
          "a finger that stayed down carries the drag on");

    r.none();
    check(r.a->ups == 1, "and exactly one release ends it");
}

// ACTION_CANCEL: the system claimed the gesture. The widget must let go, and
// nothing may be clicked.
void cancelReleasesWithoutClicking() {
    Rig r;
    r.build();
    r.fingers({Rig::pt(1, 150, 100)});
    r.none(/*cancelled=*/true);
    check(r.a->ups == 1, "a cancelled pointer still releases the widget");
    check(r.a->clicks == 0, "a cancelled pointer is not a click");
    r.none();
    check(r.a->clicks == 0, "and not a click on the frames after either");
}

// A finger is not a cursor: once it is gone nothing is hovered, so a `hover:`
// state does not stay painted on the last thing tapped.
void noPhantomHoverAfterLift() {
    Rig r;
    r.build();
    r.fingers({Rig::pt(1, 150, 100)});
    check(r.kernel.hoveredWidget() == r.a, "a finger down hovers its widget");
    r.none();
    check(r.kernel.hoveredWidget() == r.a,
          "the release frame still hovers, so the click can be judged");
    r.none();
    check(r.kernel.hoveredWidget() == nullptr,
          "after the lift there is no pointer hovering anything");
}

// A machine that has both: the mouse takes the pointer back as soon as it
// moves, and keeps it until a finger lands.
void mouseAndTouchCoexist() {
    Rig r;
    r.build();
    r.fingers({Rig::pt(1, 150, 100)});
    r.none();
    r.none();

    r.st.mouseX = 150.0f;   // the host's mouse reports a move at B
    r.st.mouseY = 350.0f;
    r.st.mouseButton = 0;
    r.kernel.frame(r.st);
    check(r.kernel.hoveredWidget() == r.b, "the mouse regains the pointer");

    r.fingers({Rig::pt(3, 150, 100)});   // and a finger wins it back
    check(r.kernel.hoveredWidget() == r.a, "a new finger takes the pointer");
    check(r.b->clicks == 0, "the mouse move alone did not click");
    const int before = r.a->clicks;
    r.none();
    check(r.a->clicks == before + 1, "the finger's tap clicks");
}

// Nothing about any of this changes a host that only reports a mouse.
void plainMouseStillWorks() {
    Rig r;
    r.build();
    r.st.mouseX = 150.0f;
    r.st.mouseY = 100.0f;
    r.st.mouseButton = 0;
    r.st.mouseDown[0] = true;
    r.kernel.frame(r.st);
    r.st.mouseDown[0] = false;
    r.kernel.frame(r.st);
    check(r.a->downs == 1 && r.a->ups == 1 && r.a->clicks == 1,
          "a mouse press and release still clicks");
    check(r.kernel.hoveredWidget() == r.a, "and still hovers");
}

}  // namespace

int main() {
    tapIsAClick();
    positionsAreScaled();
    dragKeepsCaptureAndDoesNotClick();
    secondFingerDoesNotSteal();
    cancelReleasesWithoutClicking();
    noPhantomHoverAfterLift();
    mouseAndTouchCoexist();
    plainMouseStillWorks();

    if (g_failures) {
        std::cout << "touch_test: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "touch_test: OK\n";
    return 0;
}
