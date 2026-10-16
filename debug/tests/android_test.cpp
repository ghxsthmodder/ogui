// ogui_android_test.cpp : what an Android input event means, without a phone.
//
// AndroidHost reads AMotionEvent and AKeyEvent through the NDK's getters, which
// exist only on the device. The rules those getters feed -- which finger the
// widget tree gets, what happens when a second one joins, what a cancellation
// is not, what a key code stands for, where a typed character goes -- are plain
// data over in AndroidInputBridge, and this is where they are checked. The same
// is true of the input connection's side of the host: which keyboard a focused
// field asks for, and how an IME's UTF-16 indices line up with the UTF-8
// offsets the widget counts its caret in, are decided over ints and strings
// before any Java sees them.
#include "ogui/androidhost.hpp"
#include "ogui/api.hpp"
#include "ogui/kernel.hpp"
#include "ogui/widgets.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace ogui;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) return;
    std::cout << "android_test: FAIL " << what << "\n";
    ++g_failures;
}

struct Rig {
    AndroidInputBridge bridge;
    InputState st;

    void down(int id, float x, float y) {
        const AndroidPointer p{id, x, y};
        bridge.onMotion(st, AndroidAction::Down, 0, &p, 1);
    }
    // Android reports every pointer in every event, so a second finger arrives
    // as a two-element list with the new one at `index`.
    void join(int id, float x, float y) {
        std::vector<AndroidPointer> all = current();
        all.push_back({id, x, y});
        bridge.onMotion(st, AndroidAction::PointerDown,
                        static_cast<int>(all.size()) - 1, all.data(),
                        static_cast<int>(all.size()));
    }
    void moveTo(std::vector<AndroidPointer> all) {
        bridge.onMotion(st, AndroidAction::Move, 0, all.data(),
                        static_cast<int>(all.size()));
    }
    // Android keeps reporting every pointer, the lifting one included, and says
    // with `index` which of them left -- which is not the same as which one
    // arrived first, the whole reason the bridge drops by id.
    void lift(std::vector<AndroidPointer> all, int index) {
        bridge.onMotion(st, AndroidAction::PointerUp, index, all.data(),
                        static_cast<int>(all.size()));
    }
    void liftLast(AndroidPointer gone) {
        std::vector<AndroidPointer> all{gone};
        bridge.onMotion(st, AndroidAction::Up, 0, all.data(), 1);
    }
    void cancel(std::vector<AndroidPointer> all) {
        bridge.onMotion(st, AndroidAction::Cancel, 0, all.data(),
                        static_cast<int>(all.size()));
    }
    std::vector<AndroidPointer> current() const {
        std::vector<AndroidPointer> out;
        for (int i = 0; i < st.touchCount; ++i)
            out.push_back({st.touches[i].id, st.touches[i].x, st.touches[i].y});
        return out;
    }
    int indexOf(int id) const {
        for (int i = 0; i < st.touchCount; ++i)
            if (st.touches[i].id == id) return i;
        return -1;
    }
};

// One frame's worth of input, as the host would send it, then the edges cleared.
void frame(Rig& r) {
    r.bridge.endFrame(r.st);
}

void theFirstFingerBecomesThePointer() {
    Rig r;
    r.down(7, 120.0f, 340.0f);
    check(r.st.touchCount == 1, "one finger is one touch");
    check(r.indexOf(7) == 0, "reported under its own id");
    check(r.st.touches[0].x == 120.0f && r.st.touches[0].y == 340.0f,
          "in device pixels, where the kernel divides by the scale");
    check(!r.st.touchCancelled, "a press is not a cancellation");
}

void aSecondFingerJoinsWithoutReordering() {
    Rig r;
    r.down(1, 10.0f, 10.0f);
    r.join(2, 90.0f, 90.0f);
    check(r.st.touchCount == 2, "both fingers are on the screen");
    check(r.indexOf(1) == 0 && r.indexOf(2) == 1,
          "in arrival order, which is who inherits the drag");

    r.moveTo({{1, 12.0f, 14.0f}, {2, 95.0f, 99.0f}});
    check(r.st.touches[0].x == 12.0f && r.st.touches[1].y == 99.0f,
          "a move refreshes every finger, not only the one that moved");
}

