// ogui_android_test.cpp : what an Android input event means, without a phone.
//
// AndroidHost reads AMotionEvent and AKeyEvent through the NDK's getters, which
// exist only on the device. The rules those getters feed -- which finger the
// widget tree gets, what happens when a second one joins, what a cancellation
// is not, what a key code stands for, where a typed character goes -- are plain
// data over in AndroidInputBridge, and this is where they are checked. The same
// is true of the input connection's side of the host: which keyboard a focused
// field asks for, what an edit the IME asks for does to that field, how an
// IME's UTF-16 indices line up with the UTF-8 offsets the widget counts its
// caret in, and what the window's bars become once they reach a kernel -- all of
// it is decided over ints and strings before any Java sees them.
#include "ogui/androidhost.hpp"
#include "ogui/api.hpp"
#include "ogui/kernel.hpp"
#include "ogui/widgets.hpp"

#include <cmath>
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

// ---- the edits an input connection asks for --------------------------------
// The IME's side of the bridge takes an ImeEdit by value, which is what the Java
// half fills in from a JNI call. Stamping the target is the bridge's own job, so
// a test that wants the same answer the phone gets asks the same question:
// publish the field, then queue.

void queueCommit(AndroidInputBridge& b, const std::string& text,
                 int32_t cursor = 1) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Commit;
    edit.text = text;
    edit.cursor = cursor;
    b.queueEdit(std::move(edit));
}

void queueComposing(AndroidInputBridge& b, const std::string& text) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Composing;
    edit.text = text;
    b.queueEdit(std::move(edit));
}

void queueDelete(AndroidInputBridge& b, int32_t before, int32_t after) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Delete;
    edit.before = before;
    edit.after = after;
    b.queueEdit(std::move(edit));
}

void queueKind(AndroidInputBridge& b, ImeEdit::Kind kind) {
    ImeEdit edit;
    edit.kind = kind;
    b.queueEdit(std::move(edit));
}

void queueKey(AndroidInputBridge& b, int32_t keyCode, bool down) {
    ImeEdit edit;
    edit.kind = ImeEdit::Kind::Key;
    edit.key = keyCode;
    edit.down = down;
    b.queueEdit(std::move(edit));
}

void theImeAndTheKeyboardShareOneQueue() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    r.bridge.syncEditor(field.get());

    // A keyboard that does not compose sends key events, and a rocker or a real
    // keyboard sends the same ones: one queue, in the order they arrived.
    r.bridge.onKey(r.st, 29 /* A */, true, false);
    queueKey(r.bridge, 29, false);
    r.bridge.applyEdits(field.get(), r.st);
    check(r.st.keyDown[Key_A], "the release waits for the frame the press was in");
    check(r.bridge.deliverChar(r.st) == 'a',
          "and the letter waits in the queue the frame reads");
    r.bridge.endFrame(r.st);
    check(!r.st.keyDown[Key_A], "let go by the end of the frame that saw it down");
}

void aPressAndItsReleaseInOneFrameAreStillAPress() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    r.bridge.syncEditor(field.get());

    // A phone's backspace is a down and an up in the same instant, and keyDown is
    // a level the kernel reads across frames: clearing it in the frame that set
    // it is a key that was never pressed, which is what a field that ignores the
    // delete key looks like.
    queueKey(r.bridge, 67 /* DEL */, true);
    queueKey(r.bridge, 67, false);
    r.bridge.applyEdits(field.get(), r.st);
    check(r.st.keyDown[Key_Backspace], "the frame the tree sees holds the press");
    r.bridge.endFrame(r.st);
    check(!r.st.keyDown[Key_Backspace], "and the release follows one frame later");

    // A press that outlives its own frame is released where it stands, not held
    // until some later frame decides to let go of it.
    r.bridge.onKey(r.st, 20 /* DPAD_DOWN */, true, false);
    check(r.st.keyDown[Key_Down], "down, and the frame ends");
    r.bridge.endFrame(r.st);
    check(r.st.keyDown[Key_Down], "still down: nobody said it came up");
    r.bridge.onKey(r.st, 20, false, false);
    check(!r.st.keyDown[Key_Down], "the release arrives in a later frame");
    r.bridge.endFrame(r.st);
    check(!r.st.keyDown[Key_Down], "and is not held over into the one after");
}

