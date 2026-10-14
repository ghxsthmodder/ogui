#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// Label
// ------------------------------------------------------------------
Label::Label() = default;
Label::~Label() = default;

void Label::layout(float parentWidth, float parentHeight) {
    // A wrapped label without an authored height sizes to its text, whether
    // it is a direct screen child or inside a container that already called
    // measureHeight() (calling it again is cheap and idempotent).
    if (m_wrap && m_bounds.h <= 0.0f) {
        const float need = measureHeight(m_bounds.w);
        if (need > 0.0f) m_bounds.h = need;
    }
    Widget::layout(parentWidth, parentHeight);
}

void Label::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    if (m_text.empty()) return;
    Rect b = bounds();

    // Center/right are relative to the whole box: anchoring on b.x pushed
    // aligned text half outside the widget.
    float x = b.x;
    if (m_hAlign == Align::Center) x = b.x + b.w * 0.5f;
    else if (m_hAlign == Align::Right) x = b.x + b.w;

    if (m_wrap) {
        r.drawTextWrapped(m_text.c_str(), x, b.y, b.w, m_fontSize, m_fontFamily,
                          m_textColor, m_hAlign);
        return;
    }
    // drawText treats y as the line's reference point: pick it inside the
    // box according to the vertical alignment.
    float y = b.y;
    if (m_vAlign == VertAlign::Middle) y = b.y + b.h * 0.5f;
    else if (m_vAlign == VertAlign::Bottom) y = b.y + b.h;
    r.drawText(m_text.c_str(), x, y, m_fontSize, m_fontFamily, m_textColor,
               m_hAlign, m_vAlign);
}

float Label::measureHeight(float width) const {
    if (m_text.empty()) return 0.0f;
    const std::vector<TextLine> lines =
        layoutText(m_text.c_str(), m_wrap ? width : 0.0f, m_fontSize, m_fontFamily);
    if (lines.empty()) return 0.0f;
    return static_cast<float>(lines.size()) * textLineHeight(m_fontSize, m_fontFamily);
}

// ------------------------------------------------------------------
// Button
// ------------------------------------------------------------------
Button::Button() = default;
Button::~Button() = default;

void Button::onPointerDown(MouseButton, const Offset2D&) {
    m_pressed = true;
}

void Button::onPointerUp(MouseButton) {
    if (m_pressed) {
        // emit pressed callback to owner
    }
    m_pressed = false;
}

void Button::onPointerMove(const Offset2D&) {
    // hover handled by kernel owner
}

void Button::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    // Hover brightens, press darkens, focus draws an accent border.
    Color bg = m_bgColor;
    if (m_pressed) bg = shade(bg, 0.78f);
    else if (isHovered()) bg = shade(bg, 1.22f);
    if (bg.a > 0) r.drawRoundedRect(b, bg, m_radius);
    // A faint top highlight line makes the box read as raised.
    if (bg.a > 0 && b.h > 8.0f) {
        r.drawLine({b.x + m_radius + 2.0f, b.y + 1.5f},
                   {b.x + b.w - m_radius - 2.0f, b.y + 1.5f},
                   withAlpha(palette().white, m_pressed ? 6 : 28), 1.0f);
    }
    const bool focus = isFocused();
    if (m_borderWidth > 0.0f || focus) {
        r.drawRectBorder(b, focus ? palette().focus : m_borderColor,
                         focus ? 2.0f : m_borderWidth, m_radius);
    }
    if (!m_text.empty()) {
        float tx = b.x + b.w * 0.5f;
        if (m_textAlign == Align::Left) tx = b.x + m_textPadding;
        else if (m_textAlign == Align::Right) tx = b.x + b.w - m_textPadding;
        r.drawText(m_text.c_str(), tx, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   m_textAlign, VertAlign::Middle);
    }
}

// ------------------------------------------------------------------
// Image
// ------------------------------------------------------------------
Image::Image() = default;
Image::~Image() = default;