void theLiftedFingerLeavesTheOthersAlone() {
    Rig r;
    r.down(1, 10.0f, 10.0f);
    r.join(2, 90.0f, 90.0f);
    // The event that lifts finger 1 still reports both, with the lifter at 0.
    r.lift({{1, 10.0f, 10.0f}, {2, 90.0f, 90.0f}}, 0);
    check(r.st.touchCount == 1, "the press goes on with the finger that stayed");
    check(r.indexOf(1) < 0 && r.indexOf(2) == 0,
          "by id, not by the index Android just shifted");

    r.liftLast({2, 90.0f, 90.0f});
    check(r.st.touchCount == 0, "the last lift ends the sequence");
    check(!r.st.touchCancelled, "and a lift is not a cancellation");
}

void cancelIsNotARelease() {
    Rig r;
    r.down(3, 50.0f, 50.0f);
    r.cancel({{3, 50.0f, 50.0f}});
    check(r.st.touchCount == 0, "the system took the finger away");
    check(r.st.touchCancelled, "which the kernel must read as no click");
    frame(r);
    check(!r.st.touchCancelled,
          "and only for that frame: an old cancel would kill the next drag");
}

void moreFingersThanTheStateHoldsAreNotSent() {
    Rig r;
    std::vector<AndroidPointer> all;
    for (int i = 0; i < InputState::kMaxTouchPoints + 6; ++i)
        all.push_back({i, static_cast<float>(i), static_cast<float>(i)});
    r.bridge.onMotion(r.st, AndroidAction::Down, 0, all.data(),
                      static_cast<int>(all.size()));
    check(r.st.touchCount == InputState::kMaxTouchPoints,
          "the list is capped at what InputState can carry");
}

void anEventWithNoPointersClearsTheScreen() {
    Rig r;
    r.down(4, 10.0f, 10.0f);
    r.bridge.onMotion(r.st, AndroidAction::Up, 0, nullptr, 0);
    check(r.st.touchCount == 0, "an empty ACTION_UP still lifts");
}

void keyCodesArriveInTheLibrarysOwnLanguage() {
    Rig r;
    r.bridge.onKey(r.st, 29 /* AKEYCODE_A */, true, false);
    check(r.st.keyDown[Key_A], "AKEYCODE_A is Key_A");
    r.bridge.onKey(r.st, 29, false, false);
    check(!r.st.keyDown[Key_A], "and lifting clears it");

    r.bridge.onKey(r.st, 4 /* AKEYCODE_BACK */, true, false);
    check(r.st.keyDown[Key_Escape], "back closes what is open, like Escape");
    r.bridge.onKey(r.st, 4, false, false);

    r.bridge.onKey(r.st, 24 /* AKEYCODE_VOLUME_UP */, true, false);
    check(!r.st.keyDown[1] && !r.st.keyDown[2],
          "a key with no equivalent is Key_None, never a guess");
}

void modifiersAreKeysLikeAnyOther() {
    Rig r;
    r.bridge.onKey(r.st, 59 /* SHIFT_LEFT */, true, false);
    r.bridge.onKey(r.st, 113 /* CTRL_LEFT */, true, false);
    check(r.st.keyDown[Key_LSHIFT], "shift is where the kernel reads it");
    check(r.st.keyDown[Key_LCTRL], "so is ctrl");
}

void aKeyTypesAndAutoRepeatsKeepTyping() {
    Rig r;
    r.bridge.onKey(r.st, 29 /* A */, true, false);
    check(r.bridge.deliverChar(r.st) == 'a', "a letter types itself");
    check(r.st.lastChar == 'a', "into the frame that follows");

    r.bridge.onKey(r.st, 29, true, true);
    check(r.st.keyRepeat[Key_A], "a held key says so");
    check(r.bridge.deliverChar(r.st) == 'a', "and types again");

    frame(r);
    check(r.st.lastChar == 0, "nothing is delivered twice");
    check(r.bridge.deliverChar(r.st) == 0, "and an empty queue is a zero");
}

