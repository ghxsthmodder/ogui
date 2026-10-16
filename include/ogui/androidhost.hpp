#pragma once
#include "ogui/api.hpp"
#include "ogui/kernel.hpp"
#include <bitset>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations for the pieces of the Android API this host talks to.
// On a phone <android/native_activity.h> and <android/native_window.h> define
// them; everywhere else they stay incomplete and the host simply fails to
// open, exactly like the desktop Host does off Windows.
struct ANativeActivity;
struct ANativeWindow;
struct AInputEvent;

namespace ogui {

class GlBackend;
class TextInput;

// ------------------------------------------------------------------
// What the IME is being asked for, without a phone
// ------------------------------------------------------------------
// An input connection is a Java object, so everything ogui decides *about* one
// has to be handed over as ints and strings -- but the two decisions themselves
// do not need a device to be right, and neither does the counting this boundary
// requires. They live here over plain data: which keyboard a focused widget
// wants, and how the IME's UTF-16 positions line up with the widget tree's
// UTF-8 ones.
//
// The constants below are android.text.InputType and
// android.view.inputmethod.EditorInfo spelled as the numbers those classes
// define (checked against android.jar for API 36), because the value an app
// hands to InputMethodManager is these bits or nothing else.
struct ImeRequest {
    // TYPE_CLASS_TEXT (1), or that with TYPE_TEXT_FLAG_MULTI_LINE (1<<17) added
    // for a field whose Enter is a newline. Zero means no editor has the focus,
    // which is the answer that puts the keyboard away.
    int32_t inputType = 0;
    // The return key: IME_ACTION_DONE (6) for a field that submits,
    // IME_ACTION_SEARCH (3) for one that searches, or IME_FLAG_NO_ENTER_ACTION
    // (1<<30) where Enter is text like any other.
    int32_t imeOptions = 0;
    bool visible = false;
};

// Which keyboard a focused widget wants. A TextArea and a SearchInput *are*
// TextInputs, so the order of these tests is the answer: what the IME has to
// match is the subclass's own behaviour, and anything else that is not an
// editor at all asks for nothing.
ImeRequest imeRequestFor(const Widget* focused);

// ---- code units and bytes --------------------------------------------------
// An IME counts positions in UTF-16 code units; TextInput's caret and selection
// are byte offsets into UTF-8. One emoji is two units and four bytes, which is
// the whole reason the conversion is a boundary of its own: an app that gets it
// wrong deletes half an emoji, and only finds out on a phone.
//
// `from` is always an offset the widget itself produced, so it sits on a
// code-point boundary; a stray byte in the *text* is still consumed one at a
// time rather than stalling, the way utf8Next does it.

// The byte offset `units` code units after `from`, clamped to the end.
size_t imeAdvanceUnits(const std::string& utf8, size_t from, int32_t units);
// The byte offset `units` code units before `from`, clamped to the start.
size_t imeRewindUnits(const std::string& utf8, size_t from, int32_t units);
// How many code units [from, to) is worth: 0 for an empty range, 1 for a
// letter, 2 for an emoji, and never more than the bytes between them.
int32_t imeCountUnits(const std::string& utf8, size_t from, size_t to);

// ---- the field, and the edits asked for it --------------------------------
// An input connection is called on the activity's *UI* thread while the widget
// tree is only touched by the thread `android_main` runs on, so the two never
// meet a widget: the app publishes what the focused field looks like, the IME
// queues what it wants done to it, and the queue is drained on the app's side of
// the next frame. Both sides are plain data, so both sides are testable, and the
// units each speaks are written down on the type that carries them.
struct ImeEditor {
    std::string text;             // the field's own text, UTF-8
    size_t selStart = 0;          // byte offsets, as TextInput keeps them
    size_t selEnd = 0;
    // The span the IME is still composing -- text it owns and may replace on the
    // next keystroke. `composing` is what makes an empty span different from a
    // span of nothing: an IME that has just begun has nothing to replace.
    bool composing = false;
    size_t compStart = 0;
    size_t compEnd = 0;
    // The widget this state belongs to, compared by pointer and never
    // dereferenced: a publish from the app thread and a read from the UI thread
    // are the only two things that touch it, and the tree outlives both.
    const Widget* field = nullptr;
};

// One request from the IME, in the units the IME counts in -- UTF-16 code units
// for every position and length, and text handed over as UTF-8.
struct ImeEdit {
    // Which field the IME was answering for, stamped by the bridge when the edit
    // was queued and never written by a caller. A tap that moved the focus while
    // a keyboard was mid-sentence is not a reason to type into the new field.
    const Widget* target = nullptr;

