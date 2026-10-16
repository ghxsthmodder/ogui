#include "ogui/androidhost.hpp"
#include "ogui/font.hpp"
#include "ogui/widgets.hpp"

#include <algorithm>
#include <cstddef>

namespace ogui {

namespace {

bool printableChar(uint32_t c) {
    return c >= 0x20 && c != 0x7F;
}

// android.text.InputType and android.view.inputmethod.EditorInfo as the numbers
// those classes define (the same ones javap -constants reports for API 36),
// named because the app hands the framework exactly these bits and a reader of
// this file has to be able to tell which ones.
constexpr int32_t kTypeClassText = 1;          // TYPE_CLASS_TEXT
constexpr int32_t kTextMultiLine = 1 << 17;    // TYPE_TEXT_FLAG_MULTI_LINE
constexpr int32_t kActionSearch = 3;           // IME_ACTION_SEARCH
constexpr int32_t kActionDone = 6;             // IME_ACTION_DONE
constexpr int32_t kNoEnterAction = 1 << 30;    // IME_FLAG_NO_ENTER_ACTION

// How many bytes a code point announces at *p. A continuation byte, or a lead
// byte of a shape that is not used, claims one: the same reading utf8Next gives
// a stray, and the reason an index can be computed without decoding.
size_t leadWidth(unsigned char c) {
    if ((c & 0x80u) == 0u) return 1;
    if ((c & 0xE0u) == 0xC0u) return 2;
    if ((c & 0xF0u) == 0xE0u) return 3;
    if ((c & 0xF8u) == 0xF0u) return 4;
    return 1;
}

// The bytes one code point occupies at `p`, bounded by `end`: a lead byte whose
// continuation bytes are missing or misshapen counts as one, so a broken field
// is walked a byte at a time instead of read past the end of the string.
// utf8Next has the same rule and no bound, because it is handed text it decoded
// already; this walks a caret's coordinates in whatever the widget holds.
size_t codepointWidth(const unsigned char* p, const unsigned char* end) {
    const size_t w = leadWidth(*p);
    if (w == 1) return 1;
    if (static_cast<size_t>(end - p) < w) return 1;
    for (size_t i = 1; i < w; ++i)
        if ((p[i] & 0xC0u) != 0x80u) return 1;
    return w;
}

// Code units an IME counts a code point of `bytes` as: four bytes is an astral
// one, which is a surrogate pair -- two units for one character.
int32_t unitsOfWidth(size_t bytes) { return bytes == 4 ? 2 : 1; }

// The unshifted character of a key, 0 for a key that types nothing. Enter,
// backspace and the arrows are actions the widgets already run off KeyCode, so
// giving them a character too would double every press.
uint32_t baseChar(int32_t code) {
    if (code >= 29 && code <= 54) return 'a' + (code - 29);      // A..Z
    if (code >= 7 && code <= 16) return '0' + (code - 7);        // 0..9
    if (code >= 144 && code <= 153) return '0' + (code - 144);   // numpad
    switch (code) {
    case 62:  return ' ';                                         // SPACE
    case 55:  return ',';
    case 56:  return '.';
    case 68:  return '`';
    case 69:  return '-';
    case 70:  return '=';
    case 71:  return '[';
    case 72:  return ']';
    case 73:  return '\\';
    case 74:  return ';';
    case 75:  return '\'';
    case 76:  return '/';
    case 158: return '.';                                         // NUMPAD_DOT
    case 154: return '/';
    case 155: return '*';
    case 156: return '-';
    case 157: return '+';
    case 161: return '=';
    default:  return 0;
    }
}

// The same key under shift. A US layout's legends only: on any other one the
// shifted row is the IME's business, and a key event carries no text to ask it.
uint32_t shiftedChar(int32_t code) {
    switch (code) {
    case 7:   return '!';
    case 8:   return '@';
    case 9:   return '#';
    case 10:  return '$';
    case 11:  return '%';
    case 12:  return '^';
    case 13:  return '&';
    case 14:  return '*';
    case 15:  return '(';
    case 16:  return ')';
    case 55:  return '<';
    case 56:  return '>';
    case 68:  return '~';
    case 69:  return '_';
    case 70:  return '+';
    case 71:  return '{';
    case 72:  return '}';
    case 73:  return '|';
    case 74:  return ':';
    case 75:  return '"';
    case 76:  return '?';
    default:  return 0;
    }
}

} // namespace

void AndroidInputBridge::writeTouches(InputState& io) const {
    const int n = std::min<int>(static_cast<int>(m_fingers.size()),
                                InputState::kMaxTouchPoints);
    for (int i = 0; i < n; ++i)
        io.touches[i] = {m_fingers[i].id, m_fingers[i].x, m_fingers[i].y};
    io.touchCount = n;
}

void AndroidInputBridge::dropFingers(InputState& io, bool cancelled) {
    m_fingers.clear();
    io.touchCount = 0;
    io.touchCancelled = cancelled;
}

void AndroidInputBridge::onMotion(InputState& io, AndroidAction action,
                                  int pointerIndex, const AndroidPointer* ptrs,
                                  int count) {
    if (action == AndroidAction::Cancel) {
        // Not a release. The Kernel ends the drag and clicks nothing, which is
        // what makes an incoming call, or the system back swipe, unable to fire
        // the button the finger happened to be over.
        dropFingers(io, true);
        return;
    }
    io.touchCancelled = false;

    if (count <= 0 || !ptrs) {
        // The events that may report no pointers at all; say so rather than
        // leave a finger on the screen forever.
        dropFingers(io, false);
        return;
    }

    if (action == AndroidAction::Up || action == AndroidAction::PointerUp) {
        // Rebuild from the event minus the finger that left. The arriving list
        // is the truth about who is still down, and dropping by id -- not by
        // index -- is what keeps the neighbours, whose indices shift as the
        // leaving pointer is removed from Android's own array.
        const int idx = std::clamp(pointerIndex, 0, count - 1);
        const int32_t gone = ptrs[idx].id;
        m_fingers.clear();
        for (int i = 0; i < count; ++i)
            if (ptrs[i].id != gone) m_fingers.push_back(ptrs[i]);
        writeTouches(io);
        return;
    }

    // DOWN, POINTER_DOWN, MOVE, POINTER_MOVE: Android always reports every
    // pointer, in the order it holds them, which is the arrival order the
    // Kernel inherits a drag from.
    m_fingers.assign(ptrs, ptrs + std::min(count, InputState::kMaxTouchPoints));
    writeTouches(io);
}

void AndroidInputBridge::onKey(InputState& io, int32_t androidKeyCode, bool down,
                               bool repeat) {
    const int kc = keyFromAndroidKey(androidKeyCode);
    if (kc > 0 && kc < static_cast<int>(std::size(io.keyDown))) {
        io.keyDown[kc] = down;
        io.keyRepeat[kc] = down && repeat;
    }
    if (!down) return;
    if (kc == Key_CapsLock) {
        if (!repeat) m_caps = !m_caps;
        return;
    }

    // A key that also types queues its character instead of writing it straight
    // into lastChar, so it keeps its order against what the IME committed a
    // moment before. An auto-repeat presses one too: holding a letter down on a
    // hardware keyboard has to fill the field the way it fills the character.
    const bool shift = io.keyDown[Key_LSHIFT] || io.keyDown[Key_RSHIFT];
    uint32_t ch = shift ? shiftedChar(androidKeyCode) : 0u;
    if (!ch) ch = baseChar(androidKeyCode);
    const bool upper = (shift != m_caps);
    if (ch >= 'a' && ch <= 'z' && upper) ch -= 'a' - 'A';
    else if (ch >= 'A' && ch <= 'Z' && !upper) ch += 'a' - 'A';
    if (printableChar(ch)) m_typeahead.push_back(ch);
}

void AndroidInputBridge::commitText(const std::string& utf8) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.data());
    const unsigned char* end = p + utf8.size();
    while (p < end) {
        const uint32_t cp = utf8Next(p);
        if (printableChar(cp)) m_typeahead.push_back(cp);
    }
}