void shiftAndCapsLockChangeOnlyTheLetters() {
    Rig r;
    r.bridge.onKey(r.st, 59 /* SHIFT_LEFT */, true, false);
    r.bridge.onKey(r.st, 29 /* A */, true, false);
    check(r.bridge.deliverChar(r.st) == 'A', "shift gives the capital");

    r.bridge.onKey(r.st, 7 /* 0 */, true, false);
    check(r.bridge.deliverChar(r.st) == '!', "and the shifted row of a number");
    r.bridge.onKey(r.st, 59, false, false);
    r.bridge.onKey(r.st, 7, true, false);
    check(r.bridge.deliverChar(r.st) == '0', "without it, the plain one");

    r.bridge.onKey(r.st, 115 /* CAPS_LOCK */, true, false);
    r.bridge.onKey(r.st, 29 /* A */, true, false);
    check(r.bridge.deliverChar(r.st) == 'A', "caps lock capitals on its own");
    r.bridge.onKey(r.st, 76 /* SLASH */, true, false);
    check(r.bridge.deliverChar(r.st) == '/', "and never touches the rest");
    r.bridge.onKey(r.st, 115, true, false);
    r.bridge.onKey(r.st, 29, true, false);
    check(r.bridge.deliverChar(r.st) == 'a', "tapping it again clears");
}

void theImeAndTheKeyboardShareOneQueue() {
    Rig r;
    r.bridge.commitText("olá");
    check(r.bridge.deliverChar(r.st) == 'o', "one character per frame");
    check(r.bridge.deliverChar(r.st) == 'l', "in the order it was typed");
    check(r.bridge.deliverChar(r.st) == 0xE1, "and a whole code point, not a byte");

    r.bridge.onKey(r.st, 66 /* ENTER */, true, false);
    r.bridge.commitText("x");
    check(r.bridge.deliverChar(r.st) == 'x',
          "a key in between does not jump the queue");
}

void theWindowTakingItsFingersBackClearsEverything() {
    Rig r;
    r.down(9, 1.0f, 2.0f);
    r.bridge.commitText("hi");
    r.bridge.reset();
    check(r.st.touchCount == 1, "reset does not rewrite what the host already sent");
    std::vector<AndroidPointer> none;
    r.bridge.onMotion(r.st, AndroidAction::Up, 0, none.data(), 0);
    check(r.st.touchCount == 0, "and the next event sees no fingers");
    check(r.bridge.deliverChar(r.st) == 0, "or the text that was queued");
}

void theKeyboardMatchesTheFieldThatHasTheFocus() {
    // The ints are what the app hands to InputMethodManager, so they are checked
    // as ints: a wrong bit here is a phone showing the wrong keyboard, which no
    // test on this machine would notice.
    const auto text = imeRequestFor(std::make_shared<TextInput>().get());
    check(text.visible, "a field asks for a keyboard");
    check(text.inputType == 1, "TYPE_CLASS_TEXT");
    check(text.imeOptions == 6, "IME_ACTION_DONE, because Enter submits");

    const auto area = imeRequestFor(std::make_shared<TextArea>().get());
    check(area.visible, "and so does a multiline one");
    check(area.inputType == (1 | (1 << 17)),
          "the same class with TYPE_TEXT_FLAG_MULTI_LINE");
    check(area.imeOptions == (1 << 30),
          "IME_FLAG_NO_ENTER_ACTION: Enter writes a newline here");

    const auto search = imeRequestFor(std::make_shared<SearchInput>().get());
    check(search.inputType == 1, "a search box is still text");
    check(search.imeOptions == 3, "IME_ACTION_SEARCH is the only difference");

    // TextArea and SearchInput are TextInputs, so a mapping that asked the base
    // class first would hand back DONE for all three and never say so.
    check(area.inputType != text.inputType, "the subclass wins over the base");
    check(search.imeOptions != text.imeOptions, "and so does the next one");

    const auto button = imeRequestFor(std::make_shared<Button>().get());
    check(!button.visible && button.inputType == 0 && button.imeOptions == 0,
          "a widget that edits nothing asks for no keyboard");
    const auto nothing = imeRequestFor(nullptr);
    check(!nothing.visible, "and neither does no focus at all");
}

