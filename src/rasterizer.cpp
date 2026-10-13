#include "ogui/rasterizer.hpp"
#include "ogui/font.hpp"
#include "ogui/widgets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace ogui {

namespace {

Rect intersectRect(const Rect& a, const Rect& b) {
    float x0 = std::max(a.x, b.x);
    float y0 = std::max(a.y, b.y);
    float x1 = std::min(a.x + a.w, b.x + b.w);
    float y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return Rect{0, 0, 0, 0};
    return Rect{x0, y0, x1 - x0, y1 - y0};
}

Vertex mk(float x, float y, float u, float v, const Color& c) {
    return Vertex::make(x, y, u, v, c);
}

} // namespace

// ---- Rasterizer -----------------------------------------------------------
void Rasterizer::setTexture(int texId, int width, int height) {
    m_texId = texId;
    m_texW = width;
    m_texH = height;
}

void Rasterizer::beginFrame(const Rect& clip) {
    m_clipStack.push_back(m_clip);
    m_clip = intersectRect(m_clip, clip);
}

void Rasterizer::endFrame() {
    if (!m_clipStack.empty()) {
        m_clip = m_clipStack.back();
        m_clipStack.pop_back();
    }
}

void Rasterizer::reset() {
    m_list.clear();
    m_clipStack.clear();
    m_clip = Rect{0, 0, 4096, 4096};
    m_texId = 0;
}

// Returns the active clip when `r` intersects it, otherwise an empty rect
// (caller skips the primitive). The clip reported to DrawCmd is always the
// *active* clip, not the primitive's bounds, so primitives sharing the same
// scissor merge into a single batch; out-of-bounds pixels are cut by the
// GPU scissor (or by a CPU rasterizer) downstream.
Rect Rasterizer::currentClip(const Rect& r) const {
    if (intersectRect(m_clip, r).w <= 0.0f || intersectRect(m_clip, r).h <= 0.0f) {
        return Rect{0, 0, 0, 0};
    }
    return m_clip;
}

void Rasterizer::addQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                         const Vertex& v3, const Rect& clip, int texId) {
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;
    m_list.addQuad(v0, v1, v2, v3, clip, texId);
}

void Rasterizer::drawRect(const Rect& r, const Color& col) {
    if (r.w <= 0.0f || r.h <= 0.0f || col.a == 0) return;
    Rect clip = currentClip(r);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    addQuad(mk(x0, y0, 0, 0, col), mk(x1, y0, 1, 0, col),
            mk(x1, y1, 1, 1, col), mk(x0, y1, 0, 1, col),
            clip, m_texId);
}

void Rasterizer::drawRoundedRect(const Rect& r, const Color& col, float radius) {
    if (r.w <= 0.0f || r.h <= 0.0f || col.a == 0) return;
    if (radius <= 0.0f) { drawRect(r, col); return; }
    Rect clip = currentClip(r);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;
    fillRoundedRect(r, radius, col, clip);
}

void Rasterizer::drawVerticalGradient(const Rect& r, const Color& top,
                                      const Color& bottom) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    Rect clip = currentClip(r);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    addQuad(mk(x0, y0, 0, 0, top), mk(x1, y0, 1, 0, top),
            mk(x1, y1, 1, 1, bottom), mk(x0, y1, 0, 1, bottom),
            clip, m_texId);
}

