// stb_truetype: STB_TRUETYPE_IMPLEMENTATION compiles the stb implementation
// into this TU, so all stb font functions are available here with no
// separate stb_truetype.c to link.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "ogui/font.hpp"
#include "ogui/api.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>

namespace ogui {

namespace {
    // Code points baked when a face loads, so the common case never pays an
    // on-demand bake: printable ASCII plus Latin-1 Supplement (ç ã õ á é...),
    // Latin Extended-A (Polish/Czech/Turkish...), general punctuation
    // (— ‘’“” •…) and the currency block (€ £ ₹…). Everything past this list
    // resolves through Font::resolve() and lands in the reserved cells.
    struct CpRange { int lo; int hi; };
    static constexpr CpRange ATLAS_RANGES[] = {
        {32, 126}, {160, 255}, {256, 439}, {8192, 8305}, {8352, 8368}
    };

    template <typename Fn>
    void forEachAtlasCp(Fn&& fn) {
        for (const CpRange& r : ATLAS_RANGES)
            for (int cp = r.lo; cp <= r.hi; ++cp) fn(cp);
    }

    static constexpr int ATLAS_PAD   = 2;

    // Bytes of atlas set aside for glyphs baked after the face loaded. Sized
    // per cell so a 16px face gets hundreds of spare glyphs and a 64px one gets
    // dozens, without ever reserving megabytes nobody uses.
    static constexpr int ATLAS_RESERVE_BYTES = 384 * 1024;

    // Negative texture slots handed to freshly built atlases. Negative ids
    // can never collide with a real GL texture name, so the draw list can
    // reference an atlas before (or without) any GL upload ever happening.
    std::atomic<int> s_nextAtlasSlot{1};

    // BMP coverage per file, computed once per path: the scan walks 65k code
    // points (~1.3 ms on a DejaVu), which no per-size copy should repeat.
    std::map<std::string, std::vector<uint64_t>>& coverageCache() {
        static std::map<std::string, std::vector<uint64_t>> s_cache;
        return s_cache;
    }

    std::vector<uint64_t> scanCoverage(stbtt_fontinfo& fi, const std::string& path) {
        auto it = coverageCache().find(path);
        if (it != coverageCache().end()) return it->second;

        std::vector<uint64_t> bits(0x10000 / 64, 0);
        for (int cp = 0; cp < 0x10000; ++cp) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;   // surrogates: no glyph
            if (stbtt_FindGlyphIndex(&fi, cp))
                bits[cp >> 6] |= 1ull << (cp & 63);
        }
        coverageCache().emplace(path, bits);
        return bits;
    }

    bool fileExists(const std::string& p) {
        struct stat st{};
        return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
    }

    // Depth-limited directory walk looking for one of `candidates`.
    std::string probeDir(const std::string& dir, const std::vector<std::string>& candidates,
                         int depth) {
        for (const auto& c : candidates) {
            std::string p = dir + "/" + c;
            if (fileExists(p)) return p;
        }
        if (depth <= 0) return {};

        DIR* d = opendir(dir.c_str());
        if (!d) return {};
        std::string found;
        while (struct dirent* e = readdir(d)) {
            const char* name = e->d_name;
            if (name[0] == '.') continue;
            struct stat est{};
            std::string full = dir + "/" + name;
            if (::stat(full.c_str(), &est) != 0 || !S_ISDIR(est.st_mode)) continue;
            found = probeDir(full, candidates, depth - 1);
            if (!found.empty()) break;
        }
        closedir(d);
        return found;
    }
}

// The atlas buffer is a plain vector, so the destructor needs no stb call; it
// lives here because the class only forward-declares what it owns.
Font::~Font() = default;