void aCommittedWordLandsInTheFieldItWasTypedFor() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    field->setText("hi");
    r.bridge.syncEditor(field.get());

    queueCommit(r.bridge, " there");
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "hi there", "a commit writes at the caret");
    check(field->caret() == field->text().size(),
          "and leaves it after what it wrote");

    // newCursorPosition <= 0 means the caret goes before the inserted text, which
    // is how a keyboard that has just typed a word parks itself on its first
    // letter to offer corrections over it.
    queueCommit(r.bridge, "x", 0);
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "hi therex", "the text still lands where the caret was");
    check(field->caret() == field->text().size() - 1,
          "but the caret can be asked to stop before it");

    Rig s;
    auto selected = std::make_shared<TextInput>();
    selected->setText("hi there");
    selected->setSelection(0, 2);            // "hi"
    s.bridge.syncEditor(selected.get());
    queueCommit(s.bridge, "yo");
    s.bridge.applyEdits(selected.get(), s.st);
    check(selected->text() == "yo there", "a commit takes the selection with it");
}

void aBackspaceTakesAWholeCharacterAndNotAByte() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    const std::string cafe = std::string("caf\xC3\xA9");
    field->setText(cafe + "\xF0\x9F\x98\x80");       // "café" plus one emoji
    r.bridge.syncEditor(field.get());
    check(field->caret() == cafe.size() + 4,
          "the field ends with four bytes of emoji");

    // One code unit back is the whole emoji. One byte back would be the last
    // continuation byte of it, which is the bug this boundary exists to avoid.
    queueDelete(r.bridge, 1, 0);
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == cafe, "deleteSurroundingText(1) takes four bytes");

    queueDelete(r.bridge, 1, 0);
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "caf", "and the accented letter is one, not two");

    // A selection is deleted along with what surrounds it, measured from its ends.
    Rig s;
    auto selected = std::make_shared<TextInput>();
    selected->setText("abcdef");
    selected->setSelection(1, 3);            // "bc"
    s.bridge.syncEditor(selected.get());
    queueDelete(s.bridge, 1, 0);
    s.bridge.applyEdits(selected.get(), s.st);
    check(selected->text() == "def",
          "one back before a selected range takes the letter left of it");

    // The same rule from the other side. A delete that reaches forward is the
    // key that removes what is under the caret, so it takes the whole pair
    // rather than the half of it one code unit names.
    Rig f;
    auto ahead = std::make_shared<TextInput>();
    ahead->setText(std::string("a\xF0\x9F\x98\x80") + "b");
    ahead->setSelection(1, 1);
    f.bridge.syncEditor(ahead.get());
    queueDelete(f.bridge, 0, 1);
    f.bridge.applyEdits(ahead.get(), f.st);
    check(ahead->text() == "ab", "one code unit forward takes the whole pair");
    check(ahead->caret() == 1,
          "and the caret stays on the boundary it started on");
}

void theComposingSpanIsReplacedAndNotAppended() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    r.bridge.syncEditor(field.get());

    queueComposing(r.bridge, "wor");
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "wor", "composing text is in the field as it is typed");

    // The whole point of the span: the next keystroke replaces what the IME owns
    // instead of writing the word out again after itself.
    queueComposing(r.bridge, "word");
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "word", "the next one replaces the span");
    const ImeEditor snap = r.bridge.editor();
    check(snap.composing && snap.compStart == 0 && snap.compEnd == 4,
          "and the span moved with the text it covers");

    // A commit widens over the composition, which is what picking a candidate
    // from under it means to the user.
    queueCommit(r.bridge, "word ");
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "word ",
          "committing finalises it instead of doubling it");
    check(!r.bridge.editor().composing, "and the IME owns nothing after that");

    // finishComposingText keeps the text and lets go of the span: the word stays
    // on screen, the keyboard stops treating it as one unit.
    Rig s;
    auto area = std::make_shared<TextArea>();
    s.bridge.syncEditor(area.get());
    queueComposing(s.bridge, "hal");
    s.bridge.applyEdits(area.get(), s.st);
    queueKind(s.bridge, ImeEdit::Kind::Finish);
    s.bridge.applyEdits(area.get(), s.st);
    check(area->text() == "hal", "a finish leaves the text standing");
    check(!s.bridge.editor().composing, "and the span to itself");
    check(area->caret() == 3, "with the caret at its end");
}

