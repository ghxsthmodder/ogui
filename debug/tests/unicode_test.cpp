// ogui_unicode_test.cpp : the Unicode text stack — on-demand glyph baking,
// fallback between faces, the .notdef placeholder, combining marks and CJK
// line breaking. Fonts differ from machine to machine, so every check is
// written against what the resolved face actually covers rather than against
// one specific file.
#include "ogui/font.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/software.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace ogui;

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond  \
                      << "\n";                                                 \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

namespace {

std::string utf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

uint32_t cpAt(const std::string& s, size_t i) {
    const unsigned char* q = reinterpret_cast<const unsigned char*>(s.c_str()) + i;
    return utf8Next(q);
}

bool uvSane(const ResolvedGlyph& g) {
    const Rect r = g.uv();
    return r.w > 0.0f && r.h > 0.0f && r.x >= 0.0f && r.y >= 0.0f &&
           r.x + r.w <= 1.0f + 1e-5f && r.y + r.h <= 1.0f + 1e-5f;
}

// Code points from scripts outside the eagerly baked Latin ranges.
const uint32_t SCRIPT_CPS[] = {
    0x0415,   // Cyrillic capital IE
    0x03B1,   // Greek small alpha
    0x05D0,   // Hebrew letter alef
    0x0627,   // Arabic letter alef
    0x0E31,   // Thai mai han akat
    0x4E00,   // CJK ideograph "one"
    0x3042,   // Hiragana a
    0xAC00,   // Hangul syllable ga
};

// ---- on-demand baking ------------------------------------------------------
void testOnDemandBake() {
    const Font* f = FontManager::instance().get("sans", 17);
    if (!f) { std::cout << "  (no system font; skipping)\n"; return; }

    int baked = 0;
    for (uint32_t cp : SCRIPT_CPS) {
        if (!f->covers(cp)) continue;         // this machine's font lacks it
        const unsigned before = f->atlasRevision();
        const ResolvedGlyph g = f->resolve(cp);
        CHECK(static_cast<bool>(g));
        CHECK(g.font == f);                   // its own face carries it
        if (g.info->width > 0) {
            CHECK(uvSane(g));
            CHECK(f->atlasRevision() > before);   // new pixels -> new revision
            ++baked;
        }
        // The answer is cached: same entry, same UV, and nothing baked twice.
        const unsigned after = f->atlasRevision();
        const ResolvedGlyph again = f->resolve(cp);
        CHECK(again.info == g.info);
        CHECK(again.uv().x == g.uv().x && again.uv().y == g.uv().y);
        CHECK(f->atlasRevision() == after);
    }
    CHECK(baked > 0);   // at least one non-Latin script must reach the atlas

    // Measurement follows the same path, so Cyrillic has a real width instead
    // of collapsing the words behind it.
    const std::string word = utf8(0x415) + utf8(0x43E) + utf8(0x437);
    if (f->covers(0x415)) {
        CHECK(f->measure(word.c_str()) > 0.0f);
        CHECK(f->measure(word.c_str()) > f->measure(utf8(0x415).c_str()));
    }
}

// ---- fallback between faces ------------------------------------------------
void testFallback() {
    FontManager& fm = FontManager::instance();
    const Font* serif = fm.get("serif", 18);
    const Font* sans = fm.get("sans", 18);
    if (!serif || !sans) { std::cout << "  (no faces; skipping)\n"; return; }

    // The Dingbats block: DejaVu Serif carries nearly none of it, Sans does.
    int found = 0;
    for (uint32_t cp = 0x2701; cp <= 0x27BF && found < 3; ++cp) {
        if (serif->covers(cp) || !sans->covers(cp)) continue;
        const ResolvedGlyph g = serif->resolve(cp);
        if (!g || g.font == serif || g.info->width <= 0) continue;
        CHECK(g.font->family() == sans->family());
        CHECK(g.font->size() == serif->size());   // same bake size, comparable
        CHECK(uvSane(g));
        CHECK(serif->measure(utf8(cp).c_str()) > 0.0f);
        ++found;
    }
    CHECK(found > 0);
}

// ---- the .notdef placeholder -----------------------------------------------
void testNotdef() {
    const Font* f = FontManager::instance().get("sans", 19);
    if (!f) return;

    // Unassigned: no face anywhere can have it.
    const uint32_t unknown = 0x08FF;
    CHECK(!f->covers(unknown));
    const ResolvedGlyph g = f->resolve(unknown);
    CHECK(static_cast<bool>(g));
    CHECK(g.font == f);
    CHECK(g.info->advanceX > 0);      // it takes room, ...
    CHECK(g.info->width > 0);         // ... and it is visible
    CHECK(uvSane(g));

    // Every miss shares that one box, so unknown text cannot eat the reserve.
    const ResolvedGlyph other = f->resolve(0x08FE);
    CHECK(other.info == g.info);
    CHECK(other.uv().x == g.uv().x);
    CHECK(f->measure(utf8(0x08FE).c_str()) == f->measure(utf8(unknown).c_str()));
}

// ---- combining marks -------------------------------------------------------
void testCombiningMarks() {
    const Font* f = FontManager::instance().get("sans", 20);
    if (!f) return;

    CHECK(isCombiningMark(0x0301));
    CHECK(isCombiningMark(0x05B0));
    CHECK(!isCombiningMark('e'));

    // A mark advances nothing: the cluster measures as its base, so wrapping,
    // caret placement and selection all agree with what is drawn.
    const std::string base = "e";
    const std::string cluster = base + utf8(0x0301);
    CHECK(f->measure(cluster.c_str()) == f->measure(base.c_str()));
    CHECK(f->resolve(0x0301).mark);
    CHECK(f->glyph(0x0301).advanceX == 0);
    // The caret cannot stop between them: indexFromX only reports boundaries.
    const size_t idx = f->indexFromX(cluster.c_str(), 1000.0f);
    CHECK(idx == cluster.size());
}

// ---- line breaking ---------------------------------------------------------
void testLineBreaking() {
    const Font* f = FontManager::instance().get("sans", 21);
    if (!f) return;

    // ASCII still breaks on spaces, never inside a word.
    const std::string ascii = "the quick brown fox jumps over the lazy dog";
    const std::vector<TextLine> aLines = layoutText(ascii.c_str(), 90.0f, 21, "sans");
    CHECK(aLines.size() > 1);
    size_t consumed = 0;
    for (const TextLine& ln : aLines) {
        const std::string text = ascii.substr(ln.offset, ln.length);
        CHECK(text.empty() || text.front() != ' ');
        CHECK(text.empty() || text.back() != ' ');
        // Both edges of the line sit on a word boundary.
        if (ln.offset > 0) CHECK(ascii[ln.offset - 1] == ' ');
        if (ln.offset + ln.length < ascii.size())
            CHECK(ascii[ln.offset + ln.length] == ' ');
        consumed += ln.length;
    }
    CHECK(consumed > 0 && consumed <= ascii.size());

    // CJK has no spaces: every ideograph is a break opportunity.
    std::string cjk;
    for (int i = 0; i < 12; ++i) cjk += utf8(0x4E00 + static_cast<uint32_t>(i));
    const float unit = f->measure(utf8(0x4E00).c_str());
    CHECK(unit > 0.0f);
    const std::vector<TextLine> cLines = layoutText(cjk.c_str(), unit * 5.0f, 21, "sans");
    CHECK(cLines.size() >= 3);
    consumed = 0;
    for (const TextLine& ln : cLines) {
        CHECK(ln.length % 3 == 0);            // breaks land on code points
        CHECK(f->measure(cjk.substr(ln.offset, ln.length).c_str()) <= unit * 5.0f + 1.0f);
        consumed += ln.length;
    }
    CHECK(consumed == cjk.size());            // nothing dropped, nothing doubled

    // A full stop must not be stranded at the start of a line.
    std::string tail;
    for (int i = 0; i < 5; ++i) tail += utf8(0x4E00 + static_cast<uint32_t>(i));
    tail += utf8(0x3002);                      // 。 fits exactly one line over
    tail += utf8(0x3042);
    const std::vector<TextLine> tLines =
        layoutText(tail.c_str(), f->measure(tail.substr(0, 15).c_str()), 21, "sans");
    CHECK(tLines.size() >= 2);
    for (const TextLine& ln : tLines)
        if (ln.length > 0) CHECK(cpAt(tail, ln.offset) != 0x3002);
}

// ---- ink really reaches the framebuffer ------------------------------------
void testSoftwarePixels() {
    Rasterizer r;
    r.beginFrame(Rect{0, 0, 160, 40});
    // Mixed run: Latin plus a script the requested face may only reach through
    // the fallback chain.
    const std::string text = "Европа";
    r.drawText(text.c_str(), 4.0f, 4.0f, 24, "serif", Color{255, 255, 255, 255});
    r.endFrame();
    CHECK(!r.drawList().vertices.empty());

    SoftwareRenderer sr;
    sr.syncFonts();
    Framebuffer fb;
    fb.resize(160, 40);
    sr.clear(fb, Color{0, 0, 0, 255});
    std::vector<int> missing;
    sr.render(r.drawList(), fb, 1, &missing);

    size_t lit = 0;
    for (size_t i = 0; i + 3 < fb.px.size(); i += 4)
        if (fb.px[i] > 32) ++lit;
    CHECK(missing.empty());
    CHECK(lit > 60);
    std::cout << "  cyrillic ink: " << lit << " px\n";
}

} // namespace

int main() {
    testOnDemandBake();
    testFallback();
    testNotdef();
    testCombiningMarks();
    testLineBreaking();
    testSoftwarePixels();

    std::cout << (g_failures ? "unicode: FAILED\n" : "unicode: ok\n");
    return g_failures ? 1 : 0;
}
