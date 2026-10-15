// ogui_drawlist_test.cpp : validates the Rasterizer -> DrawList pipeline.
// Covers geometry emission, clipping, batch merging, dpi scaling and font
// atlas text.
#include "ogui/rasterizer.hpp"
#include "ogui/font.hpp"
#include "ogui/xmlparser.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

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

static void testRect() {
    Rasterizer r;
    r.reset();
    r.drawRect({10, 10, 100, 50}, Color{255, 0, 0, 255});

    CHECK(r.drawList().cmds.size() == 1);
    CHECK(r.drawList().indices.size() == 6);
    CHECK(r.drawList().vertices.size() == 4);

    const auto& v = r.drawList().vertices;
    CHECK(v[0].x == 10 && v[0].y == 10);
    CHECK(v[2].x == 110 && v[2].y == 60);
    CHECK(v[0].r == 1.0f && v[0].g == 0.0f);
}

static void testBatchMerging() {
    Rasterizer r;
    r.reset();
    // Same clip + same texture -> one batch, 12 indices.
    r.drawRect({0, 0, 10, 10}, Color{1, 2, 3, 255});
    r.drawRect({20, 0, 10, 10}, Color{4, 5, 6, 255});
    r.drawRect({40, 0, 10, 10}, Color{7, 8, 9, 255});
    CHECK(r.drawList().cmds.size() == 1);
    CHECK(r.drawList().indices.size() == 18);

    // Different texture -> new batch.
    r.drawRectUV({0, 0, 10, 10}, Rect{0, 0, 1, 1}, Color{255, 255, 255, 255}, 7);
    CHECK(r.drawList().cmds.size() == 2);
    CHECK(r.drawList().cmds[1].texId == 7);
    CHECK(r.drawList().cmds[1].indexOffset == 18);
}

static void testClip() {
    Rasterizer r;
    r.reset();
    r.beginFrame(Rect{0, 0, 50, 50});
    r.drawRect({0, 0, 100, 100}, Color{0, 0, 0, 255});
    CHECK(r.drawList().cmds.size() == 1);
    CHECK(r.drawList().cmds[0].clip.w == 50);
    CHECK(r.drawList().cmds[0].clip.h == 50);
    r.endFrame();

    // After endFrame the clip is restored to the initial (full) one, so a new
    // cmd starts instead of joining the clipped batch.
    r.drawRect({0, 0, 10, 10}, Color{0, 0, 0, 255});
    CHECK(r.drawList().cmds.back().clip.w == 4096);

    // Fully clipped geometry produces nothing.
    r.reset();
    r.beginFrame(Rect{200, 200, 10, 10});
    r.drawRect({0, 0, 10, 10}, Color{0, 0, 0, 255});
    r.endFrame();
    CHECK(r.drawList().empty());

    // Nested clips intersect.
    r.reset();
    r.beginFrame(Rect{0, 0, 100, 100});
    r.beginFrame(Rect{50, 50, 100, 100});
    r.drawRect({0, 0, 500, 500}, Color{0, 0, 0, 255});
    CHECK(r.drawList().cmds[0].clip.x == 50);
    CHECK(r.drawList().cmds[0].clip.w == 50);
    r.endFrame();
    r.endFrame();
}

static void testBorderAndLine() {
    Rasterizer r;
    r.reset();
    r.drawRectBorder({0, 0, 100, 40}, Color{0, 255, 0, 255}, 2.0f, 0.0f);
    CHECK(!r.drawList().empty());
    // 4 straight edges = 4 quads = 24 indices (no corners at radius 0).
    CHECK(r.drawList().indices.size() == 24);

    r.reset();
    r.drawLine({0, 0}, {100, 0}, Color{255, 255, 255, 255}, 4.0f);
    CHECK(r.drawList().indices.size() == 6);

    r.reset();
    r.drawTriangle({0, 0}, {10, 0}, {0, 10}, Color{1, 2, 3, 255});
    CHECK(r.drawList().indices.size() == 3);
}

