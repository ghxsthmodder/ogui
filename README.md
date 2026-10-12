# ogui

GUIs for OpenGL applications, described in XML.

ogui is a small retained-mode UI library: you author a screen as an XML
document, ogui turns it into a widget tree, feeds your mouse/keyboard into
it, and hands you back a `DrawList` of 2D geometry. Rendering happens either
into a window owned by your host (`Kernel` + `GlBackend`) or one the library
creates for you (`ogui::Host`, a built-in Win32/WGL window and event loop).

* **XML layouts** — `<window>`, `<column>`, `<button>`, `<tabs>`, ... with
  named styles, authored in a text editor and hot-reloaded by re-parsing.
* **Input dispatch** — hover, capture (drag), focus, key routing, modifiers,
  click bindings, hover tooltips.
* **Text** — UTF-8 end to end: atlas glyphs, word wrap, caret + selection,
  horizontal scrolling of long lines, clipboard integration.
* **Two renderers for one DrawList** — `GlBackend` (OpenGL 3.3 core, every
  entry point resolved at runtime, so the library never links against GL)
  and `SoftwareRenderer` (pure CPU, for headless tests and screenshots).
* **Static library, few dependencies** — stb is vendored; the parser is a
  hand-rolled tokenizer with no third-party code.

## Requirements

