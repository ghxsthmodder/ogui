// ogui_touch_test.cpp : the touch stream folded into the kernel's pointer.
//
// A touch host reports fingers, not a cursor; Kernel::applyTouchStream turns
// that list into the single pointer every widget already speaks. What this
// checks is the bridging contract: a tap is a click, the drag stays with the
// finger that started it, a pointer the system took away does not click, and a
// host that only owns a mouse is affected by none of this. The second half is
// the two gestures a finger holds instead of moving: the long-press and the
// two-finger pinch.
#include "ogui/kernel.hpp"
#include "ogui/properties.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"
#include <cmath>
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

// The flow containers are the widgets a phone user actually drags: a row of
// cards running off the edge has no wheel to turn, and the only thing that
// reaches it is this pan machine walking up from the card under the finger.
// The drag is deliberately diagonal -- a finger never travels along one axis --
// so what is asserted is not that the free axis moves but that the other one
// does not, with room to have.
struct FlowRig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    ScrollView* box = nullptr;
    InputState st;

    void build(bool horizontal) {
        kernel.init();
        screen = std::make_shared<Screen>("main");
        screen->width = 800.0f;
        screen->height = 600.0f;
        screen->scale = 1.0f;
        std::shared_ptr<ScrollView> sv =
            horizontal ? std::static_pointer_cast<ScrollView>(
                             std::make_shared<ScrollableHorizontalContainer>())
                       : std::static_pointer_cast<ScrollView>(
                             std::make_shared<ScrollableVerticalContainer>());
        box = sv.get();
        box->setBounds(Rect{50, 50, 300, 120});
        for (int i = 0; i < 10; ++i) {
            auto card = std::make_shared<Panel>();
            // Overflowing on both axes, so a locked axis that moved would have
            // somewhere to go: only the lock itself can hold it at zero.
            card->setBounds(horizontal ? Rect{0, 0, 80, 200}
                                       : Rect{0, 0, 200, 40});
            box->addChild(card);
        }
        // The stack is 436 tall in a 120 viewport and the row 836 wide in 300:
        // both have more room than the drag below asks for.
        screen->widgets.push_back(sv);
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
    }

    void touch(float x, float y) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 1;
        st.touches[0] = Rig::pt(1, x, y);
        kernel.frame(st);
    }
    void lift() {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        kernel.frame(st);
    }
};

void aFlowContainerPansAlongItsLine() {
    FlowRig r;
    r.build(true);
    r.touch(200, 110);            // a press on a card, away from the bar
    for (int i = 1; i <= 8; ++i) r.touch(200.0f - 15.0f * i, 110.0f - 12.0f * i);
    check(r.box->scrollPosition().x < -80.0f, "the finger pulled the row across");
    check(r.box->scrollPosition().y == 0.0f, "a horizontal row never moves down");
    r.lift();
    r.kernel.frame(r.st);         // the fling inherits the same lock
    check(r.box->scrollPosition().y == 0.0f,
          "the inertia after the lift cannot unlock the axis either");
}