// ---- loading --------------------------------------------------------------
bool Font::load(const std::string& path, int logicalHeight, int /*dpi*/,
                float pxPerUnit) {
    if (pxPerUnit <= 0.0f) pxPerUnit = 1.0f;
    // The atlas is baked at device resolution; every metric handed back out
    // is divided by that factor again, so widgets only ever see logical units.
    const int bakeHeight =
        std::max(1, static_cast<int>(std::lround(logicalHeight * pxPerUnit)));
    m_pixels.clear();
    m_glyphs.clear();
    m_resolved.clear();
    m_notdef = GlyphInfo{};
    m_notdefBaked = false;
    m_cover.clear();
    m_w = 0; m_h = 0; m_rowBytes = 0;
    m_atlasTex = 0;
    m_ascent = 0; m_descent = 0;
    m_cellW = m_cellH = m_cols = m_rows = 0;
    m_nextFreeCell = 0;
    m_revision = 0;
    m_notdefAdvance = 0;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }

    m_buffer.resize(static_cast<size_t>(len));
    size_t got = fread(m_buffer.data(), 1, static_cast<size_t>(len), f);
    fclose(f);
    if (got != static_cast<size_t>(len)) return false;

    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, m_buffer.data(), 0)) return false;

    m_path = path;
    m_logicalHeight = logicalHeight;
    m_pxPerUnit = pxPerUnit;

    float scale = stbtt_ScaleForPixelHeight(&fi, bakeHeight);
    m_scale = scale;
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    m_ascent = static_cast<int>(std::lround(asc * scale));
    m_descent = static_cast<int>(std::lround(-desc * scale));
    int notdefAdv = 0;
    stbtt_GetGlyphHMetrics(&fi, 0, &notdefAdv, nullptr);
    m_notdefAdvance = std::max(0, static_cast<int>(std::lround(notdefAdv * scale)));

    // First pass: measure every eagerly baked glyph to size the atlas grid.
    int maxW = 1, maxH = 1, count = 0;
    forEachAtlasCp([&](int i) {
        int g = stbtt_FindGlyphIndex(&fi, static_cast<unsigned int>(i));
        if (!g) return;
        int adv = 0;
        stbtt_GetGlyphHMetrics(&fi, g, &adv, nullptr);
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetGlyphBitmapBox(&fi, g, scale, scale, &ix0, &iy0, &ix1, &iy1);
        int gw = ix1 - ix0;
        int gh = iy1 - iy0;
        if (gw > 0 && gh > 0) {
            maxW = std::max(maxW, gw);
            maxH = std::max(maxH, gh);
            ++count;
        }
    });
    if (count == 0) return false;

    // The same cells also hold whatever is baked later, so they are widened to
    // a full em: a CJK ideograph or a wide dingbat then fits without ever
    // moving a glyph that was already placed.
    maxW = std::max(maxW, bakeHeight);
    maxH = std::max(maxH, m_ascent + m_descent);
    const int cellW = maxW + ATLAS_PAD * 2;
    const int cellH = maxH + ATLAS_PAD * 2;
    const int reserve = std::clamp(ATLAS_RESERVE_BYTES / (cellW * cellH), 64, 4096);

    int cells = count + reserve + 1;   // +1 for the .notdef box
    int cols = 1;
    while (cols * cols < cells) ++cols;
    const int rows = (cells + cols - 1) / cols;

    m_cellW = cellW; m_cellH = cellH;
    m_cols = cols; m_rows = rows;
    m_w = cols * cellW;
    m_h = rows * cellH;
    m_rowBytes = m_w;
    m_pixels.assign(static_cast<size_t>(m_w) * static_cast<size_t>(m_h), 0);

    // Second pass: rasterize into grid cells. Metrics are recorded for every
    // covered code point *before* the ink test: inkless glyphs (space) must
    // still advance the pen, otherwise every word after a space lands on top
    // of the previous one.
    int index = 0;
    forEachAtlasCp([&](int i) {
        uint32_t ch = static_cast<uint32_t>(i);
        int g = stbtt_FindGlyphIndex(&fi, ch);
        if (!g) return;

        int advance = 0;
        stbtt_GetGlyphHMetrics(&fi, g, &advance, nullptr);
        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetGlyphBitmapBox(&fi, g, scale, scale, &ix0, &iy0, &ix1, &iy1);
        int gw = ix1 - ix0;
        int gh = iy1 - iy0;

        GlyphInfo& gi = m_glyphs[ch];
        gi.advanceX = isCombiningMark(ch) ? 0
                                          : static_cast<int>(std::lround(advance * scale));
        gi.lsb = ix0;
        gi.xoff = ix0;
        gi.yoff = iy0;
        gi.width = std::max(0, gw);
        gi.height = std::max(0, gh);
        if (gi.width == 0 || gi.height == 0) return;   // no ink: metrics only

        std::vector<unsigned char> bitmap(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0);
        stbtt_MakeCodepointBitmap(&fi, bitmap.data(), gw, gh, gw, scale, scale, static_cast<int>(ch));

        int col = index % cols;
        int row = index / cols;
        int cellX = col * cellW;
        int cellY = row * cellH;
        int px = cellX + ATLAS_PAD;
        int py = cellY + ATLAS_PAD;

        for (int y = 0; y < gh; ++y) {
            for (int x = 0; x < gw; ++x) {
                unsigned char alpha = bitmap[static_cast<size_t>(y) * gw + x];
                if (!alpha) continue;
                int ix = px + x;
                int iy = py + y;
                if (ix < 0 || iy < 0 || ix >= m_w || iy >= m_h) continue;
                m_pixels[static_cast<size_t>(iy) * m_rowBytes +
                         static_cast<size_t>(ix)] = alpha;
            }
        }

        gi.atlasX = px;
        gi.atlasY = py;

        ++index;
    });
    m_nextFreeCell = index;

    if (m_pixels.empty()) return false;

    m_cover = scanCoverage(fi, path);

    // Bake kerning for the printable-ASCII grid: pair lookups at draw time
    // are then a single hash probe, and accented/CJK pairs (rare) simply
    // keep their default advance.
    for (int a = 32; a <= 126; ++a) {
        for (int b = 32; b <= 126; ++b) {
            const int k = stbtt_GetCodepointKernAdvance(&fi, a, b);
            if (!k) continue;
            const int px = static_cast<int>(std::lround(k * scale));
            if (!px) continue;
            m_kern[static_cast<uint32_t>(a) * 128u + static_cast<uint32_t>(b)] =
                static_cast<int8_t>(std::clamp(px, -128, 127));
        }
    }

    // Stable, never-reused slot for this atlas; the backend maps it to a GPU
    // texture on first use (see GlBackend::syncFonts()).
    m_atlasTex = -s_nextAtlasSlot.fetch_add(1);
    return true;
}