* C++20 compiler (GCC 10+, Clang 12+, MSVC 2022), CMake 3.16+
* A system font (DejaVu, Liberation, ...). ogui looks in the usual
  directories for `sans` / `serif` / `mono`; see [Fonts](#fonts).
* For the demo: OpenGL 3.3 core (WGL on Windows, GLX/EGL elsewhere).

## Building

```sh
# Linux/macOS: configure, build, run the test suite and the headless demo
./scripts/build.sh

# Windows demo.exe cross-compiled with mingw (static, no DLL hell)
# -> written to debug/demo.exe together with the XML screens it loads
./scripts/build.sh win

# Remove build trees
./scripts/build.sh clean
```

`build.sh` is a thin wrapper over CMake; the raw equivalents are:

```sh
cmake -S . -B build -DOGUI_BUILD_EXAMPLES=ON -DOGUI_BUILD_TESTS=ON
cmake --build build -j
(cd build && ctest --output-on-failure)
```

On Windows with Visual Studio, `ogui.sln` / `ogui_demo.vcxproj` build the
Win32 + WGL demo directly (the project compiles the library sources itself).

### Options

| Option | Default | Effect |
| --- | --- | --- |
| `OGUI_BUILD_EXAMPLES` | `ON` | `ogui_demo`, `ogui_screenshot`, `ogui_demo_win32` |
| `OGUI_BUILD_TESTS` | `ON` | unit tests + golden-image test |
| `OGUI_USE_STATIC_STB` | `ON` | build stb_truetype/stb_image from `thirdparty/stb` |

## Tests

```sh
cd build && ctest --output-on-failure      # parser, drawlist, kernel, golden
```

* `parser` — XML tokenizer, attributes, styles, error reporting.
* `drawlist` — rasterizer geometry, text measurement and wrapping.
* `kernel` — input dispatch, focus/capture, text editing, tabs, tooltip.
* `golden` — renders a fixed layout through
  `Kernel -> DrawList -> SoftwareRenderer` and diffs it against
  `tests/golden/basic.png` (4/4/4 channel slack, 1% bad pixels allowed for
  FP rounding; it **skips** instead of failing when the resolved font files
  differ from the ones recorded in `tests/golden/basic.meta`).

Regenerate the golden after an intentional visual change:

```sh
OGUI_UPDATE_GOLDEN=1 ./build/ogui_golden_test
```

## Quick start

```cpp
#include "ogui/kernel.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/glbackend.hpp"

// 1. Layout -> widget tree
ogui::XmlParser parser;
auto res = parser.parseFile("ui.xml");
if (!res.root) { /* res.errors holds "line:col: message" */ }

auto screen = std::make_shared<ogui::Screen>("main");
screen->widgets.push_back(res.root);
screen->width  = 1280.f;
screen->height = 720.f;

// 2. Kernel
ogui::Kernel kernel;
kernel.init();
kernel.pushScreen(screen);
kernel.bind("onStart", []{ /* fired when a widget with onclick="onStart" is clicked */ });

// 3. GL backend (after your context is current)
ogui::GlBackend backend;
if (!backend.init(/*getProc=*/nullptr)) { /* backend.lastError() */ }

// 4. Every frame
ogui::InputState in;              // fill from your window proc
in.mouseX = ...; in.mouseY = ...;  in.mouseDown[0] = ...;
in.keyDown[...] = ...; in.lastChar = ...;  in.time = ...;  // seconds
kernel.frame(in);                 // events -> layout -> DrawList
backend.render(kernel.drawList(), fbWidth, fbHeight, /*flipY=*/false);
// swap your buffers
```

Or let the library own the window entirely (`ogui::Host`, currently Win32/WGL):

```cpp
#include "ogui/host.hpp"

ogui::Host host({.title = "ogui demo", .width = 800, .height = 600});
if (!host.open()) { /* host.lastError() */ }
host.run(kernel);   // owns input, resize, rendering and the event loop
```

`examples/demo.cpp` is this loop without a window (it prints what would be
rendered). `examples/demo_win32.cpp` shows the shortest path: `ogui::Host`
creates the Win32 window, the WGL context and the frame loop, so the demo is
just layout + handlers + `host.run(kernel)`. The host is DPI-aware
(per-monitor with a system fallback), honours `keyRepeat` for held keys, and
adds a few conveniences you opt into:

* `Settings::fullscreen` / `toggleFullscreen()` (and `F11` inside
  `run()`) — borderless fullscreen that restores the windowed placement.
* `Settings::iconPath` — an `.ico` loaded as the window icon.
* `setOnFilesDropped(...)` — Win32 drag & drop, paths arrive as UTF-8.
* system clipboard handlers installed automatically.
* the window lives in the normal Z-order band (not topmost), so it can be
  covered when minimised and never blocks taskbar apps while maximised;
  `F11` fullscreen is likewise a top *normal* window, not a topmost one.

`examples/screenshot.cpp` renders the same layout to a PNG with no GL at all.

The Win32 demo (`examples/demo_win32.cpp`) is a **gallery**: the available
widgets are grouped across several screens, all loaded into one `demo.exe`.
Which screens exist is **data, not code**: `manifest.xml` in the working
directory lists them in navigation order —

```xml
<manifest>
  <screen title="Basico" path="layout_demo.xml"/>
  <screen title="Texto"  path="layouts/layout_text.xml"/>
  ...
</manifest>
```

— so adding or removing a screen needs no recompile (a built-in list is the
fallback when the file is missing). `examples/gallery.hpp` parses it with the
library's own `XmlParser` (`<manifest>` is a data-only root tag; every widget
keeps the attributes it was authored with in `Widget::rawAttributes()`),
injects a shared `< Voltar` / `Avancar >` nav bar into each root and swaps
screens with `Kernel::switchScreen` (bindings key off widget ids, so the nav
works on every screen). `ogui_screenshot out.png W H [screenIndex]` renders
any one of those screens headless.

## Animations

`Widget::animateTo(target, duration, ease)` tweens bounds over the input
clock; `animateTo` calls stack smoothly (a new call snaps the in-flight tween
and animates from the current bounds). Combo box popups use it for their fade
open. `TweenEase` (linear, in/out cubic, ...) lives in `tween.hpp`.

## Visual states

Interactive widgets draw hover / pressed / focus feedback from
`Widget::isHovered()` / `isFocused()` (and `Kernel::mousePos()` for per-region
effects): buttons brighten on hover and darken on press with a faint top
highlight, checkboxes/radios tint their box, slider and range-slider thumbs
grow and round while grabbed, spinbox buttons highlight individually, combobox
borders accent and the arrow flips while open, and text inputs/spinboxes get a
2px accent focus ring. `shade()` / `withAlpha()` in `api.hpp` build the tints.

