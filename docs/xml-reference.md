# OGUI XML reference

Layouts are authored in XML and turned into a widget tree by `XmlParser`
(`src/xmlparser.cpp`). The parser is deliberately forgiving: it never aborts
on a bad element, it records an error and keeps going, so a broken line costs
one widget, not the whole screen.

```
XML text ──XmlParser──▶ widget tree ──Kernel::frame──▶ DrawList ──▶ GL / Software
```

## Attribute naming

Attribute names are **full camelCase words** — `background`, `width`,
`height`, `rowHeight`, `borderWidth`, `fontSize`, `onClick`, ... Matching is
case-insensitive, so `rowHeight`, `rowheight` and `ROWHEIGHT` are the same
key. The old single-word abbreviations (`bg`, `w`, `h`, `cols`, `src`,
`theight`, `panelw`, `panelh`, `headerbg`) are **not** recognised — use the
full spelling. A few conventional names are not abbreviations and stay as
they are: `x`, `y` (position), `id`, `min`, `max`, `color`, `text`, `value`,
`label`, `title`.

## Document roots

A layout file must open with one of two root elements:

| Root | Meaning |
|---|---|
| `<screen>` | A UI layout. Its children are widgets. Optional `id` (defaults to `screen`). |
| `<manifest>` | A data-only list of screens (used by the demo host to pick up layouts without recompiling). |
| `<theme>` | A stylesheet file: `<palette>` token overrides, shared `<style name>` definitions and `<font>` registrations. Loaded by `XmlParser::parseThemeFile()` or referenced from `<manifest theme="file.xml">`. `base="dark|light"` picks the preset it layers over (default: keep the current theme). |

`<manifest theme="dark|light">` sets the initial theme (any other value
names a `<theme>` stylesheet file to load instead) and contains one
`<screen title="..." path="relative/file.xml"/>` per screen, in navigation
order:

```xml
<manifest theme="dark">
  <screen title="Basico" path="layout_demo.xml"/>
  <screen title="Texto"  path="layouts/layout_text.xml"/>
</manifest>
```

Anything that is neither root falls back to a plain label holding the raw
text, and an unknown root tag reports `unknown root element <tag>` and yields
an empty container.

## General syntax rules

- **Attribute names are case-insensitive** (`FontSize`, `fontSize`,
  `FONT_SIZE` are the same key). **Element names are exact-match lowercase.**
- A bare attribute with no value means `"true"`: `<checkbox checked/>`.
- Values may be quoted (`attr="12"`) or unquoted (`attr=12`); quoted values
  stop at the closing `"`, unquoted ones at whitespace, `>` or `/`.
- Comments `<!-- ... -->`, processing instructions `<?xml ... ?>` and
  `<!DOCTYPE ...>` are skipped at element boundaries.
- Unknown elements are reported (`unknown element <tag>`) and the whole
  element, including nested content, is skipped; the rest of the document
  still parses.
- Leaf data widgets can also take their value as text content instead of an
  attribute — currently only table cells: `<cell>Zoe</cell>` ≡ `text="Zoe"`.

## Colors and `@token` references

Every colour attribute (`background`, `color`, `border`, ...) accepts either a
hex literal (`"#4488ff"`, `"#48f"`) or a **palette token** (`"@accent"`).
Tokens resolve against the *active theme* at parse time, so one theme swap
restyles the whole tree; an unknown token silently falls back to `window`.
Tag-level `<style>` definitions may use tokens too.