bool Font::unload() {
    m_pixels.clear();
    m_glyphs.clear();
    m_kern.clear();
    m_resolved.clear();
    m_cover.clear();
    m_notdef = GlyphInfo{};
    m_notdefBaked = false;
    m_w = 0; m_h = 0; m_rowBytes = 0;
    m_atlasTex = 0;
    m_buffer.clear();
    m_logicalHeight = 0;
    m_pxPerUnit = 1.0f;
    m_nextFreeCell = 0;
    m_revision = 0;
    return true;
}

// ---- on-demand glyphs ------------------------------------------------------
// Rasterizes one code point into the next reserved cell. The atlas is allocated
// whole at load and never reallocated, so a glyph baked at 3 a.m. sits at the
// same UV it will hold for the rest of the process, and a backend that cached
// the buffer pointer (the CPU rasterizer does) keeps working.
bool Font::bake(uint32_t ch) const {
    if (!loaded() || m_buffer.empty() || !covers(ch)) return false;
    if (m_glyphs.find(ch) != m_glyphs.end()) return true;
    if (m_nextFreeCell >= m_cols * m_rows) return false;   // reserve exhausted

    stbtt_fontinfo fi;
    if (!stbtt_InitFont(&fi, m_buffer.data(), 0)) return false;
    const int g = stbtt_FindGlyphIndex(&fi, ch);
    if (!g) return false;

    int advance = 0;
    stbtt_GetGlyphHMetrics(&fi, g, &advance, nullptr);
    int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
    stbtt_GetGlyphBitmapBox(&fi, g, m_scale, m_scale, &ix0, &iy0, &ix1, &iy1);
    const int gw = ix1 - ix0;
    const int gh = iy1 - iy0;

    GlyphInfo gi;
    // A combining mark must not move the pen: it is drawn over the base, and
    // its own offsets already assume that origin.
    gi.advanceX = isCombiningMark(ch) ? 0
                                      : static_cast<int>(std::lround(advance * m_scale));
    gi.lsb = ix0;
    gi.xoff = ix0;
    gi.yoff = iy0;
    gi.width = std::max(0, gw);
    gi.height = std::max(0, gh);

    if (gi.width == 0 || gi.height == 0) {   // inkless: no cell consumed
        m_glyphs[ch] = gi;
        return true;
    }

    const int col = m_nextFreeCell % m_cols;
    const int row = m_nextFreeCell / m_cols;
    gi.atlasX = col * m_cellW + ATLAS_PAD;
    gi.atlasY = row * m_cellH + ATLAS_PAD;

    std::vector<unsigned char> bitmap(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0);
    stbtt_MakeCodepointBitmap(&fi, bitmap.data(), gw, gh, gw,
                              m_scale, m_scale, static_cast<int>(ch));

    // Clip to the cell rather than spill into its neighbours: a glyph wider
    // than an em is a font oddity, and a trimmed edge beats a torn atlas.
    const int inW = std::min(gw, m_cellW - ATLAS_PAD * 2);
    const int inH = std::min(gh, m_cellH - ATLAS_PAD * 2);
    gi.width = inW;
    gi.height = inH;
    for (int y = 0; y < inH; ++y) {
        for (int x = 0; x < inW; ++x) {
            const unsigned char alpha = bitmap[static_cast<size_t>(y) * gw + x];
            if (!alpha) continue;
            m_pixels[static_cast<size_t>(gi.atlasY + y) * m_rowBytes +
                     static_cast<size_t>(gi.atlasX + x)] = alpha;
        }
    }

    ++m_nextFreeCell;
    ++m_revision;
    m_glyphs[ch] = gi;
    return true;
}

