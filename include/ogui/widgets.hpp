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
class MenuPopup;
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
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "color") setTextColor(v);
    }
    const std::string& fontFamily() const { return m_fontFamily; }
    int fontSize() const { return m_fontSize; }
    const Color& textColor() const { return m_textColor; }
    void setHAlign(Align a) { m_hAlign = a; }
    Align hAlign() const { return m_hAlign; }
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
    Color m_textColor{ogui::palette().text};
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
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setTextColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }
    void applyVisualFloat(const std::string& prop, float v) override {
        if (prop == "borderwidth") setBorderWidth(v);
        else if (prop == "radius") setRadius(v);
    }
    // Horizontal text placement inside the button (default center) and the
    // inset used by the left/right alignments.
    void setHAlign(Align a) { m_textAlign = a; }
    void setTextPadding(float p) { m_textPadding = p; }
    Align hAlign() const { return m_textAlign; }
    float textPadding() const { return m_textPadding; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    // Enter/Space fire the same click path as the mouse, so a keyboard user
    // can activate a focused button.
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;

    virtual void onDraw(Rasterizer& r) const override;

    bool pressed() const { return m_pressed; }

private:
    std::string m_text;
    std::string m_fontFamily;
    int m_fontSize = 14;
    Color m_textColor{ogui::palette().white};
    Color m_bgColor{ogui::palette().accent};
    Color m_borderColor{ogui::hexColor("#3366cc")};
    float m_borderWidth = 1.0f;
    float m_radius = 4.0f;
    Align m_textAlign = Align::Center;
    float m_textPadding = 12.0f;
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

    // Zoom into the picture: the destination stays the widget's bounds and the
    // source window shrinks, so a magnified image is cropped by its own frame
    // instead of painting over its neighbours. Clamped 1..8 -- a `zoom="40"`
    // asks for more than the texture can give and lands on the ceiling.
    // Re-setting the zoom recentres the window; a pinch puts it under the
    // fingers instead.
    void setZoom(float z);
    float zoom() const { return m_zoom; }
    // Opt this image in to a two-finger pinch: the pair scales `zoom` and keeps
    // the point under the fingers where it was. Tiled images decline, since
    // there is no zoom that does not also move every tile.
    void setPinchZoom(bool on) { m_pinchZoom = on; }
    bool pinchZoom() const { return m_pinchZoom; }
    virtual bool wantsPinch() const override { return m_pinchZoom && !m_tile; }
    virtual void onPinch(float factor, const Offset2D& pivot) override;

    virtual void onDraw(Rasterizer& r) const override;

private:
    // The source window a pinch left behind, or negative for "centre it".
    void uvWindow(float& u, float& v, float& span) const;

    std::string m_path;
    bool m_tile = false;
    bool m_pinchZoom = false;
    float m_zoom = 1.0f;
    float m_uvX = -1.0f, m_uvY = -1.0f;
};

// ------------------------------------------------------------------
// Panel: solid background + children
// ------------------------------------------------------------------
class Panel : public Widget {
public:
    Panel();
    virtual ~Panel() override;

    void setBgColor(const Color& col) { m_bgColor = col; }
    Color bgColor() const { return m_bgColor; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }
    void applyVisualFloat(const std::string& prop, float v) override {
        if (prop == "borderwidth") setBorderWidth(v);
        else if (prop == "radius") setRadius(v);
    }

    virtual void onDraw(Rasterizer& r) const override;
protected:
    Color m_bgColor{ogui::palette().window};
    Color m_borderColor{ogui::palette().accent};
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
    bool titleVisible() const { return m_titleVisible; }
    float titleHeight() const { return m_titleVisible ? m_titleHeight : 0.0f; }

    // Positions children below the title bar.
    virtual void layout(float parentWidth, float parentHeight) override;
    // Children never paint past the window body: clip to the content area.
    virtual bool clipsChildren(Rect& outRect) const override;

    // When the children overflow the body, the wheel scrolls them back into
    // view (the delta rides on childOffset(), like ScrollView).
    virtual Offset2D childOffset() const override { return Offset2D{0.0f, -m_scrollY}; }
    virtual bool wantsScroll() const override { return true; }
    virtual void onPointerScroll(float, float dy) override;
    // A finger drags the body the way the wheel does; the scrollbar thumb and
    // the title-bar move each keep their own drag.
    virtual float scrollUnit() const override { return m_scrollStep; }
    virtual bool wantsPan(float, float) const override {
        return !m_barDrag && !m_dragging;
    }

    // Overlay scrollbar on the body's right edge, draggable like the list
    // widgets' bars (claimsPoint keeps presses off the scrolled children).
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual bool claimsPoint(const Offset2D& localP) const override;
    virtual void onDrawForeground(Rasterizer& r) const override;

    virtual void onDraw(Rasterizer& r) const override;

private:
    bool barVisible() const;           // body content overflows
    Rect barTrack() const;             // widget-local space, below the title
    Rect barThumb() const;
    void applyBarScroll(float scroll); // clamp m_scrollY from a bar position

    std::string m_title;
    bool m_titleVisible = true;
    float m_titleHeight = 24.0f;
    // Title-bar offset already applied to children by the previous layout()
    // pass; layout() runs every frame, so the old offset must be undone
    // before computing the new one.
    float m_childrenOffsetY = 0.0f;
    bool m_dragging = false;
    float m_scrollY = 0.0f;     // wheel scroll of an overflowing body
    float m_overflow = 0.0f;    // px the children extend past the body
    float m_scrollStep = 24.0f;
    bool m_barDrag = false;     // pointer is dragging the scrollbar thumb
    float m_barGrab = 0.0f;     // thumb-top offset of the drag press
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
    float scrollStep() const { return m_step; }
    const Rect& contentBounds() const { return m_contentBounds; }

    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onDrawForeground(Rasterizer& r) const override;
    virtual Offset2D childOffset() const override { return m_offset; }
    virtual bool clipsChildren(Rect& outRect) const override;
    virtual bool wantsScroll() const override { return true; }
    virtual void onPointerScroll(float dx, float dy) override;
    // Both axes follow the finger; only the scrollbar thumb drag vetoes.
    virtual float scrollUnit() const override { return m_step; }
    virtual bool wantsPan(float, float) const override { return !m_barDrag; }
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual bool claimsPoint(const Offset2D& localP) const override;

private:
    void clampScroll();
    bool barVisible() const;           // content overflows the viewport
    Rect barTrack() const;             // widget-local space
    Rect barThumb() const;
    void applyBarScroll(float scroll); // set -m_offset.y from a bar position

    std::shared_ptr<Widget> m_content;
    Rect m_viewport{0.0f, 0.0f, 0.0f, 0.0f};      // within this widget's bounds
    Rect m_contentBounds{0.0f, 0.0f, 0.0f, 0.0f}; // union of the children
    Offset2D m_offset;      // scroll delta (<= 0 in both axes)
    float m_step = 40.0f;   // pixels per wheel notch
    float m_borderWidth = 0.0f;
    Color m_bgColor{ogui::palette().window};
    Color m_borderColor{ogui::palette().accent};
    bool m_barDrag = false; // pointer is dragging the scrollbar thumb
    float m_barGrab = 0.0f; // thumb-top offset of the drag press
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
    const std::string& placeholder() const { return m_placeholder; }
    void setBgColor(const Color& col) { m_bgColor = col; }
    void setFgColor(const Color& col) { m_fgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setFgColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }
    void applyVisualFloat(const std::string& prop, float v) override {
        if (prop == "borderwidth") setBorderWidth(v);
        else if (prop == "radius") setRadius(v);
    }
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
    // Raw key handling for a single-line field. onKeyDown() wraps this to
    // fire the Lua "change"/"submit" events when the text actually moved.
    void onKeyEdit(KeyCode key, uint32_t mods);
    // Caret index for a click at widget-local x.
    size_t indexAtX(float lx) const;
    // Deletes the selection and puts the caret where it started.
    void deleteSelection();
    // Replaces the selection (if any) with `s` and advances the caret.
    void insertText(const std::string& s);

    // Horizontal space taken by subclass decorations (search icon, clear
    // button). Text drawing, click-to-index mapping and caret scrolling all
    // honour them, so a subclass only has to paint its own glyphs.
    virtual float insetLeft() const { return 0.0f; }
    virtual float insetRight() const { return 0.0f; }

    std::string m_text;
    std::string m_placeholder;
    Color m_bgColor{ogui::palette().window};
    Color m_fgColor{ogui::palette().text};
    Color m_borderColor{ogui::palette().accent};
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
    // Horizontal placement of children narrower than the content width:
    // left keeps the authored x (indentation); center/right align them.
    void setChildAlign(Align a) { m_align = a; }
    Align childAlign() const { return m_align; }
    virtual void layout(float parentWidth, float parentHeight) override;

private:
    Align m_align = Align::Left;
};