| Token | Dark | Light | Typical use |
|---|---|---|---|
| `window` | `#111111` | `#e8e8ec` | screen fallback, sunken inputs |
| `screen` | `#161616` | `#f2f2f5` | full-bleed backgrounds |
| `surface` | `#262626` | `#ffffff` | panels, popups, toasts |
| `surfaceHi` | `#2f3340` | `#e4e6ee` | raised fills (buttons) |
| `surfaceLo` | `#0d0e12` | `#d8dade` | sunken fills (icon boxes) |
| `border` | `#3a3b44` | `#b8bac4` | generic hairlines |
| `borderStrong` | `#555555` | `#8a8c96` | popup outlines |
| `track` | `#333333` | `#c4c6ce` | tab underlines, slider tracks |
| `guide` | `#4a4f5a` | `#a8aab4` | tree guide lines |
| `text` | `#cccccc` | `#333333` | body text |
| `textBright` | `#eeeeee` | `#111111` | headings |
| `white` | `#ffffff` | `#ffffff` | on-accent text |
| `dim` | `#777777` | `#70727a` | placeholders |
| `focus` | `#88bbff` | `#2266cc` | focus rings |
| `accent` | `#4488ff` | `#2266dd` | primary actions |
| `danger` | `#ff8888` | `#cc3333` | destructive |
| `info` | `#2b78e4` | — | toast kind |
| `warn` | `#e4a12b` | `#b07818` | toast kind |
| `error` | `#e44b2b` | `#c03a1c` | toast kind |
| `overlay` | `#000000` | — | modal dim (alpha at use) |
| `shadow` | `#000000` | — | drop shadows |

## Geometry

`x`, `y`, `width`, `height` each accept three forms:

| Form | Example | Meaning |
|---|---|---|
| pixels | `width="150"` | absolute size/offset |
| percent | `width="50%"` | fraction of the **parent's content size** |
| fill | `width="fill"` | the whole parent content size |

Percent and `fill` components are re-resolved on **every layout pass**, so
resizing the window reflows the tree. Mixed px+% rows only stay coherent
above roughly 520 px of height; the demo gallery therefore clamps each screen
to a **design minimum of 800×600** and lets the host window scroll/clip below
that instead of overlapping widgets.

`anchor="left,right,top,bottom"` (short forms `l r t b` accepted; separated
by space, comma, `;` or `|`) pins the described edges' *margins* across
parent resizes — e.g. `anchor="right,bottom"` keeps a button glued to the
lower-right corner.

`minwidth` / `minheight` / `maxwidth` / `maxheight` (pixels) clamp the
resolved size on every layout pass, whatever unit `x/y/width/height` used —
e.g. `width="50%" maxwidth="500"` grows with the parent up to 500 px, and
`width="10%" minwidth="200"` never shrinks below it. Authored absolute sizes
are clamped at parse time too.

## Common attributes (all widgets)

| Attribute | Effect |
|---|---|
| `id` | lookup name (`Kernel::findWidget`) |
| `onClick` / `bind` | action name fired on click |
| `contextMenu` | id of a `<contextmenu>` to pop on right-click |
| `tooltip` | hover text |
| `visible` | show/hide |
| `background` | fill colour |
| `color` | foreground/text colour |
| `border` / `borderColor` | stroke colour |
| `borderWidth` | stroke thickness in px |
| `radius` | corner radius |
| `padding` / `margin` | inner / outer spacing |
| `gap` | child spacing for `column`/`row` stacks; `0` falls back to `margin`, then the default 4 px (`grid`/`accordion` keep their own `gap` attribute) |
| `font` / `fontSize` | family and size (`fontSize` overrides; legacy `font sans 20 color "#66ee66"` pseudo-syntax also works) |
| `shadow` | drop shadow as `"blur[,dy[,dx]]"` |
| `shadowColor` | shadow tint |
| `gradient` | fill gradient; `gradientDirection="horizontal"` runs the ramp left→right (radius ignored) |
| `minWidth` / `minHeight` / `maxWidth` / `maxHeight` | pixel clamp on the resolved size, re-applied on every layout pass (see Geometry) |
| `weight` | flex share inside `column`/`row`: weighted children split the leftover height/width proportionally; `0` keeps the authored size |
| `enabled` / `disabled` | `false`/`true` makes the widget inert: no hover/press/focus, skipped by hit-testing (its whole subtree for input) |
| `hover:background`, `pressed:color`, … | per-state visual overlays (see Visual states & transitions) |
| `transition` | seconds to blend visual-state changes |