void aStackedContainerPansDownItsLine() {
    FlowRig r;
    r.build(false);
    r.touch(200, 110);
    for (int i = 1; i <= 8; ++i) r.touch(200.0f - 12.0f * i, 110.0f - 12.0f * i);
    check(r.box->scrollPosition().y < -60.0f, "the finger pulled the stack down");
    check(r.box->scrollPosition().x == 0.0f, "a stack never moves sideways");
    r.lift();
    r.kernel.frame(r.st);
    check(r.box->scrollPosition().x == 0.0f, "nor does its inertia");
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

// ---- the gestures a finger holds instead of moving -----------------------

// A widget that records both: the event the hold turns into, and every factor
// the pair hands it.
class GestureProbe : public Probe {
public:
    int holds = 0, pinches = 0;
    float holdX = 0.0f, holdY = 0.0f, lastFactor = 0.0f;
    Offset2D lastPivot{};
    bool pinchOk = true;

    virtual bool wantsPinch() const override { return pinchOk; }
    virtual void onPinch(float f, const Offset2D& p) override {
        ++pinches;
        lastFactor = f;
        lastPivot = p;
    }
    virtual void onEvent(Event& e) override {
        if (e.kind != EventKind::LongPress || e.currentTarget != this) return;
        ++holds;
        holdX = e.point.x;
        holdY = e.point.y;
    }
};

// One button in the middle of the screen (50,50,200,100) and a clock that
// moves, which is all a hold needs.
struct GestureRig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    GestureProbe* g = nullptr;
    InputState st;

    void build(bool withMenu) {
        kernel.init();
        screen = std::make_shared<Screen>("main");
        screen->width = 800.0f;
        screen->height = 600.0f;
        screen->scale = 1.0f;
        auto w = std::make_shared<GestureProbe>();
        g = w.get();
        g->setId("g");
        g->setText("G");
        g->setBounds(Rect{50, 50, 200, 100});   // centre (150,100)
        screen->widgets.push_back(w);
        if (withMenu) {
            auto cm = std::make_shared<ContextMenu>();
            cm->setId("cm");
            auto item = std::make_shared<MenuItem>();
            item->setLabel("Copy");
            item->setCommand("copy");
            cm->addChild(item);
            screen->widgets.push_back(cm);
            g->setContextMenuName("cm");
        }
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
    }

    void frame(std::vector<TouchPoint> pts) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        for (auto& t : pts) st.touches[st.touchCount++] = t;
        st.touchCancelled = false;
        kernel.frame(st);
    }
    // Keep one finger exactly where it landed for `seconds`.
    void hold(float x, float y, float seconds) {
        const int frames = static_cast<int>(seconds * 60.0f);
        for (int i = 0; i < frames; ++i) frame({Rig::pt(1, x, y)});
    }
};

// The delay is measured on the host clock, so a test drives it by advancing
// time, not by counting frames.
void aHeldFingerLongPresses() {
    GestureRig r;
    r.build(false);
    r.frame({Rig::pt(1, 150, 100)});
    check(r.g->downs == 1, "the finger pressed the button");
    r.hold(150, 100, 0.4f);
    check(r.g->holds == 0, "under the delay a hold is still just a press");
    r.hold(150, 100, 0.3f);
    check(r.g->holds == 1, "past the delay the hold fires once");
    check(r.g->holdX == 150.0f && r.g->holdY == 100.0f,
          "the event carries the point under the finger");
    r.hold(150, 100, 0.5f);
    check(r.g->holds == 1, "and it does not repeat while the finger stays");
    r.frame({});
    check(r.g->ups == 1, "the button was released when the gesture fired");
    check(r.g->clicks == 0, "letting go after a long-press clicks nothing");
    // A second hold is a second gesture.
    r.frame({Rig::pt(1, 150, 100)});
    r.hold(150, 100, 0.7f);
    check(r.g->holds == 2, "a new press can arm a new long-press");
    r.frame({});
}

// The name a Lua listener registers for is the string eventName gives, so the
// gesture is scriptable without the test needing a Lua VM.
void theLongPressHasAName() {
    check(std::string(eventName(EventKind::LongPress)) == "longPress",
          "the event dispatches as longPress");
}

// A mouse button held down is the first frame of a selection, a slider drag or
// a held key on a desktop -- and right-click already owns the context menu. So
// the hold is a touch-only gesture.
void aMouseHoldIsNotALongPress() {
    GestureRig r;
    r.build(false);
    r.st.mouseX = 150.0f;
    r.st.mouseY = 100.0f;
    r.st.mouseButton = 0;
    r.st.mouseDown[0] = true;
    for (int i = 0; i < 80; ++i) {   // 1.3s of a held left button
        r.st.time += 1.0f / 60.0f;
        r.kernel.frame(r.st);
    }
    check(r.g->holds == 0, "a held mouse button never becomes a long-press");
    r.st.mouseDown[0] = false;
    r.st.time += 1.0f / 60.0f;
    r.kernel.frame(r.st);
    check(r.g->clicks == 1, "and it is still the press-release click it was");
}