uint32_t AndroidInputBridge::deliverChar(InputState& io) {
    io.lastChar = m_typeahead.empty() ? 0u : m_typeahead.front();
    if (!m_typeahead.empty()) m_typeahead.erase(m_typeahead.begin());
    return io.lastChar;
}

void AndroidInputBridge::endFrame(InputState& io) {
    io.touchCancelled = false;
    io.lastChar = 0;
}

void AndroidInputBridge::reset() {
    m_fingers.clear();
    m_typeahead.clear();
    m_caps = false;
}

int keyFromAndroidKey(int32_t code) {
    // android/keycodes.h. Spelled out as numbers rather than included: a test
    // on a Linux machine has no Android headers, and these values are a
    // published, frozen ABI. Anything with no equivalent in the library's own
    // enum -- the media keys, a gamepad's faces, a TV remote's channels --
    // comes back Key_None instead of standing in for a key it is not.
    switch (code) {
    case 3:   return Key_None;         // HOME leaves the app; not the UI's call
    case 4:   return Key_Escape;       // BACK closes what is open
    case 23:  return Key_Enter;        // DPAD_CENTER activates the focus
    case 61:  return Key_Tab;
    case 62:  return Key_Space;
    case 66:  return Key_Enter;
    case 67:  return Key_Backspace;    // DEL deletes to the left, as it does here
    case 111: return Key_Escape;
    case 112: return Key_Delete;
    case 122: return Key_Home;         // MOVE_HOME
    case 123: return Key_End;          // MOVE_END
    case 124: return Key_Insert;
    case 19:  return Key_Up;
    case 20:  return Key_Down;
    case 21:  return Key_Left;
    case 22:  return Key_Right;
    case 92:  return Key_PageUp;
    case 93:  return Key_PageDown;
    case 57:  return Key_LALT;
    case 58:  return Key_RALT;
    case 59:  return Key_LSHIFT;
    case 60:  return Key_RSHIFT;
    case 113: return Key_LCTRL;
    case 114: return Key_RCTRL;
    case 117: return Key_LGUI;         // META_LEFT
    case 118: return Key_RGUI;
    case 115: return Key_CapsLock;
    case 116: return Key_ScrollLock;
    case 143: return Key_NumLock;
    case 160: return Key_KP_Enter;
    case 154: return Key_KP_Divide;
    case 155: return Key_KP_Multiply;
    case 156: return Key_KP_Subtract;
    case 157: return Key_KP_Add;
    case 158: return Key_KP_Decimal;
    case 161: return Key_KP_Equal;
    case 55:  return Key_Comma;
    case 56:  return Key_Period;
    case 69:  return Key_Minus;
    case 70:  return Key_Equal;
    case 71:  return Key_BracketLeft;
    case 72:  return Key_BracketRight;
    case 73:  return Key_Backslash;
    case 74:  return Key_Semicolon;
    case 75:  return Key_Apostrophe;
    case 76:  return Key_Slash;
    default: break;
    }
    if (code >= 29 && code <= 54) return Key_A + (code - 29);        // A..Z
    if (code >= 7 && code <= 16) return Key_0 + (code - 7);          // 0..9
    if (code >= 131 && code <= 142) return Key_F1 + (code - 131);    // F1..F12
    if (code >= 144 && code <= 153) return Key_KP_0 + (code - 144);  // numpad
    return Key_None;
}

