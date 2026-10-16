#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// ScrollView
// ------------------------------------------------------------------
ScrollView::ScrollView() = default;
ScrollView::~ScrollView() = default;

ScrollableVerticalContainer::ScrollableVerticalContainer() { setFlow(Flow::Vertical); }
ScrollableVerticalContainer::~ScrollableVerticalContainer() = default;

ScrollableHorizontalContainer::ScrollableHorizontalContainer() {
    setFlow(Flow::Horizontal);
    setBar(Bar::Horizontal);
}
ScrollableHorizontalContainer::~ScrollableHorizontalContainer() = default;

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
    // Positive wheel (away from the user) brings the content back down. This is
    // the one path the wheel, the finger drag and the fling all arrive by, and
    // it hands the pair straight to scrollBy(): the axis a flow container
    // refuses to move is clamped away there, in clampScroll(), which is where
    // every other way of moving the content (setScroll, a bar drag) ends up too
    // -- one lock, one place, no way around it.
    scrollBy(dx * m_step, dy * m_step);
}

void ScrollView::clampScroll() {
    // No content yet (parse time, before the children exist, or an empty
    // view): nothing to clamp against -- keep the requested position so the
    // first layout pass can act on it.
    if (m_contentBounds.w <= 0.0f && m_contentBounds.h <= 0.0f) return;
    if (m_flow == Flow::Vertical) m_offset.x = 0.0f;
    else if (m_flow == Flow::Horizontal) m_offset.y = 0.0f;
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

    if (m_flow == Flow::Vertical) {
        flowVertical();
        clampScroll();
        return;
    }
    if (m_flow == Flow::Horizontal) {
        flowHorizontal();
        clampScroll();
        return;
    }

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

// A flow container positions its children itself -- stacked down (vertical) or
// tiled across (horizontal) from the padding edge, each keeping its authored
// cross-size -- and the content extent is the line they fill. The widget's own
// bounds stay the viewport; only m_contentBounds grows, which is what makes the
// overflow scrollable. The gap follows the style, matching Column/Row.
void ScrollView::flowVertical() {
    const float pad = m_style.padding;
    const float gap = m_style.gap > 0.0f ? m_style.gap
                  : (m_style.margin > 0.0f ? m_style.margin : 4.0f);
    const float contentW = std::max(m_viewport.w - pad * 2.0f, 0.0f);
    float y = pad;
    float maxW = 0.0f;
    for (const auto& c : m_children) {
        if (!c || !c->isVisible()) continue;
        c->resolveGeometry(contentW, m_viewport.h);
        const Rect cb = c->bounds();
        const float cw = cb.w > 0.0f ? cb.w : contentW;
        const float ch = cb.h > 0.0f ? cb.h : 24.0f;
        c->setBounds(Rect{cb.x, y, cw, ch});
        c->layout(cw, ch);
        const Rect after = c->bounds();
        y += after.h + gap;
        maxW = std::max(maxW, after.x + after.w);
    }
    const float totalH = y > pad ? y - gap : pad;
    m_contentBounds = Rect{0.0f, 0.0f,
                           std::max(maxW + pad, m_viewport.w), totalH};
}

void ScrollView::flowHorizontal() {
    const float pad = m_style.padding;
    const float gap = m_style.gap > 0.0f ? m_style.gap
                  : (m_style.margin > 0.0f ? m_style.margin : 4.0f);
    const float contentH = std::max(m_viewport.h - pad * 2.0f, 0.0f);
    float x = pad;
    float maxH = 0.0f;
    for (const auto& c : m_children) {
        if (!c || !c->isVisible()) continue;
        c->resolveGeometry(m_viewport.w, contentH);
        const Rect cb = c->bounds();
        const float cw = cb.w > 0.0f ? cb.w : 100.0f;
        const float ch = cb.h > 0.0f ? cb.h : contentH;
        c->setBounds(Rect{x, cb.y, cw, ch});
        c->layout(cw, ch);
        const Rect after = c->bounds();
        x += after.w + gap;
        maxH = std::max(maxH, after.y + after.h);
    }
    const float totalW = x > pad ? x - gap : pad;
    m_contentBounds = Rect{0.0f, 0.0f,
                           totalW, std::max(maxH + pad, m_viewport.h)};
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

// ---- overlay scrollbar ----
// A draggable bar on the trailing edge of the viewport: right edge for the
// vertical bar, bottom edge for the horizontal one. "Along" is the axis the bar
// runs on (y for vertical, x for horizontal); the content's own scroll is the
// negative offset along that axis over a range of (contentEnd - viewport).

float ScrollView::barView() const {
    return m_bar == Bar::Horizontal ? std::max(m_viewport.w, 0.0f)
                                    : std::max(m_viewport.h, 0.0f);
}

float ScrollView::barContentEnd() const {
    return m_bar == Bar::Horizontal ? m_contentBounds.x + m_contentBounds.w
                                    : m_contentBounds.y + m_contentBounds.h;
}

bool ScrollView::barVisible() const {
    return barContentEnd() - barView() > 1.0f;
}

Rect ScrollView::barTrack() const {
    const Rect b = bounds();
    if (m_bar == Bar::Horizontal)
        return Rect{kBarMargin, b.h - kBarW - kBarMargin,
                    std::max(b.w - 2.0f * kBarMargin, 0.0f), kBarW};
    return Rect{b.w - kBarW - kBarMargin, kBarMargin,
                kBarW, std::max(b.h - 2.0f * kBarMargin, 0.0f)};
}

Rect ScrollView::barThumb() const {
    const float view = barView();
    const float total = view + std::max(barContentEnd() - view, 0.0f);
    return m_bar == Bar::Horizontal
        ? barThumbRectH(barTrack(), view, total, -m_offset.x)
        : barThumbRect(barTrack(), view, total, -m_offset.y);
}

void ScrollView::applyBarScroll(float scroll) {
    if (m_bar == Bar::Horizontal) m_offset.x = -std::max(scroll, 0.0f);
    else m_offset.y = -std::max(scroll, 0.0f);
    clampScroll();
    if (Kernel* k = kernel()) k->invalidate();
}

bool ScrollView::claimsPoint(const Offset2D& localP) const {
    if (!barVisible()) return false;
    const Rect trk = barTrack();
    return localP.x >= trk.x && localP.x <= trk.x + trk.w &&
           localP.y >= trk.y && localP.y <= trk.y + trk.h;
}

// True when the track is longer than the thumb, so there is somewhere to drag
// it. A bar in a box no taller than the minimum thumb (a 12px scrollview) has
// the thumb filling the track: no press position means anything along it, and
// treating one as "scroll 0" would snap the content back to the start.
bool ScrollView::barHasSpan() const {
    const Rect trk = barTrack();
    const Rect th = barThumb();
    return m_bar == Bar::Horizontal ? trk.w - th.w > 0.0f : trk.h - th.h > 0.0f;
}

void ScrollView::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (!barVisible()) return;
    const Rect trk = barTrack();
    if (lx < trk.x || lx > trk.x + trk.w || ly < trk.y || ly > trk.y + trk.h)
        return;
    m_barDrag = true;
    if (!barHasSpan()) return;
    const bool horiz = m_bar == Bar::Horizontal;
    const float along = horiz ? lx : ly;
    const float view = barView();
    const float total = view + std::max(barContentEnd() - view, 0.0f);
    const Rect th = barThumb();
    const float thStart = horiz ? th.x : th.y;
    const float thLen = horiz ? th.w : th.h;
    if (along >= thStart && along <= thStart + thLen) {
        m_barGrab = along - thStart;
    } else {
        // Empty track: jump the thumb center to the press point.
        m_barGrab = thLen * 0.5f;
        applyBarScroll(horiz ? barScrollAtX(trk, view, total, along, m_barGrab)
                             : barScrollAt(trk, view, total, along, m_barGrab));
    }
}

void ScrollView::onPointerUp(MouseButton) {
    m_barDrag = false;
}

void ScrollView::onPointerMove(const Offset2D& p) {
    if (!m_barDrag) return;
    if (!barHasSpan()) return;
    const bool horiz = m_bar == Bar::Horizontal;
    const Offset2D o = absoluteOrigin();
    const float along = horiz ? static_cast<float>(p.x) - o.x
                              : static_cast<float>(p.y) - o.y;
    const float view = barView();
    const float total = view + std::max(barContentEnd() - view, 0.0f);
    applyBarScroll(horiz ? barScrollAtX(barTrack(), view, total, along, m_barGrab)
                         : barScrollAt(barTrack(), view, total, along, m_barGrab));
}

void ScrollView::onDrawForeground(Rasterizer& r) const {
    if (!barVisible()) return;
    // Bounds are shifted into absolute space by Kernel::drawTree here.
    const Rect bb = bounds();
    const Rect trk = offsetRect(barTrack(), bb.x, bb.y);
    const Rect th = offsetRect(barThumb(), bb.x, bb.y);
    if (m_bar == Bar::Horizontal) drawHorizontalBar(r, trk, th, m_barDrag);
    else drawVerticalBar(r, trk, th, m_barDrag);
}

// ------------------------------------------------------------------
// Column
// ------------------------------------------------------------------
Column::Column() = default;
Column::~Column() = default;

void Column::layout(float parentWidth, float parentHeight) {
    float pad = m_style.padding;
    float gap = m_style.gap > 0.0f ? m_style.gap
                  : (m_style.margin > 0.0f ? m_style.margin : 4.0f);
    const float contentW = parentWidth - pad * 2.0f;
    float y = pad;
    float maxW = 0.0f;

    // Weight pass: measure the fixed children, then weighted children
    // split whatever height the parent grants beyond them.
    float sumW = 0.0f, fixedH = 0.0f;
    int nKids = 0;
    for (const auto& c : children()) {
        if (!c) continue;
        ++nKids;
        c->resolveGeometry(contentW, parentHeight);
        const Rect cb = c->bounds();
        if (c->weight() > 0.0f) { sumW += c->weight(); continue; }
        float fh = cb.h > 0.0f ? cb.h : 24.0f;
        if (cb.h <= 0.0f) {
            if (auto* lab = dynamic_cast<Label*>(c.get()); lab && lab->wrap()) {
                const float need = lab->measureHeight(cb.w > 0.0f ? cb.w : contentW);
                if (need > 0.0f) fh = need;
            }
        }
        fixedH += fh;
    }
    const float gapTotal = nKids > 1 ? static_cast<float>(nKids - 1) * gap : 0.0f;
    float avail = parentHeight - pad * 2.0f - fixedH - gapTotal;
    if (avail < 0.0f) avail = 0.0f;

    for (const auto& c : children()) {
        if (!c) continue;
        // Percent/fill children resolve against the column's content width.
        c->resolveGeometry(contentW, parentHeight);
        Rect cb = c->bounds();
        // Stack vertically; keep an explicitly authored x (indentation) and
        // give zero-width children the full available width.
        float cx = cb.x;
        float cw = cb.w > 0.0f ? cb.w : contentW;
        float ch = cb.h > 0.0f ? cb.h : 24.0f;
        if (cb.h <= 0.0f) {
            // A wrapped label with no authored height sizes to its text.
            if (auto* lab = dynamic_cast<Label*>(c.get()); lab && lab->wrap()) {
                const float need = lab->measureHeight(cw);
                if (need > 0.0f) ch = need;
            }
        }
        if (sumW > 0.0f && c->weight() > 0.0f && avail > 0.0f)
            ch = avail * c->weight() / sumW;
        c->setBounds(Rect{cx, y, cw, ch});
        c->layout(cw, ch);

        Rect after = c->bounds();
        if (m_align != Align::Left) {
            // Cross-axis alignment ignores the authored x: the child is
            // centred or flush-right within the column's content width.
            const float nx = m_align == Align::Center
                ? pad + (contentW - after.w) * 0.5f
                : pad + contentW - after.w;
            after.x = nx;
            c->setBounds(after);
        }
        y += after.h + gap;
        maxW = std::max(maxW, after.x + after.w);
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
    float gap = m_style.gap > 0.0f ? m_style.gap
                  : (m_style.margin > 0.0f ? m_style.margin : 4.0f);
    float x = pad;
    float maxH = 0.0f;

    // Weight pass: measure the fixed children, then weighted children
    // split whatever width remains beyond them.
    float sumW = 0.0f, fixedW = 0.0f;
    int nKids = 0;
    for (const auto& c : children()) {
        if (!c) continue;
        ++nKids;
        c->resolveGeometry(parentWidth, parentHeight);
        if (c->weight() > 0.0f) { sumW += c->weight(); continue; }
        const Rect cb = c->bounds();
        fixedW += cb.w > 0.0f ? cb.w : 100.0f;
    }
    const float gapTotal = nKids > 1 ? static_cast<float>(nKids - 1) * gap : 0.0f;
    // An authored row width bounds the flex split; otherwise the width the
    // parent allots is used.
    const float refW = m_bounds.w > 0.0f ? m_bounds.w : parentWidth;
    float avail = refW - pad * 2.0f - fixedW - gapTotal;
    if (avail < 0.0f) avail = 0.0f;

    if (m_wrap) {
        // Flow layout: tile children left to right, starting a new line
        // whenever the next one would cross the row's right content edge.
        // The row's height becomes the stacked line heights.
        struct RItem { Widget* c; float w, h; };
        std::vector<RItem> items;
        for (const auto& c : children()) {
            if (!c) continue;
            c->resolveGeometry(parentWidth, parentHeight);
            const Rect cb = c->bounds();
            const float cbw = cb.w > 0.0f ? cb.w : 100.0f;
            const float cw = sumW > 0.0f && c->weight() > 0.0f && avail > 0.0f
                ? avail * c->weight() / sumW : cbw;
            items.push_back({c.get(), cw, cb.h > 0.0f ? cb.h : 24.0f});
        }
        const float maxRight = refW - pad;
        std::vector<std::vector<size_t>> lines;
        std::vector<float> lineH;
        for (size_t i = 0; i < items.size();) {
            lines.emplace_back();
            lineH.push_back(0.0f);
            float lx = pad;
            while (i < items.size()) {
                const RItem& it = items[i];
                if (lx > pad && it.w > maxRight - lx) break;
                lines.back().push_back(i);
                lx += it.w + gap;
                if (it.h > lineH.back()) lineH.back() = it.h;
                ++i;
            }
        }
        float lineY = 0.0f, usedW = 0.0f;
        for (size_t li = 0; li < lines.size(); ++li) {
            float lx = pad;
            for (size_t idx : lines[li]) {
                const RItem& it = items[idx];
                const Rect cb = it.c->bounds();
                float cy = lineY + cb.y;
                if (m_valign == VertAlign::Middle)
                    cy = lineY + (lineH[li] - it.h) * 0.5f;
                else if (m_valign == VertAlign::Bottom)
                    cy = lineY + lineH[li] - it.h;
                it.c->setBounds(Rect{lx, cy, it.w, it.h});
                it.c->layout(it.w, it.h);
                Rect after = it.c->bounds();
                lx += after.w + gap;
                if (lx - gap > usedW) usedW = lx - gap;
            }
            lineY += lineH[li] + gap;
        }
        m_bounds = Rect{m_bounds.x, m_bounds.y,
                        usedW > pad ? usedW : pad,
                        lines.empty() ? 0.0f : lineY - gap};
        return;
    }

    if (m_valign != VertAlign::Top) {
        // First pass: the tallest child defines the band shorter children
        // are centred or bottom-aligned inside.
        for (const auto& c : children()) {
            if (!c) continue;
            c->resolveGeometry(parentWidth, parentHeight);
            const Rect cb = c->bounds();
            maxH = std::max(maxH, cb.h > 0.0f ? cb.h : parentHeight);
        }
    }

    for (const auto& c : children()) {
        if (!c) continue;
        // Percent/fill children resolve against the row's content area.
        c->resolveGeometry(parentWidth, parentHeight);
        Rect cb = c->bounds();
        const float cbw = cb.w > 0.0f ? cb.w : 100.0f;
        const float cw = sumW > 0.0f && c->weight() > 0.0f && avail > 0.0f
            ? avail * c->weight() / sumW : cbw;
        const float ch = cb.h > 0.0f ? cb.h : parentHeight;
        float cy = cb.y;
        if (m_valign == VertAlign::Middle) cy = (maxH - ch) * 0.5f;
        else if (m_valign == VertAlign::Bottom) cy = maxH - ch;
        c->setBounds(Rect{x, cy, cw, ch});
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
