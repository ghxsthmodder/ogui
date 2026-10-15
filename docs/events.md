# Events and propagation

ogui separates two things that are easy to confuse: the **raw input** the host
feeds the kernel, and the **semantic events** the kernel derives from it and
routes through the tree. Raw input stays where it landed. Semantic events travel
upwards.

## The kinds

| Kind | C++ | Lua name | Fired when |
|---|---|---|---|
| Click | `EventKind::Click` | `"click"` | press and release land on the same widget (left button), or Enter/Space activates the focused button |
| Change | `EventKind::Change` | `"change"` | the *user* edits a value, selection or text |
| Submit | `EventKind::Submit` | `"submit"` | the user presses Enter in a `textinput` |
| Focus | `EventKind::Focus` | `"focus"` | a widget gains keyboard focus |
| Blur | `EventKind::Blur` | `"blur"` | a widget loses keyboard focus |
| LongPress | `EventKind::LongPress` | `"longPress"` | a finger stayed on one widget for half a second (`kHoldSeconds`), without travelling past the pan slop |

`Event` carries everything a handler needs:

```cpp
struct Event {
    EventKind kind;              // what happened
    EventSource source;          // Pointer | Keyboard
    Widget* target;              // the widget it happened to
    Widget* currentTarget;       // the widget being visited right now
    Offset2D point;              // logical coords of a pointer click
    bool handled;                // a C++ action already consumed it
    bool stopped;                // the walk was cut short
    void stopPropagation();
    bool isTargetLevel() const;  // currentTarget == target
};
```

## The walk

`Kernel::dispatchEvent` visits the target, then each ancestor in turn, up to the
root of the screen. At every stop it calls `Widget::onEvent` and then fires the
Lua listeners registered for that widget's id. Nothing at one stop is skipped
because of what an earlier stop did.

```cpp
class Form : public ogui::Panel {
public:
    void onEvent(ogui::Event& e) override {
        ogui::Panel::onEvent(e);
        if (e.kind == ogui::EventKind::Change) {
            m_dirty = true;              // one hook for every field inside
            if (e.target->id() == "pass") e.stopPropagation();
        }
    }
private:
    bool m_dirty = false;
};
```

```lua
-- the container hears each child's click without one listener per child
ogui.on("form", "click", function(id, info)
  ogui.log(info.event .. " on " .. info.target .. " (" .. info.phase .. ")")
  if info.target == "close" then return false end   -- stop the walk here
end)
```

The Lua callback gets `(id, info)`, where `info` is a table:

| Field | Meaning |
|---|---|
| `info.event` | `"click"` \| `"change"` \| `"submit"` \| `"focus"` \| `"blur"` |
| `info.target` | id of the widget the event happened to |
| `info.currentTarget` | id of the widget being visited now |
| `info.phase` | `"target"` on the first stop, `"bubble"` on every ancestor |
| `info.source` | `"pointer"` or `"keyboard"` (clicks only) |
| `info.x`, `info.y` | logical click position; 0 for the other kinds |
| `info.handled` | 1 when a C++ action already consumed the click |

**Stopping.** `Event::stopPropagation()` in C++, `return false` from a Lua
listener. The walk ends after the current stop — but the *other* listeners
registered on that same widget still run, exactly like the DOM's
`stopPropagation` (not `stopImmediatePropagation`). A listener that returns
nothing (`nil`) keeps the event alive, so every listener written before
propagation existed behaves the same as before.

## What does not propagate, and why

* **Raw mouse moves, wheel steps and key presses** go to one widget: the one
  that was hit, or the one that holds focus. They are positional and streaming —
  handing a wheel step to every ancestor too would scroll the panel and its
  window and the screen at once. The one exception is deliberate: a scroll is
  offered to the *nearest ancestor that can scroll* (`wantsScroll`), which is a
  hit-test rule that picks a single receiver, never a broadcast. A finger's pan
  keeps that shape: `Kernel::updatePan` walks the pressed widget and then its
  ancestors until one answers `wantsPan`, releases the capture it took over, and
  hands only that widget the displacement.
* **Actions do not bubble.** `onclick="name"`, the widget's own id as an action
  name, and a `Kernel::bind` handler are resolved against the pressed widget
  only, so a click on a Save button never runs the enclosing panel's action.
  Listeners are observers; actions are the widget's own.
* **The fall-through hook keeps the old addressing.** When nothing C++-bound
  handled a click, `ogui_on_click(key)` receives the `onclick` name when the
  widget has one and its id otherwise. Per-widget listeners are keyed by widget
  id, always.

## Lifetime during a walk

A handler may legally remove the widget the event is about — `ogui.remove` in a
click listener is the common case. `Kernel::dispatchEvent` collects the whole
chain as `shared_ptr`s before calling anything, so the ancestors stay reachable
and the removed widget stays alive until the walk ends: the event finishes its
route over a tree that no longer contains its target, and the widget is freed
afterwards. `Kernel::forgetWidget` additionally clears the cached pointers —
press, focus, hover, pan, fling, hold, pinch and the tooltip — over the whole
subtree the removal takes with it, which is why the kernel does not click into
freed memory on the next frame. A handler that clicks *another* widget nests: the inner route finishes
and restores the outer one, so the original click still travels.

## The two gestures a finger holds instead of moving

**Long-press** (`longPress`) is an event, because it is something that happened
*to* a widget: `Kernel::updateLongPress` arms a hold only when the pointer came
from `InputState::touches` (a mouse button held down is the first frame of a
selection, and right-click already owns the desktop context menu), keeps it armed
while the finger stays inside the pan slop, and past `kHoldSeconds` fires the
route. Before dispatching it the kernel releases the widget it was pressing — so
letting go afterwards clicks nothing — and opens the nearest ancestor's
`contextmenu` at the finger, exactly where a right-click would have put it.

```lua
ogui.on("row", "longPress", function(id, info)
  ogui.toast("selected " .. id)
end)
```

**Pinch** is deliberately *not* an event. A pinch is not something that happened
to a widget, it is a widget holding two fingers: it is continuous, it has to be
claimed by exactly one receiver, and its answer is a mutation of that widget's
own state. So it goes through the same claim shape the pan uses — `Widget::wantsPinch()`
asked of the pressed widget and then of each ancestor, first one to answer yes
owns the pair — and the receiver gets `onPinch(factor, pivot)`. `factor` is this
frame's finger distance divided by the last frame's, so a widget multiplies what
it already holds by it and keeps no gesture memory; `pivot` is the point between
the fingers in logical screen coordinates. Rotation is left out because nothing
in the library can rotate a subtree, so there would be nothing to hand an angle
to. Claiming the pinch releases the press without clicking and takes the fingers
away from the pan for as long as the pair holds; when only one finger is left the
one-finger machine owns it again.

`Image` is the consumer shipped with the library: `<image pinchzoom="true">`
magnifies into its own texture (`zoom`, clamped 1..8) with the point under the
fingers held still, while the page behind it stays put.

## Programmatic writes stay silent

`ogui.set`, `setValue`, `setState` and every other setter never announce a
change. Only user interaction fires `change`/`submit`. That is what lets a
listener write back into the widget it came from without echoing into an
endless loop, and it is why `ogui.bind` mirrors only on user events.

When a host *does* want the route a user edit takes — a value arriving from the
network, say, that should refresh the form — it posts the event itself:

```cpp
ogui::Event e;
e.kind = ogui::EventKind::Change;
e.target = kernel.findWidget("count");
kernel.dispatchEvent(e);          // listeners and ancestors run, exactly as if
                                  // the user had typed into that widget
```
