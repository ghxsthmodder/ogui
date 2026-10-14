# Examples

Every XML and Lua block in this page is executed by `ctest` (test name
`examples`): XML blocks must parse without errors, Lua blocks must load and run
their `ogui_on_load` hook without a script error, and the layouts under
`examples/` are driven through a real `Kernel`. The C++ blocks are the same code
as `examples/custom_widget.cpp`. The snippets there are shortened for reading; the
file itself is the version that compiles and self-tests as the `custom-widget`
test. An example that stops matching the library fails the
build instead of rotting quietly. See
[xml-reference.md](xml-reference.md) for the attribute-by-attribute reference.

## The smallest layout

`examples/hello.xml` is the shortest document that renders: one `<screen>`
root, one label, one button.

```xml
<screen id="hello">
  <label id="greeting" text="Boas-vindas!" x="20" y="20" width="240" height="24"/>
  <button id="go" text="Continuar" onClick="onGo" x="20" y="56" width="120" height="32"/>
</screen>
```

Host code for it — parse, push, one frame — is the whole contract ogui asks of
an application ([Quick start](../README.md#quick-start) covers the GL side):

```cpp
ogui::XmlParser parser;
auto res = parser.parseFile("examples/hello.xml");
if (!res.root) { /* res.errors: "line:col: message" */ }

auto screen = std::make_shared<ogui::Screen>("hello");
screen->widgets.push_back(res.root);
screen->width  = 400.0f;         // device pixels
screen->height = 300.0f;

ogui::Kernel kernel;
kernel.init();
kernel.pushScreen(screen);
kernel.bind("onGo", [] { /* onClick="onGo" lands here */ });

ogui::InputState in;             // filled from your window proc
in.time = 0.0f;
kernel.frame(in);                // events -> layout -> DrawList
```

`onClick` names a binding; the kernel resolves it against `Kernel::bind` first
and only falls through to the screen's Lua `ogui_on_click` hook when no C++
handler exists. That order is what lets a screen be scripted before it is
written in C++, and never steals an event from a handler that does exist.

## A counter with behaviour

`examples/counter.xml` is the minimal layout that has *behaviour*: two buttons,
a label, a progress bar, and one `<script>` chunk covering the four things a
screen usually needs — lifecycle, events, timers and a data binding.

```xml
<screen id="counter">
  <label id="title" text="Counter" x="20" y="16" width="200" height="22"/>
  <label id="count" text="0" x="20" y="44" width="80" height="40" fontSize="24"/>
  <button id="inc" text="+1" x="110" y="44" width="60" height="40"/>
  <button id="dec" text="-1" x="176" y="44" width="60" height="40"/>

  <!-- bound to the counter: no script code keeps them in sync -->
  <progress id="meter" value="0" x="20" y="96" width="216" height="16"/>

  <script>
n = 0

function ogui_on_load()            -- once, right after this chunk ran
  ogui.set("count", "text", tostring(n))
  ogui.log("counter: loaded")
end

function ogui_on_show()            -- every time the screen comes forward
  ogui.after(0.5, function()       -- one-shot: fires from the frame loop
    ogui.toast("counter pronto")
  end)
end

function ogui_on_click(key)        -- clicks with no C++ binding land here
  if key == "inc" then n = n + 1
  elseif key == "dec" then n = n - 1
  end
  ogui.set("count", "text", tostring(n))
  ogui.set("meter", "value", math.max(0, math.min(1, n / 10)))
end

function ogui_on_update(t, dt)     -- every frame
  -- a slow drift keeps the meter alive without a hand-written loop
end
  </script>
</screen>
```

A `<button>` with no `onClick` reports its own id to `ogui_on_click`, which is
why the handler switches on `"inc"` / `"dec"`. `ogui.log` writes to
`Kernel::addLog`, so a `<console>` widget on the same screen shows the trail
without extra plumbing.

## Text in other scripts

Text is UTF-8 from the parser through the rasterizer, so a layout can carry
more than one script and the host does not have to know about fonts:

```xml
<screen id="i18n">
  <label id="ru" text="Европа Привет" x="20" y="20" width="240" height="24"/>
  <label id="he" text="שלום עולם" x="20" y="52" width="240" height="24" family="sans"/>
  <label id="th" text="สวัสดี" x="20" y="84" width="240" height="24"/>
  <label id="de" text="Grüße, Straße" x="20" y="116" width="240" height="24" wrap="true"/>
</screen>
```

`ru` and `he` are baked on demand: the first frame that needs a Cyrillic or
Hebrew code point rasterizes it into its own reserved atlas cell. `he` asks for
`sans`, and a code point the requested family lacks is baked from the first
family in the fallback chain that carries it — that is how a `serif` label gets
the dingbats DejaVu Serif does not draw. `th` is whatever the box has for it,
and if that is nothing you get one hollow `.notdef` box per character rather
than a blank row. `de` uses `wrap`, and a `Grüße`-style word breaks at the
space, never mid-cluster — backspace in a `textinput` deletes the base plus its
marks as one unit too. `docs/unicode.md` explains the atlas and the fallback
chain; `docs/xml-reference.md` has the attribute-level summary.

## Loading order and scopes

One Lua state per `Kernel`, rebuilt whenever the foreground screen changes:

1. `XmlParser::parse()` collects the screen's `<script>` chunks in document
   order into `ParseResult::scripts` — `{payload, isFile}` — and the host moves
   them onto `Screen::scripts`. `<script src="file.lua"/>` is resolved against
   the **working directory**, exactly like `<stylesheet path>`.
2. `pushScreen` / `switchScreen` / `popScreen` call `ScriptHost::clear()` —
   `lua_close`, then timers, listeners, tweens and bindings go with it — and
   load the new screen's chunks in order.
3. Each chunk runs as a plain main chunk in the **shared globals** (no
   per-screen sandbox, no module wrapper), and is followed immediately by
   `ogui_on_load` if the script defined it.
4. After the last chunk, `ogui_on_show` fires once for the screen.
5. From then on the engine calls `ogui_on_update(t, dt)` every frame,
   `ogui_on_click(key)` for unhandled clicks, `ogui_on_event(name, key)` for
   unhandled bindings, and `ogui_on_hide` → `ogui_on_unload` when the screen
   leaves the foreground.

Consequences worth designing around:

- **Screen state is not persisted.** Globals are re-created on every switch, so
  a value that must survive a screen change belongs in C++ (`Kernel` data, a
  host-side model) or in a file the next screen reads — not in a global.
- **Names are global across the screen's chunks.** Two `<script>` elements in
  one screen share the globals table; use `local` at the top of a chunk, or
  prefix names, when a screen mixes files.
- **The `ogui` table is the engine's, not yours.** It is re-created with the
  state and is the same table on every call; the standard libraries are open,
  so `string`/`math`/`table` are available and `os`/`io` exist but are not part
  of what ogui promises on every platform.
- **Hooks are looked up by name at fire time.** Defining `ogui_on_update` after
  load still works. Overwriting the engine's table (`ogui = {}`) is not checked
  at assignment time — the next `ogui.set` simply fails as a Lua error, which
  the kernel logs into `Kernel::logs()` and carries on without.

The event-side helpers, in the shape the docs promise:

```lua
function ogui_on_load()
  -- per-widget listeners: "click", "focus", "blur", "change", "submit"
  local h = ogui.on("inc", "click", function(id) ogui.log("clicked " .. id) end)
  ogui.off(h)                                   -- or ogui.off("inc") for all

  -- timers, both driven by the frame clock (never by a thread)
  local once   = ogui.after(2.0, function() ogui.set("count", "text", "two") end)
  local ticker = ogui.every(1.0, function() ogui.log("tick") end)
  ogui.cancel(once); ogui.cancel(ticker)

  -- declarative mirrors: source attribute -> target attribute
  ogui.bind("meter", "value", "count", "value")            -- one-way
  ogui.bind("count", "text", "count", "value", true)       -- two-way
end
```

`ogui.bind` copies once at call time and then on every `"change"`/`"submit"`
the *user* produces; `ogui.set` is deliberately silent, which is what keeps a
binding from echoing back into an infinite loop.

## Registering a widget in C++

`widgetRegistry()` (`include/ogui/widgets.hpp`) is the map the parser builds
elements from: `element name -> factory`. It is a plain `std::map`, the library
fills it lazily, and a host may add — or replace — entries before parsing:

```cpp
class Led : public ogui::Widget {
public:
    void setOn(bool on) { m_on = on; }

    void onTick(float dt) override {                  // every frame
        ogui::Widget::onTick(dt);
        if (m_on) m_phase += dt * 4.0f;
    }
    void onPointerUp(ogui::MouseButton btn) override { // click toggles
        if (btn == ogui::Mouse_Left) m_on = !m_on;
    }
    void onDraw(ogui::Rasterizer& r) const override {
        const ogui::Rect b = bounds();
        const float d = std::min(b.w, b.h);
        const ogui::Rect circle{b.x + (b.w - d) * 0.5f, b.y + (b.h - d) * 0.5f, d, d};
        r.drawRoundedRect(circle, ogui::withAlpha(style().textColor, 200), d * 0.5f);
    }

private:
    bool m_on = false;
    float m_phase = 0.0f;
};

// The factory gets the inherited Style and the element's authored attributes
// (keys lowercased), which is where the widget reads values the parser knows
// nothing about.
std::shared_ptr<ogui::Widget> makeLed(const ogui::Style&,
                                      const std::map<std::string, std::string>& attrs) {
    auto led = std::make_shared<Led>();
    auto on = attrs.find("on");
    if (on != attrs.end()) led->setOn(on->second == "true" || on->second == "1");
    led->setFocusable(true);
    return led;
}

void registerWidgets() {
    ogui::widgetRegistry()["led"] = &makeLed;   // before any parse
}
```

`<led id="power" on="true" x="16" y="40" width="24" height="24" color="@accent"/>`
is then an ordinary citizen of the tree: `id` lookup, geometry, styles,
children, hit-testing, Tab focus, `ogui.set`/`ogui.get`, and the debug overlay
(#27) prints its class name from RTTI, so there is no name table to maintain.
`examples/custom_widget.cpp` is this widget with the checks that exercise it,
built as `ogui_custom_widget` and run by `ctest` as `custom-widget`.

Three rules make a custom element behave like the built-ins:

- **Attribute keys are lowercased** before the factory sees them, matching the
  case-insensitive rule of the parser; element names are exact lowercase.
- **Anything the parser recognises is applied after the factory returns** —
  `id`, geometry, `color`/`background` into `Style`, `focusable`, `tooltip`.
  Read your own fields from the factory's map (or `rawAttribute("key")` later);
  do not fight `applyStyle`.
- **Unknown elements are skipped with an error**, and the rest of the document
  still parses — so a missing registration shows up as
  `unknown element <led>`, not as a crash.

## What a mistake looks like

Validation is the parser's job and `ogui_validate` is its command-line front
end; both report `line:col: message` and keep going, so one pass finds every
problem:

```xml expect-error
<screen id="typo">
  <label id="l" text="fine"/>
  <labell id="k" text="element name mistyped"/>
</screen>
```

```sh
$ ./build/ogui_validate examples/hello.xml
examples/hello.xml: ok — 3 widgets, 0 scripts, 0 warning(s)
validate: 0 error(s), 0 warning(s)

$ ./build/ogui_validate manifest.xml --manifest --chdir .    # every screen
$ ./build/ogui_validate themes/design.xml --theme            # a stylesheet
```

Errors mean the layout will lose a widget at runtime; warnings mean it kept
something but had to guess (`unknown style "menu-on"`, a `<style>` with no
name). The exit code is 0 clean, 1 errors, 2 unreadable input, which is what
lets an editor save hook or a CI step call the same binary the docs quote.

## Running the examples

```sh
./scripts/build.sh                 # host build + ctest (11 tests)
./build/ogui_validate manifest.xml --manifest --chdir .
./build/ogui_screenshot out.png 800 600 0          # layout #0, headless PNG
./build/ogui_screenshot dbg.png 900 640 1 --debug  # with the debug overlay
./build/ogui_custom_widget                         # the widget registration
```

`examples/demo.cpp` is the headless loop for the full gallery in
`manifest.xml`; `examples/demo_win32.cpp` (`debug/demo.exe`) is the same
gallery in a window, and `examples/screenshot.cpp` renders any of its screens
to a PNG with no GL at all.
