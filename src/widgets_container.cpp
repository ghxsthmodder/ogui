#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// ScrollView
// ------------------------------------------------------------------
ScrollView::ScrollView() = default;
ScrollView::~ScrollView() = default;

void ScrollView::setContent(std::shared_ptr<Widget> content) {
    if (m_content == content) return;
    if (m_content) removeChild(m_content);
    m_content = std::move(content);
    if (m_content) addChild(m_content);   // setParent() skips duplicates
}

void ScrollView::setScroll(float x, float y) {
    m_offset = Offset2D{x, y};
    clampScroll();
}

void ScrollView::scrollBy(float dx, float dy) {
    m_offset.x += dx;
    m_offset.y += dy;
    clampScroll();
}

void ScrollView::onPointerScroll(float dx, float dy) {
    // Positive wheel (away from the user) brings the content back down.
    scrollBy(dx * m_step, dy * m_step);
}

void ScrollView::clampScroll() {
    // No content yet (parse time, before the children exist, or an empty
    // view): nothing to clamp against -- keep the requested position so the
    // first layout pass can act on it.
    if (m_contentBounds.w <= 0.0f && m_contentBounds.h <= 0.0f) return;
    const float minX = std::min(0.0f, m_viewport.w - (m_contentBounds.x + m_contentBounds.w));
    const float minY = std::min(0.0f, m_viewport.h - (m_contentBounds.y + m_contentBounds.h));
    m_offset.x = std::clamp(m_offset.x, minX, 0.0f);
    m_offset.y = std::clamp(m_offset.y, minY, 0.0f);
}

void ScrollView::layout(float parentWidth, float parentHeight) {
    const Rect b = bounds();
    m_viewport = {0.0f, 0.0f,
                  b.w > 0.0f ? b.w : parentWidth,
                  b.h > 0.0f ? b.h : parentHeight};

    // Children keep their authored bounds; they only lay themselves out
    // (a <column> page inside, ...).
    for (const auto& c : m_children) {
        if (c) {
            c->resolveGeometry(m_viewport.w, m_viewport.h);
            c->layout(m_viewport.w, m_viewport.h);
        }
    }

    // Scrollable area = bounding box of the visible children.
    Rect u{0.0f, 0.0f, 0.0f, 0.0f};
    bool any = false;
    for (const auto& c : m_children) {
        if (!c || !c->isVisible()) continue;
        const Rect cb = c->bounds();
        if (!any) { u = cb; any = true; continue; }
        const float x0 = std::min(u.x, cb.x);
        const float y0 = std::min(u.y, cb.y);
        const float x1 = std::max(u.x + u.w, cb.x + cb.w);
        const float y1 = std::max(u.y + u.h, cb.y + cb.h);
        u = Rect{x0, y0, x1 - x0, y1 - y0};
    }
    m_contentBounds = u;
    clampScroll();
}

bool ScrollView::clipsChildren(Rect& outRect) const {
    outRect = m_viewport;   // {0,0,w,h} in this widget's space
    return true;
}

void ScrollView::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRect(b, m_bgColor);
    if (m_borderWidth > 0.0f) r.drawRectBorder(b, m_borderColor, m_borderWidth, 4.0f);
    // Children are drawn by the tree walk: offset by the scroll delta and
    // clipped to the viewport (childOffset()/clipsChildren()).
}

// ------------------------------------------------------------------
// Column
// ------------------------------------------------------------------
Column::Column() = default;
Column::~Column() = default;

