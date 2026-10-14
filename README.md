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
* **Visual states** — declarative `hover:`/`pressed:`/`focus:`/`checked:`/
  `selected:`/`disabled:` overlays on colors, border width and radius, with
  optional `transition="0.2"` blending — no script required.
* **Text** — UTF-8 end to end: atlas glyphs, word wrap, caret + selection,
  horizontal scrolling of long lines, clipboard integration.
* **Two renderers for one DrawList** — `GlBackend` (OpenGL 3.3 core, every
  entry point resolved at runtime, so the library never links against GL)
  and `SoftwareRenderer` (pure CPU, for headless tests and screenshots).
* **Scripting** — screens embed Lua 5.4 chunks (`<script>`); the engine
  owns the loop and calls the script's `ogui_on_load`/`ogui_on_show`/`ogui_on_update`/`ogui_on_click`/
  `ogui_on_event` hooks through the `ogui` API table: widget attributes,
  input queries (`mouse`/`key`/`mods`), focus/hover, list content
  (full `items` or incremental `ogui.list` insert/remove/set),
  `ogui.prop` reads/writes `propertygrid` models by name,
  `ogui.cmd` drives `commandpalette` models and histories,
  engine-driven `tween` animations, `after`/`every` timers, dynamic
  widget creation (`add`/`remove`/`clear`), tree navigation
  (`children`/`parent`/`index`), per-widget event
  listeners (`ogui.on`/`ogui.off`) and declarative data bindings
  (`ogui.bind`/`ogui.unbind`).
* **Static library, few dependencies** — stb and Lua are vendored; the
  parser is a hand-rolled tokenizer with no third-party code.

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
| `OGUI_USE_LUA` | `ON` | embed the vendored Lua 5.4 (`thirdparty/lua`) for `<script>` screens |

## Tests

```sh
cd build && ctest --output-on-failure      # parser, drawlist, kernel, screens, scrollbar, script, golden
```

* `parser` — XML tokenizer, attributes, styles, error reporting.
* `drawlist` — rasterizer geometry, text measurement and wrapping.
* `kernel` — input dispatch, focus/capture, text editing, tabs, tooltip.
* `screens` — every manifest screen parses in both themes and keeps the ids the gallery binds to.
* `scrollbar` — presses on the listbox/treeview/table/scrollview/window/textarea bars scroll instead of selecting or hitting children; dropdown popups fit the screen and scroll.
* `script` — `<script>` collection, screen-bound Lua loading on screen
  switches, all four hooks and the `ogui.*` widget API; error capture.
* `golden` — renders a fixed `@token` layout through
  `Kernel -> DrawList -> SoftwareRenderer` in both themes and diffs it
  against `debug/tests/golden/basic.png` (dark) and `debug/tests/golden/basic_light.png`
  (4/4/4 channel slack, 1% bad pixels allowed for FP rounding; it **skips**
  instead of failing when the resolved font files differ from the ones
  recorded in the matching `.meta`).

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
kernel.bind("onStart", []{ /* fired when a widget with onClick="onStart" is clicked */ });

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
<manifest theme="dark">
  <screen title="Basico" path="layout_demo.xml"/>
  <screen title="Texto"  path="layouts/layout_text.xml"/>
  ...