static void testHexColors() {
    // Regression: hexColorHex() used to hand the whole hex run to strtol()
    // and truncate the result to 8 bits, which turned "#66ee66" into gray.
    Color c = hexColor("#66ee66");
    CHECK(c.r == 0x66 && c.g == 0xee && c.b == 0x66 && c.a == 0xff);

    c = hexColor("#ff00ff");
    CHECK(c.r == 0xff && c.g == 0x00 && c.b == 0xff);

    // Short form, with and without the '#'.
    c = hexColor("#abc");
    CHECK(c.r == 0xaa && c.g == 0xbb && c.b == 0xcc);
    c = hexColor("0f8");
    CHECK(c.r == 0x00 && c.g == 0xff && c.b == 0x88);

    // Long and short RGBA.
    c = hexColor("#11223344");
    CHECK(c.r == 0x11 && c.g == 0x22 && c.b == 0x33 && c.a == 0x44);
    c = hexColor("1234");
    CHECK(c.r == 0x11 && c.g == 0x22 && c.b == 0x33 && c.a == 0x44);

    // Garbage falls back to the documented opaque black.
    c = hexColor("zzz");
    CHECK(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0xff);
}

static void testDegenerate() {
    Rasterizer r;
    r.reset();
    r.drawRect({0, 0, 0, 10}, Color{0, 0, 0, 255});   // zero width
    r.drawRect({0, 0, 10, 10}, Color{0, 0, 0, 0});    // fully transparent
    CHECK(r.drawList().empty());
}

static void testFontAndText() {
    FontManager& fm = FontManager::instance();
    const Font* f = fm.get("sans", 16);
    if (!f) {
        std::cout << "  (no system font found; skipping text checks)\n";
        return;
    }
    CHECK(f->loaded());
    CHECK(f->atlasWidth() > 0);
    CHECK(f->atlasHeight() > 0);
    CHECK(f->atlasRowBytes() == f->atlasWidth());   // A8 coverage atlas
    CHECK(f->ascent() > 0);

    // Every ASCII glyph must have a sane atlas rect.
    for (uint32_t ch = 32; ch <= 126; ++ch) {
        Rect uv = f->glyphUV(ch);
        if (uv.w <= 0.0f || uv.h <= 0.0f) continue;   // missing glyph is ok
        CHECK(uv.x >= 0.0f && uv.y >= 0.0f);
        CHECK(uv.x + uv.w <= 1.0f + 1e-6f);
        CHECK(uv.y + uv.h <= 1.0f + 1e-6f);
    }

    // The atlas must actually contain ink. A8: every byte is coverage.
    const uint8_t* px = f->atlas();
    size_t inked = 0;
    const size_t atlasBytes = static_cast<size_t>(f->atlasWidth()) * f->atlasHeight();
    for (size_t i = 0; i < atlasBytes; ++i) {
        if (px[i] > 0) ++inked;
    }
    CHECK(inked > 100);

    // measure() must be positive and grow with the string.
    float w1 = f->measure("A");
    float w2 = f->measure("AAAA");
    CHECK(w1 > 0.0f);
    CHECK(w2 > w1);

    // A built atlas already carries a slot id, so glyphs reach the draw list
    // even though no GPU upload has happened yet.
    Rasterizer r;
    r.reset();
    bool ok = r.drawText("Hello", 0, 0, 16, "sans", Color{255, 255, 255, 255});
    CHECK(ok);
    const int slot = f->atlasTexture();
    CHECK(slot != 0);
    CHECK(r.drawList().cmds.size() == 1);
    CHECK(r.drawList().cmds[0].texId == slot);
    CHECK(r.drawList().vertices.size() == 5 * 4);   // 5 glyphs -> 5 quads

    // Slot 0 (atlas dropped): glyphs are skipped again.
    const_cast<Font*>(f)->setAtlasTexture(0);
    r.reset();
    ok = r.drawText("Hello", 0, 0, 16, "sans", Color{255, 255, 255, 255});
    CHECK(ok);
    CHECK(r.drawList().empty());

    // Whatever id the font carries is forwarded verbatim to the command.
    const_cast<Font*>(f)->setAtlasTexture(3);
    r.reset();
    ok = r.drawText("Hello", 0, 0, 16, "sans", Color{255, 255, 255, 255});
    CHECK(ok);
    CHECK(r.drawList().cmds.size() == 1);
    CHECK(r.drawList().cmds[0].texId == 3);
    CHECK(r.drawList().vertices.size() == 5 * 4);   // 5 glyphs -> 5 quads
    const_cast<Font*>(f)->setAtlasTexture(slot);
}