// A finger that travels is a drag: the pan owns it, the hold is off.
void aTravellingFingerDoesNotLongPress() {
    GestureRig r;
    r.build(false);
    r.frame({Rig::pt(1, 150, 100)});
    for (int i = 1; i <= 20; ++i)
        r.frame({Rig::pt(1, 150.0f + 10.0f * i, 100)});   // past the slop
    check(r.g->holds == 0, "a dragging finger does not hold");
    r.frame({});
    check(r.g->clicks == 0, "and it ends outside the button");
}

// What the finger came here for: the context menu, at exactly the point a
// right-click would have put it.
void aLongPressOpensTheContextMenu() {
    GestureRig r;
    r.build(true);
    r.frame({Rig::pt(1, 150, 100)});
    r.hold(150, 100, 0.7f);
    check(r.g->holds == 1, "the hold fired");
    check(r.kernel.popupOpen(), "and the widget's <contextmenu> opened");
    if (r.kernel.popupOpen()) {
        const Rect b = r.kernel.popup()->bounds();
        check(b.x == 150.0f && b.y == 100.0f, "at the finger, not elsewhere");
    }
    r.frame({});
    check(r.g->clicks == 0, "the button under it did not click");
    r.kernel.closePopup();
}

// Two fingers on the same widget are one gesture: the pair claims the press,
// and every frame after that is a ratio between the fingers plus the point
// between them.
void twoFingersPinchTheWidgetThatClaimsThem() {
    GestureRig r;
    r.build(false);
    r.frame({Rig::pt(1, 150, 100)});
    check(r.g->downs == 1, "the first finger pressed");

    r.frame({Rig::pt(1, 150, 100), Rig::pt(2, 150, 200)});
    check(r.g->pinches == 0, "the frame the pair forms is not a scale");
    check(r.g->ups == 1, "claiming the pinch released the press");
    check(r.g->clicks == 0, "and it did not click");

    r.frame({Rig::pt(1, 150, 100), Rig::pt(2, 150, 300)});   // 100 -> 200
    check(r.g->pinches == 1, "spreading hands the widget one factor");
    check(std::fabs(r.g->lastFactor - 2.0f) < 1e-5f, "the ratio of the two spans");
    check(r.g->lastPivot.x == 150.0f && r.g->lastPivot.y == 200.0f,
          "with the pivot between the fingers");

    r.frame({Rig::pt(1, 150, 100), Rig::pt(2, 150, 200)});   // 200 -> 100
    check(r.g->pinches == 2 && std::fabs(r.g->lastFactor - 0.5f) < 1e-5f,
          "closing in gives the inverse");

    r.frame({Rig::pt(1, 150, 100)});   // one finger left: the gesture is over
    r.frame({});
    check(r.g->clicks == 0, "a pinch never turns into a tap");
    check(r.g->holds == 0, "nor into a hold");
}

// Nobody claiming is a normal answer: two fingers on a plain button stay two
// fingers, and lifting them is still a tap on what was under the first one.
void aPinchNobodyWantsIsJustFingers() {
    GestureRig r;
    r.build(false);
    r.g->pinchOk = false;
    r.frame({Rig::pt(1, 150, 100)});
    r.frame({Rig::pt(1, 150, 100), Rig::pt(2, 150, 300)});
    r.frame({Rig::pt(1, 150, 100), Rig::pt(2, 400, 300)});
    check(r.g->pinches == 0, "the declining button got no pinch");
    check(r.g->ups == 0, "and it still holds the press");
    r.frame({});
    check(r.g->clicks == 1, "so the tap still clicks");
}