// Row: lays children horizontally
class Row : public Widget {
public:
    Row();
    virtual ~Row() override;
    // Vertical placement of children shorter than the tallest row member:
    // top keeps the authored y; middle/bottom align them.
    void setChildValign(VertAlign a) { m_valign = a; }
    VertAlign childValign() const { return m_valign; }
    // wrap="true": children that do not fit the row width continue on the
    // next line; the row grows in height to hold the extra lines.
    void setWrap(bool w) { m_wrap = w; }
    bool wrap() const { return m_wrap; }
    virtual void layout(float parentWidth, float parentHeight) override;

private:
    VertAlign m_valign = VertAlign::Top;
    bool m_wrap = false;
};

// ------------------------------------------------------------------
// Grid: row-major tiling over column tracks.
// cols="3" makes three equal tracks; a list ("120 1fr 25%") describes
// tracks: bare numbers are pixels, "NN%" is percent of the content
// width and "Nfr"/"fr" shares the leftover width after the fixed tracks.
// Children flow into cells left to right, wrapping rows. Row heights are
// the tallest child of each row, or fixed via rowheight. Child x/y are
// ignored (the cell is the position); percent/fill components resolve
// against the cell size.
// ------------------------------------------------------------------
class Grid : public Widget {
public:
    Grid();
    virtual ~Grid() override;

    void setCols(const std::string& spec);
    int colCount() const { return static_cast<int>(m_tracks.size()); }
    void setGap(float px) { m_gap = px >= 0.0f ? px : 0.0f; }
    float gap() const { return m_gap; }
    void setRowHeight(float px) { m_rowH = px; }   // <= 0: auto
    float rowHeight() const { return m_rowH; }

    virtual void layout(float parentWidth, float parentHeight) override;

private:
    struct Track {
        enum Kind { Px, Percent, Fr } kind = Fr;
        float value = 1.0f;
    };
    std::vector<Track> m_tracks;
    float m_gap = 4.0f;
    float m_rowH = 0.0f;
};

// Group: draws a named named style background + border
class Group : public Widget {
public:
    Group();
    virtual ~Group() override;
    void setGroupName(const std::string& name) { m_groupName = name; }
    const std::string& groupName() const { return m_groupName; }
    void setBgColor(const Color& col) { m_bgColor = col; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    virtual void onDraw(Rasterizer& r) const override;

private:
    std::string m_groupName;
    Color m_bgColor{ogui::palette().window};
    Color m_borderColor{ogui::palette().accent};
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
    bool isCheckedVisual() const override { return m_checked; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "color") setTextColor(v);
        else if (prop == "background") setBoxColor(v);
    }
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
    Color m_textColor{ogui::palette().text};
    Color m_boxColor{ogui::palette().accent};
    Color m_checkColor{ogui::palette().accent};
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
    Color m_lineColor{ogui::palette().border};
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
    Color barColor() const { return m_barColor; }
    void setBorderColor(const Color& col) { m_borderColor = col; }
    void setBorderWidth(float w) { m_borderWidth = w; }
    void setRadius(float r) { m_radius = r; }
    // Indeterminate mode ignores value: a block slides back and forth while
    // the widget ticks. Use it when the progress amount is unknown.
    void setIndeterminate(bool on) { m_indeterminate = on; }
    bool indeterminate() const { return m_indeterminate; }

    virtual void onTick(float dt) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool isAnimating() const override {
        return m_indeterminate || Widget::isAnimating();
    }

private:
    float m_value = 0.0f;
    float m_radius = 4.0f;
    float m_borderWidth = 1.0f;
    bool m_indeterminate = false;
    float m_phase = 0.0f;
    Color m_bgColor{ogui::palette().window};
    Color m_barColor{ogui::palette().accent};
    Color m_borderColor{ogui::palette().border};
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
    Color m_trackColor{ogui::palette().border};
    Color m_barColor{ogui::palette().accent};
    Color m_thumbColor{ogui::palette().text};
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
    int decimals() const { return m_decimals; }
    void setValue(float v);
    float value() const { return m_value; }
    float minimum() const { return m_min; }
    float maximum() const { return m_max; }
    void setWrap(bool w) { m_wrap = w; }
    void setPrefix(const std::string& p) { m_prefix = p; }
    const std::string& prefix() const { return m_prefix; }
    void setSuffix(const std::string& s) { m_suffix = s; }
    const std::string& suffix() const { return m_suffix; }
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
    Color m_bgColor{ogui::palette().window};
    Color m_textColor{ogui::palette().text};
    Color m_buttonColor{ogui::palette().track};
    Color m_borderColor{ogui::palette().accent};
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
    bool wrap() const { return m_wrap; }
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
    Color m_bgColor{ogui::palette().window};
    Color m_textColor{ogui::palette().text};
    Color m_buttonColor{ogui::palette().track};
    Color m_borderColor{ogui::palette().accent};
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
    Color m_trackColor{ogui::palette().border};
    Color m_barColor{ogui::palette().accent};
    Color m_thumbColor{ogui::palette().text};
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
    bool isVertical() const { return m_vertical; }
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
    Color m_handleColor{ogui::palette().accent};
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

    // Pixels of the list scrolled above the popup viewport (tests/peek).
    float scroll() const { return m_scroll; }
    // Re-clamp the scroll and keep the hovered row visible after the
    // opener has finished sizing the popup (setBounds runs last).
    void refreshScroll();

    static constexpr float kOpenDuration = 0.12f;

