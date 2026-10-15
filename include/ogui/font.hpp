#pragma once
#include "ogui/api.hpp"
#include <vector>
#include <map>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>

namespace ogui {

class Font;

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

// True for code points that stack onto the previous base letter instead of
// advancing the pen: combining diacritics, Hebrew/Arabic/Thai vowel marks,
// Japanese voiced-sound marks and the symbol/variation marks. This is the set
// of scripts a system font on this platform can actually render — a full
// General_Category=Mn table would be dead weight without shaping to go with
// it, and the fallback chain resolves the rest.
inline bool isCombiningMark(uint32_t c) {
    static constexpr uint32_t R[][2] = {
        {0x0300, 0x036F},   // combining diacritical marks
        {0x0483, 0x0489},   // Cyrillic combining
        {0x0591, 0x05BD}, {0x05BF, 0x05BF},
        {0x05C1, 0x05C2}, {0x05C4, 0x05C5}, {0x05C7, 0x05C7},   // Hebrew points
        {0x0610, 0x061A}, {0x064B, 0x065F}, {0x0670, 0x0670},
        {0x06D6, 0x06DC}, {0x06DF, 0x06E4}, {0x06E7, 0x06E8},
        {0x06EA, 0x06ED}, {0x0730, 0x074A}, {0x07A6, 0x07B0},   // Arabic
        {0x0E31, 0x0E31}, {0x0E34, 0x0E3A}, {0x0E47, 0x0E4E},
        {0x0EB1, 0x0EB1}, {0x0EB4, 0x0EBC}, {0x0EC8, 0x0ECD},   // Thai / Lao
        {0x102D, 0x1030}, {0x1032, 0x1037}, {0x1039, 0x103A},
        {0x1058, 0x1059},                                       // Myanmar
        {0x1DC0, 0x1DFF}, {0x20D0, 0x20F0},                     // Latin extended
        {0x2CEF, 0x2CF1}, {0x302A, 0x302F}, {0x3099, 0x309A},   // CJK / Kana
        {0xA66F, 0xA672}, {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F},
    };
    for (const auto& r : R)
        if (c >= r[0] && c <= r[1]) return true;
    return false;
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

// A code point resolved to the face that actually carries its bitmap. Baking
// happens on demand, so the face may be one of the fallback families rather
// than the one the widget asked for — the draw loop has to take the atlas (and
// therefore the texture) from `font`, not from the requested face.
struct ResolvedGlyph {
    const Font* font = nullptr;   // owns the bitmap below
    const GlyphInfo* info = nullptr;
    bool mark = false;            // combining mark: contributes no advance

    // True when this resolves to something placeable. A miss is the .notdef
    // box of the requested face, so it stays true for unknown code points too
    // (they take a real advance and show a visible hole instead of collapsing).
    explicit operator bool() const { return font != nullptr && info != nullptr; }

    // Normalized rect inside font->atlas(). Empty when the entry has no ink.
    Rect uv() const;
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

    // Every member is a container, so the compiler-generated moves are enough:
    // a moved-out font reports loaded() == false and owns nothing.
    Font(Font&& other) noexcept = default;
    Font& operator=(Font&& other) noexcept = default;

    // Load a .ttf/.otf and build the atlas. `logicalHeight` is the text size
    // in logical pixels; `pxPerUnit` is the device pixels per logical unit
    // the atlas is baked for (1.0 = one device pixel per logical pixel).
    bool load(const std::string& path, int logicalHeight, int dpi = 96,
              float pxPerUnit = 1.0f);
    bool unload();

    // This face's entry for `ch`, baked on demand. Falls back to resolve() for
    // a code point the face does not carry, so measure()/indexFromX() see the
    // advance of the glyph that actually gets drawn (a fallback face is baked
    // at the same size, so its advances are directly comparable).
    const GlyphInfo& glyph(uint32_t ch) const;

    // Full Unicode resolution: this face's bitmap, else the first face in the
    // FontManager fallback chain that covers the code point, else this face's
    // .notdef box. Glyphs are baked into reserved atlas cells on first use and
    // the answer is cached, so the steady-state cost is one map lookup.
    // `font` in the result points at the face that owns `info` — take the
    // texture (and the UVs) from there, not from the requested face.
    ResolvedGlyph resolve(uint32_t ch) const;

    // True when this face has an outline for `ch` (index 0 counts as "no").
    // Known for the whole BMP, which is where every non-CJK-unified script
    // lives; higher planes report false and take the fallback chain.
    bool covers(uint32_t ch) const {
        return ch < 0x10000 && !m_cover.empty() &&
               ((m_cover[ch >> 6] >> (ch & 63)) & 1u) != 0;
    }

    // Rasterizes `ch` into a reserved atlas cell. Const because baking through
    // a lookup is the whole point: the drawing path only ever holds const
    // fonts. False when the face has no outline, the cell is too small for the
    // ink box (clipped instead when it merely overflows) or the reserve is
    // exhausted.
    bool bake(uint32_t ch) const;

    // Bakes the placeholder drawn for a code point no face in the chain
    // carries: a hollow box one em wide, so missing text stays visible and
    // keeps its measure instead of collapsing the words behind it.
    const GlyphInfo& notdef() const;

    // Increments whenever new pixels land in the atlas. Existing cells never
    // move (the atlas is allocated once, reserve included), so a backend only
    // has to re-upload the buffer; UVs emitted earlier stay valid.
    unsigned atlasRevision() const { return m_revision; }

    // Kerning adjustment in LOGICAL pixels applied between two adjacent code
    // points; 0 when the pair has none. Baked from the font's kern table at
    // load time for the printable-ASCII grid.
    float kern(uint32_t prev, uint32_t cur) const;

    // Normalized UV rect of a glyph inside the atlas of the face that carries
    // it (see resolve()); empty when the code point has no ink.
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
    // Family this face was requested under; FontManager uses it to skip the
    // face itself when walking the fallback chain.
    const std::string& family() const { return m_family; }
    void setFamily(const std::string& f) { m_family = f; }
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
    std::string m_family;            // key it was requested under (fallbacks)
    int m_logicalHeight = 0;         // requested size in logical pixels
    float m_pxPerUnit = 1.0f;        // atlas bake factor
    float m_scale = 1.0f;            // stb units -> atlas pixels
    int m_ascent = 0;                // atlas pixels
    int m_descent = 0;               // atlas pixels
    int m_notdefAdvance = 0;         // atlas pixels, from the font's .notdef
    int m_atlasTex = 0;

    // Grid this atlas is packed on: one cell per glyph, allocated once so a
    // late bake can never move an earlier glyph.
    int m_cellW = 0, m_cellH = 0;
    int m_cols = 0, m_rows = 0;

    std::vector<uint64_t> m_cover;   // BMP outline presence, 1 bit per cp
    std::unordered_map<uint32_t, int8_t> m_kern;  // prev*128+cur -> atlas px

    // Baking happens through const lookups (glyph()/measure()/drawText() only
    // ever see a const Font), so the atlas, its glyph table and its caches are
    // mutable.
    mutable std::map<uint32_t, GlyphInfo> m_glyphs;
    mutable int m_nextFreeCell = 0;
    mutable std::vector<uint8_t> m_pixels;   // A8 flat: w*h
    mutable int m_w = 0, m_h = 0;
    mutable int m_rowBytes = 0;
    mutable unsigned m_revision = 0;
    mutable std::map<uint32_t, ResolvedGlyph> m_resolved;   // cp -> face + entry
    mutable GlyphInfo m_notdef;
    mutable bool m_notdefBaked = false;

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
    // device pixels while every metric still comes back in logical pixels. The
    // font is the manager's, and stays valid until clear() drops it -- not
    // before, since a scale change re-bakes it where it stands.
    const Font* get(const std::string& family, int size);

    // Device pixels per logical pixel for newly built atlases. Changing it
    // re-bakes every cached face in place -- the objects survive, so a
    // `const Font*` held across a dpi change is still a live font, now with the
    // new bake -- because every atlas depends on the value. The Kernel keeps
    // this in sync with the screen scale on every draw list rebuild.
    void setScale(float s);
    float scale() const { return m_scale; }

    // Explicit family -> file mapping (takes priority over probing).
    void setFontPath(const std::string& family, const std::string& path);

    // Directories probed when a family has no explicit path.
    void addSearchDir(const std::string& dir);

    // Families consulted, in order, when the requested face has no outline for
    // a code point. Seeded with the generic aliases, so Cyrillic, Greek,
    // Hebrew, Thai and the CJK blocks land on whichever installed font carries
    // them; a host adds the rest (`setFontPath("noto cjk", ...)` and then
    // `addFallback("noto cjk")`). Duplicates are ignored.
    void addFallback(const std::string& family);
    const std::vector<std::string>& fallbacks() const { return m_fallbacks; }

    // First face of that chain carrying an outline for `ch`, baked into its own
    // atlas, or nullptr when nothing carries it. Loads faces on demand. Called
    // by Font::resolve(); it never re-enters the chain, so a code point costs
    // one walk at most.
    const Font* fallbackFace(const Font& requester, uint32_t ch) const;

    // Drops the cache (keeps mappings). The one call that invalidates a
    // `const Font*` the host is still holding.
    void clear();

    // Visits every cached font (used by the GL backend to upload atlases).
    template <typename Fn>
    void forEach(Fn&& fn) {
        for (auto& kv : m_cache) {
            if (kv.second && kv.second->loaded()) fn(*kv.second);
        }
    }

private:
    FontManager();
    std::string resolve(const std::string& family) const;
    const Font* load(const std::string& family, int size) const;

    mutable std::map<std::pair<std::string, int>, std::unique_ptr<Font>> m_cache;
    std::map<std::string, std::string> m_paths;
    std::vector<std::string> m_dirs;
    std::vector<std::string> m_fallbacks;
    float m_scale = 1.0f;
};

} // namespace ogui