void Rasterizer::drawRoundedGradient(const Rect& r, const Color& top,
                                     const Color& bottom, float radius) {
    if (r.w <= 0.0f || r.h <= 0.0f) return;
    if (radius <= 0.0f) { drawVerticalGradient(r, top, bottom); return; }
    Rect clip = currentClip(r);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    const float y0 = r.y, y1 = r.y + r.h;
    auto colAt = [&](float y) {
        float t = (y - y0) / std::max(y1 - y0, 0.0001f);
        return lerpColor(top, bottom, std::clamp(t, 0.0f, 1.0f));
    };

    float x0 = r.x, x1 = r.x + r.w;
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float maxR = std::min(r.w, r.h) * 0.5f;
    float rad = std::min(radius, maxR);

    const int seg = std::max(3, curveSegments);
    auto& fan = m_scratchV;
    auto& idx = m_scratchI;
    fan.clear();
    idx.clear();
    fan.reserve(static_cast<size_t>(seg) * 2 + 8);

    auto emitCorner = [&](float ccx, float ccy, float a0, float a1) {
        size_t start = fan.size();
        for (int i = 0; i <= seg; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(seg);
            float a = a0 + (a1 - a0) * t;
            float px = ccx + std::cos(a) * rad;
            float py = ccy + std::sin(a) * rad;
            fan.push_back(mk(px, py, 0.5f, 0.5f, colAt(py)));
        }
        for (int i = 0; i < seg; ++i) {
            idx.push_back(0);
            idx.push_back(static_cast<uint32_t>(start + i));
            idx.push_back(static_cast<uint32_t>(start + i + 1));
        }
    };

    fan.push_back(mk(cx, cy, 0.5f, 0.5f, colAt(cy)));
    const float PI = 3.14159265358979f;
    emitCorner(x1 - rad, y0 + rad, -PI * 0.5f, 0.0f);
    emitCorner(x1 - rad, y1 - rad, 0.0f, PI * 0.5f);
    emitCorner(x0 + rad, y1 - rad, PI * 0.5f, PI);
    emitCorner(x0 + rad, y0 + rad, PI, PI * 1.5f);

    auto quad = [&](float ax, float ay, float bx, float by,
                    float dx2, float dy2, float ex, float ey) {
        addQuad(mk(ax, ay, 0, 0, colAt(ay)), mk(bx, by, 0, 0, colAt(by)),
                mk(dx2, dy2, 0, 0, colAt(dy2)), mk(ex, ey, 0, 0, colAt(ey)),
                clip, m_texId);
    };
    quad(x0 + rad, y0,     x1 - rad, y0,     x1 - rad, y0 + rad, x0 + rad, y0 + rad);
    quad(x0 + rad, y1 - rad, x1 - rad, y1 - rad, x1 - rad, y1,   x0 + rad, y1);
    quad(x0, y0 + rad,  x0 + rad, y0 + rad,  x0 + rad, y1 - rad, x0, y1 - rad);
    quad(x1 - rad, y0 + rad, x1, y0 + rad,   x1, y1 - rad,       x1 - rad, y1 - rad);
    quad(x0 + rad, y0 + rad, x1 - rad, y0 + rad, x1 - rad, y1 - rad, x0 + rad, y1 - rad);

    m_list.addTriangles(fan.data(), fan.size(), idx.data(), idx.size(),
                        clip, m_texId);
}

void Rasterizer::drawDropShadow(const Rect& r, float radius, const Color& col,
                                float blur, float dx, float dy) {
    if (blur <= 0.0f || col.a == 0 || r.w <= 0.0f || r.h <= 0.0f) return;
    const Rect src{r.x + dx, r.y + dy, r.w, r.h};
    // ~2 rings per blur pixel keeps the falloff smooth (no visible steps).
    const int steps = std::clamp(static_cast<int>(blur * 2.0f + 0.5f), 4, 28);
    const float sigma = blur * 0.55f;
    // Outermost ring first; per-ring alpha follows a Gaussian in the distance
    // from the shape edge, normalised so the stack composites to ~60% of col.
    const float norm = 1.2f / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        const float grow = blur * static_cast<float>(steps - i) /
                           static_cast<float>(steps);
        const float w = std::exp(-(grow * grow) / (2.0f * sigma * sigma));
        Color c = col;
        c.a = static_cast<uint8_t>(static_cast<float>(col.a) * w * norm + 0.5f);
        if (c.a == 0) continue;
        const Rect ring{src.x - grow, src.y - grow,
                        src.w + grow * 2.0f, src.h + grow * 2.0f};
        drawRoundedRect(ring, c, radius + grow);
    }
}