void Image::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f || m_path.empty()) return;

    const ImageData* img = ImageManager::instance().load(m_path);
    if (img && img->w > 0 && img->h > 0) {
        // White tint: the vertex colour multiplies the texture, so 255
        // leaves the image's own colours untouched.
        const Color tint{255, 255, 255, 255};
        if (!m_tile) {
            r.drawRectUV(b, Rect{0, 0, 1, 1}, tint, img->id);
        } else {
            // One full-UV quad per tile keeps both backends' samplers on
            // their clamp-to-edge default.
            const float tw = static_cast<float>(img->w);
            const float th = static_cast<float>(img->h);
            const float right = b.x + b.w, bottom = b.y + b.h;
            for (float ty = b.y; ty < bottom; ty += th) {
                for (float tx = b.x; tx < right; tx += tw) {
                    Rect q{tx, ty, std::min(tw, right - tx),
                           std::min(th, bottom - ty)};
                    r.drawRectUV(q, Rect{0, 0, 1, 1}, tint, img->id);
                }
            }
        }
    }

    if (m_style.borderWidth > 0.0f) {
        r.drawRectBorder(b, m_style.borderColor, m_style.borderWidth,
                         m_style.radius);
    }
}

// ------------------------------------------------------------------
// Panel
// ------------------------------------------------------------------
Panel::Panel() = default;
Panel::~Panel() = default;

void Panel::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_style.hasGradient) {
        if (m_style.gradientHorizontal)
            r.drawHorizontalGradient(b, m_bgColor, m_style.gradientColor);
        else
            r.drawRoundedGradient(b, m_bgColor, m_style.gradientColor, m_radius);
    } else if (m_bgColor.a > 0) {
        r.drawRoundedRect(b, m_bgColor, m_radius);
    }
    if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
    }
}

// ------------------------------------------------------------------
// Window
// ------------------------------------------------------------------
Window::Window() = default;
Window::~Window() = default;

void Window::layout(float parentWidth, float parentHeight) {
    const float th = titleHeight();
    const float w = m_bounds.w > 0.0f ? m_bounds.w : parentWidth;
    const float h = m_bounds.h > 0.0f ? m_bounds.h : parentHeight;
    const float innerH = std::max(h - th, 0.0f);

    for (const auto& c : m_children) {
        if (!c) continue;
        Rect cb = c->bounds();
        // Undo the offset applied on the previous frame before recomputing:
        // layout() runs every frame and these bounds are authoritative.
        c->setBounds(Rect{cb.x, cb.y - m_childrenOffsetY, cb.w, cb.h});
        c->resolveGeometry(w, innerH);
        c->layout(w, innerH);
        Rect after = c->bounds();
        c->setBounds(Rect{after.x, after.y + th, after.w, after.h});
    }
    m_childrenOffsetY = th;

    // How far the visible children reach past the bottom of the body; this
    // is the scrollable range.
    float bottom = 0.0f;
    for (const auto& c : m_children) {
        if (!c || !c->isVisible()) continue;
        const Rect cb = c->bounds();
        bottom = std::max(bottom, cb.y + cb.h);
    }
    m_overflow = std::max(bottom - h, 0.0f);
    m_scrollY = std::clamp(m_scrollY, 0.0f, m_overflow);
}

void Window::onPointerScroll(float, float dy) {
    if (m_overflow <= 0.0f) return;
    m_scrollY = std::clamp(m_scrollY - dy * m_scrollStep, 0.0f, m_overflow);
    if (Kernel* kn = kernel()) kn->invalidate();
}

bool Window::clipsChildren(Rect& outRect) const {
    const float th = titleHeight();
    outRect = Rect{0.0f, th, m_bounds.w, std::max(m_bounds.h - th, 0.0f)};
    return true;
}

void Window::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    // Body first, then title bar on top of it.
    if (m_style.hasGradient && m_style.gradientHorizontal)
        r.drawHorizontalGradient(b, m_bgColor, m_style.gradientColor);
    else if (m_style.hasGradient)
        r.drawRoundedGradient(b, m_bgColor, m_style.gradientColor, m_radius);
    else if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);

    if (m_titleVisible) {
        float th = m_titleHeight;
        // Slightly lighter title bar.
        auto lift = [](Color c, int d) {
            auto cl = [](int v) { return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v)); };
            return Color{cl(c.r + d), cl(c.g + d), cl(c.b + d), c.a};
        };
        r.drawRect({b.x, b.y, b.w, th}, lift(m_bgColor, 24));

        if (!m_title.empty()) {
            r.drawText(m_title.c_str(), b.x + 8.0f, b.y + th * 0.5f, 14, "sans",
                       palette().white, Align::Left, VertAlign::Middle);
        }

        float btnW = 16.0f, btnH = 16.0f;
        float pad = (th - btnH) * 0.5f;
        float bx = b.x + b.w - 3 * (btnW + 4) - 4;
        r.drawRect({bx, b.y + pad, btnW, btnH}, palette().error);
        bx = b.x + b.w - 2 * (btnW + 4) - 4;
        r.drawRect({bx, b.y + pad, btnW, btnH}, palette().warn);
        bx = b.x + b.w - (btnW + 4) - 4;
        r.drawRect({bx, b.y + pad, btnW, btnH}, palette().info);

    }
    if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
    }
}