## Visual states & transitions

Any widget can declare per-state values with `state:attribute="..."` — no
Lua needed. The plain attribute stays the `normal` base, so an overlay
always has something to fall back to (a state overlay on an attribute that
has no plain value is dropped with a parse warning).

Supported states: `hover`, `pressed`, `focus`, `checked`, `selected`,
`disabled` (plus `normal:` to override the base per state). Supported
attributes: `background`, `color`, `border`, `bordercolor` (colours) and
`borderwidth`, `radius` (floats). States may also be declared inside named
`<style>` definitions.

```xml
<button text="Ok" background="#223" color="#eee" radius="4"
        hover:background="#335" hover:radius="8"
        pressed:background="#112" disabled:background="#111"/>
```

The Kernel recomputes each widget's active states every frame; when several
are active the highest-priority registered overlay wins:
`disabled > pressed > hover > selected > checked > focus`.
`checked` comes from Checkbox/RadioButton/ToggleSwitch, `selected` from
ComboBox/ListBox/Table/TreeView (selection set).

`transition="0.2"` (seconds) blends colour channels and float values
linearly between states instead of snapping; interruption re-targets from
the current mid-flight values.

`enabled`/`disabled` flip interactivity; Lua mirrors it with
`ogui.set(id, "enabled", false)`.

## Named styles

`<style name="...">` anywhere in a screen declares a reusable attribute set;
`<style name="p" background="@accent" color="#fff" radius="6"/>` or a
non-self-closing `<style>` containing nested `<set .../>` elements. Elements
opt in with `style="p"`; the named style provides the base and the element's
**own attributes win**. Referencing an unknown style warns and is ignored.

`<style element="button" .../>` (no `name`) is an **element style**: it
applies to every `button` in the document without opting in. A widget can
also stack several named styles — `style="primary,dark"` — merged left to
right. The full cascade, lowest to highest:

```
element style  <  named styles (left→right)  <  own attributes
```

```xml
<style name="primary" background="@accent" color="@white" radius="6"/>
<button style="primary" text="Iniciar" width="150" height="38"/>
```

## Stylesheets

A `<theme>` file moves those definitions (plus palette tokens and font
registrations) out of the screen and into a shared document:

```xml
<theme name="game" base="dark">
  <font family="anton" source="assets/Anton-Regular.ttf"/>
  <style element="button" font="anton" fontSize="28" radius="0"/>
  <style name="menu-on" background="#c49b3deb" color="#ffffff"/>
  <palette accent="#e8b33c" text="#e6e1d4"/>
  <default font="anton" fontSize="16" color="@text"/>
</theme>
```

- `<palette .../>` re-colours the theme tokens; attribute names are token
  names (case-insensitive), values are hex colours. Unknown tokens warn.
- `<default .../>` restyles the theme's base Style (`font`, `fontSize`,
  `color`, `background`, `border`, `borderWidth`, `radius`), so every widget
  inherits it unless a style or attribute says otherwise.
- `<style>`/`<font>` work exactly as inside a screen, including element
  styles: a stylesheet is the right place for app-wide `element` defaults.
- Any screen can pull one in with `<stylesheet path="themes/game.xml"/>`:
  its named and element styles merge into the document (own `<style>`
  definitions win), fonts register, and a `<palette>` is installed app-wide
  so `@tokens` downstream re-resolve. Paths are cwd-relative, like
  `image source`.
- `<manifest theme="themes/game.xml">` loads a stylesheet as the app theme
  instead of the `dark`/`light` presets.

## Elements

### Text

| Element | Typed attributes | Children |
|---|---|---|
| `label` | `text`, `wrap`, `align` | — |
| `badge` | `text`, `count`, `removable` | — |
| `sparkline` | `values` (comma list), `label`, `max` | — |
| `separator` | `thickness` | — |
| `image` | `source`, `tile` | — |

### Inputs

