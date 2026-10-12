#pragma once
#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include "ogui/widget.hpp"
#include "ogui/kernel.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/font.hpp"
#include "ogui/tween.hpp"
#include <vector>
#include <memory>
#include <string>
#include <map>
#include <functional>

namespace ogui {

// Forward declarations of concrete widgets
class Label;
class Button;
class Image;
class Panel;
class Window;
class ScrollView;
class TextInput;
class Row;
class Column;
class Group;
class ComboOption;
class ComboPopup;
class ComboBox;

// ------------------------------------------------------------------
// Concrete widgets
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// Label: text
// ------------------------------------------------------------------
class Label : public Widget {
public:
    Label();
    virtual ~Label() override;

    Label(const Label&) = delete;
    Label& operator=(const Label&) = delete;

    Label(Label&&) = default;
    Label& operator=(Label&&) = default;

    void setText(const std::string& text) { m_text = text; }
    const std::string& text() const { return m_text; }

    void setFontFamily(const std::string& family) { m_fontFamily = family; }
    void setFontSize(int size) { m_fontSize = size; }
    void setTextColor(const Color& col) { m_textColor = col; }
    const std::string& fontFamily() const { return m_fontFamily; }
    int fontSize() const { return m_fontSize; }
    const Color& textColor() const { return m_textColor; }
    void setHAlign(Align a) { m_hAlign = a; }
    void setVAlign(VertAlign v) { m_vAlign = v; }

    // Word wrap inside the widget bounds (single line when false).
    void setWrap(bool w) { m_wrap = w; }
    bool wrap() const { return m_wrap; }

    // Height the text needs when wrapped to `width` (0 when there is no
    // text/font). Used by Column to auto-size wrapped labels.
    float measureHeight(float width) const;

    virtual void onDraw(Rasterizer& r) const override;
    virtual void layout(float parentWidth, float parentHeight) override;

private:
    std::string m_text;
    std::string m_fontFamily;
    int m_fontSize = 14;
    Color m_textColor{ogui::hexColor("#ccc")};
    Align m_hAlign = Align::Left;
    VertAlign m_vAlign = VertAlign::Top;
    bool m_wrap = false;
};

// ------------------------------------------------------------------
// Button
// ------------------------------------------------------------------
class Button : public Widget {
public:
    Button();
    virtual ~Button() override;

    void setText(const std::string& text) { m_text = text; }
    const std::string& text() const { return m_text; }

    void setFontFamily(const std::string& family) { m_fontFamily = family; }
    void setFontSize(int size) { m_fontSize = size; }
    void setTextColor(const Color& col) { m_textColor = col; }
    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;

    virtual void onDraw(Rasterizer& r) const override;

    bool pressed() const { return m_pressed; }

private:
    std::string m_text;
    std::string m_fontFamily;
    int m_fontSize = 14;
    Color m_textColor{ogui::hexColor("#ffffff")};
    Color m_bgColor{ogui::hexColor("#4488ff")};
    Color m_borderColor{ogui::hexColor("#3366cc")};
    float m_borderWidth = 1.0f;
    float m_radius = 4.0f;
    bool m_pressed = false;
};

// ------------------------------------------------------------------
// Image
//
// Draws an RGBA image file (PNG/JPG/... decoded through ImageManager)
// inside its bounds. Without tile the texture is stretched to the
// bounds; with tile it is repeated at native size. A `border` style
// draws a frame on top. An unreadable path simply draws nothing.
// ------------------------------------------------------------------
class Image : public Widget {
public:
    Image();
    virtual ~Image() override;

    void setImagePath(const std::string& path) { m_path = path; }
    const std::string& imagePath() const { return m_path; }
    void setTile(bool t) { m_tile = t; }
    bool tile() const { return m_tile; }

    virtual void onDraw(Rasterizer& r) const override;

private:
    std::string m_path;
    bool m_tile = false;
};

// ------------------------------------------------------------------
// Panel: solid background + children
// ------------------------------------------------------------------
class Panel : public Widget {
public:
    Panel();
    virtual ~Panel() override;

    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }

    virtual void onDraw(Rasterizer& r) const override;

protected:
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
    float m_borderWidth = 0.0f;
    float m_radius = 0.0f;
};

// ------------------------------------------------------------------
// Window: panel with a title bar, min/max/close buttons
// ------------------------------------------------------------------
class Window : public Panel {
public:
    Window();
    virtual ~Window() override;

    void setTitle(const std::string& t) { m_title = t; }
    const std::string& title() const { return m_title; }
    void setTitleVisible(bool v) { m_titleVisible = v; }
    float titleHeight() const { return m_titleVisible ? m_titleHeight : 0.0f; }

    // Positions children below the title bar.
    virtual void layout(float parentWidth, float parentHeight) override;

    virtual void onDraw(Rasterizer& r) const override;

private:
    std::string m_title;
    bool m_titleVisible = true;
    float m_titleHeight = 24.0f;
    // Title-bar offset already applied to children by the previous layout()
    // pass; layout() runs every frame, so the old offset must be undone
    // before computing the new one.
    float m_childrenOffsetY = 0.0f;
    bool m_dragging = false;
};

// ------------------------------------------------------------------
// ScrollView: clipped, wheel-scrollable viewport over its children.
//
// Children keep their authored bounds; whatever they occupy (their bounding
// box) is the scrollable content, so a page can be authored directly in XML.
// The Kernel asks childOffset() for the scroll delta while drawing and
// hit-testing, clipsChildren() keeps the content inside the viewport, and
// wantsScroll() routes wheel events here from anywhere below.
// ------------------------------------------------------------------
class ScrollView : public Widget {
public:
    ScrollView();
    virtual ~ScrollView() override;