void Rasterizer::drawRectUV(const Rect& r, const Rect& uv, const Color& col,
                            int texId) {
    if (r.w <= 0.0f || r.h <= 0.0f || col.a == 0) return;
    Rect clip = currentClip(r);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    addQuad(mk(x0, y0, u0, v0, col), mk(x1, y0, u1, v0, col),
            mk(x1, y1, u1, v1, col), mk(x0, y1, u0, v1, col),
            clip, texId);
}

void Rasterizer::fillRoundedRect(const Rect& r, float radius, const Color& col,
                                 const Rect& clip) {
    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float maxR = std::min(r.w, r.h) * 0.5f;
    float rad = std::min(radius, maxR);

    // Two fans (one per horizontal half) keep the winding consistent for a
    // y-down coordinate system while covering the full rect.
    const int seg = std::max(3, curveSegments);
    auto& fan = m_scratchV;
    fan.clear();
    fan.reserve(static_cast<size_t>(seg) * 2 + 8);

    // Center vertex.
    Vertex center = mk(cx, cy, 0.5f, 0.5f, col);
    auto& idx = m_scratchI;
    idx.clear();

    auto emitCorner = [&](float ccx, float ccy, float a0, float a1) {
        size_t start = fan.size();
        for (int i = 0; i <= seg; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(seg);
            float a = a0 + (a1 - a0) * t;
            float px = ccx + std::cos(a) * rad;
            float py = ccy + std::sin(a) * rad;
            fan.push_back(mk(px, py, 0.5f, 0.5f, col));
        }
        for (int i = 0; i < seg; ++i) {
            idx.push_back(0);
            idx.push_back(static_cast<uint32_t>(start + i));
            idx.push_back(static_cast<uint32_t>(start + i + 1));
        }
    };

    fan.push_back(center);
    const float PI = 3.14159265358979f;
    // top-right, bottom-right, bottom-left, top-left (y grows downward).
    emitCorner(x1 - rad, y0 + rad, -PI * 0.5f, 0.0f);
    emitCorner(x1 - rad, y1 - rad, 0.0f, PI * 0.5f);
    emitCorner(x0 + rad, y1 - rad, PI * 0.5f, PI);
    emitCorner(x0 + rad, y0 + rad, PI, PI * 1.5f);

    // Fill the straight edges between corner arcs with one quad each.
    // The corner fans already cover the rounded area; a plain center-cross
    // quad set covers the remaining rectangle interior.
    addQuad(mk(x0 + rad, y0, 0, 0, col), mk(x1 - rad, y0, 0, 0, col),
            mk(x1 - rad, y0 + rad, 0, 0, col), mk(x0 + rad, y0 + rad, 0, 0, col),
            clip, m_texId);
    addQuad(mk(x0 + rad, y1 - rad, 0, 0, col), mk(x1 - rad, y1 - rad, 0, 0, col),
            mk(x1 - rad, y1, 0, 0, col), mk(x0 + rad, y1, 0, 0, col),
            clip, m_texId);
    addQuad(mk(x0, y0 + rad, 0, 0, col), mk(x0 + rad, y0 + rad, 0, 0, col),
            mk(x0 + rad, y1 - rad, 0, 0, col), mk(x0, y1 - rad, 0, 0, col),
            clip, m_texId);
    addQuad(mk(x1 - rad, y0 + rad, 0, 0, col), mk(x1, y0 + rad, 0, 0, col),
            mk(x1, y1 - rad, 0, 0, col), mk(x1 - rad, y1 - rad, 0, 0, col),
            clip, m_texId);
    // Interior block.
    addQuad(mk(x0 + rad, y0 + rad, 0, 0, col), mk(x1 - rad, y0 + rad, 0, 0, col),
            mk(x1 - rad, y1 - rad, 0, 0, col), mk(x0 + rad, y1 - rad, 0, 0, col),
            clip, m_texId);

    m_list.addTriangles(fan.data(), fan.size(), idx.data(), idx.size(),
                        clip, m_texId);
}