// ---- overlay scrollbar ----
// Vertical bar on the body's right edge (below the title), draggable; the
// scroll range is the px the children reach past the bottom (m_overflow).

bool Window::barVisible() const {
    return m_overflow > 1.0f;
}

Rect Window::barTrack() const {
    const Rect b = bounds();
    const float th = titleHeight();
    return Rect{b.w - kBarW - kBarMargin, th + kBarMargin,
                kBarW, std::max(b.h - th - 2.0f * kBarMargin, 0.0f)};
}

Rect Window::barThumb() const {
    const float viewH = std::max(bounds().h - titleHeight(), 0.0f);
    return barThumbRect(barTrack(), viewH, viewH + m_overflow, m_scrollY);
}

void Window::applyBarScroll(float scroll) {
    m_scrollY = std::clamp(scroll, 0.0f, m_overflow);
    if (Kernel* kn = kernel()) kn->invalidate();
}

bool Window::claimsPoint(const Offset2D& localP) const {
    if (!barVisible()) return false;
    const Rect trk = barTrack();
    return localP.x >= trk.x && localP.x <= trk.x + trk.w &&
           localP.y >= trk.y && localP.y <= trk.y + trk.h;
}

void Window::onPointerDown(MouseButton btn, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (barVisible()) {
        const Rect trk = barTrack();
        if (lx >= trk.x && lx <= trk.x + trk.w &&
            ly >= trk.y && ly <= trk.y + trk.h) {
            m_barDrag = true;
            const float viewH = std::max(bounds().h - titleHeight(), 0.0f);
            const float total = viewH + m_overflow;
            const Rect th = barThumb();
            if (ly >= th.y && ly <= th.y + th.h) {
                m_barGrab = ly - th.y;
            } else {
                // Empty track: jump the thumb center to the press point.
                m_barGrab = th.h * 0.5f;
                applyBarScroll(barScrollAt(trk, viewH, total, ly, m_barGrab));
            }
            return;
        }
    }
    Panel::onPointerDown(btn, p);
}

void Window::onPointerUp(MouseButton btn) {
    m_barDrag = false;
    Panel::onPointerUp(btn);
}

void Window::onPointerMove(const Offset2D& p) {
    if (!m_barDrag) return;
    const float ly = static_cast<float>(p.y) - absoluteOrigin().y;
    const float viewH = std::max(bounds().h - titleHeight(), 0.0f);
    const float total = viewH + m_overflow;
    applyBarScroll(barScrollAt(barTrack(), viewH, total, ly, m_barGrab));
}

void Window::onDrawForeground(Rasterizer& r) const {
    if (!barVisible()) return;
    // Bounds are shifted into absolute space by Kernel::drawTree here.
    const Rect bb = bounds();
    drawVerticalBar(r, offsetRect(barTrack(), bb.x, bb.y),
                    offsetRect(barThumb(), bb.x, bb.y), m_barDrag);
}

// ------------------------------------------------------------------
// Separator
// ------------------------------------------------------------------
Separator::Separator() = default;
Separator::~Separator() = default;

void Separator::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (b.w >= b.h) {
        const float y = b.y + b.h * 0.5f;
        r.drawLine({b.x, y}, {b.x + b.w, y}, m_lineColor, m_thickness);
    } else {
        const float x = b.x + b.w * 0.5f;
        r.drawLine({x, b.y}, {x, b.y + b.h}, m_lineColor, m_thickness);
    }
}

// ------------------------------------------------------------------
// Registry
// ------------------------------------------------------------------
using WidgetFactory = std::shared_ptr<Widget> (*)(const Style&, const std::map<std::string, std::string>&);