static void testTextMetrics() {
    // Regression: glyphs with no ink (space) were skipped before their
    // metrics were recorded, so advanceX stayed 0 and every word after a
    // space collapsed onto the previous one. Code points outside ASCII had
    // no atlas entry at all, with the same result (and a byte-wise loop
    // would split "ç" into two bogus glyphs).
    FontManager& fm = FontManager::instance();
    const Font* f = fm.get("sans", 16);
    if (!f) {
        std::cout << "  (no system font found; skipping text metric checks)\n";
        return;
    }

    const float space = static_cast<float>(f->glyph(' ').advanceX);
    CHECK(space > 0.0f);

    // "a a" is exactly one space wider than "aa".
    float wA = f->measure("aa");
    float wAS = f->measure("a a");
    CHECK(wAS > wA);
    CHECK(std::fabs((wAS - wA) - space) < 0.5f);

    // Accented Latin-1 glyphs are rasterized, not skipped.
    const GlyphInfo& cedilla = f->glyph(0xE7);   // c-cedilla
    CHECK(cedilla.width > 0 && cedilla.height > 0);
    CHECK(cedilla.advanceX > 0);

    // measure() decodes UTF-8 by code point: "ç" is one glyph, not two bytes.
    float wCedilla = f->measure("\xC3\xA7");
    CHECK(std::fabs(wCedilla - static_cast<float>(cedilla.advanceX)) < 0.5f);

    // The second word lands exactly one advance + one space to the right.
    Rasterizer r;
    r.reset();
    bool ok = r.drawText("a a", 10.0f, 20.0f, 16, "sans", Color{255, 255, 255, 255});
    CHECK(ok);
    CHECK(r.drawList().vertices.size() == 2 * 4);   // two inked glyphs
    const auto& verts = r.drawList().vertices;
    float dx = verts[4].x - verts[0].x;
    CHECK(std::fabs(dx - (static_cast<float>(f->glyph('a').advanceX) + space)) < 0.5f);

    // UTF-8 text yields one quad per code point: c, c-cedilla, a-tilde, o.
    r.reset();
    ok = r.drawText("c\xC3\xA3\xC3\xB5o", 0, 0, 16, "sans",
                    Color{255, 255, 255, 255});
    CHECK(ok);
    CHECK(r.drawList().vertices.size() == 4 * 4);
}

static void testLayoutDraw() {
    // Parse the sample layout and make sure drawing it yields geometry.
    ogui::XmlParser parser;
    std::string xml = R"(<screen id="main">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#161616"/>
  <window id="win" x="120" y="80" width="560" height="420" title="Demo">
    <button id="btn" text="Iniciar" x="0" y="0" width="200" height="50" background="#4488ff"/>
  </window>
</screen>)";
    auto res = parser.parse(xml);
    CHECK(res.root != nullptr);
    if (!res.root) return;

    Rasterizer r;
    r.reset();
    for (const auto& w : res.root->children()) w->onDraw(r);

    CHECK(r.drawList().vertices.size() >= 3 * 4);   // bg + window + button
    CHECK(!r.drawList().empty());
    // Every cmd must have a non-empty clip.
    for (const auto& c : r.drawList().cmds) {
        CHECK(c.indexCount > 0);
        CHECK(c.clip.w > 0 && c.clip.h > 0);
    }
}