void aRegionTheImeClaimsIsReplacedLikeASpan() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    field->setText("one two");
    field->setSelection(8, 8);
    r.bridge.syncEditor(field.get());

    // Some keyboards mark a word that is already on screen as theirs (a swipe
    // over an existing word, a correction offer) without writing anything.
    ImeEdit region;
    region.kind = ImeEdit::Kind::ComposingRegion;
    region.start = 4;
    region.end = 7;                // "two", in code units
    r.bridge.queueEdit(std::move(region));
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "one two", "claiming a span writes nothing");
    check(r.bridge.editor().composing, "... and marks it as the IME's");
    check(r.bridge.editor().compStart == 4 && r.bridge.editor().compEnd == 7,
          "at the bytes the units stand for");

    queueComposing(r.bridge, "2");
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "one 2",
          "so the next keystroke replaces the word, not the caret");
}

void anEditorActionIsTheEnterTheFieldAlreadyUnderstands() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    r.bridge.syncEditor(field.get());

    queueKind(r.bridge, ImeEdit::Kind::Action);
    r.bridge.applyEdits(field.get(), r.st);
    check(r.st.keyDown[Key_Enter],
          "the action holds Enter for the frame that follows");
    r.bridge.endFrame(r.st);
    check(!r.st.keyDown[Key_Enter],
          "and releases it after, so a submit that is held down is not one");
}

void anEditForAFieldThatLostTheFocusIsDropped() {
    Rig r;
    auto first = std::make_shared<TextInput>();
    first->setText("first");
    auto second = std::make_shared<TextInput>();
    second->setText("second");

    r.bridge.syncEditor(first.get());
    queueCommit(r.bridge, "!");
    // A tap moved the focus before the drain got the edit. Its positions count a
    // text the new field never held, so it goes with the field it was typed for.
    r.bridge.syncEditor(second.get());
    r.bridge.applyEdits(second.get(), r.st);
    check(second->text() == "second",
          "the edit does not land in the field that took the focus");
    check(first->text() == "first",
          "and the field it was typed for is left as it was");

    // An edit queued before any field was published has no opinion about focus,
    // which is the answer for a keyboard that beat the app's first frame to it.
    Rig s;
    auto third = std::make_shared<TextInput>();
    queueCommit(s.bridge, "x");
    s.bridge.applyEdits(third.get(), s.st);
    check(third->text() == "x",
          "a first commit with nothing published still lands");
}

void theSnapshotIsWhatTheImeIsTold() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    field->setText("a\U0001F600b");                            // "a😀b"
    field->setSelection(5, 5);                                // after the emoji
    r.bridge.syncEditor(field.get());

    const ImeEditor snap = r.bridge.editor();
    check(snap.selStart == 5,
          "the snapshot keeps the bytes the widget counts in");
    check(imeCountUnits(snap.text, 0, snap.selStart) == 3,
          "and the Java side is told three code units, which is what it counts in");

    // A field that is not an editor has nothing for an IME to answer for; the
    // empty snapshot is what makes the keyboard go away rather than stay up.
    auto button = std::make_shared<Button>();
    r.bridge.syncEditor(button.get());
    check(!r.bridge.editor().field, "a button publishes no field");
    check(r.bridge.editor().text.empty(), "... and no text to compose into");

    r.bridge.syncEditor(nullptr);
    check(!r.bridge.editor().field, "and neither does no focus at all");
}

