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

Matrix4x4 Matrix4x4::perspective(float fovYDegrees, float aspect,
                                 float nearZ, float farZ) {
    Matrix4x4 m;
    const float f = 1.0f / std::tan(fovYDegrees * 0.5f * 3.14159265358979323846f / 180.0f);
    const float a = aspect > 0.0f ? aspect : 1.0f;
    const float rdepth = 1.0f / (nearZ - farZ);
    // Column-major, OpenGL clip rules: w_clip = -z_view.
    m.m[0]  = f / a;
    m.m[5]  = f;
    m.m[10] = (farZ + nearZ) * rdepth;
    m.m[11] = -1.0f;
    m.m[14] = 2.0f * farZ * nearZ * rdepth;
    m.m[15] = 0.0f;
    return m;
}

Matrix4x4 Matrix4x4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    // The camera looks down -z, so z points back towards the eye.
    Vec3 z = normalize(eye - target);
    Vec3 x = normalize(cross(up, z));
    if (dot(x, x) == 0.0f) {
        // `up` is parallel to the view direction: any perpendicular will do,
        // and a degenerate basis is worse than a surprising roll.
        const Vec3 fallback = std::fabs(z.y) > 0.9f ? Vec3{0.0f, 0.0f, 1.0f}
                                                   : Vec3{0.0f, 1.0f, 0.0f};
        x = normalize(cross(fallback, z));
    }
    const Vec3 y = cross(z, x);

    Matrix4x4 m;
    m.m[0] = x.x; m.m[4] = x.y; m.m[8]  = x.z; m.m[12] = -dot(x, eye);
    m.m[1] = y.x; m.m[5] = y.y; m.m[9]  = y.z; m.m[13] = -dot(y, eye);
    m.m[2] = z.x; m.m[6] = z.y; m.m[10] = z.z; m.m[14] = -dot(z, eye);
    m.m[3] = 0.0f; m.m[7] = 0.0f; m.m[11] = 0.0f; m.m[15] = 1.0f;
    return m;
}

Matrix4x4 Matrix4x4::translation(const Vec3& t) {
    Matrix4x4 m;
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
    return m;
}

Matrix4x4 Matrix4x4::scaling(const Vec3& s) {
    Matrix4x4 m;
    m.m[0] = s.x;
    m.m[5] = s.y;
    m.m[10] = s.z;
    return m;
}

Matrix4x4 Matrix4x4::rotationX(float radians) {
    Matrix4x4 m;
    const float c = std::cos(radians), s = std::sin(radians);
    m.m[5] = c;  m.m[6] = s;
    m.m[9] = -s; m.m[10] = c;
    return m;
}

Matrix4x4 Matrix4x4::rotationY(float radians) {
    Matrix4x4 m;
    const float c = std::cos(radians), s = std::sin(radians);
    m.m[0] = c;  m.m[2] = -s;
    m.m[8] = s;  m.m[10] = c;
    return m;
}

Matrix4x4 Matrix4x4::rotationZ(float radians) {
    Matrix4x4 m;
    const float c = std::cos(radians), s = std::sin(radians);
    m.m[0] = c; m.m[1] = s;
    m.m[4] = -s; m.m[5] = c;
    return m;
}

Matrix4x4 Matrix4x4::operator*(const Matrix4x4& rhs) const {
    Matrix4x4 out;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[k * 4 + row] * rhs.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

Vec3 Matrix4x4::transformPoint(const Vec3& p) const {
    const float x = m[0] * p.x + m[4] * p.y + m[8]  * p.z + m[12];
    const float y = m[1] * p.x + m[5] * p.y + m[9]  * p.z + m[13];
    const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w == 0.0f) return {x, y, z};
    const float inv = 1.0f / w;
    return {x * inv, y * inv, z * inv};
}

Vec3 normalize(const Vec3& v) {
    const float len2 = dot(v, v);
    if (len2 <= 0.0f) return {};
    const float inv = 1.0f / std::sqrt(len2);
    return {v.x * inv, v.y * inv, v.z * inv};
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