void Rasterizer::strokeRoundedRect(const Rect& r, float outerRadius,
                                   float thickness, const Color& col,
                                   const Rect& clip) {
    if (thickness <= 0.0f) return;

    float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    float maxR = std::min(r.w, r.h) * 0.5f;
    float ro = std::min(outerRadius, maxR);
    float ri = std::max(ro - thickness, 0.0f);

    // Straight edges as 4 quads, inset by the corner radius so they stop
    // where the corner arcs begin (full-width quads would repaint the
    // corners square over the rounded ring).
    float t = thickness;
    float xe = ro > 0.0f ? ro : 0.0f;
    float ye = ro > 0.0f ? ro : 0.0f;
    addQuad(mk(x0 + xe, y0, 0, 0, col), mk(x1 - xe, y0, 0, 0, col),
            mk(x1 - xe, y0 + t, 0, 0, col), mk(x0 + xe, y0 + t, 0, 0, col), clip, m_texId);
    addQuad(mk(x0 + xe, y1 - t, 0, 0, col), mk(x1 - xe, y1 - t, 0, 0, col),
            mk(x1 - xe, y1, 0, 0, col), mk(x0 + xe, y1, 0, 0, col), clip, m_texId);
    addQuad(mk(x0, y0 + ye, 0, 0, col), mk(x0 + t, y0 + ye, 0, 0, col),
            mk(x0 + t, y1 - ye, 0, 0, col), mk(x0, y1 - ye, 0, 0, col), clip, m_texId);
    addQuad(mk(x1 - t, y0 + ye, 0, 0, col), mk(x1, y0 + ye, 0, 0, col),
            mk(x1, y1 - ye, 0, 0, col), mk(x1 - t, y1 - ye, 0, 0, col), clip, m_texId);

    if (ro <= 0.01f) return;

    // Corner rings: strip between outer and inner arc.
    const int seg = std::max(3, curveSegments);
    auto& verts = m_scratchV;
    auto& idx = m_scratchI;
    verts.clear();
    idx.clear();
    verts.reserve(static_cast<size_t>(seg + 1) * 4 * 2);

    const float PI = 3.14159265358979f;
    auto emitCorner = [&](float ccx, float ccy, float a0, float a1) {
        size_t start = verts.size();
        for (int i = 0; i <= seg; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(seg);
            float a = a0 + (a1 - a0) * u;
            float co = std::cos(a), si = std::sin(a);
            verts.push_back(mk(ccx + co * ro, ccy + si * ro, 0, 0, col));
            verts.push_back(mk(ccx + co * ri, ccy + si * ri, 0, 0, col));
        }
        for (int i = 0; i < seg; ++i) {
            uint32_t o = static_cast<uint32_t>(start) + static_cast<uint32_t>(i) * 2;
            idx.push_back(o + 0);
            idx.push_back(o + 1);
            idx.push_back(o + 2);
            idx.push_back(o + 1);
            idx.push_back(o + 3);
            idx.push_back(o + 2);
        }
    };

    emitCorner(x1 - ro, y0 + ro, -PI * 0.5f, 0.0f);
    emitCorner(x1 - ro, y1 - ro, 0.0f, PI * 0.5f);
    emitCorner(x0 + ro, y1 - ro, PI * 0.5f, PI);
    emitCorner(x0 + ro, y0 + ro, PI, PI * 1.5f);

    m_list.addTriangles(verts.data(), verts.size(), idx.data(), idx.size(),
                        clip, m_texId);
}

void Rasterizer::drawRectBorder(const Rect& r, const Color& col, float width,
                                float radius) {
    if (width <= 0.0f || r.w <= 0.0f || r.h <= 0.0f || col.a == 0) return;
    Rect clip = currentClip(r);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    // An inset border can't be wider than half the rect.
    float w = std::min(width, std::min(r.w, r.h) * 0.5f);
    strokeRoundedRect(r, radius, w, col, clip);
}

