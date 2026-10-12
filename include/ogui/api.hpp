#pragma once
#include <cstdint>
#include <array>
#include <string>

namespace ogui {

// ---- primitives -----------------------------------------------------------
enum class Align { Left, Center, Right };
enum class VertAlign { Top, Middle, Bottom };

// Mouse buttons
enum MouseButton {
    Mouse_Left   = 0,
    Mouse_Right,
    Mouse_Middle,
    Mouse_Button4,
    Mouse_Button5
};

// Keyboard scan codes
enum KeyCode {
    Key_None = 0,
    Key_Space, Key_Apostrophe, Key_Comma, Key_Minus, Key_Period,
    Key_Slash, Key_0, Key_1, Key_2, Key_3, Key_4,
    Key_5, Key_6, Key_7, Key_8, Key_9,
    Key_Semicolon, Key_Equal, Key_A, Key_B, Key_C, Key_D, Key_E, Key_F,
    Key_G, Key_H, Key_I, Key_J, Key_K, Key_L, Key_M, Key_N,
    Key_O, Key_P, Key_Q, Key_R, Key_S, Key_T, Key_U, Key_V,
    Key_W, Key_X, Key_Y, Key_Z,
    Key_BracketLeft, Key_BracketRight, Key_Quote, Key_Backslash,
    Key_Backspace, Key_Enter, Key_Tab, Key_Escape,
    Key_Home, Key_End, Key_PageUp, Key_PageDown,
    Key_Insert, Key_Delete, Key_Left, Key_Up, Key_Right, Key_Down,
    Key_CapsLock, Key_ScrollLock, Key_NumLock, Key_PrintScreen,
    Key_Pause, Key_F1, Key_F2, Key_F3, Key_F4, Key_F5, Key_F6,
    Key_F7, Key_F8, Key_F9, Key_F10, Key_F11, Key_F12,
    Key_KP_0, Key_KP_1, Key_KP_2, Key_KP_3, Key_KP_4,
    Key_KP_5, Key_KP_6, Key_KP_7, Key_KP_8, Key_KP_9,
    Key_KP_Add, Key_KP_Subtract, Key_KP_Multiply, Key_KP_Divide, Key_KP_Enter, Key_KP_Insert,
    Key_KP_Equal, Key_KP_Decimal,
    Key_LSHIFT, Key_RSHIFT, Key_LCTRL, Key_RCTRL, Key_LALT, Key_RALT, Key_LGUI, Key_RGUI,
};

// Modifier bitmask delivered with key events (see Kernel).
enum ModBits : uint32_t {
    Mod_None  = 0,
    Mod_Shift = 1u << 0,
    Mod_Ctrl  = 1u << 1,
    Mod_Alt   = 1u << 2,
    Mod_Super = 1u << 3,
};

enum class EventType {
    None,
    MouseMove, MouseButton, MouseWheel,
    KeyDown, KeyUp, KeyChar,
    FocusGained, FocusLost,
    Resize,
    Update
};

struct Offset2D { float x = 0.0f, y = 0.0f; };
struct Size2D { float w = 0.0f, h = 0.0f; };
struct Rect { float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f; };
struct Color { uint8_t r = 0, g = 0, b = 0, a = 255; };

// 4x4 column-major matrix (OpenGL style). Default -> identity.
struct Matrix4x4 {
    std::array<float, 16> m{};

    Matrix4x4() { setIdentity(); }
    void setIdentity();
    static Matrix4x4 ortho(float left, float right, float bottom, float top,
                           float nearVal, float farVal);

    float* data() { return m.data(); }
    const float* data() const { return m.data(); }
};

// One vertex for the 2D painter.
struct Vertex {
    float x, y;
    float u, v;
    float r, g, b, a;

    static Vertex make(float x, float y, float u, float v, const Color& col);
};

// ---- colors ---------------------------------------------------------------
Color hexColorHex(const char* hex);

inline Color hexColor(const std::string& hex) {
    return hexColorHex(hex.c_str());
}

inline Color hexColorC(const char* hex) {
    return hexColorHex(hex);
}

// Multiply rgb by `k` (clamped), preserving alpha. k>1 brightens for hover,
// k<1 darkens for pressed states.
inline Color shade(const Color& c, float k) {
    auto mul = [k](uint8_t v) {
        const int x = static_cast<int>(static_cast<float>(v) * k + 0.5f);
        return static_cast<uint8_t>(x > 255 ? 255 : (x < 0 ? 0 : x));
    };
    return Color{mul(c.r), mul(c.g), mul(c.b), c.a};
}

// Copy with a new alpha (hover tints, soft fills).
inline Color withAlpha(const Color& c, uint8_t a) {
    return Color{c.r, c.g, c.b, a};
}

} // namespace ogui