// A row that would pan if it were alone, and claims the pair when it is not.
class Pinnable : public Pannable {
public:
    bool pinchOk = true;
    int pinches = 0;
    virtual bool wantsPinch() const override { return pinchOk; }
    virtual void onPinch(float, const Offset2D&) override { ++pinches; }
};

struct PinchPanRig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    std::shared_ptr<ScrollView> sv;
    Pinnable* inner = nullptr;
    InputState st;

    void build() {
        kernel.init();
        screen = std::make_shared<Screen>("main");
        screen->width = 800.0f;
        screen->height = 600.0f;
        screen->scale = 1.0f;
        sv = std::make_shared<ScrollView>();
        sv->setBounds(Rect{50, 50, 300, 400});
        auto child = std::make_shared<Pinnable>();
        inner = child.get();
        inner->setText("row");
        inner->setBounds(Rect{0, 0, 280, 800});
        sv->setContent(child);
        screen->widgets.push_back(sv);
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
    }
    void frame(std::vector<TouchPoint> pts) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        for (auto& t : pts) st.touches[st.touchCount++] = t;
        st.touchCancelled = false;
        kernel.frame(st);
    }
};

// A pinch takes the fingers away from the pan, which is the difference between
// zooming a picture and scrolling the page under it.
void aPinchDoesNotPan() {
    PinchPanRig r;
    r.build();
    r.frame({Rig::pt(1, 150, 250)});
    check(r.inner->downs == 1, "the finger pressed the row");
    r.frame({Rig::pt(1, 150, 250), Rig::pt(2, 150, 350)});
    r.frame({Rig::pt(1, 150, 250), Rig::pt(2, 150, 550)});
    check(r.inner->pinches == 1, "the row took the pair");
    check(r.inner->scrolls == 0, "and nothing was panned");
    check(r.sv->scrollPosition().y == 0.0f, "the viewport stayed put");
    r.frame({});
    check(r.inner->clicks == 0, "a pinch is not a tap");

    // The fingers fall back to the pan the moment nobody claims them.
    r.inner->pinchOk = false;
    r.inner->panOk = false;
    r.frame({Rig::pt(1, 150, 400)});
    r.frame({Rig::pt(1, 150, 400), Rig::pt(2, 150, 450)});
    r.frame({Rig::pt(1, 150, 300), Rig::pt(2, 150, 350)});
    r.frame({Rig::pt(1, 150, 200), Rig::pt(2, 150, 250)});
    check(r.sv->scrollPosition().y != 0.0f,
          "a pair nobody wants can still scroll the page");
}

// The consumer this gesture was built for: <image pinchzoom="true"> magnifies
// under the fingers instead of scrolling the page.
struct PinchRig {
    Kernel kernel;
    std::shared_ptr<Screen> screen;
    std::shared_ptr<Image> img;
    InputState st;

    void build(bool tiled) {
        kernel.init();
        screen = std::make_shared<Screen>("main");
        screen->width = 800.0f;
        screen->height = 600.0f;
        screen->scale = 1.0f;
        img = std::make_shared<Image>();
        img->setBounds(Rect{0, 0, 200, 200});
        img->setImagePath(std::string(OGUI_ASSETS_DIR) + "/checker.png");
        img->setPinchZoom(true);
        img->setTile(tiled);
        screen->widgets.push_back(img);
        kernel.pushScreen(screen);
        st = InputState{};
        st.mouseButton = -1;
    }
    void frame(std::vector<TouchPoint> pts) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        for (auto& t : pts) st.touches[st.touchCount++] = t;
        st.touchCancelled = false;
        kernel.frame(st);
    }
    // The UV window the picture is cropped to, as the draw list says.
    bool drawnWindow(float& u0, float& v0, float& u1, float& v1) const {
        Rasterizer r;
        r.reset();
        img->onDraw(r);
        const auto& v = r.drawList().vertices;
        if (v.size() < 4) return false;
        u0 = v0 = 1e9f;
        u1 = v1 = -1e9f;
        for (const auto& q : v) {
            u0 = std::min(u0, q.u);
            v0 = std::min(v0, q.v);
            u1 = std::max(u1, q.u);
            v1 = std::max(v1, q.v);
        }
        return true;
    }
};

