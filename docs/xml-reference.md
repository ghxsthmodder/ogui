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

## Adding your own element

`widgetRegistry()` is a public `std::map<std::string, WidgetFactory>`. A factory
gets the current `Style` plus the authored attributes (keys lowercased), so one
registration turns a class into a tag:

```cpp
ogui::widgetRegistry()["led"] = &makeLed;   // <led on="true" color="@accent"/>
```

Register it before parsing; replacing a built-in name works the same way.
`examples/custom_widget.cpp` is a complete, self-tested version and
[`examples.md`](examples.md) walks through it.

## General syntax rules

- **Attribute names are case-insensitive** (`FontSize`, `fontSize`,
  `FONT_SIZE` are the same key). **Element names are exact-match lowercase** —
  except a component instance, whose tag is the `<template name="…">` written
  by the author, case included ([Templates](#templates-components)).
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
restyles the whole tree. A token the palette does not carry is a parse error
(`attribute "background": unknown palette token "@nope"`): `Palette::token()`
still answers an unknown name with `window`, which is what a live theme must
draw, but an authored layout no longer gets away with the typo. Tag-level
`<style>` definitions may use tokens too.

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

"Pixels" here means **logical units**: one unit is `Screen::scale` device
pixels, so the same file comes out physically bigger on a HiDPI display
without reflowing. Percent and `fill` resolve against the parent's *logical*
content size, and every other number in a layout (`radius`, `padding`,
`borderWidth`, `fontSize`, ...) is in the same units. There is no `scale`
attribute — dpi is a property of the monitor the window sits on, so the host
sets `screen->scale` (see [DPI and scaling](#dpi-and-scaling)).

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

## DPI and scaling

A `Screen` carries two sizes that answer to different masters:

| Field | Unit | Who owns it |
|---|---|---|
| `width` / `height` | device pixels | the host (window / framebuffer size) |
| `scale` | device pixels per logical unit | the host, from the display dpi (1.0 = 96 dpi) |

The canvas the tree is laid out in is `width / scale`. No widget and no layout
pass multiplies by the scale — the single conversion lives where the
rasterizer emits vertices and clip rects, and font atlases are baked at
`fontSize * scale` while `Font::measure()` and the rest keep reporting logical
units. Drawing, hit-testing, text metrics and input therefore all agree, and
applying the scale twice is impossible by construction.

From C++ the derived numbers are `Kernel::scale()`, `Kernel::setScale(s)`,
`Kernel::logicalWidth()` and `Kernel::logicalHeight()`. `ogui::Host` reads the
window dpi every frame and writes `Screen::scale`; a custom host does the same
from its toolkit's content scale factor. A `scale` of 0 or less degrades to 1.

## Text and Unicode

Every `text`, `label`, `placeholder` and attribute value is plain UTF-8: the
parser never decodes it, the widgets keep byte offsets, and the rasterizer
decodes code points when it draws. Nothing in the pipeline is ASCII-only.

Font selection is a chain, not a single face. `family="..."` (or the C++
`FontManager::setFontPath`) picks the requested face; a code point that face
does not carry is baked from the first family in the fallback chain that does
— `sans, serif, mono` by default, extensible with
`FontManager::addFallback("cjk")`. A code point no chain member has draws one
shared `.notdef` box at the font's em width, so missing text is visible rather
than blank. Combining marks advance zero and are centred on the base glyph,
which keeps an accent from pushing the next word along; caret movement and
backspace in `textinput`/`textarea` treat base-plus-marks as one cluster.
Line breaking follows spaces, ideographic full stop, and — the part ASCII never
needed — a break opportunity between any two CJK/Hangul/kana characters, with
closing punctuation pulled up onto the line it belongs to. See
[`docs/unicode.md`](docs/unicode.md) for the atlas model and what shaping is
deliberately left out.

## Common attributes (all widgets)

| Attribute | Effect |
|---|---|
| `id` | lookup name (`Kernel::findWidget`; see Ids and lookup) |
| `onClick` / `bind` | action name fired on click |
| `contextMenu` | id of a `<contextmenu>` to pop on right-click, or on a finger held half a second |
| `tooltip` | hover text |
| `focusable` | `true`/`1`/`yes`/`on` makes the widget a tab stop, `false`/`0`/`no`/`off` removes one; whether a widget is one when nobody says is the class's own answer (`Widget::focusableByDefault`) — the interactive classes say yes, the decorative ones no |
| `accessible` / `aria-label` | accessible name for tooling and screen readers (no visual effect) |
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
| `enabled` / `disabled` | `false`/`true` makes the widget inert: no hover/press/focus, skipped by hit-testing (its whole subtree for input) and by Tab focus traversal |
| `hover:background`, `pressed:color`, … | per-state visual overlays (see Visual states & transitions) |
| `transition` | seconds to blend visual-state changes |

## Typed properties

Everything that works on an attribute **by name** at runtime resolves through
one registry (`include/ogui/properties.hpp`) instead of a second
`dynamic_cast` ladder: `Widget::setProperty`, the Lua
`ogui.get`/`ogui.set`/`ogui.props` and any tool that lists what a widget
exposes. Each declaration carries a name, a type, a description, the canonical
form of its default value and the read/write pair, and says which of the two it
offers — `pressed` on a button and `count` on a combo box read but refuse
writes.

Values travel as strings, the form XML and Lua scripts use, and the declared
type decides how they are converted and validated:

| Type | Written as | Canonical read |
|---|---|---|
| `string` | any text, `""` clears | as authored |
| `bool` | `true`/`false`/`1`/`0`/`yes`/`no`/`on`/`off` (any case) | `true` / `false` |
| `number` | decimal, no trailing junk, no `nan`/`inf` | `%.6g` (`22`, `3.5`) |
| `color` | `#rgb`/`#rgba`/`#rrggbb`/`#rrggbbaa` or `@token` | `#rrggbb` (+ alpha when not opaque) |
| `enum` | one of the declared choices, case-insensitive | the choice as authored |

A bad value never silently no-ops: the write returns `false` and the message
says what was expected (`property 'width' expects number, got 'wide'`; an
unknown name lists what the class does expose). In Lua the same message comes
back as the error text, so `pcall(ogui.set, …)` catches it.

Properties are declared per class and inherited along a declared base chain,
most-derived declaration winning: a `window` gets `title` from `Window`,
`bgcolor` from `Panel` and `tooltip` from `Widget`, and a `textarea` inherits
the whole `textinput` table. `font` and `fontsize` are the base `Style` values;
classes that draw from their own font field re-declare them so a write moves
both, exactly as the parser does. Host widget classes join the same game:

```cpp
PropertyRegistry::instance().declare(typeid(Led), typeid(Widget), {
    numberProp<Led>("level", [](const Led& l) { return l.level(); },
                    [](Led& l, double v) { l.setLevel(v); return true; },
                    "fill fraction, 0 to 1"),
});
```

The XML parser keeps applying authored attributes through its own typed chain
(it also handles geometry expressions, named styles and the legacy `font`
pseudo-syntax); the registry is the runtime, name-based path to the same
setters. It is also the parser's referee: `checkWidgetProperty()` answers the
same question a write would, without applying anything, which is what lets
`applyAttributes()` report `height="abc"` as an error after the fact (see
[Error reporting](#error-reporting)). All 61 classes the tag registry can build
are declared — 2755 properties between them, 2723 of which both read and write,
and
`debug/tests/properties_test.cpp`
writes each one back onto itself and checks the value reads out unchanged, then
walks the factory and refuses a tag whose class has no table, and finally turns
on `XmlParser::setAttributeTrace()`, parses one element per tag and refuses an
attribute name the parser asked about that no property of the class it built
declares.

Two shapes fall out of that. A spec a widget does not keep a copy of is
write-only and its resolved shape reads back separately: `columns="120,1fr"` on
a `grid` is a `string` you can set but not get, `colcount` is the number it
became. And a class that draws from its own fields, not from `Style`, shadows
the inherited colour so the name means what is on screen: `background` on a
`sparkline` or a `propertygrid` moves the fill it paints, leaving the base
`Style` value alone.

## Ids and lookup

`id` names a widget inside its screen: `Kernel::findWidget`,
`Kernel::setText`/`getText` and the Lua `ogui.get`/`ogui.set` resolve it, and
`ogui.on` keys a listener by it. Ids only have to be unique per screen — a
lookup never walks into another screen on the stack.

A dot is an ordinary character in an id, and that is what makes a
[component](#templates-components) addressable from outside: the ids a template
authored are prefixed with the instance's scope, so `ogui.get("cpu.bar",
"value")` reads the progress bar inside `<Gauge id="cpu">`.

The kernel keeps an `id -> widget` index over the current screen and rebuilds
it lazily, once, after anything that changes the tree: `addChild`,
`removeChild`, `takeChildren`, `Widget::setId`, a screen push/pop/switch or an
explicit `Kernel::invalidate()`. Lookups between two such changes are O(1)
instead of a walk of the tree.

Duplicate ids are tolerated, not useful: the index keeps the first widget in
document order (depth-first, parents before children), which is what a
sequential scan returned before. Widgets handed back by a lookup are borrowed —
the tree owns them: removing a widget from the tree — `ogui.remove`, any
`removeChild` that drops the last `shared_ptr`, a screen switch — expires its
entry, so the id reports `nullptr` rather than a dangling pointer. If you keep
a raw `Widget*` of your own, clear it through `Kernel::forgetWidget` before the
widget leaves the tree.

That call is the same promise for the kernel's own caches: it drops every
pointer the frame holds at a widget *and at everything under it* — focus,
hover, the press, the pan, the fling, the long-press hold, the pinch and the
tooltip — and dismisses the floating popup if the widget leaving is inside the
one being drawn. `ogui.remove` and `ogui.clear` make it for you; a host that
calls `removeChild` itself has to. A screen change is the other half:
`pushScreen`, `popScreen` and `switchScreen` dismiss the popup and clear the
focus *before* the outgoing tree is released, so the `blur` a widget loses on
the way out still reaches a live object.

## Event routing

Five semantic events travel the tree: `click`, `change`, `submit`, `focus` and
`blur`. Each starts at the widget it happened to and visits that widget, then
its parent, then its parent's parent, up to the root of the screen. At every
stop a widget may watch it (`Widget::onEvent`) and its Lua listeners fire;
`Event::stopPropagation()` in C++ or `return false` from a listener ends the
walk there. Raw input does not travel: a mouse move, a wheel step and a key
press go to one widget — the hit-tested or focused one — and `onclick` actions
are resolved on the pressed widget alone, so a click on a Save button never
runs the panel's action. [`docs/events.md`](events.md) has the field tables, the
scroll-to-nearest-ancestor rule and what happens when a handler removes the
widget mid-walk.

## Keyboard focus

`Tab` moves focus to the next focusable widget in document order and
`Shift+Tab` to the previous one, wrapping around; the kernel intercepts the
Tab press-edge unless the focused widget declares `consumesTab()` (a `keybind`
still captures a bare Tab while recording). Invisible and disabled widgets —
and the whole subtree of a disabled one — are never tab stops, mirroring the
hit-test rules. Clicking a widget still focuses it as before. `focusable`
overrides the class's own answer in either direction, so a `button` can be kept
out of the ring and a `label` put in it; a widget built in C++ joins exactly
where its class says, without waiting for the tag factory. `accessible`/`aria-label` give a widget
a name for tooling without affecting rendering.

`Enter` and `Space` activate a focused `button` through the same click path
the mouse uses (`onclick` binding → id binding → C++ handler →
`ogui_on_click`); checkbox, radio and switch toggle on them, `combobox` and
list popups open on `Enter`/`Space`/arrows and close on `Escape`, lists and
trees accept `Enter` as their commit key, `textinput` and `commandpalette`
submit on `Enter`. Authors draw the focus ring with the `focus:` visual
states, e.g. `focus:border="@focus" focus:borderwidth="2"`.

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

## Templates (components)

`<template name="Card">` declares a reusable piece of layout. Declaring it
registers the name as an element, so an instance is written as an ordinary tag
and expands in place:

```xml
<screen id="s">
  <style name="primary" background="@accent" color="@white" radius="6"/>
  <style name="ghost" background="transparent" color="@accent" radius="6"/>

  <template name="PrimaryButton" text="OK" variant="primary" height="38">
    <button text="${text}" style="${variant}" width="150" height="${height}"/>
  </template>

  <PrimaryButton id="startButton" text="Iniciar" y="20"/>
  <PrimaryButton text="Sair" variant="ghost" y="70"/>
</screen>
```

- **Parameters.** Every attribute on `<template>` except `name` declares a
  parameter with that default. `${name}` in the body is replaced by the
  instance's attribute of the same name, falling back to the default.
  Attribute names are lowercased, so `${rowHeight}` matches `rowheight="…"`.
  A placeholder with neither keeps its literal text and reports
  `template 'X': required parameter 'name' has no value`.
- **Local ids.** Each instance is its own id scope: the body's `<label
  id="t">` inside `<Card id="c1">` is addressed as `c1.t`, by `findWidget`, by
  `ogui.get`/`ogui.set`/`ogui.on` and by `onClick`. Scopes nest, so a
  component inside a component is `c1.row.t`, and two hosts of the same
  component never collide. An instance without an `id` gets one from its
  component name and a per-document counter (`Card#1`, `Card#2`, …). When the
  body has a **single root**, that root *is* the instance: `<PrimaryButton
  id="startButton">` names the button itself and the body's own root id is
  replaced by it; with several roots, they are wrapped in a plain container
  carrying the instance id.
- **Slots.** `<slot/>` is where the instance's children land (inside the
  instance's scope). `<slot>` with content uses that markup as the fallback
  when the instance has no children. A component with no `<slot/>` warns
  `<X>: template has no <slot/>, its children were dropped`.
- **Attributes fall through.** Whatever the body did not consume as a
  parameter still applies to the generated root, exactly as if it had been
  written there: `y="20"` above moves the button without erasing the authored
  `width`/`height`, and `style="…"`, `<style element="PrimaryButton">` and
  colour attributes style the instance like any other element. Body attributes
  are the base, the instance's win.
- **Events.** An instance is a normal widget in the tree, so [event
  routing](#event-routing) gives a component its own API for free: `ogui.on
  ("cpu", "click", fn)` fires when the user clicks anything inside `cpu`,
  with `info.target` naming the widget actually hit and `id`/
  `info.currentTarget` the component.
- **Scripts.** A `<script>` in a body is copied into every instance, already
  namespaced for it; the parser warns, because it then runs once per instance.
- **Where to declare.** The component catalogue is global to the process, so a
  shared file works: put `<template>` elements in a `<theme>` stylesheet and
  pull it in with `<stylesheet path="themes/kit.xml"/>` (or
  `<manifest theme="themes/kit.xml">`) **before** the screen that uses them.
  A later `<template name="X">` replaces an earlier one; a component name never
  shadows a built-in element or a `widgetRegistry()` entry, which always win.
- **Guards.** Self-expansion reports `template 'X' expands inside itself`, a
  17th level reports `more than 16 components nested`, an empty body reports
  `expands to no markup`, and anything wrong inside a body is re-reported at
  the instance site as `while expanding <Card>: …`, so the message names the
  component the author wrote rather than markup they never saw.

## Elements

### Text

| Element | Typed attributes | Children |
|---|---|---|
| `label` | `text`, `wrap`, `align` | — |
| `badge` | `text`, `count`, `removable` | — |
| `sparkline` | `values` (comma list), `label`, `max` | — |
| `separator` | `thickness` | — |
| `image` | `source`, `tile`, `zoom` (1..8), `pinchzoom` | — |

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
| `statusbar` | flat text band with a hairline separator on top: `left` (also via `text`), `center`, `right`, `color` (left/center text), `dimcolor` (right text), `background`, `size` | — |
| `breadcrumb` | clickable path segments separated by `separator` (default `/`): `path="a/b/c"` or nested `<crumb>` children, `current` (index styled as active, -1/omitted = last), `color`, `dimcolor`, `accent` (separator), `size`; clicking a segment sets `current`, fires its `change` event and any `onSelected` callback | `crumb` (`CrumbItem`) |
| `crumb` | a `breadcrumb` segment (data only): `label`/`text` | — |
| `keybind` | shortcut capture box: click (or first key press) arms capture, then a modifier+key combo records the binding, `Backspace`/`Delete` clears it, `Esc` cancels; combos are `ctrl+alt+shift+super+x` strings (`value` round-trips, invalid ones rejected). Attributes: `value`, `placeholder`, `color`, `background`, `bordercolor`/`border` (focus/capture tint), `radius`, `size` | — |
| `commandpalette` | incremental-search launcher: type to filter (name matches rank above hint matches), `Up`/`Down`/`Home`/`End` walk the matches, `Enter` or a click runs the selection and fires `change`, `Backspace` edits the query, `Esc` fires `submit` so the host can close it; runs land in a deduplicated newest-first history. Attributes: `query`/`filter`, `placeholder`, `maxHistory`, `rowHeight`, `headerHeight`, `color`, `dimcolor`, `accent`, `highlight`, `background`, `size` | `cmd` (`CommandAction`) |
| `cmd` | a `commandpalette` entry (data only): `name`/`label`/`text`, `hint`/`desc`, `key`/`shortcut` | — |

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
| `theme` | root only; a stylesheet — children `palette`, `default`, `style`, `font`, `template`; `base` (`dark`/`light`) |
| `palette` | token colour overrides, only valid inside a `<theme>` root |
| `default` | base Style overrides, only valid inside a `<theme>` root |
| `stylesheet` | merges an external `<theme>` file into this document | `path` |
| `template` | declares a reusable component: `<template name="Card" title="…">markup</template>`; registers `Card` as an element. Data-only, yields no widget of its own — see [Templates](#templates-components) |
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
| `ogui.set(id, attr, v)` | `attr`: `text`, `visible`, `enabled`, `focusable`, `checked`, `value`, `selected`, `tooltip`, `x`, `y`, `width`, `height` (geometry pins the spec to absolute so layout keeps the new values); on a `keybind`, `value` takes a combo string and errors when it does not parse. Any other name falls through to the typed property registry (see Typed properties), so `ogui.set("lbl", "wrap", true)` and `ogui.set("btn", "radius", 8)` work; a value the property rejects raises `property 'wrap' expects true/false/1/0/yes/no/on/off, got 'perhaps'` |
| `ogui.get(id, attr)` | same attrs; `text`/`tooltip` → string, `value`/`selected`/geometry → number (`value` → combo string on a `keybind`), `visible`/`enabled`/`focusable`/`checked` → boolean. Registry names come back typed: `number` → number, `bool` → boolean, the rest as strings; unknown → `nil` |
| `ogui.props(id)` | table of every property the widget exposes, keyed by name: `{ type, value, default, description, writable }` — runtime discovery instead of guessing which attributes a class answers to; `value` is `nil` on a write-only property, and the whole table is `nil` for an unknown id |
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
| `ogui.focus("next")` / `ogui.focus("prev")` | move Tab focus forward/backward; returns the new focused id |
| `ogui.focus(id)` | focus a focusable widget (`true`), or clear focus with `""` |
| `ogui.hovered()` | hovered widget id, or `nil` |
| `ogui.clickCount()` | presses at this spot inside the double-click window |
| `ogui.items(id, tbl)` | replace `listbox`/`optionspin` entries with a Lua array of strings |
| `ogui.list(id, op, ...)` | incremental `listbox`/`virtuallist` row updates — `count`, `get(i)`, `insert(i,text)` (`i` = `-1` appends), `remove(i)`, `set(i,text)`, `scroll(i)` (scrollIntoView). Mutation is silent (no `change` event); selection travels with its row |
| `ogui.prop(id, op, ...)` | `propertygrid` model access — `count`, `get(name)` (nil when unknown), `set(name,value)` (silent, validated per kind), `add(name,type,value[,group[,options]])` (`options` `;`-separated for `enum`), `remove(name)`, `list()` (table `{name=value}`), `selected` / `select(i)` |
| `ogui.cmd(id, op, ...)` | `commandpalette` access — `count`, `add(name[,hint[,key]])`, `remove(name)`, `list()` (table `{name=hint}`), `query` (get) / `query(s)` (set, silent), `matches`, `match(i)` (nil out of range), `exec(name)` (runs + records history, silent), `selected` / `select(i)` (filtered rows), `history()` (array, newest first), `clearhistory` |
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
| `ogui.dump()` | the whole widget tree as text — one line per widget: type, id, parent-relative bounds, absolute origin, then `hidden`/`disabled`/`focusable`/`focused`/`hovered`/`pressed`, `tab=`, `binding=`, `accessible=`, `tooltip=` |
| `ogui.inspect(id)` | table for one widget: `type`, `id`, `binding`, `accessible`, `tooltip`, `x`, `y`, `width`, `height`, `visible`, `enabled`, `focusable`, `focused`, `hovered`, `tabindex` (`-1` when not in the Tab order), `children`; `nil` for an unknown id |
| `ogui.debug()` / `ogui.debug(bits\|string)` | draw-list overlay: `1` bounds, `2` labels, `4` focus order, `8` hover. A string takes words `"bounds labels focus hover"` (`all`/`none` too). No argument reports the current bits; returns them |
| `ogui.stats()` | last frame: `updateMs`, `drawMs`, `widgets`, `tabStops`, `vertices`, `indices`, `cmds` |
| `ogui.on(id, event, fn)` | attach a per-widget listener → integer handle. Events fired: `"click"`, `"focus"`, `"blur"`, `"change"` (user edits a value/selection/text) and `"submit"` (Enter in a `TextInput`). The callback receives `(id, info)` with `info.event`, `info.target`, `info.currentTarget`, `info.phase` (`"target"`/`"bubble"`), `info.source`, `info.x/y`, `info.handled`; a listener on a container id also hears its children's events as they bubble, and `return false` stops the walk ([`docs/events.md`](events.md)) |
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

## Debugging & inspection

The debug tools are built on the kernel and the draw list, not on a parallel
system: the tree dump walks the same `Screen::widgets` the parser filled, the
focus overlay numbers the same Tab order `focusNext()` walks, and the event log
goes through `Kernel::addLog()`, so any `console` widget mirrors it for free.

```cpp
kernel.setDebugOverlay(ogui::Kernel::DebugBounds |      // 1: boxes per depth
                       ogui::Kernel::DebugLabels);      // 2: "Type#id WxH"
kernel.toggleDebugOverlay(ogui::Kernel::DebugFocus);    // 4: Tab order
                                                       // 8: hover highlight
const auto& s = kernel.stats();     // updateMs, drawMs, widgets, tabStops, ...
std::puts(kernel.dumpTree().c_str());
std::puts(ogui::Kernel::describe(kernel.focusedWidget()).c_str());  // "Button ok"
```

`kernel.setDebugEvents(true)` logs every click, focus change and binding
invocation (`event go`, `click Button ok -> go`, `focus Button ok`) into the
kernel log; unbound names are marked `(unbound)`. Off by default, so a release
build pays nothing.

`examples/screenshot.cpp` renders the overlay headlessly:
`./build/ogui_screenshot dbg.png 900 640 1 --debug` (optionally
`--debug bounds,labels,focus,hover`). With any debug bits set the corner
sanity check reports `skipped`, because the hover layer paints over the
background on purpose.

## Error reporting

`XmlParser::parse()` returns `{root, errors, warnings, scripts}`. Errors never abort
the parse: unknown root/element names are logged and skipped, and the caller
always receives a usable tree.

An **attribute whose value does not fit the property it names is an error**. Once
the element exists the parser walks every authored attribute through the typed
property registry — `checkWidgetProperty()` asks what the name expects and
compares, applying nothing — and reports the mismatches with the element's
line:col:

```
25:95: <grid> property 'gap' expects number, got 'ten'
52:66: <section> property 'expanded' expects true/false/1/0/yes/no/on/off, got 'ture'
3:62: <button> attribute "background": unknown palette token '@acent'
3:62: <button> property 'align' expects left|center|right, got 'middle'
3:62: <button> attribute "weird:radius": unknown visual state 'weird'
```

That matters because every concrete setter in the parser is forgiving by design:
`height="abc"` seeds 0, `visible="ture"` reads false, `@nope` draws the window
colour, and the widget looks like an intentional flat dark rectangle. The check
costs nothing at runtime (it runs while parsing) and turns those into a lint hit
for `ogui_validate` and for the `errors` a host already receives.

What stays silent, on purpose: the relative geometry forms (`width="50%"`,
`height="fill"`) that a number type does not describe, and a value that only the
widget's own setter can reject (a `keybind` naming no entry), which is caught when
the widget is built rather than by the registry. Nothing else is silent: every
attribute name `applyAttributes()` asks about is a declared property of the class
it was asking about, and `debug/tests/properties_test.cpp` proves that by tracing
the parser (`XmlParser::setAttributeTrace()`) over one element per tag rather than
by keeping a list of names.

`ogui_validate` (built from `examples/validate.cpp`) is the command-line driver
for that reporting — it parses files or a whole `manifest.xml`, prints each
diagnostic as `error: line:col: message` under the file it came from, and exits
non-zero if anything failed:

```sh
./build/ogui_validate layout_demo.xml
./build/ogui_validate manifest.xml --manifest --chdir .    # every screen
./build/ogui_validate themes/design.xml --theme            # a stylesheet
```

For end-to-end examples — a runnable minimal screen, Lua behaviour, script
loading order, and registering a custom C++ widget — see
[`examples.md`](examples.md).
