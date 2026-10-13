#pragma once
#include "ogui/api.hpp"
#include "ogui/font.hpp"
#include "ogui/drawlist.hpp"
#include <vector>
#include <string>

namespace ogui {

// A laid-out line: byte range [offset, offset+length) into the source text.
struct TextLine {
    size_t offset = 0;
    size_t length = 0;
};

// Greedy word wrap into lines that fit `maxWidth` (0 = no wrapping).
// '\n' always breaks. A word wider than the box is split at the last code
// point that fits. Spaces around a wrapped break are trimmed. Returns byte
// ranges into `text`. Needs no rasterizer state, so layout code (Column,
// Label) can measure text the same way drawing does.
std::vector<TextLine> layoutText(const char* text, float maxWidth, int size,
                                 const std::string& fontFamily);

// Height of one line (ascent + descent) at this size; 0 when no font.
float textLineHeight(int size, const std::string& fontFamily);

// ---- Rasterizer (2D painter) ----------------------------------------------
// Accumulates geometry into a DrawList. It never talks to the GPU: the GL
// backend (glbackend) or a CPU rasterizer consumes the list afterwards.
class Rasterizer {
public:
    Rasterizer() = default;
    ~Rasterizer() = default;

    Rasterizer(const Rasterizer&) = delete;
    Rasterizer& operator=(const Rasterizer&) = delete;

    // Texture used by the untextured primitives (0 = white 1x1 fallback).
    void setTexture(int texId, int width, int height);

    // Clip stack. beginFrame pushes, endFrame pops. Everything drawn in
    // between is intersected with the current clip.
    void beginFrame(const Rect& clip);
    void endFrame();

    void reset();                       // clears the draw list + stacks
    DrawList& drawList() { return m_list; }
    const DrawList& drawList() const { return m_list; }

    // ---- primitives ----
    void drawRect(const Rect& r, const Color& col);
    // Filled rect with rounded corners (radius 0 == drawRect).
    void drawRoundedRect(const Rect& r, const Color& col, float radius);
    // Rect filled with a top->bottom colour ramp (interpolated per-vertex).
    void drawVerticalGradient(const Rect& r, const Color& top, const Color& bottom);
    // Rounded variant of drawVerticalGradient: the ramp is evaluated per
    // vertex so the corner fans blend like the straight edges do.
    void drawRoundedGradient(const Rect& r, const Color& top,
                             const Color& bottom, float radius);
    // Soft drop shadow: concentric rounded rects with quadratic alpha
    // falloff, offset by (dx, dy). blur 0 draws nothing.
    void drawDropShadow(const Rect& r, float radius, const Color& col,
                        float blur, float dx, float dy);
    void drawRectUV(const Rect& r, const Rect& uv, const Color& col, int texId);
    void drawRectBorder(const Rect& r, const Color& col, float width, float radius);
    void drawLine(const Offset2D& a, const Offset2D& b, const Color& col, float thickness);
    void drawTriangle(const Offset2D& a, const Offset2D& b, const Offset2D& c, const Color& col);

    // ---- text ----
    // Draws text from the font atlas (uses fontFamily + size), or from a Font
    // (uses that font's atlas directly).
    bool drawText(const char* text, float x, float y, int size,
                  const std::string& fontFamily, const Color& col,
                  Align halign = Align::Left, VertAlign valign = VertAlign::Top);
    bool drawTextShifted(const char* text, float x, float y, int size,
                         const Font& font, const Color& col,
                         Align halign = Align::Left, VertAlign valign = VertAlign::Top);

    // Width of a single-line string in the given font (0 when unavailable).
    float textWidth(const char* text, const Font& font) const;
    // Resolve a font by family+size through the global font manager.
    const Font* resolveFont(const std::string& family, int size) const;

    // ---- multi-line text ----
    // Draws wrapped text from (x, y) and returns the height consumed.
    // See the free functions layoutText()/textLineHeight() for measurement.
    float drawTextWrapped(const char* text, float x, float y, float maxWidth,
                          int size, const std::string& fontFamily, const Color& col,
                          Align halign = Align::Left);

    const Rect& clip() const { return m_clip; }
    float scale() const { return m_scale; }
    void setScale(float s) { m_scale = s; }

    // Style knobs used by the primitives.
    int curveSegments = 12;            // corner segments for rounded rects

private:
    Rect currentClip(const Rect& r) const;
    void addQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                 const Vertex& v3, const Rect& clip, int texId);
    void fillRoundedRect(const Rect& r, float radius, const Color& col,
                         const Rect& clip);
    void strokeRoundedRect(const Rect& r, float outerRadius, float thickness,
                           const Color& col, const Rect& clip);

    DrawList m_list;
    Rect m_clip{0, 0, 4096, 4096};
    std::vector<Rect> m_clipStack;

    // Reusable tessellation scratch (corner fans / border strips); cleared
    // per shape but capacity is kept, so a hot redraw loop stops allocating.
    std::vector<Vertex> m_scratchV;
    std::vector<uint32_t> m_scratchI;

    int m_texId = 0;
    int m_texW = 0, m_texH = 0;
    float m_scale = 1.0f;
};

} // namespace ogui