## XML reference

```xml
<screen id="main">
  <style name="primary" bg="#4488ff" color="#ffffff" radius="6"/>

  <window id="win" x="120" y="80" w="560" h="420" title="ogui demo"
          bg="#202020" border="#4488ff" borderwidth="1">
    <column id="col">
      <button id="start" style="primary" text="Iniciar"
              x="20" y="20" w="200" h="44" onclick="onStart"
              tooltip="Iniciar a simulacao"/>
      <textinput id="log" x="20" y="80" w="520" h="40" placeholder="..."/>
      <tabs x="20" y="140" w="520" h="120" theight="26" active="0">
        <tab label="Geral">
          <label text="conteudo" x="12" y="8" w="400" h="22"/>
        </tab>
        <tab label="Avancado">
          <label text="ajustes" x="12" y="8" w="400" h="22"/>
        </tab>
      </tabs>
    </column>
  </window>
</screen>
```

### Common attributes (every element)

| Attribute | Meaning |
| --- | --- |
| `id` | lookup key for `Kernel::findWidget/setText/getText` and `bind()` |
| `onclick` / `bind` | binding name resolved on click against `Kernel::bind` |
| `tooltip` | text shown by the hover tooltip after ~0.5 s |
| `x` `y` `w` `h` | bounds, **relative to the parent**; each accepts pixels (`20`), percent (`"50%"`) or — for `w`/`h` — `"fill"` (see below) |
| `visible` | `true/false` — an invisible widget (and its subtree) is skipped by drawing *and* input |
| `style` | name of a `<style>` declared earlier in the document |
| `bg` `color` `border` `bordercolor` `borderwidth` `radius` | colors/metrics |
| `padding` `margin` | layout knobs (`<column>` uses both) |
| `font` `fontsize` | `font="sans 20"` or the legacy `font sans 20 color "#66ee66"` |

### Relative geometry (resize / maximize)

Percent and fill components are re-resolved on every layout pass against
the parent's content area (the screen for top-level widgets, the inner
area below the title bar inside a `<window>`, the content width inside a
`<column>`), so resizing or maximizing the window reflows the tree:

```xml
<panel id="bg" x="0" y="0" w="100%" h="100%" bg="#161616"/>
<slider  id="sl" x="1.25%" y="292" w="92.5%" h="20"/>
```

Absolute components keep whatever the bounds currently hold, so runtime
moves (window dragging, splitter panes) survive untouched. A widget with
no percent/fill components behaves exactly as before.

Element-specific attributes win over the named style, which wins over the
theme default.

### Elements