    enum class Kind : int32_t {
        Commit,           // replace the selection with text and finish composing
        Composing,        // replace the composing span (or the selection) with text
        ComposingRegion,  // the IME claims a span of text that is already there
        Finish,           // the span the IME owned is now the widget's
        Delete,           // text around the cursor, and any selection, goes
        Select,           // an absolute cursor or selection
        Key,              // a key event the IME sent instead of an edit
        Action,           // the return key was pressed: Done, Search, Next, ...
    };
    Kind kind = Kind::Commit;
    std::string text;
    int32_t cursor = 1;      // newCursorPosition: <= 0 lands before `text`
    int32_t before = 0;      // Delete, in code units from the selection's start
    int32_t after = 0;       // Delete, in code units from its end
    int32_t start = 0;       // Select / ComposingRegion, absolute code units
    int32_t end = 0;
    int32_t key = 0;         // Key: an AKEYCODE_* --
    bool down = true;        //      ... and whether it is a press or a release
    int32_t action = 0;      // Action: the IME_ACTION_* that was pressed
};

// ------------------------------------------------------------------
// The window's holes, as the platform reports them
// ------------------------------------------------------------------
// A bar is a rectangle the system keeps for itself and paints over the app's
// surface, and Android measures it the way it measures everything on the screen:
// device pixels. They stay device pixels across this boundary because that is
// the unit they were born in, and the division into logical units happens with
// the kernel's own scale, on the thread that draws -- see
// AndroidHost::applyWindowInsets().
//
// What is in them is decided by the platform: a status bar at the top, a
// navigation bar at the bottom or on the sides of a rotated phone, a cutout
// around the camera, and the keyboard as the bottom one while it is up.
struct WindowInsets {
    int32_t left = 0, top = 0, right = 0, bottom = 0;

    bool operator==(const WindowInsets& o) const {
        return left == o.left && top == o.top && right == o.right &&
               bottom == o.bottom;
    }
};

// ------------------------------------------------------------------
// AndroidInputBridge: what a motion event means, without a phone
// ------------------------------------------------------------------
// The NDK hands input over as opaque structs whose fields you can only read on
// the device, which would leave the interesting part of the bridge -- which
// finger the widget tree gets to see, what happens to a drag when a second
// finger joins, what a cancellation is *not* -- untestable. So that part lives
// here, over plain data, and the Android-only code in AndroidHost does nothing
// but copy AMotionEvent fields into it.
//
// The bridge keeps the list of fingers currently down and writes it into
// InputState::touches[] every event; Kernel::applyTouchStream() is what folds
// that list into the single pointer widgets speak, so the rules below are only
// about who is on the screen and when the press ends.
enum class AndroidAction : int32_t {
    Down,        // ACTION_DOWN:         the first finger of a sequence
    PointerDown, // ACTION_POINTER_DOWN: another finger joined an existing one
    Move,        // ACTION_MOVE:         a finger moved -- any finger, the
                 // list carries them all
    Up,          // ACTION_UP:           the last finger left
    PointerUp,   // ACTION_POINTER_UP:   one finger left, others stayed
    Cancel,      // ACTION_CANCEL:       the system took the pointers back
};

// One pointer as the event reports it. The position is in the window's device
// pixels -- the same unit InputState::mouseX/mouseY are in, so the Kernel's
// division by Screen::scale applies to both.
struct AndroidPointer {
    int32_t id = 0;
    float x = 0.0f, y = 0.0f;
};

class AndroidInputBridge {
public:
    // Fold one motion event into `io`. Android sends every pointer in every
    // event, so the list is rebuilt from `ptrs` (the finger the action concerns
    // is the one at `pointerIndex`) instead of patched: an ACTION_POINTER_UP
    // would otherwise drop the neighbour, whose index shifts as it is removed.
    //
    // The lifted finger is dropped from the list, which is how the Kernel learns
    // the press ended. Its final position is the one the Kernel already holds
    // from the last frame it saw -- ACTION_UP carries the same coordinates as
    // the MOVE before it in every real stream, and a release two pixels along
    // the same widget behaves identically.
    void onMotion(InputState& io, AndroidAction action, int pointerIndex,
                  const AndroidPointer* ptrs, int count);