ImeRequest imeRequestFor(const Widget* focused) {
    ImeRequest r;
    // The order is the answer, not an accident: a TextArea and a SearchInput are
    // TextInputs, so asking the most specific class first is what makes the
    // keyboard match the behaviour the field actually has.
    if (!dynamic_cast<const TextInput*>(focused)) return r;
    r.visible = true;
    r.inputType = kTypeClassText;
    if (dynamic_cast<const TextArea*>(focused)) {
        // Enter writes a newline here, so the return key must not double as a
        // submit button -- which is what the flag, rather than an action, says.
        r.inputType |= kTextMultiLine;
        r.imeOptions = kNoEnterAction;
    } else if (dynamic_cast<const SearchInput*>(focused)) {
        r.imeOptions = kActionSearch;
    } else {
        r.imeOptions = kActionDone;
    }
    return r;
}

size_t imeAdvanceUnits(const std::string& utf8, size_t from, int32_t units) {
    const unsigned char* d =
        reinterpret_cast<const unsigned char*>(utf8.data());
    const unsigned char* end = d + utf8.size();
    if (from > utf8.size()) from = utf8.size();
    const unsigned char* p = d + from;
    for (int32_t left = units; left > 0 && p < end; ) {
        const size_t w = codepointWidth(p, end);
        // An astral code point is two units, and a caller that asked for one is
        // not being asked to land in the middle of a pair: stop where it is.
        const int32_t u = unitsOfWidth(w);
        if (u > left) break;
        p += w;
        left -= u;
    }
    return static_cast<size_t>(p - d);
}

size_t imeRewindUnits(const std::string& utf8, size_t from, int32_t units) {
    const unsigned char* d =
        reinterpret_cast<const unsigned char*>(utf8.data());
    if (from > utf8.size()) from = utf8.size();
    size_t pos = from;
    for (int32_t left = units; left > 0 && pos > 0; ) {
        // Continuation bytes are the only ones shaped 10xxxxxx, so stepping back
        // over them lands on the lead byte of the code point that ends at `pos`.
        size_t start = pos;
        do { --start; } while (start > 0 && (d[start] & 0xC0u) == 0x80u);
        const size_t w = codepointWidth(d + start, d + pos);
        if (w != pos - start) {
            // Not a whole code point: a stray byte before the caret, one unit.
            pos -= 1;
            left -= 1;
            continue;
        }
        const int32_t u = unitsOfWidth(w);
        if (u > left) break;
        pos = start;
        left -= u;
    }
    return pos;
}

int32_t imeCountUnits(const std::string& utf8, size_t from, size_t to) {
    const unsigned char* d =
        reinterpret_cast<const unsigned char*>(utf8.data());
    const unsigned char* end = d + utf8.size();
    from = std::min(from, utf8.size());
    to = std::min(to, utf8.size());
    int32_t n = 0;
    for (const unsigned char* p = d + from; p < d + to; ) {
        const size_t w = codepointWidth(p, end);
        p += w;
        n += unitsOfWidth(w);
    }
    return n;
}

} // namespace ogui