| Element | Typed attributes | Children |
|---|---|---|
| `textinput` | `text`, `placeholder` | — |
| `searchinput` | `text`, `placeholder` | — |
| `textarea` | — (common attributes only) | — |
| `spinbox` | `min`, `max`, `value`, `step`, `decimals`, `wrap`, `prefix`, `suffix`, `text`, `buttonColor` | — |
| `optionspin` | `options="a,b,c"`, `value`, `index`, `wrap` | `option` |
| `slider` | `min`, `max`, `value`, `color`, `background`, `thumbColor` | — |
| `rangeslider` | `low`, `high` (plus slider attrs) | — |
| `colorpicker` | `color` | — |
| `pathpicker` | `value`, `placeholder`, `path`, `filter` | — |
| `rampeditor` | `stops` (`ramp` is an alias) | — |
| `curveeditor` | `points` (`curve` is an alias) | — |

### Options & toggles

| Element | Typed attributes | Children |
|---|---|---|
| `button` | `text`, `align` (`left`/`center`/`right`), `textPadding` (inset for left/right) | — |
| `checkbox` | `text`, `checked`, `color`, `background` (check colour), `borderColor` | — |
| `radiobutton` | `text`, `checked`, `group`, `accent` | — |
| `toggleswitch` / `toggle` | `checked`, `on`, `text` | — |
| `progress` | `value` (0..1), `indeterminate` | — |
| `spinner` | `variant`, `speed` | — |

### Lists & tables

| Element | Typed attributes | Children |
|---|---|---|
| `listbox` | `rowHeight`, `selected`, `highlight` | `item` (`ListItem`); `virtuallist` is the same widget under another tag — rows are drawn from a string model, so huge lists only ever lay out the viewport, and `ogui.list` mutates it incrementally |
| `console` | read-only log view mirroring `Kernel::logs()` (newest first, wheel scrolls history, error/script lines tinted): `rowHeight`, `maxLines` (default 200), `color`/`background`/`borderColor` | — |
| `item` | `label`/`text`, `value` | — |
| `table` | `rowHeight`, `headerHeight`, `selected`, `sortable`, `headerBackground` | `th`, `tr` |
| `th` | `label`, `width`, `resizable`, `editable`, `scrollingText` (`automatic`/`onmouseenter`) | — |
| `tr` | `cells="a;b;c"`, `value`, `editableCells`, `resizable`, `scrollingText` | `cell` |
| `cell` | `editable`, `resizable`, `scrollingText`; text content also works (`<cell>Zoe</cell>`) | — |
| `tree` | `rowHeight`, `indent`, `autoHeight`, `selected`, `icons`, `checks`, `guides`, `dir` | `treeitem` |
| `treeitem` | `label`, `value`, `expanded`, `folder`/`file`, `checked` | nested `treeitem` |
| `treetable` | tree hierarchy inside tabular columns: column 0 shows a disclosure triangle + the node label (indented by depth), the rest show the node's `cells`. Click a header to sort siblings at every level (re-click flips direction), click a row to select it (`change` event), wheel/`Up`/`Down`/`Home`/`End` scroll, `Left`/`Right`/`Enter`/`Space` collapse/expand; only the viewport rows are drawn so large trees stay cheap. Attributes: `rowHeight`, `headerHeight`, `indent`, `selected`, `sortable`, `expanded` (`true` = expand all), `color`/`background`/`headerBackground`/`borderColor`/`highlight`/`accent` | `th` (`TableColumn`), `tnode` (`TreeTableNode`) |
| `tnode` | a `treetable` row (data only): `label`/`text` (column 0), `cells="a;b;c"` (columns 1..n), `expanded`; nest `<tnode>`s for children | nested `tnode` |
| `propertygrid` | two-column typed-property editor: label on the left, a kind-appropriate editor on the right (bool → checkbox, number/text/color → inline text, enum → click-cycling select). Click/`Up`/`Down` select, `Enter`/`F2` edit text kinds, `Space` toggles bools, `Left`/`Right` cycle enums, `Esc` cancels a commit. Group headers appear wherever a `category` changes. Attributes: `rowHeight`, `color` (value text), `labelColor`, `groupColor`, `highlight`, `background`, `borderColor` | `property` (`PropertyItem`) |
| `property` | data-only row feeding a `propertygrid`: `name`/`label`, `type` (`bool`/`number`/`text`/`enum`/`color`), `value`, `category`/`group`, `options="a;b;c"` (enum), `readonly` | — |