static void testScaleEmission() {
    // Widgets author in logical units; the draw list the backends consume is
    // in device pixels. One multiply, applied to every emit path.
    Rasterizer r;
    r.setScale(2.0f);
    CHECK(r.scale() == 2.0f);
    r.reset();
    r.beginFrame(Rect{0, 0, 100, 50});

    r.drawRect({10, 10, 20, 10}, Color{1, 2, 3, 255});
    const auto& list = r.drawList();
    CHECK(list.vertices.size() == 4);
    CHECK(list.vertices[0].x == 20.0f && list.vertices[0].y == 20.0f);
    CHECK(list.vertices[2].x == 60.0f && list.vertices[2].y == 40.0f);
    CHECK(list.cmds[0].clip.x == 0.0f && list.cmds[0].clip.w == 200.0f);
    CHECK(list.cmds[0].clip.h == 100.0f);

    // Corner fans and borders go through the triangle path; their device
    // extents must match the same rect * 2.
    r.reset();
    r.beginFrame(Rect{0, 0, 100, 50});
    r.drawRoundedRect({0, 0, 10, 10}, Color{1, 1, 1, 255}, 4);
    r.drawRectBorder({0, 0, 20, 10}, Color{1, 1, 1, 255}, 1, 0);
    float maxX = 0.0f, maxY = 0.0f;
    for (const auto& v : r.drawList().vertices) {
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
    }
    CHECK(maxX == 40.0f);      // 20 logical * 2
    CHECK(maxY == 20.0f);      // 10 logical * 2
    r.endFrame();

    // A zero/negative scale falls back to 1 instead of collapsing the frame.
    r.setScale(0.0f);
    CHECK(r.scale() == 1.0f);

    // Text: the pen advances in logical units, so the two glyph quads of
    // "a a" keep the same distance whether or not the frame is scaled.
    const Font* f = FontManager::instance().get("sans", 16);
    if (!f) return;
    const float adv = f->measure("a ") ;
    r.setScale(2.0f);
    r.reset();
    r.beginFrame(Rect{0, 0, 400, 200});
    CHECK(r.drawText("a a", 10.0f, 20.0f, 16, "sans", Color{255, 255, 255, 255}));
    const auto& tv = r.drawList().vertices;
    CHECK(tv.size() >= 8);
    // First glyph box starts at logical 10 -> device 20 (plus the side bearing,
    // which the atlas carries in device pixels of its own bake).
    CHECK(tv[0].x >= 20.0f - 2.0f && tv[0].x <= 20.0f + 8.0f);
    const float gap = tv[4].x - tv[0].x;
    CHECK(std::fabs(gap - adv * 2.0f) < 4.0f);   // one advance, in device px
    r.endFrame();
    r.setScale(1.0f);
    FontManager::instance().setScale(1.0f);
}

static void testScaledFontMetrics() {
    FontManager& fm = FontManager::instance();
    fm.setScale(1.0f);
    const Font* one = fm.get("sans", 16);
    if (!one) {
        std::cout << "  (no system font found; skipping scale metric checks)\n";
        return;
    }
    const float logicalWidth = one->measure("hello world");
    const float ascent = one->ascent();
    const int atlasW = one->atlasWidth();
    const int atlasH = one->atlasHeight();
    CHECK(one->pxPerUnit() == 1.0f);

    // The same request at 2x bakes a bigger atlas but reports the same
    // logical metrics, so layout code is scale-blind.
    fm.setScale(2.0f);
    const Font* two = fm.get("sans", 16);
    if (!two) { fm.setScale(1.0f); return; }
    CHECK(two->pxPerUnit() > 1.9f && two->pxPerUnit() < 2.1f);
    CHECK(two->size() == 16);                       // logical size unchanged
    CHECK(two->atlasWidth() > atlasW);
    CHECK(two->atlasHeight() > atlasH);
    CHECK(std::fabs(two->measure("hello world") - logicalWidth) < 1.0f);
    CHECK(std::fabs(two->ascent() - ascent) < 1.0f);
    // A click 40 logical px into the string maps to the same caret index.
    CHECK(two->indexFromX("hello world", 40.0f) == one->indexFromX("hello world", 40.0f));
    fm.setScale(1.0f);
}

