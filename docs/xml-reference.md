# OGUI XML reference

Layouts are authored in XML and turned into a widget tree by `XmlParser`
(`src/xmlparser.cpp`). The parser is deliberately forgiving: it never aborts
on a bad element, it records an error and keeps going, so a broken line costs
one widget, not the whole screen.

```
XML text ──XmlParser──▶ widget tree ──Kernel::frame──▶ DrawList ──▶ GL / Software
```

## Attribute naming

Attribute names are **full snake_case** — `background`, `width`, `height`,
`row_height`, `border_width`, `font_size`, ... The old short forms (`bg`, `w`,
`h`, `rowheight`, `borderwidth`, `fontsize`, ...) still parse as **legacy
aliases** and normalise to the same keys, so existing layouts keep working,
but new layouts should use the full names. A few conventional names are not
abbreviations and stay as they are: `x`, `y` (position), `id`, `min`, `max`,
`color`, `text`, `value`, `label`, `title`.

## Document roots

A layout file must open with one of two root elements:

| Root | Meaning |
|---|---|
| `<screen>` | A UI layout. Its children are widgets. Optional `id` (defaults to `screen`). |
| `<manifest>` | A data-only list of screens (used by the demo host to pick up layouts without recompiling). |

`<manifest theme="dark|light">` sets the initial theme and contains one
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

- **Attribute names are case-insensitive** (`FontSize`, `font_size`,
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

## Common attributes (all widgets)

| Attribute | Effect |
|---|---|
| `id` | lookup name (`Kernel::findWidget`) |
| `on_click` / `bind` | action name fired on click |
| `context_menu` | id of a `<contextmenu>` to pop on right-click |
| `tooltip` | hover text |
| `visible` | show/hide |
| `background` | fill colour |
| `color` | foreground/text colour |
| `border` / `border_color` | stroke colour |
| `border_width` | stroke thickness in px |
| `radius` | corner radius |
| `padding` / `margin` | inner / outer spacing |
| `font` / `font_size` | family and size (`font_size` overrides; legacy `font sans 20 color "#66ee66"` pseudo-syntax also works) |
| `shadow` | drop shadow as `"blur[,dy[,dx]]"` |
| `shadow_color` | shadow tint |
| `gradient` | fill gradient |

## Named styles

`<style name="...">` anywhere in a screen declares a reusable attribute set;
`<style name="p" background="@accent" color="#fff" radius="6"/>` or a
non-self-closing `<style>` containing nested `<set .../>` elements. Elements
opt in with `style="p"`; the named style provides the base and the element's
**own attributes win**. Referencing an unknown style warns and is ignored.

```xml
<style name="primary" background="@accent" color="@white" radius="6"/>
<button style="primary" text="Iniciar" width="150" height="38"/>
```

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
| `spinbox` | `min`, `max`, `value`, `step`, `decimals`, `wrap`, `prefix`, `suffix`, `text`, `button_color` | — |
| `optionspin` | `options="a,b,c"`, `value`, `index`, `wrap` | `option` |
| `slider` | `min`, `max`, `value`, `color`, `background`, `thumb_color` | — |
| `rangeslider` | `low`, `high` (plus slider attrs) | — |
| `colorpicker` | `color` | — |
| `pathpicker` | `value`, `placeholder`, `path`, `filter` | — |
| `rampeditor` | `stops` (`ramp` is an alias) | — |
| `curveeditor` | `points` (`curve` is an alias) | — |

### Options & toggles

| Element | Typed attributes | Children |
|---|---|---|
| `button` | `text` | — |
| `checkbox` | `text`, `checked`, `color`, `background` (check colour), `border_color` | — |
| `radiobutton` | `text`, `checked`, `group`, `accent` | — |
| `toggleswitch` / `toggle` | `checked`, `on`, `text` | — |
| `progress` | `value` (0..1), `indeterminate` | — |
| `spinner` | `variant`, `speed` | — |

### Lists & tables

| Element | Typed attributes | Children |
|---|---|---|
| `listbox` | `row_height`, `selected`, `highlight` | `item` (`ListItem`) |
| `item` | `label`/`text`, `value` | — |
| `table` | `row_height`, `header_height`, `selected`, `sortable`, `header_background` | `th`, `tr` |
| `th` | `label`, `width`, `resizable`, `editable`, `scrolling_text` (`automatic`/`onmouseenter`) | — |
| `tr` | `cells="a;b;c"`, `value`, `editable_cells`, `resizable`, `scrolling_text` | `cell` |
| `cell` | `editable`, `resizable`, `scrolling_text`; text content also works (`<cell>Zoe</cell>`) | — |
| `tree` | `row_height`, `indent`, `auto_height`, `selected`, `icons`, `checks`, `guides`, `dir` | `treeitem` |
| `treeitem` | `label`, `value`, `expanded`, `folder`/`file`, `checked` | nested `treeitem` |

`listbox`, `table` and `tree` draw an overlay scrollbar (drag it, or click
empty track to jump) whenever content exceeds the viewport.

### Containers

| Element | Typed attributes | Children |
|---|---|---|
| `panel` | — | any widget |
| `group` | `name` (box caption) | any widget |
| `window` | `title`, `title_visible` | any widget |
| `scrollview` | `scroll_step`, `scroll_x`, `scroll_y` | any widget |
| `column` / `row` | — | any widget (stacked) |
| `grid` | `columns`, `gap`, `row_height` | any widget |
| `splitter` | `orientation`/`vertical`, `split` (0..1), `handle_size`, `handle_color` | two panels |
| `tabs` | `active`, `tab_height`, `header_color`, `tab_color`, `active_color`, `accent` | `tab` |
| `tab` | `label` | any widget |
| `accordion` | `exclusive`, `gap` | `section` |
| `section` | `header`/`text`, `expanded`, `header_height` | any widget |
| `combobox` | `selected`, `row_height`, `highlight`, `panel`, `accent` | `item`/`option` |

Overflowing tab headers collapse into a `...` popup menu automatically.

### Menus & overlays

| Element | Typed attributes | Children |
|---|---|---|
| `menubar` | — | `menu` |
| `menu` | `label` | `menuitem`, `menuseparator`, nested `menu` |
| `menuitem` | `label`/`text`, `command` | — |
| `menuseparator` | — | — |
| `contextmenu` | located by its `id` (pointed to from `context_menu="..."` on any widget) | `menuitem` |
| `modal` | `title`, `open`, `panel_width`, `panel_height` | any widget |

### Data-only

| Element | Notes |
|---|---|
| `manifest` | root only; `theme`, children `screen title path` |
| `option` | entry of `optionspin`/`combobox` |
| `item` | entry of `listbox`/`combobox` |

## Error reporting

`XmlParser::parse()` returns `{root, errors, warnings}`. Errors never abort
the parse: unknown root/element names are logged and skipped, and the caller
always receives a usable tree.