void theImeCountsInCodeUnitsAndTheWidgetInBytes() {
    const std::string emoji = "a\U0001F600b";   // 1 + 4 + 1 bytes, 1 + 2 + 1 units
    const std::string accented = "caf\xC3\xA9"; // é: two bytes, one unit

    check(imeCountUnits(emoji, 0, emoji.size()) == 4,
          "an astral code point is two units to the IME");
    check(imeCountUnits(accented, 0, accented.size()) == 4,
          "an accent is one, however many bytes it takes");
    check(imeCountUnits(emoji, 0, 0) == 0, "an empty range counts nothing");

    check(imeAdvanceUnits(emoji, 0, 1) == 1, "one unit off 'a' is the emoji's lead");
    check(imeAdvanceUnits(emoji, 0, 3) == 5, "three units is past the whole pair");
    // The case a deletion arrives as: the IME asks for one character and must not
    // be pointed at the middle of a pair, where half an emoji would be cut out.
    check(imeAdvanceUnits(emoji, 1, 1) == 1, "a pair is not one unit wide");
    check(imeAdvanceUnits(emoji, 1, 2) == 5, "and moves as two");
    check(imeAdvanceUnits(accented, 3, 1) == 5, "one unit is the whole accent");
    check(imeAdvanceUnits(emoji, emoji.size(), 1) == emoji.size(),
          "asking past the end stays at the end");

    check(imeRewindUnits(emoji, emoji.size(), 1) == 5, "one unit back from the end");
    check(imeRewindUnits(emoji, 5, 2) == 1, "two back is over the pair");
    check(imeRewindUnits(emoji, 5, 1) == 5, "and one is still inside it");
    check(imeRewindUnits(accented, accented.size(), 1) == 3,
          "the accent comes off as one unit");
    check(imeRewindUnits(emoji, 0, 3) == 0, "rewinding off the front stops at 0");

    // Round trip: a caret the widget actually holds -- so a code-point boundary,
    // never the middle of a pair -- is reachable by its own unit count from
    // either end, which is the property every query the IME makes rests on. The
    // walk asks for two units at a time because that is the widest a code point
    // can be: one request, one character, and never a step that does not move.
    std::vector<size_t> bounds;
    for (size_t at = 0;; at = imeAdvanceUnits(emoji, at, 2)) {
        bounds.push_back(at);
        if (at == emoji.size()) break;
    }
    check(bounds == std::vector<size_t>({0, 1, 5, 6}),
          "one boundary before each character and one after the last");
    for (size_t caret : bounds) {
        check(imeAdvanceUnits(emoji, 0, imeCountUnits(emoji, 0, caret)) == caret,
              "a caret's byte offset is reachable by its unit count");
        check(imeRewindUnits(emoji, emoji.size(),
                             imeCountUnits(emoji, caret, emoji.size())) == caret,
              "and from the other end");
    }

    // A field with a broken byte in it is what a paste hands over, and an index
    // that walked off the end of the string would be found on a phone instead.
    const std::string broken = "a\xC3";
    check(imeAdvanceUnits(broken, 0, 2) == 2, "a truncated sequence is one unit");
    check(imeRewindUnits(broken, 2, 1) == 1, "and steps back the same way");
    check(imeCountUnits(broken, 0, broken.size()) == 2, "counted as two");
}

} // namespace

int main() {
    theFirstFingerBecomesThePointer();
    aSecondFingerJoinsWithoutReordering();
    theLiftedFingerLeavesTheOthersAlone();
    cancelIsNotARelease();
    moreFingersThanTheStateHoldsAreNotSent();
    anEventWithNoPointersClearsTheScreen();
    keyCodesArriveInTheLibrarysOwnLanguage();
    modifiersAreKeysLikeAnyOther();
    aKeyTypesAndAutoRepeatsKeepTyping();
    shiftAndCapsLockChangeOnlyTheLetters();
    theImeAndTheKeyboardShareOneQueue();
    theWindowTakingItsFingersBackClearsEverything();
    theKeyboardMatchesTheFieldThatHasTheFocus();
    theImeCountsInCodeUnitsAndTheWidgetInBytes();

    if (g_failures) {
        std::cout << "android_test: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "android_test: OK\n";
    return 0;
}
