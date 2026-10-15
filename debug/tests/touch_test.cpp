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

// A widget that scrolls and says so: it reports its own step and hands back
// the pixel displacement it was given, so a test can assert that the content
// tracked the finger exactly.
class Pannable : public Probe {
public:
    float unit = 25.0f;
    bool panOk = true;
    int scrolls = 0;
    float sumDX = 0.0f, sumDY = 0.0f;   // in pixels, not notches

    virtual float scrollUnit() const override { return unit; }
    virtual bool wantsPan(float, float) const override { return panOk; }
    virtual void onPointerScroll(float dx, float dy) override {
        ++scrolls;
        sumDX += dx * unit;
        sumDY += dy * unit;
        Probe::onPointerScroll(dx, dy);
    }
};

struct PanRig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    std::shared_ptr<ScrollView> sv;
    Pannable* inner = nullptr;   // (0,0,280,800) inside the viewport
    InputState st;
    int frameNo = 0;

    void build() {
        kernel.init();
        screen = std::make_shared<Screen>("main");
        screen->width = 800.0f;
        screen->height = 600.0f;
        screen->scale = 1.0f;
        sv = std::make_shared<ScrollView>();
        sv->setBounds(Rect{50, 50, 300, 400});
        auto child = std::make_shared<Pannable>();
        inner = child.get();
        inner->setText("row");
        inner->setBounds(Rect{0, 0, 280, 800});
        sv->setContent(child);
        screen->widgets.push_back(sv);
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
    }

    // A finger frame; the clock advances a sixth of a second between frames,
    // which is what a flick's velocity is measured against.
    void touch(float x, float y) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 1;
        st.touches[0] = Rig::pt(1, x, y);
        st.touchCancelled = false;
        kernel.frame(st);
        ++frameNo;
    }
    void lift() {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        st.touchCancelled = false;
        kernel.frame(st);
        ++frameNo;
    }
    // Frames after the finger is gone, with nothing but the clock moving.
    void idle(int n) {
        for (int i = 0; i < n; ++i) {
            st.time += 1.0f / 60.0f;
            st.touchCount = 0;
            kernel.frame(st);
            ++frameNo;
        }
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

// The pressed widget answers for the drag before its ancestors do.
void pressedWidgetPansItself() {
    PanRig r;
    r.build();
    r.touch(150, 250);
    check(r.inner->downs == 1, "the finger pressed the row");
    r.touch(150, 240);   // 10px is the slop itself: not yet a drag
    r.touch(150, 200);   // past it: the drag is taken, this frame only picks it up
    check(r.inner->scrolls == 0, "the slop frame scrolls nothing");
    r.touch(150, 160);
    check(r.inner->scrolls == 1, "the row took its own drag");
    check(r.sv->scrollPosition().y == 0.0f,
          "and the viewport behind it stayed put");
    check(r.inner->sumDY == -40.0f, "the content moved as far as the finger");
    r.lift();
    check(r.inner->clicks == 0, "a drag is not a click");
    check(r.inner->ups == 1, "the row was released when the drag was taken");
}

// A widget that declines lets the drag rise to the scrollable ancestor, which
// is how every list row inside a ScrollView gets finger scrolling for free.
void anAnccestorTakesWhatTheChildDeclines() {
    PanRig r;
    r.build();
    r.inner->panOk = false;
    r.touch(150, 250);
    r.touch(150, 200);
    r.touch(150, 150);
    check(r.inner->scrolls == 0, "the declining row never scrolls");
    check(r.sv->scrollPosition().y == -50.0f,
          "the viewport panned by the finger's pixels");
    r.lift();
    check(r.inner->clicks == 0, "and never clicked");
}

// Under the slop the press stays a press.
void aSmallDragStillClicks() {
    PanRig r;
    r.build();
    r.inner->panOk = false;
    r.touch(150, 250);
    r.touch(150, 245);
    r.lift();
    check(r.sv->scrollPosition().y == 0.0f, "the slop does not scroll");
    check(r.inner->clicks == 1, "a tap inside the slop clicks");
}

// A mouse drag is a selection, a slider, a thumb: it must stay exactly that.
void aMouseDragDoesNotPan() {
    PanRig r;
    r.build();
    r.st.time += 1.0f / 60.0f;
    r.st.mouseX = 150.0f;
    r.st.mouseY = 250.0f;
    r.st.mouseDown[0] = true;
    r.st.mouseButton = Mouse_Left;
    r.kernel.frame(r.st);
    r.st.mouseY = 100.0f;
    r.st.time += 1.0f / 60.0f;
    r.kernel.frame(r.st);
    r.st.mouseDown[0] = false;
    r.st.time += 1.0f / 60.0f;
    r.kernel.frame(r.st);
    check(r.inner->scrolls == 0, "a mouse drag never becomes a pan");
    check(r.sv->scrollPosition().y == 0.0f, "the viewport did not move");
    check(r.inner->moves > 0, "the captured widget still got its drag");
}

// A flick keeps going after the finger leaves, and settles.
void aFlickKeepsScrollingAfterTheLift() {
    PanRig r;
    r.build();
    r.inner->panOk = false;
    r.touch(150, 400);
    for (int i = 1; i <= 6; ++i) r.touch(150, 400.0f - 8.0f * i);
    r.lift();
    const float atLift = r.sv->scrollPosition().y;
    check(atLift < 0.0f, "the drag itself scrolled");
    r.idle(400);
    const float settled = r.sv->scrollPosition().y;
    check(settled < atLift, "the flick carried the content on");
    check(settled > atLift - 200.0f, "and did not run away with it");
    r.idle(20);
    check(r.sv->scrollPosition().y == settled, "and came to a stop");
    check(settled >= -400.0f,
          "the fling is clamped to the content like any other scroll");
}

// A drag that slowed to a stop before the lift must not fly.
void aSlowLiftDoesNotFling() {
    PanRig r;
    r.build();
    r.inner->panOk = false;
    r.touch(150, 400);
    for (int i = 1; i <= 4; ++i) r.touch(150, 380.0f - 5.0f * i);
    for (int i = 0; i < 12; ++i) r.touch(150, 360.0f);   // held still
    r.lift();
    const float atLift = r.sv->scrollPosition().y;
    r.idle(20);
    check(r.sv->scrollPosition().y == atLift,
          "a slow release leaves no inertia behind");
}

// A new press grabs a scroll in flight, the way a hand stops a list.
void aNewPressStopsTheFling() {
    PanRig r;
    r.build();
    r.inner->panOk = false;
    r.touch(150, 400);
    for (int i = 1; i <= 6; ++i) r.touch(150, 400.0f - 8.0f * i);
    r.lift();
    r.idle(2);
    const float flying = r.sv->scrollPosition().y;
    r.touch(150, 300);
    r.idle(10);
    check(r.sv->scrollPosition().y == flying,
          "putting a finger down stops the scroll in flight");
}

// The flick outlives the finger, but not the screen: switching away drops the
// tree it was scrolling, and the frames after that must not touch it. Run this
// under a sanitizer to make the contract bite.
void aFlingDiesWithItsScreen() {
    Kernel kernel;
    kernel.init();
    InputState st;
    st.mouseButton = -1;
    auto page = [&](const char* name) {
        auto s = std::make_shared<Screen>(name);
        s->width = 800.0f;
        s->height = 600.0f;
        s->scale = 1.0f;
        return s;
    };
    auto next = page("next");
    auto other = std::make_shared<Probe>();
    other->setText("elsewhere");
    other->setBounds(Rect{50, 50, 200, 100});
    next->widgets.push_back(other);

    {
        auto first = page("first");
        auto sv = std::make_shared<ScrollView>();
        sv->setBounds(Rect{50, 50, 300, 400});
        auto inner = std::make_shared<Pannable>();
        inner->panOk = false;
        inner->setBounds(Rect{0, 0, 280, 800});
        sv->setContent(inner);
        first->widgets.push_back(sv);
        kernel.pushScreen(first);
        auto finger = [&](float y) {
            st.time += 1.0f / 60.0f;
            st.touchCount = 1;
            st.touches[0] = Rig::pt(1, 150, y);
            kernel.frame(st);
        };
        finger(400);
        for (int i = 1; i <= 6; ++i) finger(400.0f - 8.0f * i);
        st.touchCount = 0;
        st.time += 1.0f / 60.0f;
        kernel.frame(st);            // lifted: the flick is running
        st.time += 1.0f / 60.0f;
        kernel.frame(st);
        kernel.switchScreen(next);   // and the screen it scrolled goes away
    }

    for (int i = 0; i < 30; ++i) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        kernel.frame(st);
    }
    st.time += 1.0f / 60.0f;
    st.touchCount = 1;
    st.touches[0] = Rig::pt(7, 150, 100);
    kernel.frame(st);
    st.touchCount = 0;
    st.time += 1.0f / 60.0f;
    kernel.frame(st);
    check(other->clicks == 1, "the screen that replaced it still taps");
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
    pressedWidgetPansItself();
    anAnccestorTakesWhatTheChildDeclines();
    aSmallDragStillClicks();
    aMouseDragDoesNotPan();
    aFlickKeepsScrollingAfterTheLift();
    aSlowLiftDoesNotFling();
    aNewPressStopsTheFling();
    aFlingDiesWithItsScreen();

    if (g_failures) {
        std::cout << "touch_test: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "touch_test: OK\n";
    return 0;
}