    // A hardware key -- a phone's rocker, a stylus button, a real keyboard, and
    // the events a simple soft keyboard sends in place of text. `androidKeyCode`
    // is an AKEYCODE_* and is translated here, because the tree knows the
    // library's own KeyCode enum and not Android's.
    //
    // A key that has a character also queues it: the NDK reports a key event's
    // Unicode character to nobody but the Java layer, so the bridge derives it
    // from the key code and its own shift state -- a US layout's legends, which
    // is what a keyboard with a different one would need the IME for.
    void onKey(InputState& io, int32_t androidKeyCode, bool down, bool repeat);

    // One code point from the typeahead into `io.lastChar`, the way the desktop
    // host feeds text: a frame carries at most one character, the rest waits.
    // Composed text from an input connection does not come this way -- it is an
    // edit, with a selection and a cursor of its own. Returns the character
    // delivered (0 when the queue was empty).
    uint32_t deliverChar(InputState& io);

    // ---- the input connection's side of the bridge ------------------------
    // Every one of these is called from whichever thread the platform put it
    // on, so they share a mutex and nothing else. The app publishes the field
    // the tree has focused; the IME reads that snapshot and queues edits; the
    // app drains the queue into the field on its own thread.
    //

    // Publish `focused` as the field the IME is talking to. An editor that is
    // not a TextInput -- or none at all -- publishes an empty one, which is what
    // tells the Java side there is nothing to compose into. The composing span
    // survives a publish (the text it covers is part of what is published) and
    // dies with the field it belonged to.
    void syncEditor(const Widget* focused);
    // What the IME is allowed to know, in the units the widget keeps.
    ImeEditor editor() const;

    // Queue one edit, from the UI thread.
    void queueEdit(ImeEdit edit);
    // Apply every queued edit to the field they were asked for, in the order the
    // IME sent them and each against the text the one before it left behind --
    // which is why the drain happens on the app's thread and not in the callback.
    // `io` is for the two edits that are not text: an action sets Key_Enter down
    // for exactly the frame that follows, the way a hardware Enter would, so the
    // field's own submit event fires and nothing has to be re-implemented; a key
    // event a simple keyboard sent instead of editing goes in through onKey().
    void applyEdits(Widget* focused, InputState& io);

    // `InputState::touchCancelled` and the mouse-wheel fields are edges, not
    // levels: the Kernel reads the cancel on the frame the fingers vanish, and
    // re-acting to an old one on the next frame would kill a drag that started
    // in between. A latched Enter is the same kind of thing -- one frame of it,
    // then it is a release. The host calls this after every Kernel::frame().
    void endFrame(InputState& io);

    // ---- the window's safe area ------------------------------------------
    // The platform reports its bars on the UI thread, through the Java view that
    // owns the window (see android/src/dev/ogui/android/OguiEditorView.java),
    // and the tree may only be touched by the thread that draws, so the value is
    // published here and drained there.
    //
    // It is a latest value and not a queue: unlike an edit, an inset is not a
    // thing that happened to the window but what the window is right now, and
    // draining two of them would apply a bar that had already moved again.
    void setWindowInsets(const WindowInsets& in);

    // The frame thread's side: true and `out` filled when the window's bars are
    // not what the last drain reported. An inset that did not move must not
    // dirty the tree, because a keyboard that stays on screen for a minute is
    // sixty thousand frames of nothing otherwise.
    bool takeWindowInsets(WindowInsets& out);

    // The window went away (backgrounded, rotated): whoever held the press does
    // not hold it any more, the queue belongs to a field that may be gone, and
    // whatever the IME had composed dies with the connection -- so the snapshot
    // it was composed against goes too, and an IME that asks after this is told
    // there is no field at all.
    void reset();

    // Fingers the bridge still believes are down, for tests and for an app that
    // wants to know whether a gesture is in flight.
    int fingerCount() const { return static_cast<int>(m_fingers.size()); }

  private:
    void writeTouches(InputState& io) const;
    void dropFingers(InputState& io, bool cancelled);
    void applyEdit(TextInput* field, const ImeEdit& edit, InputState& io);
    void setComposing(bool on, size_t start, size_t end);
    // The queue, emptied. Private because there is no second reader: an app that
    // drained it itself would leave the snapshot behind, and the two are one
    // answer to one question.
    std::vector<ImeEdit> takeEdits();