void theWindowTakingItsFingersBackClearsEverything() {
    Rig r;
    auto field = std::make_shared<TextInput>();
    field->setText("hi");
    r.bridge.syncEditor(field.get());
    r.down(9, 1.0f, 2.0f);
    queueCommit(r.bridge, "!");
    r.bridge.reset();
    check(r.st.touchCount == 1,
          "reset does not rewrite what the host already sent");
    std::vector<AndroidPointer> none;
    r.bridge.onMotion(r.st, AndroidAction::Up, 0, none.data(), 0);
    check(r.st.touchCount == 0, "and the next event sees no fingers");
    check(r.bridge.deliverChar(r.st) == 0, "the typeahead is gone");
    check(!r.bridge.editor().field, "the field the IME was answering for is gone");
    r.bridge.applyEdits(field.get(), r.st);
    check(field->text() == "hi",
          "and the edit queued for it does not come back later");
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

    check(imeAdvanceUnits(emoji, 0, 1) == 1,
          "one unit off 'a' is the emoji's lead");
    check(imeAdvanceUnits(emoji, 0, 3) == 5, "three units is past the whole pair");
    // The case a deletion arrives as: the IME asks for one character and must not
    // be pointed at the middle of a pair, where half an emoji would be cut out.
    check(imeAdvanceUnits(emoji, 1, 1) == 1, "a pair is not one unit wide");
    check(imeAdvanceUnits(emoji, 1, 2) == 5, "and moves as two");
    check(imeAdvanceUnits(accented, 3, 1) == 5, "one unit is the whole accent");
    check(imeAdvanceUnits(emoji, emoji.size(), 1) == emoji.size(),
          "asking past the end stays at the end");

    check(imeRewindUnits(emoji, emoji.size(), 1) == 5,
          "one unit back from the end");
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

// The window's bars are measured by the platform on the thread that owns the
// window, in device pixels, and have to move a tree that is drawn on another
// thread in logical units. Both halves of that are rules, so both are checked
// here rather than on a phone.
void theWindowTellsTheTreeWhereItsBarsAre() {
    const float d = 2.8125f;      // the SM-A155M's 450 dpi
    AndroidHost host;
    Kernel kernel;
    kernel.init();
    auto screen = std::make_shared<Screen>("s");
    screen->width = 1080.0f;
    screen->height = 2340.0f;
    screen->scale = d;
    kernel.pushScreen(screen);

    WindowInsets out;
    check(!host.input().takeWindowInsets(out),
          "a window nobody has measured has no bars to hand over");
    host.applyWindowInsets(kernel);
    check(kernel.safeRect().x == 0.0f && kernel.safeRect().y == 0.0f,
          "and until it says otherwise the tree gets the whole of it");

    host.input().setWindowInsets(WindowInsets{0, 72, 0, 1050});
    check(host.input().takeWindowInsets(out), "a bar the platform reported is drained");
    check(out.top == 72 && out.bottom == 1050, "drained as it was measured, in device pixels");
    check(!host.input().takeWindowInsets(out),
          "and only once: a bar that has not moved must not lay the tree out again");

    // The conversion, on a pair that has not been drained by hand: a status bar
    // 72 device pixels tall is 25.6 logical units on this phone, and the number
    // the kernel keeps has to be the one the layout is authored in.
    AndroidHost measured;
    Kernel k2;
    k2.init();
    auto phone = std::make_shared<Screen>("phone");
    phone->width = 1080.0f;
    phone->height = 2340.0f;
    phone->scale = d;
    k2.pushScreen(phone);
    measured.input().setWindowInsets(WindowInsets{0, 72, 0, 1050});
    measured.applyWindowInsets(k2);
    check(std::fabs(k2.insets().top - 72.0f / d) < 0.001f,
          "the scale is what turns a bar of 72 pixels into one of 25 units");
    check(std::fabs(k2.safeRect().y - 72.0f / d) < 0.001f,
          "so the tree starts under the status bar");
    check(std::fabs(k2.safeRect().h - (2340.0f - 72.0f - 1050.0f) / d) < 0.01f,
          "and stops above the keyboard");
    k2.setInsets(Insets{1.0f, 2.0f, 3.0f, 4.0f});
    measured.applyWindowInsets(k2);
    check(k2.insets().top == 2.0f,
          "and the next frame is not told the same bar again");

    // The keyboard leaving is not a second entry under the first. An inset is
    // what the window is right now, and a queue that replayed the bar that had
    // already gone would push the layout out from under the one that stayed.
    host.input().setWindowInsets(WindowInsets{0, 72, 0, 96});
    host.input().setWindowInsets(WindowInsets{0, 72, 0, 0});
    check(host.input().takeWindowInsets(out) && out.bottom == 0,
          "the newest bar is the only bar there is");

    // Subtracting two rectangles is how a host produces a negative bar, and a
    // hole with a negative edge is more canvas, which is not a thing the system
    // ever hands back.
    host.input().setWindowInsets(WindowInsets{-40, -1, 0, 0});
    check(host.input().takeWindowInsets(out) && out.left == 0 && out.top == 0,
          "an inset that would enlarge the window is read as no inset");
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
    aCommittedWordLandsInTheFieldItWasTypedFor();
    aBackspaceTakesAWholeCharacterAndNotAByte();
    theComposingSpanIsReplacedAndNotAppended();
    aRegionTheImeClaimsIsReplacedLikeASpan();
    anEditorActionIsTheEnterTheFieldAlreadyUnderstands();
    anEditForAFieldThatLostTheFocusIsDropped();
    theSnapshotIsWhatTheImeIsTold();
    theImeAndTheKeyboardShareOneQueue();
    aPressAndItsReleaseInOneFrameAreStillAPress();
    theWindowTakingItsFingersBackClearsEverything();
    theKeyboardMatchesTheFieldThatHasTheFocus();
    theImeCountsInCodeUnitsAndTheWidgetInBytes();
    theWindowTellsTheTreeWhereItsBarsAre();

    if (g_failures) {
        std::cout << "android_test: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "android_test: OK\n";
    return 0;
}
