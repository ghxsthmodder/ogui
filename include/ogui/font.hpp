#pragma once
#include "ogui/api.hpp"
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>

namespace ogui {

// Decodes the code point at *p and advances *p past it. Stray bytes are
// consumed one at a time so broken input degrades instead of stalling.
inline uint32_t utf8Next(const unsigned char*& p) {
    uint32_t c = *p;
    if (c < 0x80) { ++p; return c; }

    uint32_t acc = 0;
    int extra = 0;
    if ((c & 0xE0) == 0xC0)      { acc = c & 0x1Fu; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { acc = c & 0x0Fu; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { acc = c & 0x07u; extra = 3; }
    else                         { ++p; return c; }

    ++p;
    for (int i = 0; i < extra; ++i) {
        if ((*p & 0xC0u) != 0x80u) break;
        acc = (acc << 6) | (*p & 0x3Fu);
        ++p;
    }
    return acc;
}

// ---- font + glyph atlas ----------------------------------------------------
struct GlyphInfo {
    int advanceX = 0;   // advance in atlas pixels (already scaled)
    int lsb = 0;
    int width = 0;      // bitmap size in atlas pixels
    int height = 0;
    int xoff = 0;       // offset from pen position to bitmap left (atlas pixels)
    int yoff = 0;       // offset from baseline to bitmap top (atlas pixels, y down)
    int atlasX = 0;     // bitmap position in the atlas (pixels)
    int atlasY = 0;
};

// A8 glyph atlas (single texture, grid packed). Texels carry coverage only;
// the GL backend maps them to (1,1,1,r) through a texture swizzle, and the
// CPU rasterizer does the same when sampling.
//
// Units: `size()`, ascent()/descent(), measure*(), indexFromX() and kern()
// speak LOGICAL pixels (the units XML geometry and widget bounds use); the
// atlas itself is baked at size()*pxPerUnit() device pixels so text stays
// crisp on a HiDPI framebuffer. GlyphInfo keeps raw atlas pixels — multiply
// or divide by pxPerUnit() when turning one into a logical rect.
class Font {
public:
    Font() = default;
    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    Font(Font&& other) noexcept;
    Font& operator=(Font&& other) noexcept;

    // Load a .ttf/.otf and build the atlas. `logicalHeight` is the text size
    // in logical pixels; `pxPerUnit` is the device pixels per logical unit
    // the atlas is baked for (1.0 = one device pixel per logical pixel).
    bool load(const std::string& path, int logicalHeight, int dpi = 96,
              float pxPerUnit = 1.0f);
    bool unload();

    const GlyphInfo& glyph(uint32_t ch) const;

    // Kerning adjustment in LOGICAL pixels applied between two adjacent code
    // points; 0 when the pair has none. Baked from the font's kern table at
    // load time for the printable-ASCII grid.
    float kern(uint32_t prev, uint32_t cur) const;

    // Normalized UV rect of a glyph inside the atlas.
    Rect glyphUV(uint32_t ch) const;

    // Atlas access (A8 coverage). Returns nullptr when not loaded.
    const uint8_t* atlas() const { return m_pixels.data(); }
    int atlasWidth() const { return m_w; }
    int atlasHeight() const { return m_h; }
    int atlasRowBytes() const { return m_rowBytes; }

    // Texture slot for the atlas. A freshly built atlas always gets a stable
    // negative slot (0 only before the first load), so a draw list can be
    // built with or without a GPU upload ever having happened. The backend
    // maps the slot to its own texture; see GlBackend::syncFonts().
    int atlasTexture() const { return m_atlasTex; }
    void setAtlasTexture(int id) { m_atlasTex = id; }

    const std::string& path() const { return m_path; }
    int size() const { return m_logicalHeight; }
    bool loaded() const { return !m_pixels.empty(); }
    // Device pixels per logical pixel this atlas was baked for.
    float pxPerUnit() const { return m_pxPerUnit > 0.0f ? m_pxPerUnit : 1.0f; }

    // Baseline metrics in logical pixels for this size.
    float ascent() const { return static_cast<float>(m_ascent) / pxPerUnit(); }
    float descent() const { return static_cast<float>(m_descent) / pxPerUnit(); }

    // Width of a UTF-8/ASCII string in logical pixels at this size.
    float measure(const char* text) const;
    // Width of the first `bytes` bytes of `text` in logical pixels; `text`
    // need not be NUL-terminated. A truncated code point at the cut is
    // ignored.
    float measureRange(const char* text, size_t bytes) const;

    // Byte offset of the code point boundary nearest to `x` LOGICAL pixels
    // from the string start (0 .. strlen). Used to place the caret from a
    // click.
    size_t indexFromX(const char* text, float x) const;

private:
    std::string m_path;
    int m_logicalHeight = 0;         // requested size in logical pixels
    float m_pxPerUnit = 1.0f;        // atlas bake factor
    int m_ascent = 0;                // atlas pixels
    int m_descent = 0;               // atlas pixels
    int m_atlasTex = 0;
    std::map<uint32_t, GlyphInfo> m_glyphs;
    std::unordered_map<uint32_t, int8_t> m_kern;  // prev*128+cur -> atlas px

    std::vector<uint8_t> m_pixels;   // A8 flat: w*h
    int m_w = 0, m_h = 0;
    int m_rowBytes = 0;

    std::vector<unsigned char> m_buffer;   // raw file data (owned)
};

// ------------------------------------------------------------------
// FontManager: family+size cache used by Rasterizer::drawText.
// Families map to files via setFontPath(); otherwise well-known system
// fonts are probed.
// ------------------------------------------------------------------
class FontManager {
public:
    static FontManager& instance();

    // Returns a loaded font, or nullptr when none could be found. `size` is
    // in LOGICAL pixels; with a scale set, the atlas is baked at size*scale
    // device pixels while every metric still comes back in logical pixels.
    const Font* get(const std::string& family, int size);

    // Device pixels per logical pixel for newly built atlases. Changing it
    // drops the cache, since every bake depends on the value. The Kernel
    // keeps this in sync with the screen scale on every draw list rebuild.
    void setScale(float s);
    float scale() const { return m_scale; }

    // Explicit family -> file mapping (takes priority over probing).
    void setFontPath(const std::string& family, const std::string& path);

    // Directories probed when a family has no explicit path.
    void addSearchDir(const std::string& dir);

    void clear();   // drops the cache (keeps mappings)

    // Visits every cached font (used by the GL backend to upload atlases).
    template <typename Fn>
    void forEach(Fn&& fn) {
        for (auto& kv : m_cache) {
            if (kv.second && kv.second->loaded()) fn(*kv.second);
        }
    }

private:
    FontManager();
    std::string resolve(const std::string& family);

    std::map<std::pair<std::string, int>, std::unique_ptr<Font>> m_cache;
    std::map<std::string, std::string> m_paths;
    std::vector<std::string> m_dirs;
    float m_scale = 1.0f;
};

} // namespace ogui