    virtual void onTick(float dt) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerScroll(float, float dy) override;
    // A finger flicks the option list; the scrollbar thumb keeps its drag.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return !m_barDrag; }
    virtual bool wantsScroll() const override { return true; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onPopupDismissed() override { if (m_onDismiss) m_onDismiss(); }
    virtual bool isAnimating() const override {
        return m_fade.running() || Widget::isAnimating();
    }

private:
    int rowAt(float localY) const;
    float contentH() const {
        return m_rowH * static_cast<float>(m_items.size());
    }
    bool barVisible() const;           // list overflows the popup
    Rect barTrack() const;             // popup-local space
    Rect barThumb() const;
    void clampScroll();
    void ensureHoverVisible();

    std::vector<std::string> m_items;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    int m_hover = -1;
    int m_selected = -1;
    float m_rowH = 24.0f;
    float m_textPad = 8.0f;
    float m_scroll = 0.0f;             // px of the list above the viewport
    bool m_barDrag = false;            // pointer is dragging the scrollbar
    float m_barGrab = 0.0f;            // thumb-top offset of the drag press
    Color m_panel{ogui::palette().surface};
    Color m_border{ogui::palette().accent};
    Color m_rowText{ogui::palette().text};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_highlightText{ogui::palette().white};
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
    bool isSelectedVisual() const override { return m_selected >= 0; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setTextColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }
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
    Color m_bg{ogui::palette().window};
    Color m_border{ogui::palette().accent};
    Color m_text{ogui::palette().text};
    Color m_arrow{ogui::palette().accent};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_highlightText{ogui::palette().white};
    Color m_panel{ogui::palette().surface};
    Color m_popupBorder{ogui::palette().accent};
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
    // "scrolling" mode: instead of truncating behind a "..." button, the
    // header is a draggable/wheel-scrollable strip showing every tab.
    void setScrollable(bool s) { m_scrollable = s; if (!s) m_stripX = 0.0f; }
    bool scrollable() const { return m_scrollable; }
    float stripOffset() const { return m_stripX; }
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
    // How many header cells fit in headerW; the rest go behind "..." .
    int visibleTabs(float headerW) const;

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // The strip slides sideways under a finger; the overflow button and the
    // strip drag each keep their own.
    virtual float scrollUnit() const override { return 60.0f; }
    virtual bool wantsPan(float, float) const override {
        return m_scrollable && !m_stripDrag;
    }
    virtual bool wantsScroll() const override { return m_scrollable; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    std::vector<Tab*> pages() const;   // non-owning, in document order
    void openOverflow();               // "..." popup listing hidden pages
    float stripMax() const;            // scrollable strip's right-most offset
    void clampStrip();                 // keep m_stripX inside [0, stripMax]
    void revealTab(int i);             // scroll the strip so cell i is whole

    int m_active = 0;
    float m_headerHeight = 28.0f;
    float m_cellPad = 10.0f;
    std::string m_fontFamily = "sans";
    int m_fontSize = 13;
    Color m_headerColor{ogui::palette().surface};
    Color m_tabColor{ogui::hexColor("#2a2a2a")};
    Color m_activeColor{ogui::hexColor("#161616")};
    Color m_accentColor{ogui::palette().accent};
    Color m_contentColor{ogui::hexColor("#161616")};
    Color m_textColor{ogui::hexColor("#999999")};
    Color m_borderColor{ogui::palette().accent};
    float m_borderWidth = 1.0f;
    ChangedCallback m_onChanged;
    std::shared_ptr<MenuPopup> m_overflow;
    bool m_scrollable = false;
    float m_stripX = 0.0f;        // strip scroll offset (px)
    bool m_stripDrag = false;     // pointer is dragging the strip
    float m_dragLastX = 0.0f;     // last drag sample, widget-local
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
    bool isCheckedVisual() const override { return m_checked; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "color") setTextColor(v);
        else if (prop == "background") setCircleColor(v);
    }
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
    Color m_textColor{ogui::palette().text};
    Color m_dotColor{ogui::palette().accent};
    Color m_circleColor{ogui::palette().borderStrong};
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

    // Raw multi-line key handling; onKeyDown() wraps it to fire "change".
    void onKeyEdit(KeyCode key, uint32_t mods);
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
// Console: read-only log view mirroring the Kernel::logs() stream.
// Newest lines sit at the top; the wheel scrolls back through history
// (capped by `maxLines`). Each line is drawn in a level colour when it
// starts with a recognised tag ([info]/[warn]/[error]/[script:]).
// ------------------------------------------------------------------
class Console : public Widget {
public:
    Console();
    virtual ~Console() override;

    void setMaxLines(int n) { m_maxLines = n < 1 ? 1 : n; trim(); }
    int maxLines() const { return m_maxLines; }
    void setRowHeight(float h) { if (h > 0.0f) m_rowH = h; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setBorderWidth(float w) { m_borderWidth = w; }

    int lineCount() const { const_cast<Console*>(this)->pull(); return static_cast<int>(m_lines.size()); }
    std::string line(int i) const;   // 0 = newest
    void clear();

    virtual void onPointerScroll(float dx, float dy) override;
    // Fingers read back through the log the way the wheel does.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return true; }
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    struct Line { std::string text; Color col; };
    void pull() const;               // mirror new kernel log entries
    void trim() const;
    void clampScroll() const;
    float viewH() const;

    mutable std::vector<Line> m_lines;   // newest first
    mutable size_t m_seen = 0;           // kernel log entries already pulled
    int m_maxLines = 200;
    float m_rowH = 18.0f;
    mutable float m_scroll = 0.0f;       // px of history scrolled past the top
    std::string m_fontFam = "sans";
    int m_fontSize = 12;
    Color m_bg{ogui::palette().surfaceLo};
    Color m_text{ogui::palette().text};
    Color m_border{ogui::palette().border};
    float m_borderWidth = 1.0f;
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
    // Incremental (virtualised) updates: rows are plain strings, so a
    // million-item list still only ever draws the viewport. Index -1
    // appends. Programmatic, like setItems — no change event fired.
    void insertItem(int index, const std::string& text);
    void removeItem(int index);
    bool setItem(int index, const std::string& text);
    void clearItems();
    bool itemAt(int index, std::string& out) const;
    // Scroll minimally so `index` is inside the viewport.
    void scrollIntoView(int index);
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
    bool isSelectedVisual() const override { return m_selected >= 0; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setTextColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // Rows scroll under a finger; the scrollbar thumb keeps its drag.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return !m_barDrag; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    void rebuildItems();               // collect <item> children once
    void clampScroll();
    void ensureVisible(int row);
    int rowAt(float localY) const;     // row under a widget-local y
    void commit(int row);              // user select -> notify
    bool barVisible() const;           // content overflows the viewport
    Rect barTrack() const;
    Rect barThumb() const;

    std::vector<std::string> m_items;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    float m_rowH = 24.0f;
    float m_scroll = 0.0f;             // pixels scrolled above the viewport
    int m_selected = -1;
    int m_wantedSelected = -1;         // selected= attr applied on rebuild
    int m_hover = -1;
    bool m_resolved = false;
    bool m_barDrag = false;            // pointer is dragging the scrollbar thumb
    float m_barGrab = 0.0f;            // thumb-top offset of the drag press
    Color m_bg{ogui::palette().window};
    Color m_border{ogui::palette().accent};
    Color m_text{ogui::palette().text};
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

// ------------------------------------------------------------------
// PropertyItem: a data row feeding a PropertyGrid (label + typed value).
// It never draws; the grid collects the entries during layout.
// ------------------------------------------------------------------
class PropertyItem : public Widget {
public:
    enum class Kind { Bool, Number, Text, Enum, Color };
    static Kind kindFromName(const std::string& t);

    void setName(const std::string& n) { m_name = n; }
    const std::string& name() const { return m_name; }
    void setKind(Kind k) { m_kind = k; }
    Kind kind() const { return m_kind; }
    void setValue(const std::string& v) { m_value = v; }
    const std::string& value() const { return m_value; }
    void setGroup(const std::string& g) { m_group = g; }
    const std::string& group() const { return m_group; }
    void setOptions(const std::vector<std::string>& o) { m_options = o; }
    const std::vector<std::string>& options() const { return m_options; }
    void setReadOnly(bool r) { m_readOnly = r; }
    bool readOnly() const { return m_readOnly; }

private:
    std::string m_name;
    std::string m_value;
    std::string m_group;
    std::vector<std::string> m_options;
    Kind m_kind = Kind::Text;
    bool m_readOnly = false;
};

// ------------------------------------------------------------------
// PropertyGrid: two-column editor over a flat list of PropertyItems.
// Left column is the label, right column is a type-appropriate editor
// (checkbox for bool, inline text for number/text, a colour swatch for
// colour, a cycling select for enum). Rows are grouped by `group`. The
// model lives in the collected items, not child widgets, so it can also
// be built entirely from Lua/C++. Selection with click or Up/Down; the
// right column enters inline edit for text/number on Enter or dbl-click.
// ------------------------------------------------------------------
class PropertyGrid : public Widget {
public:
    using ValueCallback = std::function<void(const std::string& name,
                                             const std::string& value)>;

    PropertyGrid();
    virtual ~PropertyGrid() override;

    void addItem(PropertyItem::Ptr item);         // adopt a parsed/Lua item
    bool removeItem(const std::string& name);
    void clear();
    int count() const { return static_cast<int>(m_items.size()); }
    PropertyItem* itemAt(int i);
    int indexOf(const std::string& name) const;
    std::string valueOf(const std::string& name) const;
    // Programmatic set: silent (never fires setOnChanged), clamps/validates
    // to the item's kind. Returns false for an unknown name.
    bool setValue(const std::string& name, const std::string& v);
    void setOnChanged(ValueCallback cb) { m_onChanged = std::move(cb); }
    int selectedIndex() const { return m_sel; }
    void setSelectedIndex(int i);
    // Validation message of the last rejected edit (empty when none); it
    // shows in the row until the next commit or focus loss.
    const std::string& errorText() const { return m_error; }
    int errorIndex() const { return m_errorIndex; }

    void setRowHeight(float px) { m_rowH = px; }
    float rowHeight() const { return m_rowH; }
    void setGroupColor(const Color& c) { m_groupColor = c; }
    Color groupColor() const { return m_groupColor; }
    void setLabelColor(const Color& c) { m_labelColor = c; }
    Color labelColor() const { return m_labelColor; }
    void setValueColor(const Color& c) { m_valueColor = c; }
    Color valueColor() const { return m_valueColor; }
    void setBgColor(const Color& c) { m_bg = c; }
    Color bgColor() const { return m_bg; }
    void setBorderColor(const Color& c) { m_border = c; }
    Color borderColor() const { return m_border; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    Color highlightColor() const { return m_highlight; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // The rows follow the finger.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return true; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onChar(uint32_t ch) override;
    virtual void onFocus(bool focused) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    struct GridRow {
        int item = -1;              // -1 = group header band
        std::string group;          // header text when item < 0
    };
    void collect();                 // gather child PropertyItems once
    void rebuildRows();             // flatten items + group headers
    void clampScroll();
    int rowAt(float localY) const;  // visual row under a widget-local y
    int visualRow(int item) const;  // visual row of an item (-1 if none)
    bool boolValue(const PropertyItem& it) const;
    bool editableText(const PropertyItem& it) const;  // number/text/color
    void drawEditor(Rasterizer& r, const PropertyItem& it,
                    const Rect& cell, bool editing, const Color& col) const;
    void commitEdit(bool save);
    void toggleItem(PropertyItem& it);
    void cycleEnum(PropertyItem& it, int dir);
    void ensureVisible(int row);
    void notify(PropertyItem& it);   // user value change -> callbacks
    float labelW() const;

    std::vector<PropertyItem::Ptr> m_items;
    std::vector<GridRow> m_rows;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    float m_rowH = 24.0f;
    float m_scroll = 0.0f;
    float m_labelFrac = 0.4f;       // left column share of the width
    int m_sel = -1;
    bool m_editing = false;
    int m_editIndex = -1;           // row being text-edited
    std::string m_editText;
    size_t m_editCaret = 0;
    std::string m_error;            // last rejected-edit message
    int m_errorIndex = -1;          // row the error belongs to
    bool m_collected = false;       // child PropertyItems gathered once
    Color m_bg{ogui::palette().window};
    Color m_border{ogui::palette().accent};
    Color m_groupColor{ogui::hexColor("#334455")};
    Color m_labelColor{ogui::palette().text};
    Color m_valueColor{ogui::palette().textBright};
    Color m_highlight{ogui::hexColor("#334455")};
    ValueCallback m_onChanged;
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
    bool sortable() const { return m_sortable; }
    void sortBy(int col);                     // toggle direction on re-click

    // Editing state (for tests / hosts).
    bool isEditing() const { return m_editing; }
    std::string editingText() const { return m_editText; }

    void setRowHeight(float px) { m_rowH = px; }
    float rowHeight() const { return m_rowH; }
    void setHeaderHeight(float px) { m_headH = px; }
    float headerHeight() const { return m_headH; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setHeaderBgColor(const Color& c) { m_headerBg = c; }
    Color headerBgColor() const { return m_headerBg; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    Color highlightColor() const { return m_highlight; }
    bool isSelectedVisual() const override { return m_selected >= 0; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setTextColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // Rows scroll under a finger; the thumb drag and a column resize keep
    // theirs.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override {
        return !m_barDrag && m_resizeCol < 0;
    }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onChar(uint32_t ch) override;
    virtual void onFocus(bool focused) override;
    virtual void onTick(float dt) override;
    virtual void onDraw(Rasterizer& r) const override;
    // The marquee clock only matters while a cell is actually scrolling;
    // onDraw records that in m_marqueeActive.
    virtual bool isAnimating() const override {
        return m_marqueeActive || Widget::isAnimating();
    }
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
    bool barVisible() const;           // rows overflow the viewport
    Rect barTrack() const;
    Rect barThumb() const;

    // Scrollbar thumb drag.
    bool m_barDrag = false;
    float m_barGrab = 0.0f;

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
    mutable bool m_marqueeActive = false;  // set by onDraw when a cell scrolls

    Color m_bg{ogui::palette().window};
    Color m_headerBg{ogui::palette().surface};
    Color m_border{ogui::palette().accent};
    Color m_text{ogui::palette().text};
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
    // Folder vs file icon (folders default to expanded).
    void setFolder(bool f) { m_folder = f; }
    bool folder() const { return m_folder; }
    // Checkbox state, shown when the parent tree enables checks.
    void setChecked(bool c) { m_checked = c; }
    bool checked() const { return m_checked; }
    // Backing filesystem path (set by TreeView::loadDirectory). A folder
    // with an unloaded path fetches its entries lazily on first expand.
    void setPath(const std::string& p) { m_path = p; }
    const std::string& path() const { return m_path; }
    bool childrenLoaded() const { return m_childrenLoaded; }
    void setChildrenLoaded(bool v) { m_childrenLoaded = v; }

private:
    std::string m_label;
    std::string m_value;
    bool m_expanded = true;
    bool m_folder = false;
    bool m_checked = false;
    std::string m_path;
    bool m_childrenLoaded = true;
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
    float rowHeight() const { return m_rowH; }
    void setIndent(float px) { m_indent = px; }
    float indent() const { return m_indent; }
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
    Color highlightColor() const { return m_highlight; }
    void setAccentColor(const Color& c) { m_accent = c; }
    Color accentColor() const { return m_accent; }
    bool isSelectedVisual() const override { return m_selected >= 0; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setTextColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }
    // File-tree presentation extras (all off by default so plain trees are
    // unchanged): folder/file icons, a per-node checkbox, and dotted guide
    // lines connecting each node to its parent.
    void setShowIcons(bool on) { m_showIcons = on; }
    bool showIcons() const { return m_showIcons; }
    void setShowChecks(bool on) { m_showChecks = on; }
    void setShowGuides(bool on) { m_showGuides = on; }
    bool showGuides() const { return m_showGuides; }
    bool showChecks() const { return m_showChecks; }
    int checkedCount() const;
    // Comma-joined labels of the checked nodes (empty when none).
    std::string checkedLabels() const;

    // Replace the tree content with a real filesystem view of `path`: one
    // root folder node whose entries load lazily as folders expand (dirs
    // first, alphabetical). Returns false when the path is not a directory.
    bool loadDirectory(const std::string& path);

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // The tree scrolls under a finger; the scrollbar thumb keeps its drag.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return !m_barDrag; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onTick(float dt) override;
    virtual bool isAnimating() const override {
        return m_anim.running() || Widget::isAnimating();
    }
    virtual bool wantsScroll() const override { return true; }

private:
    struct Row { TreeItem* node = nullptr; int depth = 0; };
    // The pre-toggle layout, held weakly: a row's node can be removed from the
    // tree while the 0.18s compression tween is still drawing it.
    struct Snap { std::weak_ptr<Widget> node; int depth = 0; };

    void flatten(Widget* parent, int depth);   // rebuild m_rows
    void clampSelected();
    void ensureVisible(int row);
    int rowAt(float localY) const;
    bool toggleAt(float localX, const Row& row) const;  // click on triangle?
    bool barVisible() const;           // rows overflow the viewport
    Rect barTrack() const;
    Rect barThumb() const;
    void toggleRow(int row);
    // A node can expand if it has children already, or a not-yet-scanned
    // filesystem path behind it (lazy directory loading).
    bool hasExpandable(const TreeItem* node) const;
    void ensureLoaded(TreeItem* node);   // scan node->path() on first expand
    // Re-flatten and start the compression tween. `oldRows` must be the
    // pre-toggle layout (the caller clears m_rows right after snapshotting).
    void rebuildAnimated();
    void commit(int row);
    // Offset from the tree's left edge to the first content column (icon or
    // checkbox) for a node at the given depth; the label follows these columns.
    float contentX(int depth) const;
    Rect checkRect(float baseX, float rowY) const;   // absolute-space box
    void drawGuides(Rasterizer& r, const Rect& b) const;

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
    Color m_bg{ogui::palette().window};
    Color m_border{ogui::palette().accent};
    Color m_text{ogui::palette().text};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_accent{ogui::palette().accent};
    ChangedCallback m_onChanged;

    bool m_showIcons = false;
    bool m_showChecks = false;
    bool m_showGuides = false;
    float m_iconSize = 14.0f;
    float m_checkSize = 13.0f;
    Color m_folderColor{ogui::hexColor("#e6b455")};
    Color m_fileColor{ogui::hexColor("#9fb4d8")};
    Color m_checkBorder{ogui::hexColor("#8a8f9c")};
    Color m_checkFill{ogui::palette().accent};

    // Compression animation. On a toggle we snapshot the pre-toggle rows
    // (node -> pixel y) and the row that was toggled; during the tween the
    // draw lerps every row from its old y to its new slot. Rows that just
    // appeared slide out of the toggled parent and fade in, rows that just
    // vanished slide into it and fade out -- so the list compresses instead
    // of the children simply blinking away.
    std::map<const TreeItem*, float> m_animOldY;
    std::vector<Snap> m_animOldRows;   // for detecting vanishing rows
    float m_animToggleY = 0.0f;
    float m_animOldH = 0.0f;           // widget height when the tween began
    Tween m_anim;

    // Scrollbar thumb drag.
    bool m_barDrag = false;
    float m_barGrab = 0.0f;
};

// ------------------------------------------------------------------
// TreeTableNode: one <tnode> of a TreeTable. The label is column 0;
// cells="a;b;c" fills the remaining columns. Data only -- the grid
// draws every row. Nest <tnode>s for children.
// ------------------------------------------------------------------
class TreeTableNode : public Widget {
public:
    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    // "a;b;c" -> cell values for columns 1..n (col 0 is the label).
    void setCells(const std::string& semicolonSeparated);
    void setCells(std::vector<std::string> c) { m_cells = std::move(c); }
    const std::vector<std::string>& cells() const { return m_cells; }
    void setExpanded(bool e) { m_expanded = e; }
    bool expanded() const { return m_expanded; }

private:
    std::string m_label;
    std::vector<std::string> m_cells;
    bool m_expanded = true;
};

// ------------------------------------------------------------------
// TreeTable: tree hierarchy inside tabular columns. Rows flatten from
// nested <tnode> children; columns come from <th> children (TableColumn)
// or setColumns(). Triangle or double... single click toggles expansion,
// clicking a row selects it (change event); header clicks sort the
// siblings at every level (re-click flips direction). Wheel scrolls,
// Up/Down/Home/End move the selection, Left/Right collapse/expand.
// Only the viewport rows are drawn, so deep trees stay cheap.
// ------------------------------------------------------------------
class TreeTable : public Widget {
public:
    using ChangedCallback = std::function<void(int)>;
    struct Column {
        std::string label;
        float width = 0.0f;    // 0 == share the leftover evenly
    };

    TreeTable();
    virtual ~TreeTable() override;

    void setColumns(const std::vector<Column>& cols);
    int columnCount() const { return static_cast<int>(m_cols.size()); }
    std::string columnLabel(int col) const;
    float columnWidth(int col) const;      // resolved px

    int rowCount() const { return static_cast<int>(m_rows.size()); }
    TreeTableNode* nodeAt(int row);
    // Column 0 is the node label, columns 1..n are its cells.
    std::string cell(int row, int col) const;
    int selectedIndex() const { return m_selected; }
    // Programmatic calls are silent (no change event).
    void setSelectedIndex(int index);
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    bool isExpanded(int row) const;
    void setExpanded(int row, bool on);
    void expandAll();
    void collapseAll();

    int sortColumn() const { return m_sortCol; }
    bool sortAscending() const { return m_sortAsc; }
    void setSortable(bool s) { m_sortable = s; }
    bool sortable() const { return m_sortable; }
    void sortBy(int col);                  // toggle direction on re-click

    void setRowHeight(float px) { m_rowH = px; }
    float rowHeight() const { return m_rowH; }
    void setHeaderHeight(float px) { m_headH = px; }
    float headerHeight() const { return m_headH; }
    void setIndent(float px) { m_indent = px; }
    float indent() const { return m_indent; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void setBgColor(const Color& c) { m_bg = c; }
    void setHeaderBgColor(const Color& c) { m_headerBg = c; }
    Color headerBgColor() const { return m_headerBg; }
    void setBorderColor(const Color& c) { m_border = c; }
    void setTextColor(const Color& c) { m_text = c; }
    void setHighlightColor(const Color& c) { m_highlight = c; }
    Color highlightColor() const { return m_highlight; }
    void setAccentColor(const Color& c) { m_accent = c; }
    Color accentColor() const { return m_accent; }
    bool isSelectedVisual() const override { return m_selected >= 0; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") setBgColor(v);
        else if (prop == "color") setTextColor(v);
        else if (prop == "border" || prop == "bordercolor") setBorderColor(v);
    }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // Rows scroll under a finger; the scrollbar thumb keeps its drag.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return !m_barDrag; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    struct FlatRow { TreeTableNode* node = nullptr; int depth = 0; };

    void rebuildColumns();          // collect <th> children once
    void flatten();                 // visible nodes -> m_rows
    void resort();                  // sort siblings at every level
    bool hasChildren(const TreeTableNode* node) const;
    float columnX(int col) const;
    float columnW(int col) const;
    int colAt(float localX) const;
    void clampScroll();
    void ensureVisible(int row);
    int rowAt(float localY) const;  // -1 == header strip
    bool toggleAt(float localX, const FlatRow& row) const;
    void commit(int row);           // user select -> notify
    void toggleRow(int row);
    bool barVisible() const;
    Rect barTrack() const;
    Rect barThumb() const;

    std::vector<Column> m_cols;
    std::vector<FlatRow> m_rows;
    bool m_colsResolved = false;    // <th> children collected
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    float m_rowH = 22.0f;
    float m_headH = 24.0f;
    float m_indent = 16.0f;
    float m_scroll = 0.0f;
    int m_selected = -1;            // flattened row index
    int m_wantedSelected = -1;      // selected= attr applied on flatten
    bool m_wantedApplied = false;
    int m_hover = -1;
    int m_sortCol = -1;             // -1 == document order
    bool m_sortAsc = true;
    bool m_sortable = true;
    Color m_bg{ogui::palette().window};
    Color m_headerBg{ogui::palette().surface};
    Color m_border{ogui::palette().accent};
    Color m_text{ogui::palette().text};
    Color m_highlight{ogui::hexColor("#334455")};
    Color m_accent{ogui::palette().accent};
    ChangedCallback m_onChanged;

    // Scrollbar thumb drag.
    bool m_barDrag = false;
    float m_barGrab = 0.0f;
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
    // Toggle and, when the value actually changed, notify + fire binding.
    void toggle();

    void setText(const std::string& t) { m_text = t; }
    const std::string& text() const { return m_text; }

    using ChangedCallback = std::function<void(bool)>;
    void setOnChanged(ChangedCallback cb) { m_onChanged = std::move(cb); }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onTick(float dt) override;
    virtual bool isAnimating() const override {
        return m_knobTween.running() || Widget::isAnimating();
    }
    bool isCheckedVisual() const override { return m_checked; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") m_bgColor = v;
        else if (prop == "color") m_textColor = v;
    }

    // Layout/spacing
    float m_gap = 6.0f;

private:
    // Widget-local rect of the pill track (label sits to its right).
    Rect trackRect() const;

    bool m_checked = false;
    Tween m_knobTween{};  // 0 = off side, 1 = on side
    float m_knob = 0.0f;
    std::string m_text;
    int m_fontSize = 13;
    std::string m_fontFamily = "sans";
    Color m_textColor{ogui::palette().text};
    Color m_bgColor{ogui::palette().surface};
    Color m_dotColor{ogui::palette().accent};
    Color m_activeBgColor{ogui::hexColor("#161616")};
    ChangedCallback m_onChanged;
};

// ------------------------------------------------------------------
// Badge / Tag / Chip: small pill label, optional count and [x] remove
// button that fires the widget's binding when clicked.
// ------------------------------------------------------------------
class Badge : public Widget {
public:
    Badge();
    virtual ~Badge() override;

    void setText(const std::string& t) { m_text = t; }
    const std::string& text() const { return m_text; }
    void setCount(int c) { m_count = c; }
    int count() const { return m_count; }
    void setRemovable(bool r) { m_removable = r; }
    bool removable() const { return m_removable; }
    void setBgColor(const Color& c) { m_bgColor = c; }
    void setFgColor(const Color& c) { m_fgColor = c; }
    void setBorderColor(const Color& c) { m_borderColor = c; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    // Widget-local rect of the [x] button (empty when not removable).
    Rect removeRect() const;

    std::string m_text;
    int m_count = -1;             // <0: hidden
    bool m_removable = false;
    bool m_removeHot = false;     // pointer pressed on the [x]
    Color m_bgColor{ogui::palette().surfaceHi};
    Color m_fgColor{ogui::palette().textBright};
    Color m_borderColor{ogui::palette().border};
    int m_fontSize = 12;
};

// ------------------------------------------------------------------
// StatusBar: thin info band with left / centre / right text slots.
// ------------------------------------------------------------------
class StatusBar : public Widget {
public:
    StatusBar();
    virtual ~StatusBar() override;

    void setText(const std::string& t) { m_left = t; }   // alias: text=
    void setLeft(const std::string& t) { m_left = t; }
    void setCenter(const std::string& t) { m_center = t; }
    void setRight(const std::string& t) { m_right = t; }
    const std::string& left() const { return m_left; }
    const std::string& center() const { return m_center; }
    const std::string& right() const { return m_right; }
    void setTextColor(const Color& c) { m_textColor = c; }
    void setDimColor(const Color& c) { m_dimColor = c; }
    void setBgColor(const Color& c) { m_bgColor = c; }
    void setFontSize(int s) { m_fontSize = s; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") m_bgColor = v;
        else if (prop == "color") m_textColor = v;
    }

    virtual void onDraw(Rasterizer& r) const override;

private:
    std::string m_left, m_center, m_right;
    Color m_textColor{ogui::palette().text};
    Color m_dimColor{ogui::palette().dim};
    Color m_bgColor{ogui::palette().surface};
    int m_fontSize = 12;
};

// ------------------------------------------------------------------
// CrumbItem: one <crumb label=".."> child of a Breadcrumb (data only).
// ------------------------------------------------------------------
class CrumbItem : public Widget {
public:
    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }

private:
    std::string m_label;
};

// ------------------------------------------------------------------
// Breadcrumb: "a / b / c" path of clickable segments. Clicking a segment
// makes it current and fires setOnSelected(index) + the widget change
// event; the current segment draws bright, the rest dim.
// ------------------------------------------------------------------
class Breadcrumb : public Widget {
public:
    using SegmentCallback = std::function<void(int)>;

    Breadcrumb();
    virtual ~Breadcrumb() override;

    // "a/b/c" -> segments (the separator string is kept for display).
    void setPath(const std::string& path);
    void setSegments(const std::vector<std::string>& segs);
    const std::vector<std::string>& segments() const { return m_segs; }
    void setSeparator(const std::string& s) { m_sep = s.empty() ? "/" : s; }
    const std::string& separator() const { return m_sep; }
    void setCurrent(int i);                 // silent
    int current() const { return m_current; }
    void setOnSelected(SegmentCallback cb) { m_onSelected = std::move(cb); }
    void setTextColor(const Color& c) { m_textColor = c; }
    Color textColor() const { return m_textColor; }
    void setDimColor(const Color& c) { m_dimColor = c; }
    Color dimColor() const { return m_dimColor; }
    void setAccentColor(const Color& c) { m_accentColor = c; }
    Color accentColor() const { return m_accentColor; }
    void setFontSize(int s) { m_fontSize = s; }
    int fontSize() const { return m_fontSize; }
    bool isSelectedVisual() const override { return m_current >= 0; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "color") m_textColor = v;
        else if (prop == "border" || prop == "bordercolor") m_accentColor = v;
    }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void collect();                          // adopt <crumb> children once
    // Segment rects in widget-local coords, refreshed before use.
    void measureSegs() const;

    std::vector<std::string> m_segs;
    std::string m_sep = "/";
    int m_current = -1;                      // -1: last segment
    Color m_textColor{ogui::palette().textBright};
    Color m_dimColor{ogui::palette().dim};
    Color m_accentColor{ogui::palette().accent};
    int m_fontSize = 13;
    bool m_collected = false;
    SegmentCallback m_onSelected;
    mutable std::vector<Rect> m_segRects;    // refreshed by measureSegs()
};

// ------------------------------------------------------------------
// Keybind: shortcut capture editor. Shows the current combo
// ("Ctrl+Alt+K") or a placeholder. Click starts capturing: the next key
// press stores mods+key and fires setOnChanged; Escape cancels and
// Backspace/Delete clears the combo. Empty value draws the placeholder.
// ------------------------------------------------------------------
class Keybind : public Widget {
public:
    using ValueCallback = std::function<void(const std::string&)>;

    Keybind();
    virtual ~Keybind() override;

    // "ctrl+shift+k" (case-insensitive, order-free); "" clears.
    bool setValue(const std::string& combo);
    std::string value() const;               // formatted canonical combo
    void setPlaceholder(const std::string& p) { m_placeholder = p; }
    void setOnChanged(ValueCallback cb) { m_onChanged = std::move(cb); }
    bool capturing() const { return m_capturing; }

    void setTextColor(const Color& c) { m_textColor = c; }
    void setBgColor(const Color& c) { m_bgColor = c; }
    void setBorderColor(const Color& c) { m_borderColor = c; }
    void setRadius(float r) { m_radius = r; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "background") m_bgColor = v;
        else if (prop == "color") m_textColor = v;
        else if (prop == "border" || prop == "bordercolor") m_borderColor = v;
    }
    void applyVisualFloat(const std::string& prop, float v) override {
        if (prop == "radius") m_radius = v;
    }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onFocus(bool focused) override;
    virtual void onDraw(Rasterizer& r) const override;
    // While capturing a combo the user may press Tab as the target key, so
    // the Kernel must not steal it for focus traversal.
    virtual bool consumesTab() const override { return m_capturing; }

    // Combo text <-> KeyCode/mods (public for tests and hosts).
    static std::string format(KeyCode key, uint32_t mods);
    static bool parse(const std::string& combo, KeyCode& key, uint32_t& mods);

private:
    KeyCode m_key = Key_None;
    uint32_t m_mods = Mod_None;
    bool m_capturing = false;
    std::string m_placeholder = "Press a key...";
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    Color m_textColor{ogui::palette().textBright};
    Color m_bgColor{ogui::palette().window};
    Color m_borderColor{ogui::palette().accent};
    float m_radius = 4.0f;
    ValueCallback m_onChanged;
};

// ------------------------------------------------------------------
// CommandAction: data-only row feeding a CommandPalette.
// ------------------------------------------------------------------
class CommandAction : public Widget {
public:
    CommandAction();
    virtual ~CommandAction() override;

    void setName(const std::string& n) { m_name = n; }
    const std::string& name() const { return m_name; }
    void setHint(const std::string& h) { m_hint = h; }
    const std::string& hint() const { return m_hint; }
    void setKey(const std::string& k) { m_key = k; }
    const std::string& key() const { return m_key; }

private:
    std::string m_name;
    std::string m_hint;
    std::string m_key;
};

// ------------------------------------------------------------------
// CommandPalette: incremental-search launcher over registered actions.
// Type to filter (case-insensitive substring on name, then hint),
// Up/Down/Home/End walk the matches, Enter or a click runs the
// selection and fires the widget "change" event, Escape fires
// "submit" so the host can close it. Runs are kept in a deduplicated
// newest-first history.
// ------------------------------------------------------------------
class CommandPalette : public Widget {
public:
    using RunCallback = std::function<void(const std::string&)>;

    CommandPalette();
    virtual ~CommandPalette() override;

    // Model (silent — the palette never re-announces programmatic edits).
    void addCommand(const std::string& name, const std::string& hint = {},
                    const std::string& key = {});
    bool removeCommand(const std::string& name);
    void clearCommands();
    int count() const { return static_cast<int>(m_cmds.size()); }
    std::string name(int i) const;
    std::string hint(int i) const;
    std::string key(int i) const;

    void setQuery(const std::string& q);      // silent
    const std::string& query() const { return m_query; }
    int matchCount() const;                   // refreshes the filtered view
    std::string match(int row) const;         // name of the row-th match
    int selectedIndex() const { return m_sel; }
    void setSelectedIndex(int i);             // silent

    void setOnRun(RunCallback cb) { m_onRun = std::move(cb); }
    bool exec(const std::string& name);       // silent: runs + history only

    int historyCount() const { return static_cast<int>(m_history.size()); }
    std::string history(int i) const;         // 0 = newest
    void clearHistory();
    void setMaxHistory(int n) { m_maxHistory = n < 0 ? 0 : n; trimHistory(); }
    int maxHistory() const { return m_maxHistory; }

    void setPlaceholder(const std::string& p) { m_placeholder = p; }
    const std::string& placeholder() const { return m_placeholder; }
    void setTextColor(const Color& c) { m_textColor = c; }
    void setDimColor(const Color& c) { m_dimColor = c; }
    void setAccentColor(const Color& c) { m_accentColor = c; }
    void setHighlightColor(const Color& c) { m_highlightColor = c; }
    void setBgColor(const Color& c) { m_bgColor = c; }
    void setRowHeight(float h) { if (h > 0.0f) m_rowH = h; }
    float rowHeight() const { return m_rowH; }
    void setHeaderHeight(float h) { m_headH = h < 0.0f ? 0.0f : h; }
    float headerHeight() const { return m_headH; }
    void setFontFamily(const std::string& f) { m_fontFam = f; }
    void setFontSize(int s) { m_fontSize = s; }
    void applyVisualColor(const std::string& prop, const Color& v) override {
        if (prop == "color") m_textColor = v;
        else if (prop == "background") m_bgColor = v;
        else if (prop == "highlight") m_highlightColor = v;
        else if (prop == "accent") m_accentColor = v;
    }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerScroll(float dx, float dy) override;
    // The filtered result list follows the finger.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return true; }
    virtual void onKeyDown(KeyCode key, uint32_t mods) override;
    virtual void onChar(uint32_t ch) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool wantsScroll() const override { return true; }

private:
    struct Cmd { std::string name, hint, key; };

    void collect();                 // adopt <cmd> children once
    void refilter();
    void clampScroll();
    void ensureVisible();
    int rowAt(float ly) const;      // filtered row under local y (-1 = none)
    void runRow(int row);           // user run: exec + history + change event
    void pushHistory(const std::string& name);
    void trimHistory();

    std::vector<Cmd> m_cmds;
    std::vector<size_t> m_rows;     // filtered indices into m_cmds
    bool m_dirty = true;
    bool m_collected = false;

    std::string m_query;
    std::string m_placeholder = "Type to filter...";
    int m_sel = -1;                 // index into m_rows
    float m_scroll = 0.0f;

    std::vector<std::string> m_history;   // newest first, deduplicated
    int m_maxHistory = 50;

    float m_rowH = 22.0f;
    float m_headH = 26.0f;
    std::string m_fontFam = "sans";
    int m_fontSize = 13;
    Color m_textColor{ogui::palette().textBright};
    Color m_dimColor{ogui::palette().dim};
    Color m_accentColor{ogui::palette().accent};
    Color m_highlightColor{ogui::palette().surfaceHi};
    Color m_bgColor{ogui::palette().window};
    RunCallback m_onRun;
};

// ------------------------------------------------------------------
// Spinner: animated loading indicator. variant="glyph" cycles |/-\,
// variant="blocks" slides a filled window through [####......].
// ------------------------------------------------------------------
class Spinner : public Widget {
public:
    Spinner();
    virtual ~Spinner() override;

    void setText(const std::string& t) { m_text = t; }
    const std::string& text() const { return m_text; }
    void setVariant(const std::string& v);   // "glyph" | "blocks"
    const char* variant() const { return m_blocks ? "blocks" : "glyph"; }
    void setSpeed(float hz) { m_speed = hz > 0.0f ? hz : 8.0f; }
    float speed() const { return m_speed; }
    void setFgColor(const Color& c) { m_fgColor = c; }

    virtual void onTick(float dt) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual bool isAnimating() const override { return true; }

private:
    std::string m_text;
    bool m_blocks = false;
    float m_speed = 8.0f;
    float m_phase = 0.0f;
    Color m_fgColor{ogui::palette().text};
    int m_fontSize = 14;
};

// ------------------------------------------------------------------
// SearchInput: text field with a magnifier inset and a [x] clear button
// (clears the text and fires the binding).
// ------------------------------------------------------------------
class SearchInput : public TextInput {
public:
    SearchInput();
    virtual ~SearchInput() override;

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

protected:
    virtual float insetLeft() const override  { return kIconW; }
    virtual float insetRight() const override { return kIconW; }

private:
    static constexpr float kIconW = 16.0f;
    Rect clearRect() const;      // widget-local
};

// ------------------------------------------------------------------
// Accordion: stack of collapsible sections.
// <accordion><section header="...">...children...</section></accordion>
// Clicking a header toggles it (one open at a time when exclusive).
// ------------------------------------------------------------------
class AccordionSection : public Widget {
public:
    AccordionSection();
    virtual ~AccordionSection() override;

    void setHeader(const std::string& h) { m_header = h; }
    const std::string& header() const { return m_header; }
    void setExpanded(bool e) { m_expanded = e; }
    bool expanded() const { return m_expanded; }
    float headerHeight() const { return m_headerH; }
    void setHeaderHeight(float h) { if (h > 0.0f) m_headerH = h; }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    friend class Accordion;
    std::string m_header;
    bool m_expanded = false;
    float m_headerH = 24.0f;
    float m_contentH = 0.0f;   // measured by the accordion
    Color m_headerColor{ogui::palette().surfaceHi};
    Color m_fgColor{ogui::palette().textBright};
    int m_fontSize = 14;
};

class Accordion : public Widget {
public:
    Accordion();
    virtual ~Accordion() override;

    void setExclusive(bool e) { m_exclusive = e; }
    bool exclusive() const { return m_exclusive; }
    void setGap(float g) { m_gap = g >= 0.0f ? g : 0.0f; }
    float gap() const { return m_gap; }

    virtual void layout(float parentWidth, float parentHeight) override;

private:
    bool m_exclusive = true;
    float m_gap = 4.0f;
};

// ------------------------------------------------------------------
// Modal: full-parent dim backdrop with a centered panel. The modal is
// the hit target while open, blocking everything painted below it (put
// it last among the screen's children). open shows/hides at runtime.
// ------------------------------------------------------------------
class Modal : public Widget {
public:
    Modal();
    virtual ~Modal() override;

    void setTitle(const std::string& t) { m_title = t; }
    const std::string& title() const { return m_title; }
    void setOpen(bool o);
    bool isOpen() const { return m_open; }
    void setPanelSize(float w, float h) { m_panelW = w; m_panelH = h; }
    Rect panelSize() const { return Rect{0.0f, 0.0f, m_panelW, m_panelH}; }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    Rect panelRect() const;      // parent-space

    std::string m_title;
    bool m_open = true;
    float m_panelW = 340.0f;
    float m_panelH = 160.0f;

    // Authored child offsets, captured once before we start repositioning.
    std::vector<Offset2D> m_childOff;
    bool m_childOffCaptured = false;
};

// ------------------------------------------------------------------
// Sparkline / Plot: polyline over a value buffer (FPS, frametime...).
// values="60;59;58;..." seeds it; addSample() appends live data.
// ------------------------------------------------------------------
class Sparkline : public Widget {
public:
    Sparkline();
    virtual ~Sparkline() override;

    void setValues(const std::string& csv);   // ';'- or ','-separated floats
    const std::vector<float>& values() const { return m_values; }
    void addSample(float v);
    void setMaxSamples(int n) { if (n > 1) m_max = static_cast<size_t>(n); }
    int maxSamples() const { return static_cast<int>(m_max); }
    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    void setLineColor(const Color& c) { m_lineColor = c; }
    Color lineColor() const { return m_lineColor; }
    void setBgColor(const Color& c) { m_bgColor = c; }
    Color bgColor() const { return m_bgColor; }

    virtual void onDraw(Rasterizer& r) const override;

private:
    std::vector<float> m_values;
    size_t m_max = 60;
    std::string m_label;
    Color m_lineColor{ogui::hexColor("#44dd88")};
    Color m_bgColor{ogui::palette().window};
    Color m_gridColor{ogui::hexColor("#2a2a2a")};
    int m_fontSize = 12;
};

// ------------------------------------------------------------------
// Menu system: MenuPopup is the floating list (owned by whoever opens
// it); MenuBar/Menu/MenuItem describe the XML tree; ContextMenu is an
// invisible holder opened by the Kernel on right-click.
// ------------------------------------------------------------------
struct MenuEntry {
    std::string label;
    std::string command;
    bool separator = false;
    std::vector<MenuEntry> sub;
};

class MenuPopup : public Widget {
public:
    MenuPopup();
    virtual ~MenuPopup() override;

    void setEntries(const std::vector<MenuEntry>& items);
    void setOnSelect(std::function<void(const std::string&)> cb) {
        m_onSelect = std::move(cb);
    }
    void setOnDismiss(std::function<void()> cb) { m_onDismiss = std::move(cb); }
    void fadeIn();

    void setRowHeight(float px) { m_rowH = px; }
    int rowCount() const { return static_cast<int>(m_rows.size()); }
    float contentHeight() const {
        return static_cast<float>(m_rows.size()) * m_rowH;
    }
    // Pixels of the row list scrolled above the popup viewport (tests/peek).
    float scroll() const { return m_scroll; }

    virtual void onTick(float dt) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerScroll(float, float dy) override;
    // A long menu scrolls under a finger; the scrollbar thumb keeps its drag.
    virtual float scrollUnit() const override { return m_rowH * 3.0f; }
    virtual bool wantsPan(float, float) const override { return !m_barDrag; }
    virtual bool wantsScroll() const override { return true; }
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onPopupDismissed() override { if (m_onDismiss) m_onDismiss(); }
    virtual bool isAnimating() const override {
        return m_fade.running() || Widget::isAnimating();
    }

private:
    int rowAt(float localY) const;      // index into the visible rows
    void rebuildRows();                 // flatten current level into rows
    void pushLevel(const MenuEntry& e);
    void popLevel();
    bool barVisible() const;            // rows overflow the popup
    Rect barTrack() const;              // popup-local space
    Rect barThumb() const;
    void clampScroll();

    struct Level { std::string title; std::vector<MenuEntry> entries; };

    std::vector<Level> m_stack;
    // Per pushed level (m_stack.size()-1 entries): the popup height and
    // scroll restored when going back, so a short submenu does not shrink
    // the parent level permanently.
    std::vector<float> m_levelH, m_levelScroll;
    // Visible rows: separators are inert rows; sub entries map to their item.
    struct Row { const MenuEntry* entry = nullptr; bool separator = false;
                 bool back = false; };
    std::vector<Row> m_rows;
    int m_hover = -1;
    float m_rowH = 24.0f;
    float m_scroll = 0.0f;              // px of rows above the viewport
    bool m_barDrag = false;             // pointer is dragging the scrollbar
    float m_barGrab = 0.0f;             // thumb-top offset of the drag press
    int m_fontSize = 13;
    Tween m_fade;                        // open-in fade, like ComboPopup
    std::function<void(const std::string&)> m_onSelect;
    std::function<void()> m_onDismiss;
    Color m_panel{ogui::palette().surface};
    Color m_border{ogui::palette().border};
    Color m_text{ogui::palette().textBright};
    Color m_highlight{ogui::hexColor("#2b3a55")};
    Color m_sepColor{ogui::palette().border};
};

// One <menuitem label command> (may nest more for a submenu) or <menuseparator/>.
class MenuItem : public Widget {
public:
    MenuItem();
    virtual ~MenuItem() override;

    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }
    void setCommand(const std::string& c) { m_command = c; }
    const std::string& command() const { return m_command; }
    void setSeparator(bool s) { m_separator = s; }
    bool separator() const { return m_separator; }

    static MenuEntry buildEntry(const Widget& w);   // recurse children

private:
    std::string m_label;
    std::string m_command;
    bool m_separator = false;
};

// A top-level <menu label="Arquivo"> inside a MenuBar: holds MenuItem kids.
class Menu : public Widget {
public:
    Menu();
    virtual ~Menu() override;

    void setLabel(const std::string& l) { m_label = l; }
    const std::string& label() const { return m_label; }

private:
    std::string m_label;
};

class MenuBar : public Widget {
public:
    MenuBar();
    virtual ~MenuBar() override;

    void setOnCommand(std::function<void(const std::string&)> cb) {
        m_onCommand = std::move(cb);
    }

    virtual void layout(float parentWidth, float parentHeight) override;
    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void openMenu(size_t index);

    std::shared_ptr<MenuPopup> m_popup;
    std::function<void(const std::string&)> m_onCommand;
    float m_pad = 10.0f;
    int m_fontSize = 13;
    Color m_bgColor{ogui::palette().surface};
    Color m_fgColor{ogui::palette().text};

    // Label widths from the last layout; <menu> children stay zero-sized so
    // they never steal the hit-test from the bar.
    std::vector<float> m_widths;
};

// <contextmenu id="..."> holder: invisible in the tree, opened by the
// Kernel when a widget with contextmenu="<id>" is right-clicked.
class ContextMenu : public Widget {
public:
    ContextMenu();
    virtual ~ContextMenu() override;

    void setOnCommand(std::function<void(const std::string&)> cb) {
        m_onCommand = std::move(cb);
    }
    void openAt(float x, float y);

    virtual void onDraw(Rasterizer&) const override {}   // never drawn inline

private:
    std::shared_ptr<MenuPopup> m_popup;
    std::function<void(const std::string&)> m_onCommand;
};

// ------------------------------------------------------------------
// ColorPicker: swatch + hex; click opens an SV square / hue strip popup.
// ------------------------------------------------------------------
class ColorPopup : public Widget {
public:
    ColorPopup();
    virtual ~ColorPopup() override;

    void setHSV(float h, float s, float v);
    void getHSV(float& h, float& s, float& v) const {
        h = m_h; s = m_s; v = m_v;
    }
    void setOnChanged(std::function<void(float, float, float)> cb) {
        m_onChanged = std::move(cb);
    }
    void setOnDismiss(std::function<void()> cb) { m_onDismiss = std::move(cb); }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onDraw(Rasterizer& r) const override;
    virtual void onPopupDismissed() override { if (m_onDismiss) m_onDismiss(); }

    static void hsvToRgb(float h, float s, float v, float& r, float& g, float& b);

private:
    Rect svRect() const;    // absolute-space regions (bounds are absolute)
    Rect hueRect() const;
    void pick(const Offset2D& p, bool fromHue);

    float m_h = 0.6f, m_s = 0.6f, m_v = 1.0f;
    int m_drag = 0;              // 1 = SV square, 2 = hue strip
    std::function<void(float, float, float)> m_onChanged;
    std::function<void()> m_onDismiss;
    int m_fontSize = 12;
    Color m_panel{ogui::palette().surface};
    Color m_border{ogui::palette().border};
};

class ColorPicker : public Widget {
public:
    ColorPicker();
    virtual ~ColorPicker() override;

    void setColorHex(const std::string& hex);
    std::string colorHex() const;
    Color color() const { return m_color; }
    void setOnChanged(std::function<void(const std::string&)> cb) {
        m_onChanged = std::move(cb);
    }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void applyHsv();

    float m_h = 0.0f, m_s = 0.0f, m_v = 0.0f;
    Color m_color{ogui::palette().accent};
    std::shared_ptr<ColorPopup> m_popup;
    std::function<void(const std::string&)> m_onChanged;
    int m_fontSize = 12;
};

// ------------------------------------------------------------------
// PathPicker: read-only path field + "..." button that opens a file
// browser popup (directories first, ".." to go up).
// ------------------------------------------------------------------
class PathPicker : public Widget {
public:
    PathPicker();
    virtual ~PathPicker() override;

    void setValue(const std::string& v) { m_value = v; }
    const std::string& value() const { return m_value; }
    void setPlaceholder(const std::string& p) { m_placeholder = p; }
    const std::string& placeholder() const { return m_placeholder; }
    void setStartPath(const std::string& p) { m_start = p; }
    const std::string& startPath() const { return m_start; }
    void setFilter(const std::string& ext) { m_filter = ext; }
    const std::string& filter() const { return m_filter; }
    void setOnChanged(std::function<void(const std::string&)> cb) {
        m_onChanged = std::move(cb);
    }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void openBrowser();
    void listDir(const std::string& dir);

    std::string m_value;
    std::string m_placeholder;
    std::string m_start = ".";
    std::string m_filter;          // e.g. ".png"; empty = everything
    std::shared_ptr<MenuPopup> m_popup;
    std::function<void(const std::string&)> m_onChanged;
    int m_fontSize = 13;
    Color m_bgColor{ogui::palette().window};
    Color m_fgColor{ogui::palette().textBright};
    Color m_borderColor{ogui::palette().border};
};

// ------------------------------------------------------------------
// RampEditor / ColorGradient: a horizontal bar built from movable color
// stops. Click the bar to add a stop, drag a stop to reposition it. The
// gradient is the piecewise-linear interpolation between adjacent stops.
// ------------------------------------------------------------------
class RampEditor : public Widget {
public:
    struct Stop { float pos = 0.0f; Color color{}; };

    RampEditor();
    virtual ~RampEditor() override;

    void setStops(const std::vector<Stop>& stops);
    // "0:#000000 0.5:#ff3300 1:#ffffaa" — whitespace-separated pos:hex pairs.
    bool setStopsFromSpec(const std::string& spec);
    const std::vector<Stop>& stops() const { return m_stops; }
    int stopCount() const { return static_cast<int>(m_stops.size()); }
    // Interpolated color at pos (clamped to [0,1]).
    Color sample(float pos) const;
    void clear();
    void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void notify();

    std::vector<Stop> m_stops;
    int m_drag = -1;             // index of the stop being dragged
    std::function<void()> m_onChanged;
    Color m_borderColor{ogui::palette().border};
    Color m_thumbColor{ogui::palette().white};
};

// ------------------------------------------------------------------
// CurveEditor: a cubic Bezier with four draggable control points, drawn
// in a unit box (x = input 0..1, y = output 0..1, top edge = 1.0).
// ------------------------------------------------------------------
class CurveEditor : public Widget {
public:
    struct Point { float x = 0.0f, y = 0.0f; };   // normalised, y up

    CurveEditor();
    virtual ~CurveEditor() override;

    void setPoints(const Point pts[4]);
    // "0,0 0.33,1 0.66,0 1,1" — whitespace-separated x,y pairs (y up).
    bool setPointsFromSpec(const std::string& spec);
    const Point& point(int i) const { return m_pts[i]; }
    // Output value (0..1, y up) for input t (0..1).
    float evaluate(float t) const;
    void setOnChanged(std::function<void()> cb) { m_onChanged = std::move(cb); }

    virtual void onPointerDown(MouseButton btn, const Offset2D& p) override;
    virtual void onPointerUp(MouseButton btn) override;
    virtual void onPointerMove(const Offset2D& p) override;
    virtual void onDraw(Rasterizer& r) const override;

private:
    void notify();

    Point m_pts[4] = { {0.0f, 0.0f}, {0.33f, 1.0f}, {0.66f, 0.0f}, {1.0f, 1.0f} };
    int m_drag = -1;
    int m_hover = -1;
    std::function<void()> m_onChanged;
    Color m_bgColor{ogui::palette().window};
    Color m_gridColor{ogui::palette().surface};
    Color m_handleColor{ogui::hexColor("#444455")};
    Color m_curveColor{ogui::palette().white};
    Color m_endColor{ogui::hexColor("#00e5ff")};
    Color m_ctrlColor{ogui::hexColor("#ffea00")};
};

// ------------------------------------------------------------------
// XML widgets *ebay registry* (tag -> creator)
// ------------------------------------------------------------------
using WidgetFactory = std::shared_ptr<Widget> (*)(const Style&, const std::map<std::string, std::string>&);

std::map<std::string, WidgetFactory>& widgetRegistry();

} // namespace ogui