    std::vector<AndroidPointer> m_fingers;
    std::vector<uint32_t> m_typeahead;
    // Only caps lock is state a keyboard keeps between events; shift is read
    // from the InputState the press itself set.
    bool m_caps = false;

    // The input connection's half, guarded because it is written by the UI
    // thread and read by the one that draws. Nothing here is ever dereferenced
    // as a widget: `field` is only an identity, so the lock never has to cover
    // the tree itself.
    mutable std::mutex m_imeMutex;
    ImeEditor m_editor;
    std::vector<ImeEdit> m_edits;
    // An editor action arrives between frames; Key_Enter is held down for the
    // one frame the tree sees it and released by the endFrame() after it.
    bool m_enterLatched = false;

    // The keys pressed since the frame began, and the releases that arrived in
    // that same frame as their press and so wait for it to end. Read and written
    // on the thread that draws, so neither is under the input connection's lock.
    // Indexed by the library's own KeyCode.
    std::bitset<256> m_pressedThisFrame;
    std::bitset<256> m_releaseAtFrameEnd;

    // The window's bars, published by the UI thread and drained by the one that
    // draws. Its own lock rather than the input connection's: the two are moved
    // by the same pair of threads, but an inset has no business being hidden
    // behind a text edit's critical section, and `m_insetsReported` is written by
    // the draining thread alone.
    mutable std::mutex m_insetsMutex;
    WindowInsets m_windowInsets;
    WindowInsets m_insetsReported;
    // Two flags because "nothing has been said" and "nothing has changed" are
    // different answers: the first means the tree keeps the whole window, the
    // second means it keeps what it was already told.
    bool m_windowInsetsKnown = false;
    bool m_insetsReportedKnown = false;
};

// AKEYCODE_* -> the library's KeyCode. Anything the library has no equivalent
// for comes back as Key_None (0) rather than a guess, so an unknown key cannot
// look like a known one. Out-of-range returns are what the Kernel's keyDown[]
// array is sized for.
int keyFromAndroidKey(int32_t androidKeyCode);

// ------------------------------------------------------------------
// AndroidHost: ANativeWindow + EGL + AAsset, wired to a Kernel
// ------------------------------------------------------------------
// The Android counterpart of the desktop Host: it owns an EGL context and the
// window surface drawn with it, resolves GL through GlBackend's own loader,
// installs the AAssetManager as ogui's asset reader and turns Android input
// events into an InputState. The activity itself -- its callbacks, its looper,
// its lifecycle -- belongs to the app (see examples/android_main.cpp), because
// NativeActivity wants those on the app's thread.
//
// Off Android every entry point fails cleanly with a message in lastError(),
// so a single code base compiles for both targets.
class AndroidHost {
public:
    AndroidHost() = default;
    AndroidHost(const AndroidHost&) = delete;
    AndroidHost& operator=(const AndroidHost&) = delete;
    ~AndroidHost();

    // EGL display. No context yet: an Android config has to agree with the
    // window's pixel format, and a NativeActivity is created before it has a
    // window, so the config, the context and the pipeline are settled with the
    // first setWindow(). The context then outlives the surface -- an app loses
    // its window when it is backgrounded and gets another one back on resume,
    // and rebuilding the font atlases on the GPU for every resume would both
    // stall the resume and leak the old ones.
    bool create(ANativeActivity* activity);
    void destroy();

    // The window. setWindow() attaches (or re-attaches after the window itself
    // went away, which is what a pause and resume does) and makes the surface
    // current. A resize that the activity survives is not this road at all: see
    // refreshWindowSize().
    // releaseWindow() drops the surface but keeps the context, and is what
    // APP_CMD_TERM_WINDOW must call before the window is destroyed.
    bool setWindow(ANativeWindow* window);
    void releaseWindow();
    bool hasWindow() const;

    // The other half of a resize: a window whose *size* changed without the
    // activity going away. An app that survives orientation changes through
    // configChanges -- as this host's own manifest does -- is told about it as
    // APP_CMD_WINDOW_RESIZED on the same ANativeWindow and the same EGLSurface:
    // no TERM_WINDOW, no INIT_WINDOW, nothing that would come back through
    // setWindow() and re-read the geometry. Without this call the surface grows
    // or shrinks under a width() and height() that stayed where they were, the
    // viewport is then the old rectangle on the new buffer, and a portrait
    // screen rotated to landscape draws its whole layout above the top of the
    // frame: black, at a steady sixty frames a second. Returns false when there
    // is no surface to ask; the size it read is what width() and height() say
    // afterwards, so the app re-fits its screen from them.
    bool refreshWindowSize();