</manifest>
```

— so adding or removing a screen needs no recompile (a built-in list is the
fallback when the file is missing). `theme="dark|light"` (`escuro`/`claro`
also accepted) picks the initial palette — or name a `<theme>` stylesheet
file (e.g. `theme="themes/game.xml"`) to layer custom tokens, named styles
and fonts over dark; the nav bar toggle still flips the preset at runtime.
`examples/gallery.hpp` parses it with the
library's own `XmlParser` (`<manifest>` is a data-only root tag; every widget
keeps the attributes it was authored with in `Widget::rawAttributes()`),
injects a shared `< Voltar` / `Avancar >` nav bar (plus a `Tema claro`
switch that flips `darkTheme()`/`lightTheme()` and re-parses every screen)
into each root and swaps
screens with `Kernel::switchScreen` (bindings key off widget ids, so the nav
works on every screen). `ogui_screenshot out.png W H [screenIndex] [light]`
renders any one of those screens headless, optionally with the light theme.

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

The complete element/attribute catalogue — every registered tag, its typed
attributes, `@token` palette values for both themes and the named-style
rules — lives in [`docs/xml-reference.md`](docs/xml-reference.md). Quick
intro below.

Attribute names are full camelCase (`background`, `width`, `height`,
`rowHeight`, `borderWidth`, `fontSize`, `onClick`, …) and matching is
case-insensitive (`rowHeight` ≡ `rowheight` ≡ `ROWHEIGHT`). The old
single-word abbreviations (`bg`, `w`, `h`, `cols`, `src`, `theight`, `panelw`,
`panelh`, `headerbg`) are not recognised — use the full spelling.
Conventional names that are not abbreviations stay as-is: `x`, `y`, `id`,
`min`, `max`, `color`, `text`, `value`, `label`, `title`.

```xml
<screen id="main">
  <style name="primary" background="#4488ff" color="#ffffff" radius="6"/>

  <window id="win" x="120" y="80" width="560" height="420" title="ogui demo"
          background="#202020" border="#4488ff" borderWidth="1">
    <column id="col">
      <button id="start" style="primary" text="Iniciar"
              x="20" y="20" width="200" height="44" onClick="onStart"
              tooltip="Iniciar a simulacao"/>
      <textinput id="log" x="20" y="80" width="520" height="40" placeholder="..."/>
      <tabs x="20" y="140" width="520" height="120" tabHeight="26" active="0">
        <tab label="Geral">
          <label text="conteudo" x="12" y="8" width="400" height="22"/>
        </tab>
        <tab label="Avancado">
          <label text="ajustes" x="12" y="8" width="400" height="22"/>
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
| `onClick` / `bind` | binding name resolved on click against `Kernel::bind` |
| `tooltip` | text shown by the hover tooltip after ~0.5 s |
| `x` `y` `width` `height` | bounds, **relative to the parent**; each accepts pixels (`20`), percent (`"50%"`) or — for `width`/`height` — `"fill"` (see below) |
| `anchor` | pin edges across parent resizes: `anchor="right"`, `"l,r"`, `"top,bottom"` etc. (`left/right/top/bottom`, short forms `l/r/t/b`, space/comma-separated) |
| `minWidth` `minHeight` `maxWidth` `maxHeight` | pixel clamp on the resolved size, applied on every layout pass whatever unit `width`/`height` used (`"50%"` + `maxWidth="500"` grows up to 500 px; `"10%"` + `minWidth="200"` never shrinks below) |
| `weight` | flex share inside a `column` (leftover height) or `row` (leftover width), proportionally to the other weighted children; 0 keeps the authored size |
| `visible` | `true/false` — an invisible widget (and its subtree) is skipped by drawing *and* input |
| `style` | named style(s) applied to the element; a comma/space list stacks (`"a,b"`, later wins) |
| `background` `color` `border` `borderColor` `borderWidth` `radius` | colors/metrics |
| `enabled` / `disabled` | `false`/`true` — an inert widget takes no hover/press/focus and is skipped by hit-testing (subtree included) |
| `hover:background` `pressed:color` `focus:border` `disabled:*` `checked:*` `selected:*` | per-state visual overlays; the plain attribute is the `normal` base, priority `disabled > pressed > hover > selected > checked > focus` (see `docs/xml-reference.md`) |
| `transition` | seconds to blend visual-state changes (linear, interruptible) |
| `shadow` | soft drop shadow: `blur` or `blur,dy` or `blur,dx,dy` (pixels) |
| `shadowColor` | shadow color (default `#000000`), e.g. `shadowColor="#000000"` |
| `gradient` | vertical background gradient end color: `background` stays the top color; `gradientDirection="horizontal"` ramps left→right instead |
| `padding` `margin` | layout knobs (`<column>` uses both) |
| `gap` | `column`/`row` child spacing in px (falls back to `margin`, then the default 4) |
| `font` `fontSize` | `font="sans 20"` or the legacy `font sans 20 color "#66ee66"` |