void Column::layout(float parentWidth, float parentHeight) {
    float pad = m_style.padding;
    float gap = m_style.margin > 0.0f ? m_style.margin : 4.0f;
    float y = pad;
    float maxW = 0.0f;

    for (const auto& c : children()) {
        if (!c) continue;
        // Percent/fill children resolve against the column's content width.
        c->resolveGeometry(parentWidth - pad * 2.0f, parentHeight);
        Rect cb = c->bounds();
        // Stack vertically; keep an explicitly authored x (indentation) and
        // give zero-width children the full available width.
        float cx = cb.x;
        float cw = cb.w > 0.0f ? cb.w : (parentWidth - pad * 2.0f);
        float ch = cb.h > 0.0f ? cb.h : 24.0f;
        if (cb.h <= 0.0f) {
            // A wrapped label with no authored height sizes to its text.
            if (auto* lab = dynamic_cast<Label*>(c.get()); lab && lab->wrap()) {
                const float need = lab->measureHeight(cw);
                if (need > 0.0f) ch = need;
            }
        }
        c->setBounds(Rect{cx, y, cw, ch});
        c->layout(cw, ch);

        Rect after = c->bounds();
        y += after.h + gap;
        maxW = std::max(maxW, cx + after.w);
    }

    float totalH = y > pad ? y - gap : pad;
    m_bounds = Rect{m_bounds.x, m_bounds.y,
                    std::max(maxW + pad, parentWidth),
                    std::max(totalH, parentHeight)};
    (void)parentHeight;
}

// ------------------------------------------------------------------
// Row
// ------------------------------------------------------------------
Row::Row() = default;
Row::~Row() = default;

void Row::layout(float parentWidth, float parentHeight) {
    // Horizontal analogue of Column: keep each child's authored size and
    // tile them left to right with the style gap.
    float pad = m_style.padding;
    float gap = m_style.margin > 0.0f ? m_style.margin : 4.0f;
    float x = pad;
    float maxH = 0.0f;
    for (const auto& c : children()) {
        if (!c) continue;
        // Percent/fill children resolve against the row's content area.
        c->resolveGeometry(parentWidth, parentHeight);
        Rect cb = c->bounds();
        const float cw = cb.w > 0.0f ? cb.w : 100.0f;
        const float ch = cb.h > 0.0f ? cb.h : parentHeight;
        c->setBounds(Rect{x, cb.y, cw, ch});
        c->layout(cw, ch);
        Rect after = c->bounds();
        x += after.w + gap;
        maxH = std::max(maxH, after.h);
    }
    m_bounds = Rect{m_bounds.x, m_bounds.y,
                    x > pad ? x - gap : pad,
                    std::max(maxH, 0.0f)};
    (void)parentWidth;
}

// ------------------------------------------------------------------
// Grid
// ------------------------------------------------------------------
Grid::Grid() = default;
Grid::~Grid() = default;

void Grid::setCols(const std::string& spec) {
    m_tracks.clear();
    std::vector<std::string> toks;
    std::string tok;
    auto flush = [&]() {
        if (!tok.empty()) toks.push_back(tok);
        tok.clear();
    };
    for (char c : spec) {
        if (c == ',' || c == ' ' || c == ';' || c == '|') flush();
        else if (c >= 'A' && c <= 'Z') tok.push_back(char(c - 'A' + 'a'));
        else tok.push_back(c);
    }
    flush();
    if (toks.empty()) return;

    auto parseTrack = [](const std::string& t) {
        Track tr;
        if (!t.empty() && t.back() == '%') {
            tr.kind = Track::Percent;
            tr.value = std::strtof(t.substr(0, t.size() - 1).c_str(), nullptr);
        } else if (t.size() > 2 && t.back() == 'r' && t[t.size() - 2] == 'f') {
            tr.kind = Track::Fr;
            tr.value = std::strtof(t.substr(0, t.size() - 2).c_str(), nullptr);
            if (tr.value <= 0.0f) tr.value = 1.0f;
        } else if (t == "fr") {
            tr.kind = Track::Fr;
            tr.value = 1.0f;
        } else {
            tr.kind = Track::Px;
            tr.value = std::strtof(t.c_str(), nullptr);
        }
        return tr;
    };

    // "3" (one bare integer) means three equal fractional tracks.
    if (toks.size() == 1 && toks[0].find_first_not_of("0123456789") == std::string::npos) {
        const int n = std::atoi(toks[0].c_str());
        for (int i = 0; i < n; ++i) m_tracks.push_back(Track{});
        return;
    }
    for (const auto& t : toks) m_tracks.push_back(parseTrack(t));
}