    // Convenience handle for a content widget; it is adopted as a child so
    // the tree walk draws it. The scrollable area is all children either way.
    void setContent(std::shared_ptr<Widget> content);
    const std::shared_ptr<Widget>& content() const { return m_content; }

    // Scroll position in pixels (0,0 = content at the top-left), clamped to
    // the content on every call and after every layout pass.
    void setScroll(float x, float y);
    void scrollBy(float dx, float dy);
    Offset2D scrollPosition() const { return m_offset; }
    void setScrollStep(float px) { m_step = px; }    // pixels per wheel notch
    const Rect& contentBounds() const { return m_contentBounds; }

    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual Offset2D childOffset() const override { return m_offset; }
    virtual bool clipsChildren(Rect& outRect) const override;
    virtual bool wantsScroll() const override { return true; }
    virtual void onPointerScroll(float dx, float dy) override;

private:
    void clampScroll();

    std::shared_ptr<Widget> m_content;
    Rect m_viewport{0.0f, 0.0f, 0.0f, 0.0f};      // within this widget's bounds
    Rect m_contentBounds{0.0f, 0.0f, 0.0f, 0.0f}; // union of the children
    Offset2D m_offset;      // scroll delta (<= 0 in both axes)
    float m_step = 40.0f;   // pixels per wheel notch
    float m_borderWidth = 0.0f;
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
};

// ------------------------------------------------------------------
// TextInput: editable single line
// ------------------------------------------------------------------
class TextInput : public Widget {
public:
    TextInput();
    virtual ~TextInput() override;