std::map<std::string, WidgetFactory>& widgetRegistry() {
    static std::map<std::string, WidgetFactory> reg;
    if (reg.empty()) {
        reg["screen"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Widget>(); };
        // Data-only root for a list of screens; children carry title/path in
        // their raw attributes (see Widget::rawAttributes).
        reg["manifest"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Widget>(); };
        reg["label"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Label>(); };
        reg["button"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Button>(); };
        reg["image"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Image>(); };
        reg["panel"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Panel>(); };
        reg["window"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Window>(); };
        reg["scrollview"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ScrollView>(); };
        reg["textinput"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TextInput>(); };
        reg["column"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Column>(); };
        reg["row"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Row>(); };
        reg["group"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Group>(); };
        reg["checkbox"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Checkbox>(); };
        reg["separator"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Separator>(); };
        reg["radiobutton"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<RadioButton>(); };
        reg["progress"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ProgressBar>(); };
        reg["slider"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Slider>(); };
        reg["tabs"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Tabs>(); };
        reg["tab"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Tab>(); };
        reg["combobox"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ComboBox>(); };
        reg["option"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ComboOption>(); };
        reg["toggleswitch"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ToggleSwitch>(); };
        reg["toggle"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ToggleSwitch>(); };
        reg["textarea"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TextArea>(); };
        reg["listbox"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ListBox>(); };
        reg["virtuallist"] = reg["listbox"];   // same virtualised row engine
        reg["propertygrid"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<PropertyGrid>(); };
        reg["property"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<PropertyItem>(); };
        reg["console"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Console>(); };
        reg["item"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ListItem>(); };
        reg["tree"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TreeView>(); };
        reg["table"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Table>(); };
        reg["th"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TableColumn>(); };
        reg["tr"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TableRow>(); };
        reg["cell"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TableCell>(); };
        reg["treeitem"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TreeItem>(); };
        reg["spinbox"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<SpinBox>(); };
        reg["optionspin"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<OptionSpin>(); };
        reg["rangeslider"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<RangeSlider>(); };
        reg["splitter"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Splitter>(); };
        reg["grid"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Grid>(); };
        reg["badge"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Badge>(); };
        reg["spinner"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Spinner>(); };
        reg["searchinput"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<SearchInput>(); };
        reg["accordion"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Accordion>(); };
        reg["section"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<AccordionSection>(); };
        reg["modal"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Modal>(); };
        reg["sparkline"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Sparkline>(); };
        reg["menubar"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<MenuBar>(); };
        reg["menu"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Menu>(); };
        reg["menuitem"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<MenuItem>(); };
        reg["menuseparator"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { auto m = std::make_shared<MenuItem>(); m->setSeparator(true); return m; };
        reg["contextmenu"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ContextMenu>(); };
        reg["colorpicker"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ColorPicker>(); };
        reg["pathpicker"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<PathPicker>(); };
        reg["rampeditor"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<RampEditor>(); };
        reg["ramp"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<RampEditor>(); };
        reg["curveeditor"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<CurveEditor>(); };
        reg["curve"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<CurveEditor>(); };
    }
    return reg;
}

// ------------------------------------------------------------------
// Badge
// ------------------------------------------------------------------
Badge::Badge() = default;
Badge::~Badge() = default;

Rect Badge::removeRect() const {
    if (!m_removable) return Rect{};
    const Rect b = bounds();
    return Rect{b.x + b.w - 16.0f, b.y + b.h * 0.5f - 6.0f, 12.0f, 12.0f};
}

void Badge::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    // Bounds (and removeRect) live in the parent's space: lift the local
    // pointer back into it before comparing.
    const Offset2D l{static_cast<float>(p.x) - o.x + b.x,
                     static_cast<float>(p.y) - o.y + b.y};
    const Rect r = removeRect();
    if (r.w > 0.0f && l.x >= r.x && l.x <= r.x + r.w &&
        l.y >= r.y && l.y <= r.y + r.h)
        m_removeHot = true;
}

void Badge::onPointerUp(MouseButton) {
    if (!m_removeHot) return;
    m_removeHot = false;
    setVisible(false);   // dynamic tag removal
}