`listbox`, `table`, `tree`, `scrollview` and `window` draw an overlay
scrollbar (drag the thumb, or click the empty track to jump) whenever
content exceeds the viewport. In `window` the bar sits below the title bar.
The same bar appears inside dropdown, menu and context-menu popups; popups
are always fitted to the screen (they flip above the anchor when there is
no room below).

### Containers

| Element | Typed attributes | Children |
|---|---|---|
| `panel` | — | any widget |
| `group` | `name` (box caption) | any widget |
| `window` | `title`, `titleVisible` | any widget |
| `scrollview` | `scrollStep`, `scrollX`, `scrollY` | any widget |
| `column` / `row` | `gap` (child spacing px, falls back to `margin` then 4), `align` on `column` (`center`/`right` child placement), `valign` on `row` (`middle`/`bottom` against the tallest child), `wrap` on `row` (children that exceed the width continue on a new line and the row grows in height) | any widget (stacked; children may set `weight` to split leftover height/width) |
| `grid` | `columns`, `gap`, `rowHeight` | any widget |
| `splitter` | `orientation`/`vertical`, `split` (0..1), `handleSize`, `handleColor` | two panels |
| `tabs` | `active`, `tabHeight`, `headerColor`, `tabColor`, `activeColor`, `accent`, `scrolling` (`true`: the header is a drag/wheel-scrollable strip instead of a `...` overflow) | `tab` |
| `tab` | `label` | any widget |
| `accordion` | `exclusive`, `gap` | `section` |
| `section` | `header`/`text`, `expanded`, `headerHeight` | any widget |
| `combobox` | `selected`, `rowHeight`, `highlight`, `panel`, `accent` | `item`/`option` |

Overflowing tab headers collapse into a `...` popup menu automatically.

### Menus & overlays

| Element | Typed attributes | Children |
|---|---|---|
| `menubar` | — | `menu` |
| `menu` | `label` | `menuitem`, `menuseparator`, nested `menu` |
| `menuitem` | `label`/`text`, `command` | — |
| `menuseparator` | — | — |
| `contextmenu` | located by its `id` (pointed to from `contextMenu="..."` on any widget) | `menuitem` |
| `modal` | `title`, `open`, `panelWidth`, `panelHeight` | any widget |

### Data-only