    // Replaces the text and moves the caret to the end.
    virtual void setText(const std::string& text);
    const std::string& text() const { return m_text; }
    void setPlaceholder(const std::string& ph) { m_placeholder = ph; }
    void setBgColor(const Color& col) { m_bgColor = col; }
    void setFgColor(const Color& col) { m_fgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }
    void setFontSize(int s) { m_fontSize = s; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onKeyUp(KeyCode key) override;
    virtual void onChar(uint32_t ch) override;
    virtual void onFocus(bool focused) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onPointerScroll(float dx, float dy) override;
    virtual bool wantsScroll() const override { return true; }

    bool focused() const { return m_focused; }
    bool hasSelection() const { return m_selectionStart != m_selectionEnd; }

    // Caret and selection as byte offsets into text() (UTF-8 boundaries).
    size_t caret() const { return m_caret; }
    size_t selectionStart() const { return m_selectionStart; }
    size_t selectionEnd() const { return m_selectionEnd; }
    std::string selectedText() const;
    // Selects [start, end); the caret lands on `end`. Clamped to the text.
    void setSelection(size_t start, size_t end);

protected:
    // Clamps caret/anchor, refreshes [start,end] and scrolls the caret into
    // view (the text slice is what gets drawn when the line overflows).
    virtual void syncState();
    // Caret index for a click at widget-local x.
    size_t indexAtX(float lx) const;
    // Deletes the selection and puts the caret where it started.
    void deleteSelection();
    // Replaces the selection (if any) with `s` and advances the caret.
    void insertText(const std::string& s);

    std::string m_text;
    std::string m_placeholder;
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_fgColor{ogui::hexColor("#ccc")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
    float m_borderWidth = 1.0f;
    float m_radius = 4.0f;
    int m_fontSize = 14;
    bool m_focused = false;
    bool m_dragging = false;      // pointer is selecting
    // Last pointer x/y processed while dragging. The kernel delivers a
    // move with the press position in the same frame; applying it would
    // undo a double/triple-click selection (it re-parks the caret on the
    // click point).
    Offset2D m_lastMove{-1.0f, -1.0f};
    float m_scrollX = 0.0f;       // horizontal scroll of the text slice
    size_t m_caret = 0;           // insertion point
    size_t m_selAnchor = 0;       // fixed end of the selection
    size_t m_selectionStart = 0;  // min(anchor, caret)
    size_t m_selectionEnd = 0;    // max(anchor, caret)
};

// ------------------------------------------------------------------
// Generic layout containers
// ------------------------------------------------------------------

// Column: stacks children vertically
class Column : public Widget {
public:
    Column();
    virtual ~Column() override;
    virtual void layout(float parentWidth, float parentHeight) override;
};

// Row: lays children horizontally
class Row : public Widget {
public:
    Row();
    virtual ~Row() override;
    virtual void layout(float parentWidth, float parentHeight) override;
};

// Group: draws a named named style background + border
class Group : public Widget {
public:
    Group();
    virtual ~Group() override;
    void setGroupName(const std::string& name) { m_groupName = name; }
    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    virtual void onDraw(Rasterizer& r) const override;

private:
    std::string m_groupName;
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
    float m_borderWidth = 0.0f;
};

// ------------------------------------------------------------------
// Checkbox: square toggle + caption
// ------------------------------------------------------------------
class Checkbox : public Widget {
public:
    using ValueCallback = std::function<void(bool)>;

    Checkbox();
    virtual ~Checkbox() override;

    void setText(const std::string& text) { m_text = text; }
    const std::string& text() const { return m_text; }
    void setChecked(bool v) { m_checked = v; }
    bool checked() const { return m_checked; }
    // Fires only on user interaction (click / keyboard), not on setChecked().
    void setOnChanged(ValueCallback cb) { m_onChanged = std::move(cb); }

    void setFontFamily(const std::string& family) { m_fontFamily = family; }
    void setFontSize(int size) { m_fontSize = size; }
    void setTextColor(const Color& col) { m_textColor = col; }
    void setBoxColor(const Color& col) { m_boxColor = col; }
    void setCheckColor(const Color& col) { m_checkColor = col; }
    const std::string& fontFamily() const { return m_fontFamily; }
    int fontSize() const { return m_fontSize; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void toggle();

    std::string m_text;
    std::string m_fontFamily;
    int m_fontSize = 14;
    Color m_textColor{ogui::hexColor("#ccc")};
    Color m_boxColor{ogui::hexColor("#4488ff")};
    Color m_checkColor{ogui::hexColor("#4488ff")};
    bool m_checked = false;
    ValueCallback m_onChanged;
};

// ------------------------------------------------------------------
// Separator: a 1px (or thicker) rule, horizontal or vertical
// ------------------------------------------------------------------
class Separator : public Widget {
public:
    Separator();
    virtual ~Separator() override;

    void setThickness(float t) { m_thickness = t; }
    float thickness() const { return m_thickness; }
    void setLineColor(const Color& col) { m_lineColor = col; }
    const Color& lineColor() const { return m_lineColor; }

    virtual void onDraw(Rasterizer& r) const override;

private:
    float m_thickness = 1.0f;
    Color m_lineColor{ogui::hexColor("#3a3a3a")};
};

// ------------------------------------------------------------------
// ProgressBar: value 0..1 filled from the left
// ------------------------------------------------------------------
class ProgressBar : public Widget {
public:
    ProgressBar();
    virtual ~ProgressBar() override;

    void setValue(float v);
    float value() const { return m_value; }
    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBarColor(const Color& col) { m_barColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }

    virtual void onDraw(Rasterizer& r) const override;

private:
    float m_value = 0.0f;
    float m_radius = 4.0f;
    float m_borderWidth = 1.0f;
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_barColor{ogui::hexColor("#4488ff")};
    Color m_borderColor{ogui::hexColor("#3a3a3a")};
};

// ------------------------------------------------------------------
// Slider: draggable thumb over a track, value in [min,max]
// ------------------------------------------------------------------
class Slider : public Widget {
public:
    using ValueCallback = std::function<void(float)>;

    Slider();
    virtual ~Slider() override;

    void setRange(float lo, float hi);
    void setValue(float v);
    float value() const { return m_value; }
    float minimum() const { return m_min; }
    float maximum() const { return m_max; }
    // Fires only when the value actually moves (drag or arrow keys).
    void setOnChanged(ValueCallback cb) { m_onChanged = std::move(cb); }

    void setTrackColor(const Color& col) { m_trackColor = col; }
    void setBarColor(const Color& col) { m_barColor = col; }
    void setThumbColor(const Color& col) { m_thumbColor = col; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    // Maps a pointer x (widget-local) onto the range and commits it.
    void valueFromLocalX(float localX);
    void commit(float v);                  // clamp + store + notify
    float tFromValue() const;              // value -> 0..1

    float m_min = 0.0f;
    float m_max = 1.0f;
    float m_value = 0.0f;
    bool m_dragging = false;
    float m_trackThickness = 4.0f;
    float m_thumbWidth = 12.0f;
    Color m_trackColor{ogui::hexColor("#3a3a3a")};
    Color m_barColor{ogui::hexColor("#4488ff")};
    Color m_thumbColor{ogui::hexColor("#cccccc")};
    ValueCallback m_onChanged;
};

// ------------------------------------------------------------------
// SpinBox: numeric value with a step and +/- buttons on each side. Click a
// button (or focus it and press Up/Down/Left/Right, Home/End) to change the
// value. The number is drawn centred with an optional prefix/suffix.
// ------------------------------------------------------------------
class SpinBox : public Widget {
public:
    using ValueCallback = std::function<void(float)>;

    SpinBox();
    virtual ~SpinBox() override;

    void setRange(float lo, float hi);
    void setStep(float s) { m_step = s > 0.0f ? s : 1.0f; }
    void setDecimals(int d) { m_decimals = d < 0 ? 0 : d; }
    void setValue(float v);
    float value() const { return m_value; }
    float minimum() const { return m_min; }
    float maximum() const { return m_max; }
    void setWrap(bool w) { m_wrap = w; }
    void setPrefix(const std::string& p) { m_prefix = p; }
    void setSuffix(const std::string& s) { m_suffix = s; }
    // Fires only when the value actually moves (button or key).
    void setOnChanged(ValueCallback cb) { m_onChanged = std::move(cb); }

    void setBgColor(const Color& col) { m_bgColor = col; }
    void setTextColor(const Color& col) { m_textColor = col; }
    void setButtonColor(const Color& col) { m_buttonColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setRadius(float r) { m_radius = r; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void stepBy(int dir);              // dir = +1 up / -1 down (wraps if set)
    std::string formatValue() const;

    float m_min = 0.0f;
    float m_max = 100.0f;
    float m_value = 0.0f;
    float m_step = 1.0f;
    int m_decimals = 0;
    bool m_wrap = false;
    std::string m_prefix;
    std::string m_suffix;
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_textColor{ogui::hexColor("#ccc")};
    Color m_buttonColor{ogui::hexColor("#333")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
    float m_radius = 4.0f;
    std::string m_fontFam = "sans";
    int m_fontSize = 14;
    ValueCallback m_onChanged;
};

// ------------------------------------------------------------------
// OptionSpin: the numeric SpinBox applied to text-only options.
// Draws [ < ][ option ][ > ] as one unified widget; the arrows cycle
// through setOptions() (wrapping by default). Keys: Left/Down back,
// Right/Up forward.
// ------------------------------------------------------------------
class OptionSpin : public Widget {
public:
    using OptionCallback = std::function<void(int index, const std::string& value)>;

    OptionSpin();
    virtual ~OptionSpin() override;

    void setOptions(std::vector<std::string> opts);   // clamps the index
    const std::vector<std::string>& options() const { return m_options; }
    void setIndex(int i);                             // clamped; no notify
    int index() const { return m_index; }
    std::string value() const;                        // current option text
    void setValue(const std::string& v);              // selects a matching option
    void setWrap(bool w) { m_wrap = w; }
    void setOnChanged(OptionCallback cb) { m_onChanged = std::move(cb); }

    void setBgColor(const Color& col) { m_bgColor = col; }
    void setTextColor(const Color& col) { m_textColor = col; }
    void setButtonColor(const Color& col) { m_buttonColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setRadius(float r) { m_radius = r; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void stepBy(int dir);

    std::vector<std::string> m_options;
    int m_index = 0;
    bool m_wrap = true;
    Color m_bgColor{ogui::hexColor("#111")};
    Color m_textColor{ogui::hexColor("#ccc")};
    Color m_buttonColor{ogui::hexColor("#333")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
    float m_radius = 4.0f;
    std::string m_fontFam = "sans";
    int m_fontSize = 14;
    OptionCallback m_onChanged;
};

// ------------------------------------------------------------------
// RangeSlider: two-thumb slider selecting a [low, high] sub-range. Drag the
// nearest thumb; the band between them is filled. When focused, Left/Right
// nudge the low thumb and Up/Down nudge the high thumb.
// ------------------------------------------------------------------
class RangeSlider : public Widget {
public:
    using RangeCallback = std::function<void(float, float)>;

    RangeSlider();
    virtual ~RangeSlider() override;

    void setRange(float lo, float hi);
    void setLow(float v);
    void setHigh(float v);
    float low() const { return m_low; }
    float high() const { return m_high; }
    float minimum() const { return m_min; }
    float maximum() const { return m_max; }
    void setOnChanged(RangeCallback cb) { m_onChanged = std::move(cb); }

    void setTrackColor(const Color& col) { m_trackColor = col; }
    void setBarColor(const Color& col) { m_barColor = col; }
    void setThumbColor(const Color& col) { m_thumbColor = col; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    float valueFromLocalX(float localX) const;   // x -> range value
    float thumbX(float value) const;             // value -> local x
    void commit(float low, float high);          // clamp, order, store, notify

    float m_min = 0.0f;
    float m_max = 100.0f;
    float m_low = 25.0f;
    float m_high = 75.0f;
    int m_drag = -1;                             // 0 = low, 1 = high, -1 none
    float m_trackThickness = 4.0f;
    float m_thumbWidth = 12.0f;
    Color m_trackColor{ogui::hexColor("#3a3a3a")};
    Color m_barColor{ogui::hexColor("#4488ff")};
    Color m_thumbColor{ogui::hexColor("#cccccc")};
    RangeCallback m_onChanged;
};

// ------------------------------------------------------------------
// Splitter: two panes side by side (horizontal) or stacked (vertical) with a
// draggable divider between them. The first two children are the panes; the
// split fraction (0..1) is kept in sync with the divider position.
// ------------------------------------------------------------------
class Splitter : public Widget {
public:
    Splitter();
    virtual ~Splitter() override;

    void setVertical(bool v) { m_vertical = v; }   // vertical == divider is a vertical bar
    void setSplit(float frac);
    float split() const { return m_split; }
    void setHandleSize(float px) { m_handle = px > 2.0f ? px : 2.0f; }
    void setHandleColor(const Color& col) { m_handleColor = col; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    float splitPos() const;        // left/top edge of the divider, in pixels
    bool m_vertical = true;      // default: side-by-side panes, vertical divider
    float m_split = 0.5f;        // fraction of the track before the divider
    float m_handle = 6.0f;       // divider thickness in pixels
    bool m_dragging = false;
    Color m_handleColor{ogui::hexColor("#4488ff")};
};

// ------------------------------------------------------------------
// ComboOption: one entry declared as a <combobox> child in XML. It carries
// label/value data; ComboBox draws the list itself.
// ------------------------------------------------------------------
class ComboOption : public Widget {
public:
    ComboOption();
    virtual ~ComboOption() override;

    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    void setValue(const std::string& v) { m_value = v; }
    const std::string& value() const { return m_value; }

private:
    std::string m_label;
    std::string m_value;
};

// ------------------------------------------------------------------
// ComboPopup: the dropdown list a ComboBox opens. Drawn by the Kernel's
// popup layer (above everything, hit-tested first); bounds are
// screen-absolute. Picks the row under the pointer and reports the choice
// through onSelect. Fades + slides in via the open-in tween.
// ------------------------------------------------------------------
class ComboPopup : public Widget {
public:
    using SelectCallback = std::function<void(int)>;

    ComboPopup();
    virtual ~ComboPopup() override;

    void setItems(const std::vector<std::string>& items);
    const std::vector<std::string>& items() const { return m_items; }
    void setHover(int index);
    int hover() const { return m_hover; }
    void setSelected(int index) { m_selected = index; }

    void setRowHeight(float px) { m_rowH = px; }
    void setFontSize(int s) { m_fontSize = s; }
    void setPanelColor(const Color& c) { m_panel = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_rowText = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    void setHighlightTextColor(const Color& c) { m_highlightText = c; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }

    void setOnSelect(SelectCallback cb) { m_onSelect = std::move(cb); }
    // Fired when the Kernel dismisses this popup (outside press); lets the
    // owning ComboBox mirror the closed state.
    void setOnDismiss(std::function<void()> cb) { m_onDismiss = std::move(cb); }
    void fadeIn();                       // restarts the open-in animation

    static constexpr float kOpenDuration = 0.12f;

    virtual void onTick(float dt) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onPopupDismissed() override { if (m_onDismiss) m_onDismiss(); }

private:
    int rowAt(float localY) const;

    std::vector<std::string> m_items;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    int m_hover = -1;
    int m_selected = -1;
    float m_rowH = 24.0f;
    float m_textPad = 8.0f;
    Color m_panel{ogui::hexColor("#202428")};
    Color m_border{ogui::hexColor("#4488ff")};
    Color m_rowText{ogui::hexColor("#cccccc")};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_highlightText{ogui::hexColor("#ffffff")};
    Tween m_fade;
    SelectCallback m_onSelect;
    std::function<void()> m_onDismiss;
};

// ------------------------------------------------------------------
// ComboBox: collapsed selector box + dropdown popup.
// Options are <option> children; picking one (pointer or keyboard) fires
// setOnChanged. The popup is routed through Kernel::openPopup so it draws
// above the rest of the UI and closes when the pointer presses elsewhere.
// ------------------------------------------------------------------
class ComboBox : public Widget {
public:
    using ChangedCallback = std::function<void(int)>;

    ComboBox();
    virtual ~ComboBox() override;

    int count() const;
    int selectedIndex() const { return m_selected; }
    // Sets the selected option; clamps to the known options. Programmatic
    // calls do NOT fire setOnChanged (same rule as Checkbox).
    void setSelectedIndex(int index);
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setRowHeight(float px) { m_rowH = px; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setArrowColor(const Color& c) { m_arrow = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    void setHighlightTextColor(const Color& c) { m_highlightText = c; }
    void setPanelColor(const Color& c) { m_panel = c; }
    void setPopupBorderColor(const Color& c) { m_popupBorder = c; }

    bool popupOpen() const { return m_open; }
    const std::string& selectedLabel() const;
    // The open dropdown (nullptr while closed); read hover() for keyboard state.
    ComboPopup* popup() const;

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onFocus(bool focused) override;
    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void rebuildItems();
    void openPopup();                      // position + register with Kernel
    void closePopup();
    void commit(int index);                // user choice -> select + notify

    std::vector<std::string> m_items;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    int m_selected = -1;
    int m_wantedSelected = -1;
    bool m_resolved = false;
    float m_rowH = 24.0f;
    float m_arrowWidth = 24.0f;
    Color m_bg{ogui::hexColor("#111")};
    Color m_border{ogui::hexColor("#4488ff")};
    Color m_text{ogui::hexColor("#cccccc")};
    Color m_arrow{ogui::hexColor("#4488ff")};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_highlightText{ogui::hexColor("#ffffff")};
    Color m_panel{ogui::hexColor("#202428")};
    Color m_popupBorder{ogui::hexColor("#4488ff")};
    bool m_open = false;
    std::shared_ptr<ComboPopup> m_popup;
    ChangedCallback m_onChanged;
};

// ------------------------------------------------------------------
// Tab: a single page inside <tabs>. Its children are the page content;
// the label= attribute is what the header strip shows. The Tabs parent
// owns activation: inactive pages are invisible (no drawing, no input).
// ------------------------------------------------------------------
class Tab : public Widget {
public:
    Tab();
    virtual ~Tab() override;

    void setLabel(const std::string& label) { m_label = label; }
    const std::string& label() const { return m_label; }

private:
    std::string m_label;
};

// ------------------------------------------------------------------
// Tabs: clickable header strip over a stack of <tab> pages.
// ------------------------------------------------------------------
class Tabs : public Widget {
public:
    using ChangedCallback = std::function<void(int)>;

    Tabs();
    virtual ~Tabs() override;

    int active() const { return m_active; }
    // Clamped to the existing pages by the next layout pass; notifies
    // setOnChanged only when the index actually changes.
    void setActive(int index);
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    void setHeaderHeight(float h) { m_headerHeight = h; }
    float headerHeight() const { return m_headerHeight; }
    void setFontFamily(const std::string& family) { m_fontFamily = family; }
    void setFontSize(int size) { m_fontSize = size; }
    void setHeaderColor(const Color& col) { m_headerColor = col; }
    void setTabColor(const Color& col) { m_tabColor = col; }
    void setActiveColor(const Color& col) { m_activeColor = col; }
    void setAccentColor(const Color& col) { m_accentColor = col; }
    void setContentColor(const Color& col) { m_contentColor = col; }
    void setTextColor(const Color& col) { m_textColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }

    // Width of page i's header cell (label + padding) at the current font.
    float tabCellWidth(int i) const;
    // Page whose header cell contains localX (widget-local); -1 when none.
    int tabAt(float localX) const;

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    std::vector<Tab*> pages() const;   // non-owning, in document order

    int m_active = 0;
    float m_headerHeight = 28.0f;
    float m_cellPad = 10.0f;
    std::string m_fontFamily = "sans";
    int m_fontSize = 13;
    Color m_headerColor{ogui::hexColor("#1a1a1a")};
    Color m_tabColor{ogui::hexColor("#2a2a2a")};
    Color m_activeColor{ogui::hexColor("#161616")};
    Color m_accentColor{ogui::hexColor("#4488ff")};
    Color m_contentColor{ogui::hexColor("#161616")};
    Color m_textColor{ogui::hexColor("#999999")};
    Color m_borderColor{ogui::hexColor("#4488ff")};
    float m_borderWidth = 1.0f;
    ChangedCallback m_onChanged;
};

// ------------------------------------------------------------------
// RadioButton
// ------------------------------------------------------------------
class RadioButton : public Widget {
public:
    RadioButton();
    ~RadioButton() override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

    bool checked() const { return m_checked; }
    void setChecked(bool c);

    const std::string& text() const { return m_text; }
    void setText(const std::string& t) { m_text = t; }

    void setTextColor(const Color& c) { m_textColor = c; }
    Color textColor() const { return m_textColor; }
    void setFontFamily(const std::string& family) { m_fontFamily = family; }
    void setFontSize(int size) { m_fontSize = size; }
    void setDotColor(const Color& c) { m_dotColor = c; }
    Color dotColor() const { return m_dotColor; }
    void setCircleColor(const Color& c) { m_circleColor = c; }
    Color circleColor() const { return m_circleColor; }

    void toggle();
    const std::string& group() const { return m_group; }
    void setGroup(const std::string& g) { m_group = g; }

    using ChangedCallback = std::function<void(bool)>;
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

private:
    std::string m_text;
    bool m_checked = false;
    std::string m_group;
    int m_fontSize = 13;
    std::string m_fontFamily = "sans";
    Color m_textColor{ogui::hexColor("#cccccc")};
    Color m_dotColor{ogui::hexColor("#4488ff")};
    Color m_circleColor{ogui::hexColor("#555555")};
    ChangedCallback m_onChanged;
};


// ------------------------------------------------------------------
// TextArea: editable multiline text (Enter inserts newlines, Up/Down
// navigate lines, wheel scrolls, caret is kept in view in both axes).
// ------------------------------------------------------------------
class TextArea : public TextInput {
public:
    TextArea();
    virtual ~TextArea() override;

    virtual void setText(const std::string& text) override;

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onPointerScroll(float dx, float dy) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    struct Line { size_t start; size_t len; };   // byte range in m_text

    std::vector<Line> lines() const;
    // Index of the line containing the caret.
    int lineOf(size_t index) const;
    // Caret byte index for a click at widget-local (lx, ly).
    size_t indexAtPoint(float lx, float ly) const;
    float lineHeight() const;

    virtual void syncState() override;

    // Vertical scrollbar geometry helpers. `contentH` is the full text
    // height, `viewH` the visible slice; the bar only shows when content
    // overflows. sb* return the track/thumb rectangles in widget-local space.
    float contentH() const;
    float viewH() const;
    bool sbVisible() const { return contentH() - viewH() > 1.0f; }
    Rect sbTrack() const;
    Rect sbThumb() const;
    // Adjust m_scrollY so the thumb's top sits at local y `ly` (minus grab).
    void sbScrollTo(float ly, float grab);

    float m_scrollY = 0.0f;   // pixels scrolled above the viewport
    float m_caretX = 0.0f;    // sticky x for Up/Down navigation
    bool m_sbDrag = false;    // pointer is dragging the scrollbar thumb
    float m_sbGrab = 0.0f;    // offset from thumb top to the grab point
};

// ------------------------------------------------------------------
// ListItem: one entry declared as a <listbox> child in XML. It carries
// label/value data; ListBox draws the rows itself.
// ------------------------------------------------------------------
class ListItem : public Widget {
public:
    ListItem();
    virtual ~ListItem() override;

    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    void setValue(const std::string& v) { m_value = v; }
    const std::string& value() const { return m_value; }

private:
    std::string m_label;
    std::string m_value;
};

// ------------------------------------------------------------------
// ListBox: scrollable list of rows with single selection. Items are
// <item> children (label=/text= attrs); click, wheel and Up/Down/Home/End
// move the selection and fire setOnChanged.
// ------------------------------------------------------------------
class ListBox : public Widget {
public:
    using ChangedCallback = std::function<void(int)>;

    ListBox();
    virtual ~ListBox() override;

    void setItems(const std::vector<std::string>& items);
    int count() const { return static_cast<int>(m_items.size()); }
    int selectedIndex() const { return m_selected; }
    const std::string& selectedLabel() const;
    // Programmatic select: clamps, does NOT fire setOnChanged.
    void setSelectedIndex(int index);
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    void setRowHeight(float px) { m_rowH = px; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    void rebuildItems();               // collect <item> children once
    void clampScroll();
    void ensureVisible(int row);
    int rowAt(float localY) const;     // row under a widget-local y
    void commit(int row);              // user select -> notify

    std::vector<std::string> m_items;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    float m_rowH = 24.0f;
    float m_scroll = 0.0f;             // pixels scrolled above the viewport
    int m_selected = -1;
    int m_wantedSelected = -1;         // selected= attr applied on rebuild
    int m_hover = -1;
    bool m_resolved = false;
    Color m_bg{ogui::hexColor("#111")};
    Color m_border{ogui::hexColor("#4488ff")};
    Color m_text{ogui::hexColor("#cccccc")};
    Color m_highlight{ogui::hexColor("#334455")};
    ChangedCallback m_onChanged;
};

// ------------------------------------------------------------------
// TableColumn / TableRow: <th>/<tr> children declaring a <table> grid.
// Data-only, like ListItem: the Table draws every cell.
// ------------------------------------------------------------------
// How a cell whose text is wider than its column behaves.
enum class CellScroll : uint8_t {
    None,        // clip, no scrolling
    Automatic,   // marquee scrolls on its own
    OnHover,     // scrolls only while the pointer is over the cell
};

class TableColumn : public Widget {
public:
    TableColumn();
    virtual ~TableColumn() override;

    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    void setWidth(float w) { m_width = w; }
    float width() const { return m_width; }   // 0 == share the leftover

    void setResizable(bool b) { m_resizable = b; }
    bool resizable() const { return m_resizable; }
    void setEditable(bool b) { m_editable = b; }
    bool editable() const { return m_editable; }
    void setScrollMode(CellScroll m) { m_scroll = m; }
    CellScroll scrollMode() const { return m_scroll; }

private:
    std::string m_label;
    float m_width = 0.0f;
    bool m_resizable = false;
    bool m_editable = false;
    CellScroll m_scroll = CellScroll::None;
};

// TableCell: one <cell>...</cell> child of a <tr>. Its text is the value;
// the flags (when explicitly authored) promote the owning column's
// resizable/editable/scroll behaviour. Data only -- Table draws it.
class TableCell : public Widget {
public:
    TableCell();
    virtual ~TableCell() override;

    void setText(const std::string& t) { m_text = t; }
    const std::string& text() const { return m_text; }

    void setEditable(bool b) { m_editable = b; m_hasEditable = true; }
    bool editable() const { return m_editable; }
    bool hasEditable() const { return m_hasEditable; }
    void setResizable(bool b) { m_resizable = b; m_hasResizable = true; }
    bool resizable() const { return m_resizable; }
    bool hasResizable() const { return m_hasResizable; }
    void setScrollMode(CellScroll m) { m_scroll = m; m_hasScroll = true; }
    CellScroll scrollMode() const { return m_scroll; }
    bool hasScroll() const { return m_hasScroll; }

private:
    std::string m_text;
    bool m_editable = false;      bool m_hasEditable = false;
    bool m_resizable = false;     bool m_hasResizable = false;
    CellScroll m_scroll = CellScroll::None; bool m_hasScroll = false;
};

class TableRow : public Widget {
public:
    TableRow();
    virtual ~TableRow() override;

    // "a;b;c" -> cells (empty fields allowed).
    void setCells(const std::string& semicolonSeparated);
    const std::vector<std::string>& cells() const { return m_cells; }
    void setValue(const std::string& v) { m_value = v; }
    const std::string& value() const { return m_value; }

    // <tr editableCells="Nome;Cidade"> names the editable columns by label.
    void setEditableColumns(const std::string& s) { m_editableColumns = s; }
    const std::string& editableColumns() const { return m_editableColumns; }
    // <tr resizable="true"> / scrollingText="..." apply to every column.
    void setRowResizable(bool b) { m_resizable = b; m_hasResizable = true; }
    bool rowResizable() const { return m_resizable; }
    bool hasRowResizable() const { return m_hasResizable; }
    void setRowScrollMode(CellScroll m) { m_scroll = m; m_hasScroll = true; }
    CellScroll rowScrollMode() const { return m_scroll; }
    bool hasRowScroll() const { return m_hasScroll; }

private:
    std::vector<std::string> m_cells;
    std::string m_value;
    std::string m_editableColumns;
    bool m_resizable = false;   bool m_hasResizable = false;
    CellScroll m_scroll = CellScroll::None; bool m_hasScroll = false;
};

// ------------------------------------------------------------------
// Table: data grid with a header strip. Rows are <tr cells="a;b"> (or
// <tr><cell>..</cell></tr>) and columns <th label="Name" w="120">
// children. Clicking a header sorts by that column (re-click flips the
// direction); clicking a row selects it and fires setOnChanged. Wheel and
// Up/Down/Home/End scroll the selection. Columns can be resized by
// dragging their header edge, cells can be edited inline, and overflowing
// cell text can marquee-scroll (see CellScroll).
// ------------------------------------------------------------------
class Table : public Widget {
public:
    using ChangedCallback = std::function<void(int)>;
    struct Column {
        std::string label;
        float width = 0.0f;    // 0 == share the leftover evenly
        bool resizable = false;
        bool editable = false;
        CellScroll scroll = CellScroll::None;
    };

    Table();
    virtual ~Table() override;

    void setColumns(const std::vector<Column>& cols);
    void setRows(const std::vector<std::vector<std::string>>& rows);
    int rowCount() const { return static_cast<int>(m_rows.size()); }
    int columnCount() const { return static_cast<int>(m_cols.size()); }
    // Resolved pixel width of a column (after any header-edge resize drag).
    float columnWidth(int col) const { return columnW(col); }
    // True when the cell at (row via sort order, col) is editable.
    bool cellEditable(int row, int col) const;
    // Cell through the sort order (what the user sees at row r).
    std::string cell(int row, int col) const;
    int selectedIndex() const { return m_selected; }
    // Programmatic select: clamps, does NOT fire setOnChanged.
    void setSelectedIndex(int index);
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    // Sorting state (sortColumn < 0 == insertion order).
    int sortColumn() const { return m_sortCol; }
    bool sortAscending() const { return m_sortAsc; }
    void setSortable(bool s) { m_sortable = s; }
    void sortBy(int col);                     // toggle direction on re-click

    // Editing state (for tests / hosts).
    bool isEditing() const { return m_editing; }
    std::string editingText() const { return m_editText; }

    void setRowHeight(float px) { m_rowH = px; }
    void setHeaderHeight(float px) { m_headH = px; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setHeaderBgColor(const Color& c) { m_headerBg = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onChar(uint32_t ch) override;
    virtual void onFocus(bool focused) override;
    virtual void onTick(float dt) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    void rebuildCells();               // collect <th>/<tr> children once
    void clampScroll();
    void ensureVisible(int row);
    int rowAt(float localY) const;     // visible row under a local y (-1 header)
    int colAt(float localX) const;     // column under a local x (-1 none)
    float columnX(int col) const;      // local x of a column edge
    float columnW(int col) const;
    void commit(int row);              // user select -> notify
    void resort();
    void resolveWidths();              // materialise flexible widths once
    int colAtEdge(float localX) const; // resizable column whose edge is hit
    void beginEdit(int visRow, int col);
    void endEdit(bool save);
    void insertEdit(uint32_t ch);

    std::vector<Column> m_cols;
    std::vector<std::vector<std::string>> m_rows;
    std::vector<int> m_order;          // row permutation applied by sorting
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    float m_rowH = 22.0f;
    float m_headH = 24.0f;
    float m_scroll = 0.0f;             // pixels scrolled above the viewport
    int m_selected = -1;               // index into m_rows (stable across sorts)
    int m_wantedSelected = -1;         // selected= attr applied on rebuild
    int m_hover = -1;
    int m_hoverCol = -1;
    int m_sortCol = -1;
    bool m_sortAsc = true;
    bool m_sortable = true;
    bool m_resolved = false;

    // Column resize drag.
    int m_resizeCol = -1;
    float m_resizeGrabX = 0.0f;
    float m_resizeStartW = 0.0f;

    // Inline cell editing.
    bool m_editing = false;
    int m_editOrig = -1;               // original (unsorted) row being edited
    int m_editCol = -1;
    std::string m_editText;
    size_t m_editCaret = 0;            // byte offset into m_editText

    // Marquee clock for scrollingText.
    float m_phase = 0.0f;

    Color m_bg{ogui::hexColor("#111")};
    Color m_headerBg{ogui::hexColor("#222222")};
    Color m_border{ogui::hexColor("#4488ff")};
    Color m_text{ogui::hexColor("#cccccc")};
    Color m_highlight{ogui::hexColor("#334455")};
    ChangedCallback m_onChanged;
};

// ------------------------------------------------------------------
// TreeItem: one node declared as a (possibly nested) <treeitem> child.
// Carries the label and its expand state; TreeView draws the rows.
// ------------------------------------------------------------------
class TreeItem : public Widget {
public:
    TreeItem();
    virtual ~TreeItem() override;

    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    void setValue(const std::string& v) { m_value = v; }
    const std::string& value() const { return m_value; }
    void setExpanded(bool e) { m_expanded = e; }
    bool expanded() const { return m_expanded; }

private:
    std::string m_label;
    std::string m_value;
    bool m_expanded = true;
};

// ------------------------------------------------------------------
// TreeView: hierarchical list of TreeItem nodes. Rows are flattened on
// every layout pass from the nested <treeitem> children; clicking the
// triangle toggles expansion, clicking a row selects it. Wheel scrolls;
// Up/Down move the selection, Left/Right collapse/expand, Enter toggles.
// ------------------------------------------------------------------
class TreeView : public Widget {
public:
    using ChangedCallback = std::function<void(int)>;

    TreeView();
    virtual ~TreeView() override;

    int rowCount() const { return static_cast<int>(m_rows.size()); }
    int selectedIndex() const { return m_selected; }
    const std::string& selectedLabel() const;
    TreeItem* selectedNode() const;
    // Index in the flattened visible rows. Programmatic calls do NOT fire
    // setOnChanged.
    void setSelectedIndex(int index);
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    void setRowHeight(float px) { m_rowH = px; }
    void setIndent(float px) { m_indent = px; }
    // When on (default) the tree sizes itself to its visible rows: collapse a
    // node and the whole widget shrinks to the height of the remaining top
    // level labels instead of keeping the authored box. Turn it off to hold a
    // fixed height and scroll the content instead.
    void setAutoHeight(bool on) { m_autoHeight = on; }
    bool autoHeight() const { return m_autoHeight; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    void setAccentColor(const Color& c) { m_accent = c; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onTick(float dt) override;
    virtual bool wantsScroll() const override { return true; }

private:
    struct Row { TreeItem* node = nullptr; int depth = 0; };

    void flatten(Widget* parent, int depth);   // rebuild m_rows
    void clampSelected();
    void ensureVisible(int row);
    int rowAt(float localY) const;
    bool toggleAt(float localX, const Row& row) const;  // click on triangle?
    void toggleRow(int row);
    // Re-flatten and start the compression tween. `oldRows` must be the
    // pre-toggle layout (the caller clears m_rows right after snapshotting).
    void rebuildAnimated();
    void commit(int row);

    std::vector<Row> m_rows;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    float m_rowH = 22.0f;
    float m_indent = 14.0f;
    bool m_autoHeight = true;          // shrink to visible rows when collapsed
    float m_authoredH = -1.0f;         // height captured on the first layout
    float m_scroll = 0.0f;
    int m_selected = -1;
    int m_wantedSelected = -1;         // selected= attr applied on flatten
    bool m_wantedApplied = false;
    int m_hover = -1;
    Color m_bg{ogui::hexColor("#111")};
    Color m_border{ogui::hexColor("#4488ff")};
    Color m_text{ogui::hexColor("#cccccc")};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_accent{ogui::hexColor("#4488ff")};
    ChangedCallback m_onChanged;

    // Compression animation. On a toggle we snapshot the pre-toggle rows
    // (node -> pixel y) and the row that was toggled; during the tween the
    // draw lerps every row from its old y to its new slot. Rows that just
    // appeared slide out of the toggled parent and fade in, rows that just
    // vanished slide into it and fade out -- so the list compresses instead
    // of the children simply blinking away.
    std::map<const TreeItem*, float> m_animOldY;
    std::vector<Row> m_animOldRows;    // for detecting vanishing rows
    float m_animToggleY = 0.0f;
    float m_animOldH = 0.0f;           // widget height when the tween began
    Tween m_anim;
};

// ------------------------------------------------------------------
// ToggleSwitch
// ------------------------------------------------------------------
class ToggleSwitch : public Widget {
public:
    ToggleSwitch();
    virtual ~ToggleSwitch();

    void setState(bool state);
    bool state() const { return m_checked; }

    using ChangedCallback = std::function<void(bool)>;
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    // Layout/spacing
    float m_gap = 6.0f;

private:
    bool m_checked = false;
    std::string m_text;
    int m_fontSize = 13;
    std::string m_fontFamily = "sans";
    Color m_textColor{ogui::hexColor("#cccccc")};
    Color m_bgColor{ogui::hexColor("#1a1a1a")};
    Color m_dotColor{ogui::hexColor("#4488ff")};
    Color m_activeBgColor{ogui::hexColor("#161616")};
    ChangedCallback m_onChanged;
};

// ------------------------------------------------------------------
// XML widgets *ebay registry* (tag -> creator)
// ------------------------------------------------------------------
using WidgetFactory = std::shared_ptr<Widget> (*)(const Style&, const std::map<std::string, std::string>&);

std::map<std::string, WidgetFactory>& widgetRegistry();

} // namespace ogui
