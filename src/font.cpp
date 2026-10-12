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
    // Code points baked into the atlas: printable ASCII plus Latin-1
    // Supplement, so accented text (ç ã õ á é, ...) gets a real glyph
    // instead of a zero advance that collapses the words behind it.
    struct CpRange { int lo; int hi; };
    static constexpr CpRange ATLAS_RANGES[] = {{32, 126}, {160, 255}};

    template <typename Fn>
    void forEachAtlasCp(Fn&& fn) {
        for (const CpRange& r : ATLAS_RANGES)
            for (int cp = r.lo; cp <= r.hi; ++cp) fn(cp);
    }

    static constexpr int ATLAS_PAD   = 2;

    // Negative texture slots handed to freshly built atlases. Negative ids
    // can never collide with a real GL texture name, so the draw list can
    // reference an atlas before (or without) any GL upload ever happening.
    std::atomic<int> s_nextAtlasSlot{1};

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

// ---- Font (movement) ------------------------------------------------------
Font::Font(Font&& other) noexcept
    : m_path(std::move(other.m_path)),
      m_pixelHeight(other.m_pixelHeight),
      m_ascent(other.m_ascent),
      m_descent(other.m_descent),
      m_atlasTex(other.m_atlasTex),
      m_glyphs(std::move(other.m_glyphs)),
      m_pixels(std::move(other.m_pixels)),
      m_w(other.m_w), m_h(other.m_h),
      m_rowBytes(other.m_rowBytes),
      m_buffer(std::move(other.m_buffer)) {
    other.m_pixelHeight = 0;
    other.m_w = 0; other.m_h = 0;
    other.m_rowBytes = 0;
    other.m_atlasTex = 0;
}

Font& Font::operator=(Font&& other) noexcept {
    if (this != &other) {
        m_path = std::move(other.m_path);
        m_pixelHeight = other.m_pixelHeight;
        m_ascent = other.m_ascent;
        m_descent = other.m_descent;
        m_atlasTex = other.m_atlasTex;
        m_glyphs = std::move(other.m_glyphs);
        m_pixels = std::move(other.m_pixels);
        m_w = other.m_w; m_h = other.m_h;
        m_rowBytes = other.m_rowBytes;
        m_buffer = std::move(other.m_buffer);
        other.m_pixelHeight = 0;
        other.m_w = 0; other.m_h = 0;
        other.m_rowBytes = 0;
        other.m_atlasTex = 0;
    }
    return *this;
}

Font::~Font() = default;

// ---- loading --------------------------------------------------------------
bool Font::load(const std::string& path, int pixelHeight, int /*dpi*/) {
    m_pixels.clear();
    m_glyphs.clear();
    m_w = 0; m_h = 0; m_rowBytes = 0;
    m_atlasTex = 0;
    m_ascent = 0; m_descent = 0;

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
    m_pixelHeight = pixelHeight;

    float scale = stbtt_ScaleForPixelHeight(&fi, pixelHeight);
    int asc = 0, desc = 0, gap = 0;
    stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    m_ascent = static_cast<int>(std::lround(asc * scale));
    m_descent = static_cast<int>(std::lround(-desc * scale));

    // First pass: measure every glyph to size the atlas grid.
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

    const int cellW = maxW + ATLAS_PAD * 2;
    const int cellH = maxH + ATLAS_PAD * 2;
    int cols = 1;
    while (cols * cols < count) ++cols;
    const int rows = (count + cols - 1) / cols;

    m_w = cols * cellW;
    m_h = rows * cellH;
    m_rowBytes = m_w * 4;
    m_pixels.assign(static_cast<size_t>(m_w) * static_cast<size_t>(m_h) * 4, 0);

    // Start every texel white with alpha 0. Leaving RGB at 0 would pull the
    // colour toward black wherever LINEAR filtering samples across a glyph
    // edge, and straight-alpha blending darkens that fringe even further.
    for (size_t i = 0; i < m_pixels.size(); i += 4) {
        m_pixels[i + 0] = 255;
        m_pixels[i + 1] = 255;
        m_pixels[i + 2] = 255;
    }

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
        gi.advanceX = static_cast<int>(std::lround(advance * scale));
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
                size_t off = static_cast<size_t>(iy) * m_rowBytes + static_cast<size_t>(ix) * 4;
                m_pixels[off + 0] = 255;
                m_pixels[off + 1] = 255;
                m_pixels[off + 2] = 255;
                m_pixels[off + 3] = alpha;
            }
        }

        gi.atlasX = px;
        gi.atlasY = py;

        ++index;
    });

    if (m_pixels.empty()) return false;
    // Stable, never-reused slot for this atlas; the backend maps it to a GPU
    // texture on first use (see GlBackend::syncFonts()).
    m_atlasTex = -s_nextAtlasSlot.fetch_add(1);
    return true;
}

bool Font::unload() {
    m_pixels.clear();
    m_glyphs.clear();
    m_w = 0; m_h = 0; m_rowBytes = 0;
    m_atlasTex = 0;
    m_buffer.clear();
    m_pixelHeight = 0;
    return true;
}

const GlyphInfo& Font::glyph(uint32_t ch) const {
    static GlyphInfo empty;
    auto it = m_glyphs.find(ch);
    if (it == m_glyphs.end()) {
        auto si = m_glyphs.find(' ');
        return si == m_glyphs.end() ? empty : si->second;
    }
    return it->second;
}

Rect Font::glyphUV(uint32_t ch) const {
    if (m_w <= 0 || m_h <= 0) return Rect{};
    const GlyphInfo& gi = glyph(ch);
    if (gi.width <= 0 || gi.height <= 0) return Rect{};
    return Rect{
        static_cast<float>(gi.atlasX) / static_cast<float>(m_w),
        static_cast<float>(gi.atlasY) / static_cast<float>(m_h),
        static_cast<float>(gi.width) / static_cast<float>(m_w),
        static_cast<float>(gi.height) / static_cast<float>(m_h),
    };
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
    while (p < end) {
        uint32_t ch = utf8Next(p);
        if (p > end) break;   // code point cut in half by the range
        w += static_cast<float>(glyph(ch).advanceX);
    }
    return w;
}

size_t Font::indexFromX(const char* text, float x) const {
    if (!text || x <= 0.0f) return 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    const unsigned char* base = p;
    float acc = 0.0f;
    while (*p) {
        size_t idx = static_cast<size_t>(p - base);
        uint32_t ch = utf8Next(p);
        const float adv = static_cast<float>(glyph(ch).advanceX);
        if (x < acc + adv * 0.5f) return idx;   // closer to this boundary
        acc += adv;
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

std::string FontManager::resolve(const std::string& family) {
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
    auto key = std::make_pair(family.empty() ? std::string("sans") : family, size);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second.get();

    std::string path = resolve(key.first);
    if (path.empty()) return nullptr;

    auto font = std::make_unique<Font>();
    if (!font->load(path, size)) return nullptr;

    Font* raw = font.get();
    m_cache[key] = std::move(font);
    return raw;
}

} // namespace ogui