// A scale change re-bakes every cached face where it stands, so a font a host
// is holding stays the font the manager hands out: same object, new bake, and
// tables rebuilt from scratch instead of carried over from the last one.
static void testFontSurvivesScaleChange() {
    FontManager& fm = FontManager::instance();
    fm.setScale(1.0f);
    const Font* held = fm.get("sans", 16);
    if (!held) {
        std::cout << "  (no system font found; skipping font lifetime checks)\n";
        return;
    }
    const std::string path = held->path();
    const float logical = held->measure("hello world");

    fm.setScale(3.0f);
    CHECK(fm.get("sans", 16) == held);            // the very same object
    CHECK(held->pxPerUnit() > 2.9f);              // re-baked at the new factor
    CHECK(held->loaded() && held->atlasWidth() > 0);
    CHECK(std::fabs(held->measure("hello world") - logical) < 1.0f);
    CHECK(held->indexFromX("hello world", 40.0f) > 0);   // and still editable

    // Back down: a pair whose kerning rounds to nothing at 1x must not keep the
    // 3x value left in the table, so every printable pair is compared against a
    // face loaded fresh at this scale.
    fm.setScale(1.0f);
    CHECK(held->pxPerUnit() == 1.0f);
    Font fresh;
    CHECK(fresh.load(path, 16, 96, 1.0f));
    int pairs = 0;
    for (uint32_t a = 32; a <= 126; ++a) {
        for (uint32_t b = 32; b <= 126; ++b) {
            const float got = held->kern(a, b);
            const float want = fresh.kern(a, b);
            if (got != 0.0f || want != 0.0f) ++pairs;
            CHECK(std::fabs(got - want) < 1e-3f);
        }
    }
    if (pairs == 0)
        std::cout << "  (this face carries no ASCII kern pairs; table reset unchecked)\n";
    CHECK(std::fabs(held->measure("hello world") - logical) < 1.0f);
}

static void testIndexFromXAndMeasureRange() {
    FontManager& fm = FontManager::instance();
    const Font* f = fm.get("sans", 16);
    if (!f) {
        std::cout << "  (no system font found; skipping caret/measure checks)\n";
        return;
    }

    // measureRange must agree with measure() over the same prefix, and a
    // range starting mid-string is just measuring the suffix.
    const std::string t = "abcdef";
    CHECK(std::fabs(f->measure(t.c_str()) - f->measureRange(t.c_str(), t.size())) < 1e-3f);
    CHECK(std::fabs(f->measure("abc") - f->measureRange(t.c_str(), 3)) < 1e-3f);
    CHECK(std::fabs(f->measure("def") - f->measureRange(t.c_str() + 3, 3)) < 1e-3f);
    CHECK(f->measureRange(t.c_str(), 0) == 0.0f);

    // indexFromX clamps outside the string and snaps inside a glyph to one
    // of its boundaries (never to a byte that splits a character).
    CHECK(f->indexFromX(t.c_str(), -50.0f) == 0);
    CHECK(f->indexFromX(t.c_str(), 0.0f) == 0);
    CHECK(f->indexFromX(t.c_str(), f->measure(t.c_str()) + 50.0f) == t.size());
    const float aWidth = static_cast<float>(f->glyph('a').advanceX);
    const size_t mid = f->indexFromX(t.c_str(), aWidth * 0.5f);
    CHECK(mid == 0 || mid == 1);

    // UTF-8: the caret never lands inside a multi-byte character.
    const std::string accented = "a\xC3\xA7o";   // a-ç-o (4 bytes)
    for (float x = -2.0f; x < f->measure(accented.c_str()) + 4.0f; x += 1.0f) {
        const size_t idx = f->indexFromX(accented.c_str(), x);
        CHECK(idx <= accented.size());
        if (idx < accented.size())
            CHECK((static_cast<unsigned char>(accented[idx]) & 0xC0) != 0x80);
    }
}