// The hole drawn where no face in the chain has a glyph: a hollow box about
// four fifths of an em tall, sitting on the baseline. Invisible fallbacks are
// how a missing font stops being a bug report.
const GlyphInfo& Font::notdef() const {
    if (m_notdefBaked) return m_notdef;
    m_notdefBaked = true;

    GlyphInfo& gi = m_notdef;
    const int em = std::max(1, static_cast<int>(std::lround(m_logicalHeight * m_pxPerUnit)));
    const int h = std::max(3, static_cast<int>(std::lround(m_ascent * 0.8f)));
    const int w = std::max(3, std::min(em * 4 / 5, m_cellW - ATLAS_PAD * 2));
    gi.advanceX = std::max(m_notdefAdvance, w + ATLAS_PAD * 2);
    gi.lsb = ATLAS_PAD;
    gi.xoff = ATLAS_PAD;
    gi.yoff = -h;
    gi.width = w;
    gi.height = h;
    if (!loaded() || m_nextFreeCell >= m_cols * m_rows) {
        gi.width = 0; gi.height = 0;      // no room: advance only
        return gi;
    }

    const int col = m_nextFreeCell % m_cols;
    const int row = m_nextFreeCell / m_cols;
    gi.atlasX = col * m_cellW + ATLAS_PAD;
    gi.atlasY = row * m_cellH + ATLAS_PAD;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool edge = y == 0 || y == h - 1 || x == 0 || x == w - 1;
            if (!edge) continue;
            m_pixels[static_cast<size_t>(gi.atlasY + y) * m_rowBytes +
                     static_cast<size_t>(gi.atlasX + x)] = 220;
        }
    }
    ++m_nextFreeCell;
    ++m_revision;
    return gi;
}