void Grid::layout(float parentWidth, float parentHeight) {
    const Rect b = bounds();
    const float pw = b.w > 0.0f ? b.w : parentWidth;
    const float ph = b.h > 0.0f ? b.h : parentHeight;

    if (m_tracks.empty()) { Widget::layout(pw, ph); return; }

    const float pad = m_style.padding;
    const float cw = std::max(pw - 2.0f * pad, 0.0f);
    const size_t nCols = m_tracks.size();

    // Track widths: fixed (px/%) first, fr shares the leftover.
    std::vector<float> trackW(nCols, 0.0f);
    float fixedSum = 0.0f, frSum = 0.0f;
    size_t frCount = 0;
    for (size_t t = 0; t < nCols; ++t) {
        const Track& tr = m_tracks[t];
        if (tr.kind == Track::Px) trackW[t] = tr.value;
        else if (tr.kind == Track::Percent) trackW[t] = cw * tr.value * 0.01f;
        if (tr.kind != Track::Fr) {
            fixedSum += trackW[t];
        } else {
            frSum += tr.value;
            ++frCount;
        }
    }
    const float gapTotal = nCols > 1 ? static_cast<float>(nCols - 1) * m_gap : 0.0f;
    float leftover = std::max(cw - gapTotal - fixedSum, 0.0f);
    for (size_t t = 0; t < nCols; ++t) {
        if (m_tracks[t].kind != Track::Fr) continue;
        trackW[t] = frSum > 0.0f ? leftover * m_tracks[t].value / frSum
                                 : leftover / static_cast<float>(frCount);
    }

    const auto& kids = children();
    float y = pad;
    size_t i = 0;
    while (i < kids.size()) {
        const size_t rowEnd = std::min(i + nCols, kids.size());
        const size_t rowCount = rowEnd - i;

        // Resolve this row's children against their cell size first, so an
        // auto row height sees the final widths.
        float autoH = 0.0f;
        for (size_t j = 0; j < rowCount; ++j) {
            const auto& c = kids[i + j];
            if (!c) continue;
            const size_t col = (i + j) % nCols;
            c->resolveGeometry(trackW[col], ph);
            autoH = std::max(autoH, c->bounds().h);
        }
        const float rowH = m_rowH > 0.0f ? m_rowH : autoH;

        float x = pad;
        for (size_t j = 0; j < rowCount; ++j) {
            const auto& c = kids[i + j];
            if (c) {
                Rect cb = c->bounds();
                if (cb.h <= 0.0f && m_rowH > 0.0f) cb.h = m_rowH;
                c->setBounds(Rect{x, y, cb.w, cb.h});
                c->layout(cb.w, cb.h);
            }
            x += trackW[j] + m_gap;
        }
        y += std::max(rowH, 0.0f) + m_gap;
        i = rowEnd;
    }
}

// ------------------------------------------------------------------
// Group
// ------------------------------------------------------------------
Group::Group() = default;
Group::~Group() = default;

void Group::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, 4.0f);
    }
    if (m_bgColor.a > 0) {
        r.drawRect(b, m_bgColor);
    }
}

// ------------------------------------------------------------------
// Splitter
// ------------------------------------------------------------------
Splitter::Splitter() = default;
Splitter::~Splitter() = default;

void Splitter::setSplit(float frac) {
    m_split = std::clamp(frac, 0.0f, 1.0f);
}

float Splitter::splitPos() const {
    const Rect b = bounds();
    const float total = m_vertical ? b.w : b.h;
    return std::clamp(total * m_split, 0.0f, std::max(total - m_handle, 0.0f));
}