void Rasterizer::drawLine(const Offset2D& a, const Offset2D& b, const Color& col,
                          float thickness) {
    if (col.a == 0 || thickness <= 0.0f) return;

    float dx = b.x - a.x, dy = b.y - a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0001f) return;

    float nx = -dy / len * (thickness * 0.5f);
    float ny = dx / len * (thickness * 0.5f);

    Rect bounds{std::min(a.x, b.x) - thickness, std::min(a.y, b.y) - thickness,
                std::fabs(dx) + thickness * 2.0f,
                std::fabs(dy) + thickness * 2.0f};
    Rect clip = currentClip(bounds);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    addQuad(mk(a.x + nx, a.y + ny, 0, 0, col),
            mk(b.x + nx, b.y + ny, 0, 0, col),
            mk(b.x - nx, b.y - ny, 0, 0, col),
            mk(a.x - nx, a.y - ny, 0, 0, col),
            clip, m_texId);
}

void Rasterizer::drawTriangle(const Offset2D& a, const Offset2D& b,
                              const Offset2D& c, const Color& col) {
    if (col.a == 0) return;

    Rect bounds{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), 0, 0};
    bounds.w = std::max({a.x, b.x, c.x}) - bounds.x;
    bounds.h = std::max({a.y, b.y, c.y}) - bounds.y;
    Rect clip = currentClip(bounds);
    if (clip.w <= 0.0f || clip.h <= 0.0f) return;

    Vertex v[3] = {
        mk(a.x, a.y, 0, 0, col),
        mk(b.x, b.y, 0, 0, col),
        mk(c.x, c.y, 0, 0, col),
    };
    uint32_t idx[3] = {0, 1, 2};
    m_list.addTriangles(v, 3, idx, 3, clip, m_texId);
}

// ---- text -----------------------------------------------------------------
const Font* Rasterizer::resolveFont(const std::string& family, int size) const {
    return FontManager::instance().get(family, size);
}

float Rasterizer::textWidth(const char* text, const Font& font) const {
    return font.measure(text);
}

bool Rasterizer::drawText(const char* text, float x, float y, int size,
                          const std::string& fontFamily, const Color& col,
                          Align halign, VertAlign valign) {
    const Font* f = resolveFont(fontFamily, size);
    if (!f || !f->loaded() || !text || !*text) return false;
    return drawTextShifted(text, x, y, size, *f, col, halign, valign);
}

bool Rasterizer::drawTextShifted(const char* text, float x, float y, int /*size*/,
                                 const Font& font, const Color& col,
                                 Align halign, VertAlign valign) {
    if (!font.loaded() || !text || !*text) return false;
    if (!font.atlas()) return false;

    float totalW = font.measure(text);
    float lineH = static_cast<float>(font.ascent() + font.descent());

    float drawX = x;
    if (halign == Align::Center) drawX = x - totalW * 0.5f;
    else if (halign == Align::Right) drawX = x - totalW;

    float baseline = y;
    if (valign == VertAlign::Middle) baseline = y - lineH * 0.5f + static_cast<float>(font.ascent());
    else if (valign == VertAlign::Bottom) baseline = y - lineH + static_cast<float>(font.ascent());
    else baseline = y + static_cast<float>(font.ascent());

    const int texId = font.atlasTexture();
    float penX = drawX;
    uint32_t prev = 0;

    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (*p) {
        uint32_t ch = utf8Next(p);
        const GlyphInfo& gi = font.glyph(ch);
        if (prev) penX += static_cast<float>(font.kern(prev, ch));

        if (gi.width > 0 && gi.height > 0) {
            Rect uv = font.glyphUV(ch);
            Rect dst{
                penX + static_cast<float>(gi.xoff),
                baseline + static_cast<float>(gi.yoff),
                static_cast<float>(gi.width),
                static_cast<float>(gi.height),
            };
            Rect clip = currentClip(dst);
            if (clip.w > 0.0f && clip.h > 0.0f && texId != 0) {
                drawRectUV(dst, uv, col, texId);
            }
        }
        penX += static_cast<float>(gi.advanceX);
        prev = ch;
    }
    return true;
}

// ---- multi-line text ------------------------------------------------------