static void testTextWrapLayout() {
    FontManager& fm = FontManager::instance();
    const Font* f = fm.get("sans", 16);
    if (!f) {
        std::cout << "  (no system font found; skipping wrap checks)\n";
        return;
    }

    const std::string s = "the quick brown fox jumps over the lazy dog";

    // maxWidth == 0 keeps everything on one line when there is no '\n'.
    auto one = layoutText(s.c_str(), 0.0f, 16, "sans");
    CHECK(one.size() == 1);
    CHECK(one[0].offset == 0 && one[0].length == s.size());

    auto wide = layoutText(s.c_str(), 400.0f, 16, "sans");
    auto narrow = layoutText(s.c_str(), 100.0f, 16, "sans");
    CHECK(wide.size() >= 1);
    CHECK(narrow.size() > wide.size());

    // Lines are ordered byte ranges of the original and each one fits.
    for (size_t i = 0; i < narrow.size(); ++i) {
        const TextLine& ln = narrow[i];
        CHECK(ln.offset + ln.length <= s.size());
        const std::string piece = s.substr(ln.offset, ln.length);
        CHECK(f->measure(piece.c_str()) <= 100.0f + 1.0f);
        if (i > 0)
            CHECK(ln.offset >= narrow[i - 1].offset + narrow[i - 1].length);
    }

    // '\n' always breaks, even with room to spare.
    auto brk = layoutText("a\nbb", 400.0f, 16, "sans");
    CHECK(brk.size() == 2);
    CHECK(brk[0].offset == 0 && brk[0].length == 1);
    CHECK(brk[1].offset == 2 && brk[1].length == 2);

    const float lineH = textLineHeight(16, "sans");
    CHECK(lineH > 0.0f);

    // drawTextWrapped lays out with the same algorithm: one quad per inked
    // code point across all lines, and every quad inside the box.
    Rasterizer r;
    r.reset();
    const bool ok = r.drawTextWrapped(s.c_str(), 0.0f, 0.0f, 100.0f, 16, "sans",
                                      Color{255, 255, 255, 255});
    CHECK(ok);
    size_t expected = 0;
    for (const TextLine& ln : narrow)
        for (size_t i = 0; i < ln.length; ++i)
            if (s[ln.offset + i] != ' ') ++expected;   // spaces have no ink
    const auto& verts = r.drawList().vertices;
    CHECK(verts.size() == expected * 4);

    bool below = false;
    for (size_t i = 0; i + 3 < verts.size(); i += 4) {
        const float x0 = verts[i].x < verts[i + 2].x ? verts[i].x : verts[i + 2].x;
        const float x1 = verts[i].x < verts[i + 2].x ? verts[i + 2].x : verts[i].x;
        CHECK(x0 >= -1.0f);
        CHECK(x1 <= 100.0f + 1.0f);
        if (verts[i].y > verts[0].y + 1.0f) below = true;
    }
    CHECK(below);   // wrapped lines move down
}

int main() {
    testRect();
    testBatchMerging();
    testClip();
    testBorderAndLine();
    testHexColors();
    testDegenerate();
    testFontAndText();
    testTextMetrics();
    testScaleEmission();
    testScaledFontMetrics();
    testFontSurvivesScaleChange();
    testIndexFromXAndMeasureRange();
    testTextWrapLayout();
    testLayoutDraw();

    if (g_failures) {
        std::cerr << "drawlist_test: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "drawlist_test: OK\n";
    return 0;
}