void Splitter::layout(float parentWidth, float parentHeight) {
    Rect b = bounds();
    if (b.w <= 0.0f) b.w = parentWidth;
    if (b.h <= 0.0f) b.h = parentHeight;

    const auto& kids = children();
    if (kids.size() < 2) {
        // Fewer than two panes: just let each child fill the box.
        for (const auto& c : kids) if (c) { c->setBounds(b); c->layout(b.w, b.h); }
        return;
    }
    const float first = splitPos();
    const float total = m_vertical ? b.w : b.h;
    const float rest = std::max(total - first - m_handle, 0.0f);

    Widget* a = kids[0].get();
    Widget* c = kids[1].get();
    // Children live in the splitter's own space: origin at (0, 0), not b.x/b.y.
    if (m_vertical) {
        a->setBounds(Rect{0.0f, 0.0f, first, b.h});
        c->setBounds(Rect{first + m_handle, 0.0f, rest, b.h});
    } else {
        a->setBounds(Rect{0.0f, 0.0f, b.w, first});
        c->setBounds(Rect{0.0f, first + m_handle, b.w, rest});
    }
    a->layout(a->bounds().w, a->bounds().h);
    c->layout(c->bounds().w, c->bounds().h);
}

void Splitter::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    const float along = m_vertical ? lx : ly;
    // Grab anywhere in (or next to) the divider band [pos, pos + m_handle].
    const float pos = splitPos();
    if (along >= pos - m_handle * 0.5f && along <= pos + m_handle * 1.5f)
        m_dragging = true;
}

void Splitter::onPointerUp(MouseButton) {
    m_dragging = false;
}

void Splitter::onPointerMove(const Offset2D& p) {
    if (!m_dragging) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const float total = m_vertical ? b.w : b.h;
    if (total <= 0.0f) return;
    const float along = m_vertical ? (static_cast<float>(p.x) - o.x)
                                   : (static_cast<float>(p.y) - o.y);
    m_split = std::clamp((along - m_handle * 0.5f) / total, 0.0f, 1.0f);
}

void Splitter::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    const float pos = splitPos();
    if (m_handleColor.a > 0) {
        const Rect handle = m_vertical
            ? Rect{b.x + pos, b.y, m_handle, b.h}
            : Rect{b.x, b.y + pos, b.w, m_handle};
        r.drawRect(handle, m_handleColor);
    }
}

// ------------------------------------------------------------------
// Accordion
// ------------------------------------------------------------------
AccordionSection::AccordionSection() = default;
AccordionSection::~AccordionSection() = default;

void AccordionSection::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    if (static_cast<float>(p.y) - o.y > m_headerH) return;   // content click
    const bool next = !m_expanded;
    Widget* par = parent();
    if (auto* acc = dynamic_cast<Accordion*>(par)) {
        if (acc->exclusive() && next)
            for (const auto& c : acc->children())
                if (auto* s = dynamic_cast<AccordionSection*>(c.get()))
                    s->m_expanded = (s == this);
    }
    m_expanded = next;
}

void AccordionSection::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    const Rect head{b.x, b.y, b.w, m_headerH};
    r.drawRect(head, m_headerColor);
    r.drawRectBorder(head, palette().border, 1.0f, 0.0f);

    // ▶ / ▼ glyph as a filled triangle.
    const float cy = head.y + m_headerH * 0.5f;
    const float cx = head.x + 12.0f;
    if (m_expanded) {
        r.drawTriangle({cx - 4.0f, cy - 3.0f}, {cx + 4.0f, cy - 3.0f},
                       {cx, cy + 4.0f}, m_fgColor);
    } else {
        r.drawTriangle({cx - 3.0f, cy - 4.0f}, {cx - 3.0f, cy + 4.0f},
                       {cx + 4.0f, cy}, m_fgColor);
    }

    const Font* f = r.resolveFont("sans", m_fontSize);
    if (f) {
        r.drawTextShifted(m_header.c_str(), cx + 10.0f,
                          cy - static_cast<float>(m_fontSize) * 0.5f - 1.0f,
                          m_fontSize, *f, m_fgColor);
    }
}

Accordion::Accordion() = default;
Accordion::~Accordion() = default;