### Theme & palette

Widget chrome resolves through a `Palette` token set (surface, border, text,
accent, …) instead of fixed colors: pass a `Theme` to `XmlParser::parse/parseFile`
and it becomes the active theme (`ogui::setActiveTheme`), so the whole tree —
including tooltips, toasts and popups — picks up the new look. The theme's
`defaultStyle` seeds every widget before its own attributes are applied.

Any colour attribute also accepts `@token` instead of a hex literal — the
token is resolved against the active palette at parse time:

```xml
<panel id="bg" x="0" y="0" width="100%" height="100%" background="@screen"/>
<label text="Title" color="@textBright"/>
```

Tokens: `window screen surface surfaceHi surfaceLo border borderStrong track
guide text textBright white dim focus accent danger info warn error overlay
shadow` (case-insensitive; unknown names fall back to `window`).

Two presets ship with the library: `ogui::darkTheme()` (the default palette)
and `ogui::lightTheme()`. Because `@tokens` and widget defaults are resolved
while parsing, switching themes at runtime means re-installing the preset and
re-parsing the layouts; the demo's nav bar does exactly that with its
"Tema claro" toggle (binding `navTheme`).

### Relative geometry (resize / maximize)

Percent and fill components are re-resolved on every layout pass against
the parent's content area (the screen for top-level widgets, the inner
area below the title bar inside a `<window>`, the content width inside a
`<column>`), so resizing or maximizing the window reflows the tree:

```xml
<panel id="bg" x="0" y="0" width="100%" height="100%" background="#161616"/>
<slider  id="sl" x="1.25%" y="292" width="92.5%" height="20"/>
```

Absolute components keep whatever the bounds currently hold, so runtime
moves (window dragging, splitter panes) survive untouched. A widget with
no percent/fill components behaves exactly as before.

`minWidth`/`minHeight`/`maxWidth`/`maxHeight` clamp the resolved size in
pixels on every layout pass, independent of the unit `width`/`height` were
authored in:

```xml
<!-- grows with the parent, capped at 500px; never below 200px -->
<panel id="card" x="0" y="0" width="50%" height="80" minWidth="200" maxWidth="500"/>
```

Authored absolute sizes are clamped at parse time too.

### Anchors