ResolvedGlyph Font::resolve(uint32_t ch) const {
    if (!loaded()) return ResolvedGlyph{};
    const bool mark = isCombiningMark(ch);

    // Own atlas first: the eagerly baked ranges hit here, one map lookup, and
    // anything else the face carries bakes into its own reserve.
    auto it = m_glyphs.find(ch);
    if (it == m_glyphs.end() && bake(ch)) it = m_glyphs.find(ch);
    if (it != m_glyphs.end()) return ResolvedGlyph{this, &it->second, mark};

    // Past that the answer never changes, so misses and fallbacks cache too.
    auto seen = m_resolved.find(ch);
    if (seen != m_resolved.end()) return seen->second;

    ResolvedGlyph out;
    const Font* face = FontManager::instance().fallbackFace(*this, ch);
    if (face) {
        // fallbackFace() baked it; the entry lives in that atlas, so the draw
        // loop reads the texture from `face` rather than from this one.
        auto fi = face->m_glyphs.find(ch);
        if (fi != face->m_glyphs.end()) out = ResolvedGlyph{face, &fi->second, mark};
    }
    if (!out) {
        // A mark no face carries stays silent: a .notdef box would deface the
        // base letter for a diacritic that has no bitmap anywhere on this box.
        static const GlyphInfo kSilent;
        out = mark ? ResolvedGlyph{this, &kSilent, true}
                   : ResolvedGlyph{this, &notdef(), false};
    }
    m_resolved[ch] = out;
    return out;
}

const GlyphInfo& Font::glyph(uint32_t ch) const {
    static const GlyphInfo empty;
    const ResolvedGlyph r = resolve(ch);
    return r.info ? *r.info : empty;
}

Rect ResolvedGlyph::uv() const {
    if (!font || !info || info->width <= 0 || info->height <= 0) return Rect{};
    const float w = static_cast<float>(font->atlasWidth());
    const float h = static_cast<float>(font->atlasHeight());
    if (w <= 0.0f || h <= 0.0f) return Rect{};
    return Rect{
        static_cast<float>(info->atlasX) / w,
        static_cast<float>(info->atlasY) / h,
        static_cast<float>(info->width) / w,
        static_cast<float>(info->height) / h,
    };
}

Rect Font::glyphUV(uint32_t ch) const {
    return resolve(ch).uv();
}

float Font::kern(uint32_t prev, uint32_t cur) const {
    if (m_kern.empty() || prev > 126 || cur > 126) return 0.0f;
    auto it = m_kern.find(prev * 128u + cur);
    if (it == m_kern.end()) return 0.0f;
    return static_cast<float>(it->second) / pxPerUnit();
}

float Font::measure(const char* text) const {
    if (!text) return 0.0f;
    return measureRange(text, std::strlen(text));
}

float Font::measureRange(const char* text, size_t bytes) const {
    if (!text) return 0.0f;
    float w = 0.0f;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    const unsigned char* end = p + bytes;
    uint32_t prev = 0;
    while (p < end) {
        uint32_t ch = utf8Next(p);
        if (p > end) break;   // code point cut in half by the range
        if (prev) w += kern(prev, ch);
        w += static_cast<float>(glyph(ch).advanceX);
        prev = ch;
    }
    return w / pxPerUnit();
}

size_t Font::indexFromX(const char* text, float x) const {
    if (!text || x <= 0.0f) return 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    const unsigned char* base = p;
    // Advances live in atlas pixels; the click offset arrives in logical ones.
    const float target = x * pxPerUnit();
    float acc = 0.0f;
    uint32_t prev = 0;
    while (*p) {
        size_t idx = static_cast<size_t>(p - base);
        uint32_t ch = utf8Next(p);
        if (prev) acc += kern(prev, ch) * pxPerUnit();
        const float adv = static_cast<float>(glyph(ch).advanceX);
        if (target < acc + adv * 0.5f) return idx;   // closer to this boundary
        acc += adv;
        prev = ch;
    }
    return static_cast<size_t>(p - base);
}

// ---- FontManager ----------------------------------------------------------
FontManager& FontManager::instance() {
    static FontManager fm;
    return fm;
}