namespace {
// Byte length of the UTF-8 sequence at s (1 for anything malformed).
size_t utf8LenAt(const char* s) {
    const unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// ASCII space/tab only: both single-byte, so this is safe next to UTF-8
// continuation bytes. NBSP (U+00A0) deliberately does NOT break lines.
bool isBreakSpace(char c) { return c == ' ' || c == '\t'; }
} // namespace

std::vector<TextLine> layoutText(const char* text, float maxWidth,
                                 int size, const std::string& fontFamily) {
    std::vector<TextLine> lines;
    if (!text) return lines;
    const Font* f = FontManager::instance().get(fontFamily, size);
    if (!f) return lines;

    const size_t len = std::strlen(text);
    if (maxWidth <= 0.0f) {
        lines.push_back(TextLine{0, len});
        return lines;
    }

    auto pushLine = [&](size_t start, size_t end) {
        while (end > start && isBreakSpace(text[end - 1])) --end;
        if (end > start) lines.push_back(TextLine{start, end - start});
    };

    size_t paraStart = 0;
    for (;;) {
        size_t paraEnd = paraStart;
        while (paraEnd < len && text[paraEnd] != '\n') ++paraEnd;

        if (paraEnd == paraStart) {
            lines.push_back(TextLine{paraStart, 0});   // blank line keeps its row
        } else {
            size_t i = paraStart;
            bool firstLine = true;
            while (i < paraEnd) {
                if (!firstLine) {
                    while (i < paraEnd && isBreakSpace(text[i])) ++i;
                }
                if (i >= paraEnd) break;
                firstLine = false;

                const size_t start = i;
                size_t lastFit = i;
                size_t breakAfter = std::string::npos;
                size_t p = i;
                while (p < paraEnd) {
                    const size_t cpStart = p;
                    p += utf8LenAt(text + p);
                    if (p > paraEnd) p = paraEnd;
                    // Accept the first code point unconditionally so an
                    // oversized word still advances instead of looping.
                    if (cpStart > start &&
                        f->measureRange(text + start, p - start) > maxWidth) {
                        break;
                    }
                    lastFit = p;
                    if (isBreakSpace(text[cpStart])) breakAfter = p;
                }

                size_t lineEnd;
                if (p >= paraEnd) {
                    lineEnd = paraEnd;
                    p = paraEnd;
                } else if (breakAfter != std::string::npos && breakAfter > start) {
                    lineEnd = breakAfter;          // break after the last space
                    p = breakAfter;
                } else {
                    lineEnd = lastFit;             // hard break inside the word
                    p = lastFit;
                }
                pushLine(start, lineEnd);
                i = p;
            }
        }

        if (paraEnd >= len) break;
        paraStart = paraEnd + 1;
    }
    return lines;
}

float textLineHeight(int size, const std::string& fontFamily) {
    const Font* f = FontManager::instance().get(fontFamily, size);
    if (!f) return 0.0f;
    return static_cast<float>(f->ascent() + f->descent());
}

float Rasterizer::drawTextWrapped(const char* text, float x, float y, float maxWidth,
                                  int size, const std::string& fontFamily, const Color& col,
                                  Align halign) {
    if (!text || !*text) return 0.0f;
    const Font* f = resolveFont(fontFamily, size);
    if (!f) return 0.0f;

    const std::vector<TextLine> lines = layoutText(text, maxWidth, size, fontFamily);
    const float lineH = static_cast<float>(f->ascent() + f->descent());
    float cy = y;
    std::string buf;
    for (const TextLine& ln : lines) {
        buf.assign(text + ln.offset, ln.length);
        const float lw = f->measureRange(text + ln.offset, ln.length);
        float sx = x;
        if (halign == Align::Center) sx = x - lw * 0.5f;
        else if (halign == Align::Right) sx = x - lw;
        drawTextShifted(buf.c_str(), sx, cy, size, *f, col, Align::Left, VertAlign::Top);
        cy += lineH;
    }
    return cy - y;
}

} // namespace ogui