| Tag | Purpose | Extra attributes |
| --- | --- | --- |
| `screen` | document root (one per file) | — |
| `manifest` | data-only root listing gallery screens (`<screen title path/>` children) | — |
| `style` | named style definition | `name` |
| `panel` | plain rectangle container | — |
| `window` | bordered panel with a title bar (children are offset by it) | `title`, `title_visible` |
| `column` | stacks children vertically (gap 4, honours `padding`/`margin`) | — |
| `row` | lays children out horizontally, tiling authored widths with a gap | — |
| `label` | text | `text`, `wrap`, `align="left\|center\|right"` |
| `button` | clickable button, press/release feedback | `text` |
| `textinput` | single-line UTF-8 editor: caret, click-drag / Shift+arrows / Shift+click selection, double-click selects the word, triple-click selects all, Ctrl+Left/Right jump words, Ctrl+A/C/X/V | `text`, `placeholder` |
| `textarea` | multiline UTF-8 editor: Enter inserts a newline, Up/Down navigate lines, selection spans lines (word/line drag, double/triple-click and Shift+click like `textinput`), long lines soft-wrap to the widget width, wheel scrolls, vertical scrollbar (drag the thumb or click the track) when content overflows | `text`, `placeholder` (style attrs as `textinput`) |
| `listbox` | scrollable list with single selection (click, wheel, Up/Down/Home/End) | `selected`, `rowheight`, `highlight` + `<item>` children |
| `item` | a `listbox` entry | `label` / `text`, `value` |
| `table` | data grid: header click sorts (re-click flips direction), row click selects, wheel + Up/Down/Home/End; drag a header edge to resize a `resizable` column, click a cell in an `editable` column (or press F2) to edit it inline (Enter saves, Esc cancels), overflowing cell text marquees per `scrollingtext` | `selected`, `rowheight`, `headerheight`, `sortable`, `headerbg`, `highlight` + `<th>`/`<tr>` children |
| `th` | a `table` column | `label` / `text`, `w` (0 = share leftover width), `resizable`, `editable`, `scrollingtext` (`automatic` / `onmouseenter`) |
| `tr` | a `table` row: cells from `cells="a;b"` or nested `<cell>` children | `cells` (`;`-separated), `value`, `editablecells` (`;`-separated column labels), `resizable`, `scrollingtext` |
| `cell` | a `tr` cell written as an element (`<cell>text</cell>`) | text content; `editable`, `resizable`, `scrollingtext` (override the column) |
| `tree` | hierarchical tree: triangle toggles expansion (rows compress/slide instead of blinking away), click selects, Left/Right collapse/expand; auto-shrinks to its visible rows when collapsed | `selected`, `rowheight`, `indent`, `autoheight` (default true), `accent`, `highlight` + nested `<treeitem>` children |
| `treeitem` | a `tree` node (may nest further `<treeitem>`s) | `label` / `text`, `value`, `expanded` |
| `checkbox` | toggle + label | `text`, `checked` |
| `radiobutton` | radio toggle in group | `text`, `checked`, `group` |
| `combobox` | dropdown: current value shown, popup lists `<option>` children (keyboard Up/Down/Enter, autoclose on outside press) | `selected` (index or `label=`), `rowheight`, `panel` (popup bg), `highlight`, `highlighttext`, `arrow` |
| `slider` | value in `min..max` (drag, arrows, Home/End) | `min`, `max`, `value`, `color` (bar), `bg` (track), `thumbcolor` |
| `spinbox` | numeric value with `-`/`+` buttons on each end, drawn as one unified widget (flat button regions + hairline separators); focused Up/Right/Down/Left step, Home/End clamp | `min`, `max`, `value`, `step`, `decimals`, `wrap` (cycles min↔max), `prefix`, `suffix` (or `text`), `bg`, `color` (text), `buttoncolor`, `bordercolor`/`border`, `font` |
| `optionspin` | text-only spin `[ < ][ option ][ > ]`: cycles through a fixed list of options as one unified widget; focused Right/Up forward, Left/Down back, Home/End first/last | `options` (comma-separated list), `value` (matching option text) or `index`, `wrap` (default true), `bg`, `color`, `buttoncolor`, `bordercolor`/`border`, `font` |
| `rangeslider` | two-thumb slider over a `[low, high]` sub-range; drag the nearest thumb (the band between is filled), focused Left/Right nudge low and Up/Down nudge high | `min`, `max`, `low`, `high`, `color` (band), `bg` (track), `thumbcolor` |
| `splitter` | two panes (first two children, placed in the splitter's own space) with a draggable divider kept at a `split` fraction; the panes resize as the handle is dragged | `orientation` (`"vertical"` = vertical divider/side-by-side panes (default), `"horizontal"` = stacked panes) or `vertical` (bool), `split` (0..1), `handlesize`, `handlecolor` |
| `progress` | read-only fill | `value` (0..1), `color`, `bg`, `radius` |
| `separator` | rule, horizontal or vertical | `thickness`, `color` |
| `tabs` / `tab` | header strip over stacked pages (inactive pages take no input) | `active`, `theight`, `headercolor`, `tabcolor`, `activecolor`, `accent` / `label` |
| `group` | titled grouping box | `name` |
| `image` | RGBA image file (PNG/JPG/… decoded by `ImageManager` via stb_image): stretched to the bounds, or repeated at native size with `tile`; `border` draws a frame on top; a missing file draws nothing | `src` (path, relative to the working directory), `tile` |
| `scrollview` | wheel-scrollable clipped viewport over its children | `scrollx`, `scrolly`, `scrollstep` |

## Fonts

Text is rasterized into an atlas with stb_truetype and drawn as textured
quads. Families are resolved by `ogui::FontManager`:

```cpp
ogui::FontManager::instance().setFontPath("sans", "/path/to/font.ttf");
ogui::FontManager::instance().addSearchDir("/opt/fonts");
```

With no explicit mapping, ogui probes the well-known file names for
`sans` (default), `serif` and `mono` inside `/usr/share/fonts`,
`/usr/local/share/fonts`, `~/.fonts` (Windows: `%WINDIR%\Fonts`). A family
that cannot be found simply produces no text — tests report it instead of
crashing.

Per-frame texture upload is handled by `GlBackend::render()` via
`syncFonts()` and `syncImages()`; hosts normally never call them
themselves. Internally a font's atlas is a stable **negative** slot id
in the `DrawList` (`0` = white 1×1 fallback, `>0` = image slot handed
out by `ImageManager::load()` — or a texture id the host created with
`createTexture()`).

## Input details

`InputState` is a flat struct the host fills once per frame:

* `mouseX/mouseY`, `mouseDown[4]`, `mouseButton`, `scrollX/scrollY`
* `keyDown[256]` / `keyRepeat[256]` indexed by `ogui::KeyCode`
* `lastChar` — one UTF-8 code point per frame (host deduplicates)
* `time` — seconds since start; drives the tooltip delay

`Kernel::frame()` recomputes hover, delivers press/release with capture
(dragging keeps working outside the widget), routes characters to the focus
widget and key edges with `Mod_Shift/Ctrl/Alt/Super`. Pointer events arrive
in screen coordinates; `Widget::absoluteOrigin()` converts bounds.

## Clipboard

`Ctrl+C/X/V` in a `textinput` go through the process-wide
`ogui::Clipboard`. The default keeps the text in memory (fine for tests);
windowing hosts should install platform handlers:

```cpp
#include "ogui/clipboard.hpp"
ogui::Clipboard::instance().setHandlers(getFromSystem, putToSystem);
```

`examples/demo_win32.cpp` wires it to `CF_UNICODETEXT`.

## Consuming ogui

```cmake
find_package(ogui CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE ogui::ogui)
```

Install it first (GNU layout, so any prefix works):

```sh
cmake --install build --prefix /usr/local
# or: cmake -DCMAKE_PREFIX_PATH=/opt/ogui ...
```

Alternatively add it to your build tree with `add_subdirectory(ogui)`.
The exported target carries the include path; stb stays an implementation
detail.

## Repository layout

```
include/ogui/     public headers (api, kernel, widgets, xmlparser, ...)
src/              library implementation
examples/         demo.cpp (headless), demo_win32.cpp (WGL gallery host),
                  gallery.hpp (multi-screen loader), screenshot.cpp
layouts/          extra gallery screens (text, selection, data, containers)
assets/           files the demo loads at runtime (checker.png for <image>)
manifest.xml      gallery screen list (edit to add/remove screens)
debug/            test package: demo.exe, its XML screens (synced by
                  `build.sh win`), MSVC project, screenshots/ — download this
                  folder whole via SFTP to test on Windows
tests/            unit tests + tests/golden/ (golden image + meta)
cmake/            CMake package config template
scripts/build.sh  host / win / clean
thirdparty/stb    vendored stb_truetype + stb_image[_write]
```

## Known limitations

* `row` tiles its children horizontally using their authored widths; it does
  not shrink or wrap overflowing rows.
* No complex text shaping (no HarfBuzz): Latin scripts and single-code-point
  advance only; combining marks are not positioned.
* One screen size per `Screen`; window resizing is the host's job
  (`screen->width/height`).
* `examples/screenshot.cpp` is a debugging aid, not a shipping renderer.
