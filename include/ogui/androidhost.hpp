#pragma once
#include "ogui/api.hpp"
#include "ogui/kernel.hpp"
#include <cstdint>
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

    // The soft keyboard's commit: a whole string at a time, which is why the
    // characters queue instead of overwriting each other. Nothing in the NDK
    // can call this -- an input connection is a Java object -- but a host with
    // one has somewhere to put it.
    void commitText(const std::string& utf8);

    // One queued code point into `io.lastChar`, the way the desktop host feeds
    // text: a frame carries at most one character, the rest waits. Returns the
    // character delivered (0 when the queue was empty).
    uint32_t deliverChar(InputState& io);

    // `InputState::touchCancelled` and the mouse-wheel fields are edges, not
    // levels: the Kernel reads the cancel on the frame the fingers vanish, and
    // re-acting to an old one on the next frame would kill a drag that started
    // in between. The host calls this after every Kernel::frame().
    void endFrame(InputState& io);

    // The window went away (backgrounded, rotated): whoever held the press does
    // not hold it any more, the queue belongs to a field that may be gone, and
    // whatever the IME had composed dies with the connection.
    void reset();

    // Fingers the bridge still believes are down, for tests and for an app that
    // wants to know whether a gesture is in flight.
    int fingerCount() const { return static_cast<int>(m_fingers.size()); }

  private:
    void writeTouches(InputState& io) const;
    void dropFingers(InputState& io, bool cancelled);

    std::vector<AndroidPointer> m_fingers;
    std::vector<uint32_t> m_typeahead;
    // Only caps lock is state a keyboard keeps between events; shift is read
    // from the InputState the press itself set.
    bool m_caps = false;
};

// AKEYCODE_* -> the library's KeyCode. Anything the library has no equivalent
// for comes back as Key_None (0) rather than a guess, so an unknown key cannot
// look like a known one. Out-of-range returns are what the Kernel's keyDown[]
// array is sized for.
int keyFromAndroidKey(int32_t androidKeyCode);

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

    // The window. setWindow() attaches (or re-attaches after a resize; a resize
    // arrives as a whole new ANativeWindow) and makes the surface current.
    // releaseWindow() drops the surface but keeps the context, and is what
    // APP_CMD_TERM_WINDOW must call before the window is destroyed.
    bool setWindow(ANativeWindow* window);
    void releaseWindow();
    bool hasWindow() const;

    // Device pixels of the current surface, 0 while there is none.
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Seconds since create(), for InputState::time (the Kernel's animation and
    // tooltip clock, and the flick's sample window).
    float elapsed() const;

    // Device pixels per logical unit, from the configuration's density bucket:
    // mdpi 1.0, hdpi 1.5, xhdpi 2.0, xxhdpi 3.0, xxxhdpi 4.0. Assign it to
    // Screen::scale so a layout authored in logical units keeps its physical
    // size from a 5" phone to a 12" tablet.
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

    // Ask the IME to show or hide itself for this activity's window. Showing
    // it is only half the story: the NDK cannot create the input connection a
    // composing keyboard commits through (that object is Java), so what reaches
    // the tree from a soft keyboard is the key events the IME sends on the
    // activity's window, which onInputEvent() turns into characters. A host
    // that adds a Java-side connection feeds the composed text to
    // AndroidInputBridge::commitText().
    void showKeyboard();
    void hideKeyboard();

    void setClearColor(float r, float g, float b);
    const std::string& lastError() const { return m_error; }

    ANativeActivity* activity() const { return m_activity; }

  private:
    bool makeCurrent();

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
};

} // namespace ogui