`anchor` pins the distance between a widget edge and the parent's
matching edge. The margins are captured on the first layout pass (after
percent/fill resolution, so the authored units don't matter) and held for
the rest of the session:

```xml
<!-- stays 20px from the navbar's right edge, 120px wide -->
<button id="exit" x="700" y="6" width="120" height="20" anchor="top,right"/>
<!-- stretches with the parent, keeping a 10px gap on each side -->
<panel id="band" x="10" y="100" width="620" height="40" anchor="l,r"/>
```

Anchoring both edges of an axis keeps both margins and stretches the
widget between them (the anchored size replaces any percent/`fill`
component on that axis). Anchoring a single edge moves the widget so that
edge's margin is preserved, while its size keeps following its own spec.
Axes without anchor bits behave exactly as plain relative geometry.

Element-specific attributes win over the named style, which wins over the
theme default.

### Elements

| Tag | Purpose | Extra attributes |
| --- | --- | --- |
| `screen` | document root (one per file) | — |
| `manifest` | data-only root listing gallery screens (`<screen title path/>` children) | `theme` (`dark`/`light`, initial palette) |
| `style` | named style definition (`name`) or element-wide defaults (`element="button"`) applied under own attributes and named styles | `name` / `element` |
| `default` | inside `<theme>` only: base Style overrides (`font`, `fontSize`, `color`, `background`, ...) | — |
| `font` | registers a font family for later `font="..."` use (data-only, no widget) | `family`, `source` (TTF/OTF path) |
| `theme` | stylesheet root: shared palette + named styles + fonts (`base="dark\|light"` preset it layers over) | `name`, `base` |
| `palette` | inside `<theme>` only: re-colours tokens (`accent="#e8b33c" ...`); unknown tokens warn | token names |
| `stylesheet` | merges an external `<theme>` file into the current document (named styles, fonts, palette) | `path` |
| `script` | Lua chunk for the screen — `<script src="file.lua"/>` or inline `<script>code</script>`; hooks `ogui_on_load`/`ogui_on_update`/`ogui_on_click`/`ogui_on_event` + `ogui` API (see [xml-reference](docs/xml-reference.md)) | `src` |
| `panel` | plain rectangle container | — |
| `window` | bordered panel with a title bar (children are offset by it) | `title`, `titleVisible` |
| `column` | stacks children vertically (gap 4 or `gap`, honours `padding`/`margin`); stretches to the parent's width | `gap`, `align` (`center`/`right` place children horizontally, `left` keeps the authored x), `weight` on children (split leftover height) |
| `row` | lays children out horizontally, tiling authored widths with a gap | `gap`, `valign` (`middle`/`bottom` place shorter children against the tallest), `wrap` (`true`: children past the row width flow to a new line, growing the height), `weight` on children (split leftover width when the row has an authored width or room from its parent) |
| `grid` | row-major tiling over column tracks; children fill each cell origin (child `x`/`y` are ignored, percent/fill resolve against the cell) | `columns` (`"3"` = 3 equal tracks, or a list like `"120 1fr 25%"`: px, fractional, percent), `gap` (px, default 4), `rowHeight` (px; default is auto = tallest child of the row) |
| `label` | text | `text`, `wrap`, `align="left\|center\|right"` |
| `button` | clickable button, press/release feedback | `text`, `align="left\|center\|right"`, `textPadding` (inset for left/right) |
| `textinput` | single-line UTF-8 editor: caret, click-drag / Shift+arrows / Shift+click selection, double-click selects the word, triple-click selects all, Ctrl+Left/Right jump words, Ctrl+A/C/X/V | `text`, `placeholder` |
| `textarea` | multiline UTF-8 editor: Enter inserts a newline, Up/Down navigate lines, selection spans lines (word/line drag, double/triple-click and Shift+click like `textinput`), long lines soft-wrap to the widget width, wheel scrolls, vertical scrollbar (drag the thumb or click the track) when content overflows | `text`, `placeholder` (style attrs as `textinput`) |
| `listbox` / `virtuallist` | scrollable list with single selection (click, wheel, Up/Down/Home/End); rows are drawn from a string model so large lists stay cheap — `virtuallist` is the same widget, and `ogui.list` inserts/removes/changes rows incrementally | `selected`, `rowHeight`, `highlight` + `<item>` children |
| `console` | read-only log view mirroring `Kernel::logs()` — newest line on top, wheel scrolls history, error/script lines tinted red | `rowHeight`, `maxLines` (default 200) |
| `item` | a `listbox` entry | `label` / `text`, `value` |
| `propertygrid` | two-column typed property editor: label left, kind-appropriate editor right (bool → checkbox, number/text/color → inline edit, enum → click-cycle select). Group headers where `category` changes; click/Up/Down select, Enter/F2 edit, Space toggles bools, Left/Right cycle enums, Esc cancels. Wheel scrolls; overlay scrollbar on overflow | `rowHeight`, `color`, `labelColor`, `groupColor`, `highlight`, `background`, `borderColor` + `<property>` children |
| `property` | a `propertygrid` row (data only): `name`/`label`, `type` (`bool`/`number`/`text`/`enum`/`color`), `value`, `category`/`group`, `options="a;b;c"` (enum), `readonly` | — |
| `table` | data grid: header click sorts (re-click flips direction), row click selects, wheel + Up/Down/Home/End; drag a header edge to resize a `resizable` column, click a cell in an `editable` column (or press F2) to edit it inline (Enter saves, Esc cancels), overflowing cell text marquees per `scrollingText` | `selected`, `rowHeight`, `headerHeight`, `sortable`, `headerBackground`, `highlight` + `<th>`/`<tr>` children |
| `th` | a `table` column | `label` / `text`, `width` (0 = share leftover width), `resizable`, `editable`, `scrollingText` (`automatic` / `onmouseenter`) |
| `tr` | a `table` row: cells from `cells="a;b"` or nested `<cell>` children | `cells` (`;`-separated), `value`, `editableCells` (`;`-separated column labels), `resizable`, `scrollingText` |
| `cell` | a `tr` cell written as an element (`<cell>text</cell>`) | text content; `editable`, `resizable`, `scrollingText` (override the column) |
| `tree` | hierarchical tree: triangle toggles expansion (rows compress/slide instead of blinking away), click selects, Left/Right collapse/expand; auto-shrinks to its visible rows when collapsed. `icons`/`checks`/`guides` turn it into a file-tree view (folder/file glyphs, a per-node checkbox that fires the widget `bind()`, and dotted parent connectors). With `dir`, the tree mirrors a real filesystem folder: children are read lazily on first expand (dotfiles hidden, dirs first), `<treeitem>` children are ignored | `selected`, `rowHeight`, `indent`, `autoHeight` (default true), `accent`, `highlight`, `icons`, `checks`, `guides`, `dir` + nested `<treeitem>` children |
| `treeitem` | a `tree` node (may nest further `<treeitem>`s) | `label` / `text`, `value`, `expanded`, `folder`/`file`, `checked` |
| `treetable` | tree hierarchy inside tabular columns: column 0 shows a triangle + the indented node label, the remaining columns show each node's `cells`. Click a header to sort siblings at every level (re-click flips direction), click a row to select it, wheel + Up/Down/Home/End scroll, Left/Right/Enter/Space collapse/expand. Only the viewport rows are drawn, so large trees stay cheap | `selected`, `rowHeight`, `headerHeight`, `indent`, `sortable`, `expanded` (true = expand all), `headerBackground`, `highlight`, `accent` + `<th>`/`<tnode>` children |
| `tnode` | a `treetable` row (data only; nests further `<tnode>`s) | `label` / `text` (column 0), `cells="a;b;c"` (columns 1..n), `expanded` |
| `statusbar` | flat text band with a hairline separator on top, sized for the foot of a window or panel | `left` (or `text`), `center`, `right`, `color`, `dimcolor` (right text), `background`, `size` |
| `breadcrumb` | clickable path segments joined by a separator: clicking sets the active segment, fires the widget `bind()` and any `onSelected` callback; segments come from a `path` string or nested `<crumb>` children | `path="a/b/c"`, `separator` (default `/`), `current` (index, omitted = last), `color`, `dimcolor`, `accent` (separator), `size` + `<crumb>` children |
| `crumb` | a `breadcrumb` segment (data only) | `label` / `text` |
| `keybind` | shortcut capture box: click or keypress arms capture, a modifier+key combo records the binding (`change` fires), Backspace/Delete clears, Esc cancels; combos are validated `ctrl+alt+shift+super+x` strings | `value`, `placeholder`, `color`, `background`, `bordercolor`/`border` (focus/capture tint), `radius`, `size` |
| `commandpalette` | incremental-search launcher over registered commands: type to filter (name matches rank above hint matches), Up/Down/Home/End walk the matches, Enter or a click runs the selection (`change`) and lands in a deduplicated newest-first history, Esc fires `submit` so the host can close it | `query`/`filter`, `placeholder`, `maxHistory`, `rowHeight`, `headerHeight`, `color`, `dimcolor`, `accent`, `highlight`, `background`, `size` + `<cmd>` children |
| `cmd` | a `commandpalette` entry (data only) | `name` / `label` / `text`, `hint` / `desc`, `key` / `shortcut` |
| `toggleswitch` / `toggle` | compact pill switch for a binary mode: click slides the knob over an accent-tinted track, firing `onChanged(bool)` and the widget `bind()` | `checked`/`on`, `text` (trailing label) |
| `checkbox` | toggle + label | `text`, `checked` |
| `radiobutton` | radio toggle in group | `text`, `checked`, `group` |
| `combobox` | dropdown: current value shown, popup lists `<option>` children (keyboard Up/Down/Enter, autoclose on outside press) | `selected` (index or `label=`), `rowHeight`, `panel` (popup bg), `highlight`, `highlighttext`, `arrow` |
| `slider` | value in `min..max` (drag, arrows, Home/End) | `min`, `max`, `value`, `color` (bar), `background` (track), `thumbColor` |
| `spinbox` | numeric value with `-`/`+` buttons on each end, drawn as one unified widget (flat button regions + hairline separators); focused Up/Right/Down/Left step, Home/End clamp | `min`, `max`, `value`, `step`, `decimals`, `wrap` (cycles min↔max), `prefix`, `suffix` (or `text`), `background`, `color` (text), `buttonColor`, `borderColor`/`border`, `font` |
| `optionspin` | text-only spin `[ < ][ option ][ > ]`: cycles through a fixed list of options as one unified widget; focused Right/Up forward, Left/Down back, Home/End first/last | `options` (comma-separated list), `value` (matching option text) or `index`, `wrap` (default true), `background`, `color`, `buttonColor`, `borderColor`/`border`, `font` |
| `rangeslider` | two-thumb slider over a `[low, high]` sub-range; drag the nearest thumb (the band between is filled), focused Left/Right nudge low and Up/Down nudge high | `min`, `max`, `low`, `high`, `color` (band), `background` (track), `thumbColor` |
| `splitter` | two panes (first two children, placed in the splitter's own space) with a draggable divider kept at a `split` fraction; the panes resize as the handle is dragged | `orientation` (`"vertical"` = vertical divider/side-by-side panes (default), `"horizontal"` = stacked panes) or `vertical` (bool), `split` (0..1), `handleSize`, `handleColor` |
| `progress` | read-only fill; `indeterminate="true"` slides a block back and forth instead of showing a value | `value` (0..1), `indeterminate`, `color`, `background`, `radius` |
| `badge` | small pill tag (rounded to a capsule); `removable` adds an ✕ hit-region at the right that hides the badge when clicked; `count` appends `(n)` | `text`, `count` (-1 = none), `removable`, `background`, `color` (text), `border` |
| `spinner` | animated loading indicator ticked by the frame clock: `variant="glyph"` cycles &#124;/-\, `variant="blocks"` runs a comet of cells; optional trailing label | `text`, `variant` (`glyph`/`blocks`), `speed`, `color` |
| `searchinput` | `textinput` with a magnifier icon inset on the left and a ✕ clear button on the right (click clears the text) | `text`, `placeholder`, `background` |
| `accordion` / `section` | stack of collapsible sections: clicking a section header toggles it; with `exclusive` (default) opening one closes its siblings; collapsed sections hide their children | `exclusive`, `gap` / `header`, `expanded`, `headerHeight` |
| `modal` | full-screen dim + centred panel that blocks all input below while open; children are laid out relative to the panel origin | `title`, `open`, `panelWidth`, `panelHeight` (toggle with `Modal::setOpen`) |
| `sparkline` | mini line plot of a value history (scaled to min/max); `addSample(v)` from C++ pushes a point and trims to `max` samples | `values` (`;`-separated), `label`, `max`, `color`, `background` |
| `menubar` / `menu` | menu bar strip; each top-level `menu` opens a floating popup under its label, commands route to `Kernel::bind()` | — / `label` |
| `menuitem` | a menu entry (nest more `menuitem`s inside it for a ▶ submenu) | `label` / `text`, `command` (a `bind()` name) |
| `menuseparator` | divider line inside a menu | — |
| `contextmenu` | right-click menu holder (invisible in the tree); any widget points at it with `contextMenu="<id>"` and right-clicking it opens the menu at the pointer (right-click never fires click bindings) | `id`; children are `menuitem`/`menuseparator` |
| `colorpicker` | swatch + hex label; click opens a popup with a saturation/value grid, a hue strip and a live hex readout; drags update the color and fire the widget's `bind()` | `color` (`#rrggbb`) |
| `pathpicker` | read-only path field with a `...` button that opens a file-browser popup (folders first with a trailing `/`, `..` to go up, capped at 40 entries); picking a file fires the widget's `bind()` | `value`, `placeholder`, `path` (start folder), `filter` (extension, e.g. `.png`) |
| `rampeditor` / `ramp` | horizontal color-gradient bar built from movable stops; click the bar to add a stop, drag a stop to reposition it, colours blend piecewise-linearly between neighbours; a change fires the widget's `bind()` | `stops` (`pos:#rrggbb pos:#rrggbb ...`, pos 0..1) |
| `curveeditor` / `curve` | cubic-Bézier curve in a unit box with four draggable control points (endpoints cyan, handles yellow); `evaluate(t)` samples the eased output for animation | `points` (`x,y x,y x,y x,y`, normalised 0..1, y up) |
| `separator` | rule, horizontal or vertical | `thickness`, `color` |
| `tabs` / `tab` | header strip over stacked pages (inactive pages take no input); pages that don't fit the width collapse into a `...` popup menu, or with `scrolling="true"` the header becomes a drag/wheel-scrollable strip that keeps every tab visible | `active`, `tabHeight`, `headerColor`, `tabColor`, `activeColor`, `accent`, `scrolling` / `label` |
| `group` | titled grouping box | `name` |
| `image` | RGBA image file (PNG/JPG/… decoded by `ImageManager` via stb_image): stretched to the bounds, or repeated at native size with `tile`; `border` draws a frame on top; a missing file draws nothing | `source` (path, relative to the working directory), `tile` |
| `scrollview` | wheel-scrollable clipped viewport over its children | `scrollX`, `scrollY`, `scrollStep` |

Toasts (transient notification banners) are a kernel layer rather than an
XML element: `Kernel::showToast("saved", Kernel::ToastKind::Info)` stacks a
banner bottom-right that fades out after ~3.5 s (`Info` / `Warn` / `Error`
pick the accent bar colour). `Modal` open state is driven from C++
(`Modal::setOpen`) or a `bind()` handler.

The gallery's **FileTree** screen (`layouts/layout_filetree.xml`) shows the
tree as a file browser: a `dir="."` tree mirroring the real folder next to a
static tree with per-node checkboxes feeding a `bind()` counter.

![File tree view](docs/file-tree-view.png)

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
debug/tests/      unit tests + debug/tests/golden/ (golden image + meta)
cmake/            CMake package config template
scripts/build.sh  host / win / clean
thirdparty/stb    vendored stb_truetype + stb_image[_write]
thirdparty/lua    vendored Lua 5.4 interpreter sources (scripting)
```

## Known limitations

* `row` tiles its children horizontally using their authored widths; it does
  not shrink or wrap overflowing rows.
* No complex text shaping (no HarfBuzz): Latin scripts and single-code-point
  advance only; combining marks are not positioned.
* One screen size per `Screen`; window resizing is the host's job
  (`screen->width/height`).
* `examples/screenshot.cpp` is a debugging aid, not a shipping renderer.