| Element | Notes |
|---|---|
| `manifest` | root only; `theme`, children `screen title path` |
| `theme` | root only; a stylesheet — children `palette`, `default`, `style`, `font`; `base` (`dark`/`light`) |
| `palette` | token colour overrides, only valid inside a `<theme>` root |
| `default` | base Style overrides, only valid inside a `<theme>` root |
| `stylesheet` | merges an external `<theme>` file into this document | `path` |
| `font` | registers a font family: `<font family="anton" source="assets/Anton-Regular.ttf"/>`; later `font="anton"` attributes resolve to that file. Data-only, yields no widget |
| `script` | a Lua chunk for the screen: `<script src="file.lua"/>` or inline `<script>code</script>` (raw text up to `</script>`, no entity decoding). Data-only, yields no widget — see [Scripting](#scripting-lua) |
| `option` | entry of `optionspin`/`combobox` |
| `item` | entry of `listbox`/`combobox` |

## Scripting (Lua)

Screens embed behaviour with the `<script>` element (Lua 5.4, vendored;
build with `OGUI_USE_LUA=OFF` to drop it). The engine owns the loop and
calls the script — never the other way around:

- `XmlParser::parse()` collects chunks into `res.scripts` (`{payload,
  isFile}`); hosts move them onto `Screen::scripts`.
- The `Kernel` clears its `ScriptHost` and runs the current screen's
  chunks on every `pushScreen`/`popScreen`/`switchScreen`, so hooks always
  belong to the visible screen. A switch fires the outgoing script's
  `ogui_on_hide` → `ogui_on_unload`, then the incoming chunks run (each
  followed by its `ogui_on_load`) and finish with `ogui_on_show`.

Hooks (plain global functions, all optional):

| Hook | Fired |
|---|---|
| `ogui_on_load()` | right after the chunk runs |
| `ogui_on_show()` | after the screen's chunks loaded, when it becomes the foreground screen |
| `ogui_on_hide()` | when the screen loses the foreground |
| `ogui_on_unload()` | right before its script state is destroyed (always after `ogui_on_hide`: screen script state is rebuilt on every switch, not persisted) |
| `ogui_on_update(t, dt)` | every frame (`t` = seconds since kernel start) |
| `ogui_on_click(key)` | left-click released on a widget with **no** C++ binding (onclick name, else id) |
| `ogui_on_event(name, key)` | an `invokeBinding(name)` with no C++ binding (C++ wins; scripts see the leftovers) |

All hooks carry the `ogui_on_` prefix (snake_case, matching the rest of the
API): game code can live in the same Lua state, and bare names like
`on_update` would collide with the game's own global functions. The engine
only ever looks hooks up by their prefixed name; anything else in the
globals is left alone.

API table `ogui` (all calls are safe on unknown ids):

| Call | Effect |
|---|---|
| `ogui.set(id, attr, v)` | `attr`: `text`, `visible`, `checked`, `value`, `selected`, `tooltip`, `x`, `y`, `width`, `height` (geometry pins the spec to absolute so layout keeps the new values) |
| `ogui.get(id, attr)` | same attrs; `text`/`tooltip` → string, `value`/`selected`/geometry → number, `visible`/`checked` → boolean; unknown → `nil` |
| `ogui.exists(id)` | widget lookup |
| `ogui.click(id)` | fire the widget's C++ binding/handler |
| `ogui.time()` | seconds since kernel start |
| `ogui.toast(msg[, kind])` | `kind`: `"info"` (default) / `"warn"` / `"error"` |
| `ogui.log(msg)` | push to `Kernel::addLog` (shown by `<console>` widgets) |
| `ogui.mouse()` | last pointer position → `x, y` |
| `ogui.mouseDown([btn])` | `btn`: `0/1/2` or `"left"/"right"/"middle"`; no arg = any button |
| `ogui.key(name\|code)` | key down; `"a".."z"`, `"0".."9"`, `"space"`, `"enter"`, `"escape"`, `"tab"`, `"backspace"`, `"delete"`, `"insert"`, `"home"`, `"end"`, `"pageup"`, `"pagedown"`, `"left/right/up/down"`, `"f1".."f12"`, `"lshift/rshift/lctrl/rctrl/lalt/ralt/lgui/rgui"` — or the raw `KeyCode` number |
| `ogui.mods()` | modifier bitmask: Shift=1, Ctrl=2, Alt=4, Super=8 |
| `ogui.focus()` | focused widget id, or `nil` |
| `ogui.hovered()` | hovered widget id, or `nil` |
| `ogui.clickCount()` | presses at this spot inside the double-click window |
| `ogui.items(id, tbl)` | replace `listbox`/`optionspin` entries with a Lua array of strings |
| `ogui.list(id, op, ...)` | incremental `listbox`/`virtuallist` row updates — `count`, `get(i)`, `insert(i,text)` (`i` = `-1` appends), `remove(i)`, `set(i,text)`, `scroll(i)` (scrollIntoView). Mutation is silent (no `change` event); selection travels with its row |
| `ogui.prop(id, op, ...)` | `propertygrid` model access — `count`, `get(name)` (nil when unknown), `set(name,value)` (silent, validated per kind), `add(name,type,value[,group[,options]])` (`options` `;`-separated for `enum`), `remove(name)`, `list()` (table `{name=value}`), `selected` / `select(i)` |
| `ogui.tween(id, attr, to, dur[, ease[, delay]])` | animate `value`/`x`/`y`/`width`/`height` from the current value; `ease`: `"linear"` (default) / `"out"` / `"inout"` / `"back"`. Engine-driven — runs even without `ogui_on_update`; a new tween on the same `id+attr` replaces the old one |
| `ogui.tweenStop([id[, attr]])` | cancel matching tweens → count stopped |
| `ogui.after(secs, fn)` | one-shot timer → id; fires from the frame loop, outside any hook |
| `ogui.every(secs, fn)` | repeating timer → id (one fire per frame max) |
| `ogui.cancel(id)` | stop a timer → boolean; timers also die on screen swaps |
| `ogui.add(parentId, xmlFragment)` | parse `xmlFragment` (a snippet of `<screen>` body) and attach it as children of `parentId`; returns the first new widget's id, or `nil, err` if the parent is missing or the fragment fails to parse |
| `ogui.remove(id)` | drop a widget from its parent → `true`, or `nil, err` for a missing id or an orphan. Safe to call on the widget currently being pressed: the Kernel forgets focus/hover/press bindings before the shared_ptr disappears |
| `ogui.clear(id)` | detach every child of a container → count removed |
| `ogui.children(id)` | ordered array of child ids (`""` for unnamed children), or `nil` for an unknown id |
| `ogui.parent(id)` | parent's id, or `nil` at the screen root / unknown id |
| `ogui.index(id)` | 1-based position among its siblings, or `nil` |
| `ogui.on(id, event, fn)` | attach a per-widget listener → integer handle. Events fired: `"click"`, `"focus"`, `"blur"`, `"change"` (user edits a value/selection/text) and `"submit"` (Enter in a `TextInput`); the callback receives the widget id |
| `ogui.off(handle)` | remove one listener → boolean |
| `ogui.off(id)` | remove every listener on that widget → count dropped; listeners also die automatically when the widget is removed or the screen swaps |
| `ogui.bind(targetId, targetAttr, sourceId, sourceAttr[, twoWay])` | mirror `source` → `target`: copy once now, then on every source `"change"`/`"submit"`. `twoWay` adds the reverse mirror. Loop-safe — `ogui.set` is silent, so only user edits propagate (attrs default to `"value"`) |
| `ogui.unbind(targetId[, targetAttr])` | drop mirrors → count removed |
| `ogui.quit()` | `Kernel::requestQuit` |

Example:

```xml
<screen id="counter">
  <button id="add" text="+1" x="20" y="20" width="80" height="36"/>
  <label  id="num" text="0"  x="120" y="20" width="80" height="36"/>
  <script>
n = 0
function ogui_on_click(key)
  if key == "add" then n = n + 1 end
  ogui.set("num", "text", tostring(n))
end
  </script>
</screen>
```

Runtime errors never unwind into the engine: they are captured in
`ScriptHost::errors()` and mirrored to `Kernel::addLog`.

Per-widget listeners (`ogui.on`) let several independent callbacks share one
screen without routing every click through a single `ogui_on_click`, and
`ogui.add`/`ogui.remove` build the tree at runtime:

```lua
function ogui_on_load()
  local id = ogui.add("inbox", '<button id="row1" text="new row"/>')
  ogui.on(id, "click", function(w) ogui.remove(w) end)  -- click removes itself
  ogui.on(id, "focus", function(w) ogui.log("focused " .. w) end)
end
```

Data bindings mirror one widget's attribute onto another without a hand-written
callback; a slider drives a progress bar, or two-way keeps a text field and a
label in step:

```lua
function ogui_on_load()
  ogui.bind("volume_bar", "value", "volume_slider", "value")          -- one-way
  ogui.bind("name_label", "text",  "name_input",    "text", true)      -- two-way
end
```

## Error reporting

`XmlParser::parse()` returns `{root, errors, warnings, scripts}`. Errors never abort
the parse: unknown root/element names are logged and skipped, and the caller
always receives a usable tree.