void theImageZoomsUnderTwoFingers() {
    PinchRig r;
    r.build(false);
    check(r.img->zoom() == 1.0f, "an untouched image shows the whole picture");

    // The first finger at (50,50) presses the image, the second one 100px to
    // the right forms the pair, and spreading them doubles the zoom.
    r.frame({Rig::pt(1, 50, 50)});
    r.frame({Rig::pt(1, 50, 50), Rig::pt(2, 150, 50)});
    r.frame({Rig::pt(1, 50, 50), Rig::pt(2, 250, 50)});   // 100 -> 200: x2
    check(r.img->zoom() == 2.0f, "spreading the fingers magnifies");

    // The pivot (150,50) sits at 0.75/0.25 of the 200x200 bounds, so the window
    // the picture is cropped from has to keep that point where it was.
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    const bool drawn = r.drawnWindow(u0, v0, u1, v1);
    check(drawn, "the zoomed image still emits its quad");
    if (drawn) {
        check(std::fabs((u1 - u0) - 0.5f) < 1e-5f &&
                  std::fabs((v1 - v0) - 0.5f) < 1e-5f,
              "a 2x window covers half the picture");
        check(std::fabs(u0 - 0.375f) < 1e-5f && std::fabs(v0 - 0.125f) < 1e-5f,
              "and it is anchored under the fingers");
    }

    r.frame({Rig::pt(1, 50, 50), Rig::pt(2, 1650, 50)});   // 8x wider: over the top
    check(r.img->zoom() == 8.0f, "magnifying stops at the ceiling");
    r.frame({Rig::pt(1, 50, 50), Rig::pt(2, 70, 50)});     // closed almost flat
    check(r.img->zoom() == 1.0f, "and pinching in stops at the whole picture");
    if (r.drawnWindow(u0, v0, u1, v1))
        check(u0 == 0.0f && v0 == 0.0f && u1 == 1.0f && v1 == 1.0f,
              "at 1x the frame shows all of it again");

    r.frame({});
    r.frame({Rig::pt(1, 50, 50)});
    r.frame({});
    check(r.img->zoom() == 1.0f, "a later single-finger tap does not rescale it");
}

// A tiled image repeats the picture, so there is no window to shrink: the
// fingers belong to whatever scrolls behind it.
void aTiledImageIsNotZoomable() {
    PinchRig r;
    r.build(true);
    check(!r.img->wantsPinch(), "a tiled image declines the pinch");
    r.frame({Rig::pt(1, 50, 50)});
    r.frame({Rig::pt(1, 50, 50), Rig::pt(2, 150, 50)});
    r.frame({Rig::pt(1, 50, 50), Rig::pt(2, 350, 50)});
    check(r.img->zoom() == 1.0f, "and its zoom never moved");
    r.frame({});
    check(r.img->zoom() == 1.0f, "not even after the fingers left");
}