    // Device pixels of the current surface, 0 while there is none.
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Seconds since create(), for InputState::time (the Kernel's animation and
    // tooltip clock, and the flick's sample window).
    float elapsed() const;

    // Device pixels per logical unit: the configuration's density divided by
    // mdpi, which is the unit a layout is authored in -- 450 dpi is 2.8125, and
    // the density the header has no name for is still a density. Assign it to
    // Screen::scale so a layout keeps its physical size from a 5" phone to a 12"
    // tablet. A report that cannot be a dpi (0, the ANY/NONE sentinels) is 1.0.
    float dpiScale() const;

    // Points ogui::setAssetReader at the APK: a layout, a theme, a script, a
    // font face or a png shipped inside assets/ is then found by its bare
    // name, and a name the archive does not have is declined (nullopt) so the
    // filesystem still gets its turn. Call once, before parsing anything.
    bool installAssetReader();

    // Translate one Android input event into the InputState the Kernel is fed.
    // Returns false for the event types this host does not speak (a trackball,
    // a joystick) so the app can ack what it consumed and ignore the rest.
    bool onInputEvent(AInputEvent* event, InputState& io);

    // The bridge that holds the rules; the app owns the InputState it fills.
    AndroidInputBridge& input() { return m_input; }

    // Clear, draw the Kernel's current list, present. Returns false when the
    // surface or the context is gone -- the app then releases and re-creates
    // the window rather than spinning on a dead swap.
    bool present(Kernel& kernel);

    // ---- the soft keyboard -------------------------------------------------
    // A composing keyboard commits through an input connection, and that object
    // is Java: no NDK header can make one. So the app's Java side (see
    // android/src/dev/ogui/android) owns the connection, hands the bridge the
    // edits the IME asks for, and is told which field has the focus -- in three
    // calls around the frame:
    //
    //   host.applyImeEdits(kernel, input);   // what the IME queued, to the tree
    //   ... Kernel::frame(input) ...
    //   host.syncIme(kernel);                // what the tree says, to the IME
    //
    // An app that adds nothing else therefore types: a tap that focuses a field
    // brings up the keyboard that field wants, the words arrive as commits and
    // backspaces, and the return key fires the field's own submit event.
    void applyImeEdits(Kernel& kernel, InputState& io);
    void syncIme(Kernel& kernel);

    // The window's safe area, from the thread that owns the window to the tree
    // that has to live inside it. Device pixels become logical units by the
    // kernel's own scale, because that is the unit the screen is authored in,
    // and a bar that did not move leaves the kernel alone. Call it with the
    // frame, like the two above.
    void applyWindowInsets(Kernel& kernel);

    // Put the keyboard away whatever has the focus -- what a paused activity
    // owes the user, since the field it is leaving may be behind another app.
    void hideKeyboard();

    // The activity's Java bridge, when the app has one: without it the keyboard
    // request falls back to ANativeActivity_showSoftInput(), which shows a
    // keyboard with nothing to commit through, and the only text that reaches the
    // tree is the key events a hardware keyboard sends. False when no Java
    // library answered the registration.
    bool hasImeBridge() const;

    void setClearColor(float r, float g, float b);
    const std::string& lastError() const { return m_error; }

    ANativeActivity* activity() const { return m_activity; }

  private:
    bool makeCurrent();
    void sendImeRequest(const ImeRequest& request);

    std::string m_error;
    ANativeActivity* m_activity = nullptr;
    // EGLDisplay, EGLConfig, EGLContext, EGLSurface, stored as opaque pointers
    // so this header stays includable where EGL is not.
    void* m_display = nullptr;
    void* m_config = nullptr;
    void* m_context = nullptr;
    void* m_surface = nullptr;
    ANativeWindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    double m_startSeconds = 0.0;
    float m_clearR = 0.06f, m_clearG = 0.06f, m_clearB = 0.07f;
    bool m_glReady = false;
    GlBackend* m_gl = nullptr;
    AndroidInputBridge m_input;
    // The last request the Java side was given. The keyboard does not need to be
    // told the same thing sixty times a second, and a restart it does not need
    // closes the composition it is in the middle of.
    ImeRequest m_imeSent;
    bool m_imeSentKnown = false;
};

} // namespace ogui
