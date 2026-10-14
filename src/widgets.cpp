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

void Button::onKeyDown(KeyCode key, uint32_t) {
    if (key != Key_Space && key != Key_Enter && key != Key_KP_Enter) return;
    if (m_kernel) m_kernel->clickWidget(this, {}, EventSource::Keyboard);
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

// Factory helper: build an interactive widget and mark it focusable so the
// Kernel's Tab/Shift+Tab traversal stops on it (see Widget::isFocusable).
template <typename W>
std::shared_ptr<Widget> fw(const Style&, const std::map<std::string, std::string>&) {
    auto w = std::make_shared<W>();
    w->setFocusable(true);
    return w;
}

std::map<std::string, WidgetFactory>& widgetRegistry() {
    static std::map<std::string, WidgetFactory> reg;
    if (reg.empty()) {
        reg["screen"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Widget>(); };
        // Data-only root for a list of screens; children carry title/path in
        // their raw attributes (see Widget::rawAttributes).
        reg["manifest"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Widget>(); };
        reg["label"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Label>(); };
        reg["button"] = fw<Button>;
        reg["image"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Image>(); };
        reg["panel"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Panel>(); };
        reg["window"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Window>(); };
        reg["scrollview"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ScrollView>(); };
        reg["textinput"] = fw<TextInput>;
        reg["column"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Column>(); };
        reg["row"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Row>(); };
        reg["group"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Group>(); };
        reg["checkbox"] = fw<Checkbox>;
        reg["separator"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Separator>(); };
        reg["radiobutton"] = fw<RadioButton>;
        reg["progress"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ProgressBar>(); };
        reg["slider"] = fw<Slider>;
        reg["tabs"] = fw<Tabs>;
        reg["tab"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Tab>(); };
        reg["combobox"] = fw<ComboBox>;
        reg["option"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ComboOption>(); };
        reg["toggleswitch"] = fw<ToggleSwitch>;
        reg["toggle"] = fw<ToggleSwitch>;
        reg["textarea"] = fw<TextArea>;
        reg["listbox"] = fw<ListBox>;
        reg["virtuallist"] = fw<ListBox>;   // same virtualised row engine
        reg["propertygrid"] = fw<PropertyGrid>;
        reg["property"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<PropertyItem>(); };
        reg["console"] = fw<Console>;
        reg["item"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ListItem>(); };
        reg["tree"] = fw<TreeView>;
        reg["treetable"] = fw<TreeTable>;
        reg["tnode"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TreeTableNode>(); };
        reg["statusbar"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<StatusBar>(); };
        reg["breadcrumb"] = fw<Breadcrumb>;
        reg["crumb"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<CrumbItem>(); };
        reg["keybind"] = fw<Keybind>;
        reg["commandpalette"] = fw<CommandPalette>;
        reg["cmd"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<CommandAction>(); };
        reg["table"] = fw<Table>;
        reg["th"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TableColumn>(); };
        reg["tr"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TableRow>(); };
        reg["cell"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TableCell>(); };
        reg["treeitem"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<TreeItem>(); };
        reg["spinbox"] = fw<SpinBox>;
        reg["optionspin"] = fw<OptionSpin>;
        reg["rangeslider"] = fw<RangeSlider>;
        reg["splitter"] = fw<Splitter>;
        reg["grid"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Grid>(); };
        reg["badge"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Badge>(); };
        reg["spinner"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Spinner>(); };
        reg["searchinput"] = fw<SearchInput>;
        reg["accordion"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Accordion>(); };
        reg["section"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<AccordionSection>(); };
        reg["modal"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Modal>(); };
        reg["sparkline"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<Sparkline>(); };
        reg["menubar"] = fw<MenuBar>;
        reg["menu"] = fw<Menu>;
        reg["menuitem"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<MenuItem>(); };
        reg["menuseparator"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { auto m = std::make_shared<MenuItem>(); m->setSeparator(true); return m; };
        reg["contextmenu"] = [](const Style&, const std::map<std::string, std::string>&) -> std::shared_ptr<Widget> { return std::make_shared<ContextMenu>(); };
        reg["colorpicker"] = fw<ColorPicker>;
        reg["pathpicker"] = fw<PathPicker>;
        reg["rampeditor"] = fw<RampEditor>;
        reg["ramp"] = fw<RampEditor>;
        reg["curveeditor"] = fw<CurveEditor>;
        reg["curve"] = fw<CurveEditor>;
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

// ------------------------------------------------------------------
// StatusBar
// ------------------------------------------------------------------
StatusBar::StatusBar() = default;
StatusBar::~StatusBar() = default;

void StatusBar::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);
    // Hairline separator along the top edge.
    r.drawLine({b.x, b.y + 0.5f}, {b.x + b.w, b.y + 0.5f},
               ogui::palette().border, 1.0f);
    const float midY = b.y + b.h * 0.5f;
    r.drawText(m_left.c_str(),   b.x + 10.0f, midY, m_fontSize, "sans",
               m_textColor, Align::Left,   VertAlign::Middle);
    if (!m_center.empty())
        r.drawText(m_center.c_str(), b.x + b.w * 0.5f, midY, m_fontSize, "sans",
                   m_textColor, Align::Center, VertAlign::Middle);
    if (!m_right.empty())
        r.drawText(m_right.c_str(), b.x + b.w - 10.0f, midY, m_fontSize, "sans",
                   m_dimColor, Align::Right, VertAlign::Middle);
}

// ------------------------------------------------------------------
// Breadcrumb
// ------------------------------------------------------------------
Breadcrumb::Breadcrumb() = default;
Breadcrumb::~Breadcrumb() = default;

void Breadcrumb::collect() {
    if (m_collected) return;
    m_collected = true;
    if (!m_segs.empty()) return;         // authored via path=/setSegments
    std::vector<std::string> segs;
    for (const auto& c : children())
        if (auto* cr = dynamic_cast<CrumbItem*>(c.get()))
            segs.push_back(cr->label());
    if (!segs.empty()) m_segs = std::move(segs);
}

void Breadcrumb::setPath(const std::string& path) {
    m_segs.clear();
    size_t start = 0;
    while (start <= path.size()) {
        const size_t slash = path.find('/', start);
        if (slash == std::string::npos) {
            if (start < path.size()) m_segs.push_back(path.substr(start));
            break;
        }
        m_segs.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    m_collected = true;
    measureSegs();
    if (m_kernel) m_kernel->invalidate();
}

void Breadcrumb::setSegments(const std::vector<std::string>& segs) {
    m_segs = segs;
    m_collected = true;
    measureSegs();
    if (m_kernel) m_kernel->invalidate();
}

void Breadcrumb::setCurrent(int i) {
    if (i < -1 || i >= static_cast<int>(m_segs.size())) i = -1;
    m_current = i;
    if (m_kernel) m_kernel->invalidate();
}

void Breadcrumb::layout(float parentWidth, float parentHeight) {
    collect();
    Widget::layout(parentWidth, parentHeight);
    measureSegs();
}

// Segment rects must exist before the first draw (clicks can arrive in any
// frame), so measure here instead of recording them in onDraw. Falls back to
// an average-glyph estimate when no font resource is available (headless).
void Breadcrumb::measureSegs() const {
    m_segRects.clear();
    const Rect b = bounds();
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    const bool haveFont = f && f->loaded();
    const auto segW = [&](const std::string& s) {
        return haveFont ? f->measure(s.c_str())
                        : static_cast<float>(s.size()) * m_fontSize * 0.55f;
    };
    float x = b.x;
    for (size_t i = 0; i < m_segs.size(); ++i) {
        const float w = segW(m_segs[i]);
        m_segRects.push_back({x, b.y, w, b.h});
        x += w;
        if (i + 1 < m_segs.size()) x += segW(" " + m_sep + " ");
    }
}

void Breadcrumb::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    collect();
    measureSegs();
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const Offset2D l{static_cast<float>(p.x) - o.x + b.x,
                     static_cast<float>(p.y) - o.y + b.y};
    for (size_t i = 0; i < m_segRects.size(); ++i) {
        const Rect& s = m_segRects[i];
        if (l.x >= s.x && l.x <= s.x + s.w && l.y >= s.y && l.y <= s.y + s.h) {
            m_current = static_cast<int>(i);
            if (m_onSelected) m_onSelected(m_current);
            announceChange();
            if (m_kernel) m_kernel->invalidate();
            return;
        }
    }
}

void Breadcrumb::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    if (m_segs.empty()) return;
    measureSegs();
    const int cur = m_current < 0 ? static_cast<int>(m_segs.size()) - 1
                                  : m_current;
    const float midY = b.y + b.h * 0.5f;
    for (size_t i = 0; i < m_segs.size(); ++i) {
        const Color col = static_cast<int>(i) == cur ? m_textColor : m_dimColor;
        r.drawText(m_segs[i].c_str(), m_segRects[i].x, midY, m_fontSize, "sans",
                   col, Align::Left, VertAlign::Middle);
        if (i + 1 < m_segs.size()) {
            const std::string sep = " " + m_sep + " ";
            r.drawText(sep.c_str(), m_segRects[i].x + m_segRects[i].w, midY,
                       m_fontSize, "sans", m_accentColor, Align::Left,
                       VertAlign::Middle);
        }
    }
}

// ------------------------------------------------------------------
// Keybind
// ------------------------------------------------------------------
Keybind::Keybind() = default;
Keybind::~Keybind() = default;

namespace {

struct KeyName { KeyCode key; const char* name; };
const KeyName kKeyNames[] = {
    {Key_Space, "space"}, {Key_Backspace, "backspace"}, {Key_Enter, "enter"},
    {Key_Tab, "tab"}, {Key_Escape, "escape"}, {Key_Insert, "insert"},
    {Key_Delete, "delete"}, {Key_Home, "home"}, {Key_End, "end"},
    {Key_PageUp, "pageup"}, {Key_PageDown, "pagedown"},
    {Key_Left, "left"}, {Key_Up, "up"}, {Key_Right, "right"}, {Key_Down, "down"},
    {Key_Minus, "minus"}, {Key_Equal, "equal"}, {Key_Semicolon, "semicolon"},
    {Key_Slash, "slash"}, {Key_Comma, "comma"}, {Key_Period, "period"},
    {Key_Apostrophe, "quote"}, {Key_BracketLeft, "leftbracket"},
    {Key_BracketRight, "rightbracket"}, {Key_Backslash, "backslash"},
    {Key_CapsLock, "capslock"}, {Key_NumLock, "numlock"}, {Key_ScrollLock, "scrolllock"},
    {Key_F1, "f1"}, {Key_F2, "f2"}, {Key_F3, "f3"}, {Key_F4, "f4"},
    {Key_F5, "f5"}, {Key_F6, "f6"}, {Key_F7, "f7"}, {Key_F8, "f8"},
    {Key_F9, "f9"}, {Key_F10, "f10"}, {Key_F11, "f11"}, {Key_F12, "f12"},
};

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string Keybind::format(KeyCode key, uint32_t mods) {
    std::string out;
    if (mods & Mod_Ctrl)  out += "ctrl+";
    if (mods & Mod_Alt)   out += "alt+";
    if (mods & Mod_Shift) out += "shift+";
    if (mods & Mod_Super) out += "super+";
    std::string k;
    if (key >= Key_A && key <= Key_Z)
        k = std::string(1, static_cast<char>('a' + (key - Key_A)));
    else if (key >= Key_0 && key <= Key_9)
        k = std::string(1, static_cast<char>('0' + (key - Key_0)));
    else {
        for (const auto& e : kKeyNames)
            if (e.key == key) { k = e.name; break; }
    }
    if (k.empty()) { out.clear(); return out; }
    return out + k;
}

bool Keybind::parse(const std::string& combo, KeyCode& key, uint32_t& mods) {
    key = Key_None;
    mods = Mod_None;
    if (combo.empty()) return true;              // empty == cleared
    bool gotKey = false;
    size_t start = 0;
    while (start <= combo.size()) {
        size_t plus = combo.find('+', start);
        std::string tok = lower(combo.substr(
            start, plus == std::string::npos ? std::string::npos
                                             : plus - start));
        if (plus == std::string::npos) {
            if (start == combo.size()) break;    // trailing '+'
        }
        if (tok == "ctrl" || tok == "control")        mods |= Mod_Ctrl;
        else if (tok == "alt" || tok == "option")     mods |= Mod_Alt;
        else if (tok == "shift")                      mods |= Mod_Shift;
        else if (tok == "super" || tok == "meta" || tok == "cmd") mods |= Mod_Super;
        else if (tok.size() == 1 && tok[0] >= 'a' && tok[0] <= 'z') {
            key = static_cast<KeyCode>(Key_A + (tok[0] - 'a')); gotKey = true;
        } else if (tok.size() == 1 && tok[0] >= '0' && tok[0] <= '9') {
            key = static_cast<KeyCode>(Key_0 + (tok[0] - '0')); gotKey = true;
        } else {
            for (const auto& e : kKeyNames)
                if (tok == e.name) { key = e.key; gotKey = true; break; }
        }
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    return gotKey;
}

bool Keybind::setValue(const std::string& combo) {
    KeyCode k; uint32_t m;
    if (!parse(combo, k, m)) return false;
    m_key = k; m_mods = m;
    if (m_kernel) m_kernel->invalidate();
    return true;
}

std::string Keybind::value() const {
    if (m_key == Key_None && m_mods == Mod_None) return {};
    return format(m_key, m_mods);
}

void Keybind::onPointerDown(MouseButton, const Offset2D&) {
    m_capturing = true;
    if (m_kernel) m_kernel->invalidate();
}

void Keybind::onFocus(bool focused) {
    if (!focused) m_capturing = false;
}

void Keybind::onKeyDown(KeyCode key, uint32_t mods) {
    if (!m_capturing) { m_capturing = true; return; }
    const bool isModKey = key == Key_LCTRL || key == Key_RCTRL ||
                          key == Key_LALT || key == Key_RALT ||
                          key == Key_LSHIFT || key == Key_RSHIFT ||
                          key == Key_LGUI || key == Key_RGUI;
    if (isModKey) return;                       // wait for the real key
    if (key == Key_Escape) {
        m_capturing = false;
        if (m_kernel) m_kernel->invalidate();
        return;
    }
    if (key == Key_Backspace || key == Key_Delete) {
        m_key = Key_None; m_mods = Mod_None; m_capturing = false;
        if (m_onChanged) m_onChanged({});
        announceChange();
        if (m_kernel) m_kernel->invalidate();
        return;
    }
    m_key = key;
    m_mods = mods & (Mod_Ctrl | Mod_Alt | Mod_Shift | Mod_Super);
    // Strip the key itself when it is also a modifier (never is, but safe).
    m_capturing = false;
    if (m_onChanged) m_onChanged(value());
    announceChange();
    if (m_kernel) m_kernel->invalidate();
}

void Keybind::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const bool focused = isFocused() || m_capturing;
    r.drawRoundedRect(b, m_bgColor, m_radius);
    r.drawRectBorder(b, focused ? m_borderColor : ogui::palette().border,
                     1.0f, m_radius);
    const Font* f = r.resolveFont(m_fontFam, m_fontSize);
    if (!f) return;
    const std::string txt = m_capturing ? "…" : value();
    const Color col = m_capturing ? m_borderColor
                      : (txt.empty() ? ogui::palette().dim : m_textColor);
    r.drawText(txt.empty() ? m_placeholder.c_str() : txt.c_str(),
               b.x + b.w * 0.5f, b.y + b.h * 0.5f, m_fontSize, m_fontFam.c_str(),
               col, Align::Center, VertAlign::Middle);
}

} // namespace ogui
