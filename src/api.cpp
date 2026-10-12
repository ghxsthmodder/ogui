#include "ogui/api.hpp"
#include <cstdlib>
#include <cmath>

namespace ogui {

// ---- matrix ---------------------------------------------------------------
void Matrix4x4::setIdentity() {
    m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f; m[3] = 0.0f;
    m[4] = 0.0f; m[5] = 1.0f; m[6] = 0.0f; m[7] = 0.0f;
    m[8] = 0.0f; m[9] = 0.0f; m[10] = 1.0f; m[11] = 0.0f;
    m[12] = 0.0f; m[13] = 0.0f; m[14] = 0.0f; m[15] = 1.0f;
}

Matrix4x4 Matrix4x4::ortho(float left, float right, float bottom, float top,
                           float nearVal, float farVal) {
    Matrix4x4 m;
    float rwidth = 1.0f / (right - left);
    float rheight = 1.0f / (top - bottom);
    float rdepth = 1.0f / (farVal - nearVal);
    m.m[0] = 2.0f * rwidth;
    m.m[1] = 0.0f;
    m.m[2] = 0.0f;
    m.m[3] = 0.0f;
    m.m[4] = 0.0f;
    m.m[5] = 2.0f * rheight;
    m.m[6] = 0.0f;
    m.m[7] = 0.0f;
    m.m[8] = 0.0f;
    m.m[9] = 0.0f;
    m.m[10] = -2.0f * rdepth;
    m.m[11] = 0.0f;
    m.m[12] = -(right + left) * rwidth;
    m.m[13] = -(top + bottom) * rheight;
    m.m[14] = -(farVal + nearVal) * rdepth;
    m.m[15] = 1.0f;
    return m;
}

// ---- vertex ---------------------------------------------------------------
Vertex Vertex::make(float x, float y, float u, float v, const Color& col) {
    Vertex out;
    out.x = x; out.y = y;
    out.u = u; out.v = v;
    out.r = col.r / 255.0f;
    out.g = col.g / 255.0f;
    out.b = col.b / 255.0f;
    out.a = col.a / 255.0f;
    return out;
}

// ---- colors ---------------------------------------------------------------
namespace {

int hexNibble(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

} // namespace

// "#rgb", "#rgba", "#rrggbb" or "#rrggbbaa" (the leading '#' is optional).
Color hexColorHex(const char* hex) {
    Color c;
    c.r = c.g = c.b = 0;
    c.a = 255;
    if (!hex || !*hex) return c;

    const char* p = hex;
    if (*p == '#') ++p;

    int n = 0;
    while (hexNibble(p[n]) >= 0) ++n;

    // Short form: one nibble per channel, doubled (f -> ff).
    if (n == 3 || n == 4) {
        c.r = static_cast<uint8_t>(hexNibble(p[0]) * 17);
        c.g = static_cast<uint8_t>(hexNibble(p[1]) * 17);
        c.b = static_cast<uint8_t>(hexNibble(p[2]) * 17);
        if (n == 4) c.a = static_cast<uint8_t>(hexNibble(p[3]) * 17);
        return c;
    }

    // Long form: exactly two nibbles per channel.
    if (n == 6 || n == 8) {
        auto pair = [&](int i) { return hexNibble(p[i]) * 16 + hexNibble(p[i + 1]); };
        c.r = static_cast<uint8_t>(pair(0));
        c.g = static_cast<uint8_t>(pair(2));
        c.b = static_cast<uint8_t>(pair(4));
        if (n == 8) c.a = static_cast<uint8_t>(pair(6));
        return c;
    }

    return c;
}

} // namespace ogui