void Accordion::layout(float parentWidth, float parentHeight) {
    const Rect b = bounds();
    const float w = b.w > 0.0f ? b.w : parentWidth;
    // Children keep parent-relative bounds: stack from our own origin.
    float y = 0.0f;
    for (const auto& c : children()) {
        auto* sec = dynamic_cast<AccordionSection*>(c.get());
        if (!sec) continue;
        sec->resolveGeometry(w, parentHeight);
        Rect sb = sec->bounds();
        const float sw = sb.w > 0.0f ? sb.w : w;

        float cy = sec->m_headerH;
        for (const auto& gc : sec->children()) {
            if (!gc) continue;
            gc->resolveGeometry(sw, parentHeight);
            Rect cb = gc->bounds();
            if (sec->m_expanded) {
                gc->setVisible(true);
                gc->setBounds(Rect{m_gap, cy, cb.w, cb.h});
                gc->layout(sw - 2.0f * m_gap, cb.h);
                cy += cb.h + m_gap;
            } else {
                // Hidden content must not catch hits either.
                gc->setVisible(false);
            }
        }
        sec->m_contentH = cy - sec->m_headerH;
        sec->setBounds(Rect{0.0f, y, sw,
                            sec->m_expanded ? cy : sec->m_headerH});
        sec->layout(sw, sec->bounds().h);
        y += sec->bounds().h + m_gap;
    }
}

// ------------------------------------------------------------------
// Modal
// ------------------------------------------------------------------
Modal::Modal() = default;
Modal::~Modal() = default;

void Modal::setOpen(bool o) {
    m_open = o;
    setVisible(o);
}

Rect Modal::panelRect() const {
    const Rect b = bounds();
    float x = b.x + (b.w - m_panelW) * 0.5f;
    float y = b.y + (b.h - m_panelH) * 0.5f;
    if (x < b.x) x = b.x;
    if (y < b.y) y = b.y;
    return Rect{x, y,
                std::min(m_panelW, b.w), std::min(m_panelH, b.h)};
}

void Modal::layout(float parentWidth, float parentHeight) {
    Rect fb = bounds();
    const float pw = fb.w > 0.0f ? fb.w : parentWidth;
    const float ph = fb.h > 0.0f ? fb.h : parentHeight;
    fb = Rect{fb.x, fb.y, pw, ph};
    setBounds(fb);
    const Rect pr = panelRect();
    // Children are relative to our origin, so express the panel in that space.
    const float px = pr.x - fb.x, py = pr.y - fb.y;
    const auto& kids = children();
    if (!m_childOffCaptured || m_childOff.size() != kids.size()) {
        m_childOff.clear();
        for (const auto& c : kids) {
            Rect cb = c ? c->bounds() : Rect{};
            m_childOff.push_back(Offset2D{cb.x, cb.y});
        }
        m_childOffCaptured = true;
    }
    size_t i = 0;
    for (const auto& c : kids) {
        if (!c) { ++i; continue; }
        c->resolveGeometry(pr.w, pr.h);
        Rect cb = c->bounds();
        const Offset2D off = i < m_childOff.size() ? m_childOff[i]
                                                   : Offset2D{cb.x, cb.y};
        c->setBounds(Rect{px + off.x, py + off.y, cb.w, cb.h});
        c->layout(cb.w, cb.h);
        ++i;
    }
}

void Modal::onPointerDown(MouseButton, const Offset2D&) {
    // Consume the press so widgets below the backdrop never see it.
}

void Modal::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    Color dim = palette().overlay;
    dim.a = 140;
    r.drawRect(b, dim);
    const Rect pr = panelRect();
    r.drawDropShadow(pr, 6.0f, palette().shadow, 8.0f, 0.0f, 3.0f);
    r.drawRoundedRect(pr, palette().surface, 6.0f);
    r.drawRectBorder(pr, palette().borderStrong, 1.0f, 6.0f);
    if (!m_title.empty()) {
        const Font* f = r.resolveFont("sans", 14);
        if (f) {
            r.drawTextShifted(m_title.c_str(), pr.x + 12.0f, pr.y + 8.0f,
                              14, *f, palette().textBright);
        }
    }
}

} // namespace ogui
