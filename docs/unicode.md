# Text and Unicode

ogui draws text without a shaping engine. This page documents what the
text stack actually does — how a code point becomes a pixel, what happens
when a font does not have a glyph, how line breaking decides where to cut —
and the evaluation behind the decision to stop short of HarfBuzz for now.

Everything here is exercised by `ctest` under the test name `unicode`
(`debug/tests/unicode_test.cpp`), which asserts behaviour against *coverage*
rather than against one specific installed font, so it stays meaningful on a
machine whose fonts differ.

## The model

Text is a UTF-8 `std::string` from beginning to end: XML attributes, Lua
strings, `Widget::text()`, the draw list. Nothing in the library reencodes
it. Code points are decoded with `ogui::utf8Next()`, which consumes stray
continuation bytes one at a time so malformed input degrades into visible
junk instead of stalling or reading past the end.

Three rules keep the stack consistent:

* **A glyph is baked the first time it is needed.** `Font::load()` rasterizes
  the eagerly baked ranges (printable ASCII, Latin-1, Latin Extended-A,
  general punctuation, currency) and reserves the rest of the atlas.
  `Font::resolve(cp)` bakes anything else into a reserved cell on first use
  and caches the answer, so the steady-state cost is one map lookup.
* **Cells never move.** The atlas is allocated once, reserve included, and
  never reallocated. A glyph baked in the tenth minute of a session holds the
  UV it had in the first, which is what lets `SoftwareRenderer` cache the raw
  buffer pointer and what keeps a draw list built earlier in the frame valid.
* **Everything a widget sees is logical units.** Advances and ink boxes live
  in atlas pixels; the draw loop divides by `Font::pxPerUnit()` exactly once.

```cpp
const Font* f = ogui::FontManager::instance().get("sans", 16);
ogui::ResolvedGlyph g = f->resolve(0x0415);   // Cyrillic capital IE
g.font;        // the face that owns the bitmap — may be a fallback
g.info;        // advance, ink box, atlas position
g.mark;        // true for a combining mark
g.uv();        // normalized rect inside g.font->atlas()
```

`ResolvedGlyph::font` is the reason the field exists: a fallback glyph is
baked into the *fallback* face's atlas, so the texture id and the UV
normalization have to come from that face, not from the one the widget asked
for. `Rasterizer::drawTextShifted()` reads both per glyph.

## Fallback between faces

A code point the requested face does not carry walks
`FontManager::fallbacks()` — by default `sans`, `serif`, `mono` — and lands
in the first face that has an outline for it. Hosts and themes extend the
chain the same way they register a family:

```cpp
auto& fm = ogui::FontManager::instance();
fm.setFontPath("noto cjk", "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
fm.addFallback("noto cjk");
```

Fallback faces are fetched at the *same* logical size and the same device
scale, so their advances are directly comparable and `measure()` stays
correct across a script change. The chain is walked once per code point and
the result is cached, including the misses.

When nothing carries the code point, `Font::notdef()` bakes one hollow box
about an em wide and reuses it for every unknown character. Missing text is
therefore visible and correctly measured instead of collapsing into the
space glyph and running the words behind it together. Combining marks are the
one exception: a mark nobody has stays silent, because a box over a letter is
worse than a missing accent.

Coverage is known for the whole BMP (65 536 code points), scanned once per
file at load and shared by every size loaded from it. Higher planes report
`covers() == false` and take the fallback chain, which on a machine without a
non-BMP font ends at the `.notdef` box.

## Combining marks and clusters

`ogui::isCombiningMark()` covers the marks the fonts on a typical system can
actually reach — Latin, Cyrillic, Greek, Hebrew, Arabic, Thai, Myanmar, CJK
kana and the symbol/variation blocks. A mark is baked with a zero advance and
drawn centred over the base glyph that precedes it, so `é` written as
`e` + U+0301 measures exactly as wide as `e` and stays in one piece when the
line wraps.

The text widgets move the caret by *cluster*, not by code point:
`utf8NextBoundary()`/`utf8PrevBoundary()` in `src/widgets_internal.hpp` walk
over trailing marks, so one Backspace removes `e` + U+0301 together and one
Left never parks the caret between them.

## Measurement and line breaking

`Font::measure()`, `Font::measureRange()`, `Font::indexFromX()` and
`layoutText()` all resolve through the same path, so a width measured for
wrapping is the width the draw loop produces, whatever face each glyph ended
up in.

`layoutText()` breaks a paragraph into `[offset, length]` byte ranges:

* newlines start a line and an empty line keeps its row;
* ASCII space and tab break a line, U+00A0 (NBSP) does not;
* every CJK ideograph, kana and Hangul syllable is its own break
  opportunity, because those scripts are written without spaces;
* closing punctuation (、。〉」』】，．：；！？［］｛｝ and the small
  kana/iteration marks) is pulled onto the line it closes rather than stranded
  at the start of the next;
* a word longer than the line is hard-broken at the last code point that
  fitted, so an oversized token advances instead of looping.

## What is *not* implemented, and why

**Shaping.** There is no GSUB/GPOS processing: no required ligatures
(`fi`, `ffl`), no contextual forms, no mark attachment from anchor tables, no
kerning outside the printable-ASCII pairs baked at load. In practice that
means:

* Latin, Greek and Cyrillic look right — they need almost no shaping.
* Hebrew renders correctly letter-for-letter (it needs no contextual forms),
  in logical order.
* **Arabic renders in isolated forms**, which is wrong: every Arabic
  joiner/substitutor rule lives in GSUB. Same for Devanagari and other Indic
  scripts, and for Thai below-position vowels.
* Emoji sequences (ZWJ, variation selectors) are not assembled.

**Bidirectional text.** Runs are laid out in logical order left to right, so a
mixed Latin + Arabic/Hebrew line shows the RTL words in reverse order and
punctuation on the wrong side. `layoutText()` has no paragraph direction and
no `dir` attribute.

**The evaluation.** Shaping and bidi are not problems with a small correct
solution; they are the reason HarfBuzz and fribidi exist.

| Option | Cost | Notes |
| --- | --- | --- |
| HarfBuzz + FreeType | ~1 MB of code, two dependencies, a second font-loading path | The reference answer. FreeType duplicates what stb_truetype already does here, and the atlas/UV model would have to be rebuilt around `hb_buffer_t` runs. |
| HarfBuzz with a custom `hb_font_funcs` on stb | ~1 MB, one dependency | Keeps stb as the only rasterizer. HarfBuzz only needs glyph metrics + outline-free funcs for advance/position work, so this is the shape that would fit ogui. Still the largest single dependency the project would carry. |
| Roll our own GSUB subset | Weeks, and a correctness trap | A partial `liga`/`calt`/`rlig` implementation is worse than none: it works on the test fonts and produces wrong text on real ones. |
| Nothing, document it | 0 | What shipped. Latin/Greek/Cyrillic/Hebrew are correct; Arabic/Indic are visibly wrong and say so. |

Decision: **not yet**, and the trigger is concrete. The pieces that made
international text impossible — coverage, fallback, measurement, breaking,
clusters — are now in place and dependency-free, so a shaping pass can be
added later as one more step in `Font::resolve()` without touching anything
else. Add HarfBuzz when a real layout needs Arabic or Devanagari; until then
the missing forms are a documented limitation rather than a silent one, and
the binary stays a single static library with stb and Lua in it.