// A gesture must not outlive the widget it was heading for: `ogui.remove` frees
// the tree under a finger that is still down, and the frames after that must
// touch nothing. Run this under a sanitizer to make the contract bite.
void aGestureDiesWithItsWidget() {
    Kernel kernel;
    kernel.init();
    auto screen = std::make_shared<Screen>("main");
    screen->width = 800.0f;
    screen->height = 600.0f;
    screen->scale = 1.0f;
    auto gone = std::make_shared<GestureProbe>();
    gone->setText("G");
    gone->setBounds(Rect{50, 50, 200, 100});
    gone->setContextMenuName("cm");   // a menu that is not there either
    auto stay = std::make_shared<GestureProbe>();
    stay->setText("S");
    stay->setBounds(Rect{500, 50, 200, 100});
    screen->widgets.push_back(gone);
    screen->widgets.push_back(stay);
    kernel.pushScreen(screen);

    InputState st;
    st.mouseButton = -1;
    auto frame = [&](std::vector<TouchPoint> pts) {
        st.time += 1.0f / 60.0f;
        st.touchCount = 0;
        for (auto& t : pts) st.touches[st.touchCount++] = t;
        st.touchCancelled = false;
        kernel.frame(st);
    };

    frame({Rig::pt(1, 150, 100)});          // a finger on the doomed row
    kernel.forgetWidget(gone.get());
    screen->widgets.erase(screen->widgets.begin());
    gone.reset();                            // and it is really gone
    kernel.invalidate();
    for (int i = 0; i < 50; ++i)             // a whole second of nothing to hold
        frame({Rig::pt(1, 150, 100)});
    frame({Rig::pt(1, 150, 100), Rig::pt(2, 150, 300)});   // a pair to pinch
    frame({Rig::pt(1, 150, 100), Rig::pt(2, 150, 500)});
    frame({});
    check(stay->clicks == 0,
          "the frames over the freed row clicked nothing on their way past");

    frame({Rig::pt(3, 600, 100)});          // the tree still works
    frame({});
    check(stay->clicks == 1, "and a new finger taps what is left");
}

// The XML attributes the feature is actually used through.
void zoomIsSettableFromXmlAndScript() {
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <image id="a" x="0" y="0" width="100" height="100" source="x.png"
         zoom="3" pinchzoom="true"/>
  <image id="b" x="0" y="0" width="100" height="100" zoom="40"/>
  <image id="c" x="0" y="0" width="100" height="100" zoom="-5"/>
</screen>)");
    check(res.errors.empty(), "the layout parses");
    Kernel k;
    k.init();
    auto s = std::make_shared<Screen>("s");
    s->width = 800.0f;
    s->height = 600.0f;
    s->scale = 1.0f;
    s->widgets.push_back(res.root);
    k.pushScreen(s);
    auto* a = dynamic_cast<Image*>(k.findWidget("a"));
    auto* b = dynamic_cast<Image*>(k.findWidget("b"));
    auto* c = dynamic_cast<Image*>(k.findWidget("c"));
    check(a && b && c, "the three images are there");
    if (!a || !b || !c) return;
    check(a->zoom() == 3.0f, "zoom=3 is a 3x window");
    check(a->pinchZoom(), "pinchzoom=true arms the gesture");
    check(!b->pinchZoom(), "and a plain image is not pinching");
    check(b->zoom() == 8.0f, "a wild zoom lands on the ceiling");
    check(c->zoom() == 1.0f, "a zoom below 1 is the whole picture");

    std::string error;
    check(setWidgetProperty(*b, "zoom", "2.5", &error), error);
    check(b->zoom() == 2.5f, "the property writes what the attribute writes");
    check(setWidgetProperty(*b, "pinchzoom", "true", &error), error);
    check(b->pinchZoom(), "pinchzoom is writable");
    check(widgetProperty(*b, "zoom") == "2.5", "and readable");
    check(widgetProperty(*b, "pinchzoom") == "true", "including as text");
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
    aFlowContainerPansAlongItsLine();
    aStackedContainerPansDownItsLine();
    aFlickKeepsScrollingAfterTheLift();
    aSlowLiftDoesNotFling();
    aNewPressStopsTheFling();
    aFlingDiesWithItsScreen();
    aHeldFingerLongPresses();
    theLongPressHasAName();
    aMouseHoldIsNotALongPress();
    aTravellingFingerDoesNotLongPress();
    aLongPressOpensTheContextMenu();
    twoFingersPinchTheWidgetThatClaimsThem();
    aPinchNobodyWantsIsJustFingers();
    aPinchDoesNotPan();
    theImageZoomsUnderTwoFingers();
    aTiledImageIsNotZoomable();
    aGestureDiesWithItsWidget();
    zoomIsSettableFromXmlAndScript();

    if (g_failures) {
        std::cout << "touch_test: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "touch_test: OK\n";
    return 0;
}