void Badge::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    r.drawRoundedRect(b, m_bgColor, b.h * 0.5f);
    r.drawRectBorder(b, m_borderColor, 1.0f, b.h * 0.5f);

    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    const float midY = b.y + b.h * 0.5f;
    float x = b.x + 8.0f;
    r.drawText(m_text.c_str(), x, midY, m_fontSize, "sans", m_fgColor,
               Align::Left, VertAlign::Middle);
    x += r.textWidth(m_text.c_str(), *f) + 4.0f;
    if (m_count >= 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "(%d)", m_count);
        Color dim = m_fgColor;
        dim.a = static_cast<uint8_t>(dim.a > 60 ? dim.a - 60 : 0);
        r.drawText(buf, x, midY, m_fontSize, "sans", dim,
                   Align::Left, VertAlign::Middle);
    }
    if (m_removable) {
        const Rect q = removeRect();
        const Color col = m_removeHot ? palette().danger : m_fgColor;
        r.drawLine({q.x + 2.0f, q.y + 2.0f},
                   {q.x + q.w - 2.0f, q.y + q.h - 2.0f}, col, 1.5f);
        r.drawLine({q.x + q.w - 2.0f, q.y + 2.0f},
                   {q.x + 2.0f, q.y + q.h - 2.0f}, col, 1.5f);
    }
}

// ------------------------------------------------------------------
// Spinner
// ------------------------------------------------------------------
Spinner::Spinner() = default;
Spinner::~Spinner() = default;

void Spinner::setVariant(const std::string& v) {
    m_blocks = (v == "blocks");
}

void Spinner::onTick(float dt) {
    Widget::onTick(dt);
    m_phase += dt * m_speed;
}

void Spinner::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    const Rect b = bounds();
    const float midY = b.y + b.h * 0.5f;

    static const char* kGlyphs[4] = {"|", "/", "-", "\\"};
    std::string s = m_text;
    if (!s.empty()) s += " ";
    if (!m_blocks) {
        const int idx = static_cast<int>(m_phase) & 3;
        s += kGlyphs[idx];
    } else {
        constexpr int kCells = 10;
        constexpr int kRun = 3;
        const int head = static_cast<int>(m_phase) % kCells;
        s += '[';
        for (int i = 0; i < kCells; ++i) {
            const int d = (i - head + kCells) % kCells;
            s += d < kRun ? '#' : '.';
        }
        s += ']';
    }
    r.drawText(s.c_str(), b.x + 4.0f, midY, m_fontSize, "sans", m_fgColor,
               Align::Left, VertAlign::Middle);
}

// ------------------------------------------------------------------
// Sparkline
// ------------------------------------------------------------------
Sparkline::Sparkline() = default;
Sparkline::~Sparkline() = default;

void Sparkline::setValues(const std::string& csv) {
    m_values.clear();
    std::string tok;
    auto flush = [&]() {
        if (!tok.empty()) {
            char* end = nullptr;
            const float v = std::strtof(tok.c_str(), &end);
            if (end != tok.c_str()) m_values.push_back(v);
        }
        tok.clear();
    };
    for (char c : csv) {
        if (c == ';' || c == ',' || c == ' ') flush();
        else tok.push_back(c);
    }
    flush();
    while (m_values.size() > m_max)
        m_values.erase(m_values.begin());
}

void Sparkline::addSample(float v) {
    m_values.push_back(v);
    while (m_values.size() > m_max)
        m_values.erase(m_values.begin());
}

void Sparkline::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);
    r.drawRectBorder(b, palette().border, 1.0f, 0.0f);
    if (m_values.size() < 2) return;

    float lo = m_values[0], hi = m_values[0];
    for (float v : m_values) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    const float span = hi - lo > 0.001f ? hi - lo : 1.0f;
    const float dx = b.w / static_cast<float>(m_values.size() - 1);
    auto yOf = [&](float v) {
        return b.y + b.h - 2.0f - (v - lo) / span * (b.h - 4.0f);
    };
    for (size_t i = 1; i < m_values.size(); ++i) {
        r.drawLine({b.x + static_cast<float>(i - 1) * dx, yOf(m_values[i - 1])},
                   {b.x + static_cast<float>(i) * dx, yOf(m_values[i])},
                   m_lineColor, 1.5f);
    }

    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    std::string info = m_label;
    if (!info.empty()) info += " ";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", m_values.back());
    info += buf;
    Color ink = m_lineColor;
    r.drawText(info.c_str(), b.x + 6.0f, b.y + 6.0f, m_fontSize, "sans", ink,
               Align::Left, VertAlign::Top);
}

} // namespace ogui