FontManager::FontManager() {
#ifdef _WIN32
    const char* sysRoot = std::getenv("SystemRoot");
    std::string win = sysRoot ? std::string(sysRoot) : std::string("C:\\Windows");
    m_dirs.push_back(win + "\\Fonts");
#else
    m_dirs.push_back("/usr/share/fonts");
    m_dirs.push_back("/usr/local/share/fonts");
    m_dirs.push_back("/usr/share/fonts/truetype");
    m_dirs.push_back(std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.fonts");
#endif
    // The generic aliases are the fallback chain by default: whichever file
    // they probe to is the font on this box with the widest coverage, so a
    // serif or mono request that runs out of glyphs still finds real ink.
    m_fallbacks = {"sans", "serif", "mono"};
}

void FontManager::addFallback(const std::string& family) {
    if (family.empty()) return;
    for (const auto& f : m_fallbacks)
        if (f == family) return;
    m_fallbacks.push_back(family);
}

const Font* FontManager::fallbackFace(const Font& requester, uint32_t ch) const {
    if (ch >= 0x10000) return nullptr;    // coverage is only known for the BMP
    for (const std::string& family : m_fallbacks) {
        if (family == requester.family()) continue;
        const Font* f = load(family, requester.size());
        if (!f || f == &requester || !f->covers(ch)) continue;
        // The candidate bakes into its own atlas; no re-entry into this chain.
        if (f->bake(ch)) return f;
    }
    return nullptr;
}

void FontManager::setFontPath(const std::string& family, const std::string& path) {
    m_paths[family] = path;
    clear();
}

void FontManager::addSearchDir(const std::string& dir) {
    m_dirs.push_back(dir);
}

void FontManager::clear() {
    m_cache.clear();
}

std::string FontManager::resolve(const std::string& family) const {
    auto it = m_paths.find(family);
    if (it != m_paths.end() && fileExists(it->second)) return it->second;

    // Well-known candidates per family, probed inside every search dir
    // (non-recursive; the common distro layouts are covered by m_dirs).
    std::vector<std::string> candidates;
    if (family == "serif") {
        candidates = {"DejaVuSerif.ttf", "FreeSerif.ttf", "LiberationSerif-Regular.ttf",
                      "Times_New_Roman.ttf", "times.ttf"};
    } else if (family == "mono" || family == "monospace") {
        candidates = {"DejaVuSansMono.ttf", "FreeMono.ttf", "LiberationMono-Regular.ttf",
                      "consola.ttf", "cour.ttf"};
    } else {   // sans (default)
        candidates = {"DejaVuSans.ttf", "FreeSans.ttf", "LiberationSans-Regular.ttf",
                      "arial.ttf", "segoeui.ttf", "Ubuntu-R.ttf"};
    }

    for (const auto& dir : m_dirs) {
        std::string p = probeDir(dir, candidates, 4);
        if (!p.empty()) return p;
    }
    return {};
}

const Font* FontManager::get(const std::string& family, int size) {
    if (size <= 0) size = 14;
    return load(family.empty() ? std::string("sans") : family, size);
}

// One body for explicit requests and fallback probes. Const because a glyph
// lookup that misses has to be able to build the face it is asking: the cache
// is mutable, and a Font once handed out lives there until clear().
const Font* FontManager::load(const std::string& family, int size) const {
    if (size <= 0) size = 14;
    auto key = std::make_pair(family, size);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second.get();

    std::string path = resolve(family);
    if (path.empty()) return nullptr;

    auto font = std::make_unique<Font>();
    if (!font->load(path, size, 96, m_scale)) return nullptr;
    font->setFamily(family);

    Font* raw = font.get();
    m_cache[key] = std::move(font);
    return raw;
}

void FontManager::setScale(float s) {
    if (s <= 0.0f) s = 1.0f;
    if (s == m_scale) return;
    // Every cached atlas was baked for the old factor, so nothing survives.
    m_scale = s;
    m_cache.clear();
}

} // namespace ogui
