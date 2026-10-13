#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// ComboBox
// ------------------------------------------------------------------
ComboOption::ComboOption() = default;
ComboOption::~ComboOption() = default;

ComboPopup::ComboPopup() = default;
ComboPopup::~ComboPopup() = default;

void ComboPopup::setItems(const std::vector<std::string>& items) {
    m_items = items;
    if (m_hover >= static_cast<int>(m_items.size())) m_hover = -1;
}

void ComboPopup::setHover(int index) {
    m_hover = (index >= 0 && index < static_cast<int>(m_items.size()))
                  ? index : -1;
}

void ComboPopup::fadeIn() {
    m_fade.start(0.0f, 1.0f, kOpenDuration, TweenEase::EaseOutCubic);
}

void ComboPopup::onTick(float dt) {
    if (m_fade.running()) m_fade.update(dt);
}

int ComboPopup::rowAt(float localY) const {
    if (m_rowH <= 0.0f || m_items.empty()) return -1;
    const int row = static_cast<int>(localY / m_rowH);
    return (row >= 0 && row < static_cast<int>(m_items.size())) ? row : -1;
}

void ComboPopup::onPointerMove(const Offset2D& p) {
    // p is screen-absolute (popup bounds are absolute).
    const Offset2D local{p.x - bounds().x, p.y - bounds().y};
    setHover(rowAt(local.y));
}

void ComboPopup::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D local{p.x - bounds().x, p.y - bounds().y};
    const int row = rowAt(local.y);
    if (row >= 0 && m_onSelect) m_onSelect(row);
}

void ComboPopup::onKeyDown(KeyCode key, uint32_t) {
    if (m_items.empty()) return;
    if (key == Key_Up || key == Key_Down) {
        int h = m_hover;
        if (h < 0) h = -1;
        if (key == Key_Up) --h;
        if (key == Key_Down) ++h;
        if (h < 0) h = 0;
        if (h >= (int)m_items.size()) h = (int)m_items.size() - 1;
        setHover(h);
        return;
    }
    if (key == Key_Enter || key == Key_Space) {
        if (m_hover >= 0 && m_onSelect) {
            m_onSelect(m_hover);
        }
        return;
    }
    if (key == Key_Escape) {
        // Let kernel handle closing
        return;
    }
}

void ComboPopup::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    // Open-in animation: fade the whole list while it slides up into place.
    float fade = m_fade.value();
    if (fade <= 0.0f) return;
    const Rect box{b.x, b.y, b.w, b.h};

    auto tint = [&](Color c) {
        c.a = static_cast<uint8_t>(static_cast<float>(c.a) * fade);
        return c;
    };
    r.drawRect(box, tint(m_panel));
    r.drawRectBorder(box, tint(m_border), 1.0f, 4.0f);

    const Font* f = r.resolveFont(m_fontFam, m_fontSize);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const float y = b.y + static_cast<float>(i) * m_rowH;
        const Rect row{b.x, y, b.w, m_rowH};
        const bool hi = static_cast<int>(i) == m_hover;
        if (hi) r.drawRect(row, tint(m_highlight));
        if (f) {
            r.drawTextShifted(m_items[i].c_str(),
                              b.x + m_textPad, y + (m_rowH - m_fontSize) * 0.5f,
                              m_fontSize, *f,
                              tint(hi ? m_highlightText : m_rowText));
        }
    }
}

ComboBox::ComboBox() = default;
ComboBox::~ComboBox() = default;

ComboPopup* ComboBox::popup() const { return m_popup.get(); }

int ComboBox::count() const {
    return static_cast<int>(m_items.size());
}

const std::string& ComboBox::selectedLabel() const {
    static const std::string kEmpty;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        return kEmpty;
    return m_items[static_cast<size_t>(m_selected)];
}

void ComboBox::setSelectedIndex(int index) {
    m_wantedSelected = index;
    m_selected = index;
    if (m_selected >= static_cast<int>(m_items.size()))
        m_selected = static_cast<int>(m_items.size()) - 1;
}

void ComboBox::rebuildItems() {
    if (m_resolved) return;   // option list is static once parsed
    m_items.clear();
    for (const auto& c : children()) {
        if (auto* o = dynamic_cast<ComboOption*>(c.get()))
            m_items.push_back(o->label());
    }
    m_resolved = true;
    if (m_items.empty()) {
        m_selected = -1;
        return;
    }
    if (m_wantedSelected >= 0)
        m_selected = std::min(m_wantedSelected, static_cast<int>(m_items.size()) - 1);
    if (m_selected < 0) m_selected = 0;
}

void ComboBox::openPopup() {
    rebuildItems();
    if (m_items.empty()) return;
    if (!m_popup) m_popup = std::make_shared<ComboPopup>();
    m_popup->setItems(m_items);
    m_popup->setSelected(m_selected);
    m_popup->setHover(m_selected);
    m_popup->setRowHeight(m_rowH);
    m_popup->setFontSize(m_fontSize);
    m_popup->setFontFamily(m_fontFam);
    m_popup->setPanelColor(m_panel);
    m_popup->setBorderColor(m_popupBorder);
    m_popup->setTextColor(m_text);
    m_popup->setHighlightColor(m_highlight);
    m_popup->setHighlightTextColor(m_highlightText);
    m_popup->setOnSelect([this](int row) { commit(row); });
    m_popup->setOnDismiss([this] { m_open = false; });   // outside press closed it
    m_popup->fadeIn();
    m_popup->setVisible(true);   // closePopup() hid it; re-arm before drawing

    const Offset2D org = absoluteOrigin();
    const Rect b = bounds();
    const float ph = static_cast<float>(m_items.size()) * m_rowH;
    m_popup->setBounds(Rect{org.x, org.y + b.h, b.w, ph});
    if (Kernel* k = kernel()) k->openPopup(m_popup);

    m_open = true;
}

void ComboBox::closePopup() {
    if (m_popup) m_popup->setVisible(false);
    if (Kernel* k = kernel()) k->closePopup();
    m_open = false;
}

void ComboBox::commit(int index) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    const bool changed = (m_selected != index);
    m_selected = index;
    closePopup();
    if (changed && m_onChanged) m_onChanged(m_selected);
}

void ComboBox::onPointerDown(MouseButton, const Offset2D&) {
    if (!m_items.empty() && m_open) {
        closePopup();
        return;
    }
    openPopup();
}

void ComboBox::onKeyDown(KeyCode key, uint32_t) {
    if (m_items.empty()) return;
    if (!m_open) {
        switch (key) {
            case Key_Down:
            case Key_Up:
            case Key_Enter:
            case Key_Space:
                openPopup();
                break;
            default:
                break;
        }
        return;
    }
    // Open: arrows move the highlight, Enter commits, Escape closes.
    switch (key) {
        case Key_Down: {
            int h = m_popup->hover();
            if (h < 0) h = m_selected >= 0 ? m_selected : 0;
            m_popup->setHover(h + 1 < static_cast<int>(m_items.size()) ? h + 1 : h);
            break;
        }
        case Key_Up: {
            int h = m_popup->hover();
            if (h < 0) h = m_selected >= 0 ? m_selected : static_cast<int>(m_items.size()) - 1;
            m_popup->setHover(h - 1 >= 0 ? h - 1 : h);
            break;
        }
        case Key_Enter:
        case Key_Space:
            commit(m_popup->hover());
            break;
        case Key_Escape:
            closePopup();
            break;
        default:
            break;
    }
}

void ComboBox::onFocus(bool focused) {
    if (!focused) closePopup();
}

void ComboBox::layout(float parentWidth, float parentHeight) {
    rebuildItems();   // options are parsed as children; resolve the list once
    Widget::layout(parentWidth, parentHeight);
}

void ComboBox::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    if (m_bg.a > 0) r.drawRect(b, isHovered() ? shade(m_bg, 1.25f) : m_bg);
    if (m_border.a > 0)
        r.drawRectBorder(b, m_open ? palette().focus : m_border,
                         m_open ? 2.0f : 1.0f, 4.0f);

    const Font* f = r.resolveFont(m_fontFam, m_fontSize);
    const float midY = b.y + (b.h - m_fontSize) * 0.5f;
    if (f && !selectedLabel().empty()) {
        r.drawTextShifted(selectedLabel().c_str(), b.x + 8.0f, midY, m_fontSize,
                          *f, m_text);
    }
    // Filled triangle at the right edge (same shape as the TreeView toggle);
    // flips up while the popup is open.
    {
        const float cx = b.x + b.w - m_arrowWidth * 0.5f;
        const float cy = b.y + b.h * 0.5f;
        const float s = 4.0f;
        const Color ac = isHovered() || m_open ? shade(m_arrow, 1.4f) : m_arrow;
        if (m_open)
            r.drawTriangle({cx - s, cy + s * 0.6f}, {cx + s, cy + s * 0.6f},
                           {cx, cy - s * 0.8f}, ac);
        else
            r.drawTriangle({cx - s, cy - s * 0.6f}, {cx + s, cy - s * 0.6f},
                           {cx, cy + s * 0.8f}, ac);
    }
}

// ------------------------------------------------------------------
// Tab / Tabs
// ------------------------------------------------------------------
Tab::Tab() = default;
Tab::~Tab() = default;

Tabs::Tabs() = default;
Tabs::~Tabs() = default;

std::vector<Tab*> Tabs::pages() const {
    std::vector<Tab*> out;
    out.reserve(m_children.size());
    for (const auto& c : m_children) {
        if (auto* t = dynamic_cast<Tab*>(c.get())) out.push_back(t);
    }
    return out;
}

void Tabs::setActive(int index) {
    if (index < 0) index = 0;
    if (index == m_active) return;
    m_active = index;
    if (m_onChanged) m_onChanged(m_active);
}

float Tabs::tabCellWidth(int i) const {
    const std::vector<Tab*> ps = pages();
    if (i < 0 || i >= static_cast<int>(ps.size())) return 0.0f;
    float w = m_cellPad * 2.0f;
    if (const Font* f = FontManager::instance().get(m_fontFamily, m_fontSize)) {
        w += f->measure(ps[i]->label().c_str());
    }
    return w;
}

int Tabs::tabAt(float localX) const {
    if (localX < 0.0f) return -1;
    const std::vector<Tab*> ps = pages();
    float x = 0.0f;
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
        const float w = tabCellWidth(i);
        if (localX < x + w) return i;
        x += w;
    }
    return -1;
}

int Tabs::visibleTabs(float headerW) const {
    const std::vector<Tab*> ps = pages();
    float total = 0.0f;
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) total += tabCellWidth(i);
    if (total <= headerW) return static_cast<int>(ps.size());
    // Reserve the "..." button, then fit as many cells as possible.
    constexpr float kBtnW = 28.0f;
    float x = 0.0f;
    int n = 0;
    for (int i = 0; i < static_cast<int>(ps.size()); ++i) {
        const float w = tabCellWidth(i);
        if (x + w > headerW - kBtnW) break;
        x += w;
        ++n;
    }
    return n;
}

void Tabs::openOverflow() {
    const std::vector<Tab*> ps = pages();
    const Rect b = bounds();
    const int vis = visibleTabs(b.w);
    if (vis >= static_cast<int>(ps.size())) return;

    if (!m_overflow) m_overflow = std::make_shared<MenuPopup>();
    std::vector<MenuEntry> entries;
    for (int i = vis; i < static_cast<int>(ps.size()); ++i) {
        MenuEntry e;
        e.label = ps[i]->label();
        e.command = std::to_string(i);
        entries.push_back(e);
    }
    m_overflow->setEntries(entries);
    m_overflow->fadeIn();
    m_overflow->setVisible(true);
    const Offset2D o = absoluteOrigin();
    m_overflow->setBounds(Rect{o.x + b.w - 170.0f, o.y + m_headerHeight,
                               170.0f, m_overflow->contentHeight()});
    m_overflow->setOnSelect([this](const std::string& cmd) {
        if (Kernel* k = kernel()) k->closePopup();
        if (m_overflow) m_overflow->setVisible(false);
        setActive(std::atoi(cmd.c_str()));
    });
    if (Kernel* k = kernel()) k->openPopup(m_overflow);
}

void Tabs::layout(float parentWidth, float parentHeight) {
    Rect b = bounds();
    const float cw = b.w > 0.0f ? b.w : parentWidth;
    const float totalH = b.h > 0.0f ? b.h : parentHeight;
    const float hh = std::min(m_headerHeight, totalH);
    const float ch = std::max(totalH - hh, 0.0f);

    const std::vector<Tab*> ps = pages();
    if (m_active >= static_cast<int>(ps.size())) {
        m_active = ps.empty() ? 0 : static_cast<int>(ps.size()) - 1;
    }

    int i = 0;
    for (const auto& c : m_children) {
        if (!c) continue;
        if (auto* t = dynamic_cast<Tab*>(c.get())) {
            // Every page occupies the content area; only the active one is
            // visible, which takes it out of drawing AND hit-testing.
            t->setBounds(Rect{0.0f, hh, cw, ch});
            t->setVisible(i == m_active);
            ++i;
        }
        c->resolveGeometry(cw, ch);
        c->layout(cw, ch);
    }
}

void Tabs::onPointerDown(MouseButton, const Offset2D& p) {
    // Content clicks land on the page itself; this only ever fires for the
    // header strip (or dead space beside the cells).
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const float localY = static_cast<float>(p.y) - o.y;
    if (localY < 0.0f || localY > m_headerHeight) return;
    const float localX = static_cast<float>(p.x) - o.x;
    const int vis = visibleTabs(b.w);
    const bool truncated = vis < static_cast<int>(pages().size());
    // The "..." button owns the right-most 28px whenever pages are hidden.
    if (truncated && localX >= b.w - 28.0f) {
        openOverflow();
        return;
    }
    const int idx = tabAt(localX);
    if (idx >= 0 && idx < vis) setActive(idx);
}

void Tabs::onKeyDown(KeyCode key, uint32_t) {
    const int count = static_cast<int>(pages().size());
    if (count == 0) return;
    int next = m_active;
    switch (key) {
        case Key_Left:  next = m_active - 1; break;
        case Key_Right: next = m_active + 1; break;
        case Key_Home:  next = 0; break;
        case Key_End:   next = count - 1; break;
        default: return;
    }
    if (next < 0) next = 0;
    if (next > count - 1) next = count - 1;
    setActive(next);
}

void Tabs::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    const float hh = std::min(m_headerHeight, b.h);

    // Content area first; pages draw on top of it.
    r.drawRect(Rect{b.x, b.y + hh, b.w, std::max(b.h - hh, 0.0f)}, m_contentColor);
    // Header strip.
    r.drawRect(Rect{b.x, b.y, b.w, hh}, m_headerColor);

    const std::vector<Tab*> ps = pages();
    const int vis = visibleTabs(b.w);
    const bool truncated = vis < static_cast<int>(ps.size());
    float x = b.x;
    for (int i = 0; i < vis; ++i) {
        const float w = tabCellWidth(i);
        const bool act = (i == m_active);
        r.drawRect(Rect{x, b.y, w, hh}, act ? m_activeColor : m_tabColor);
        if (act) r.drawRect(Rect{x, b.y, w, 2.0f}, m_accentColor);
        r.drawText(ps[i]->label().c_str(), x + w * 0.5f, b.y + hh * 0.5f,
                   m_fontSize, m_fontFamily,
                   act ? palette().white : m_textColor,
                   Align::Center, VertAlign::Middle);
        x += w;
    }
    if (truncated) {
        const bool actHidden = (m_active >= vis);
        const Rect btn{b.x + b.w - 28.0f, b.y, 28.0f, hh};
        r.drawRect(btn, actHidden ? m_activeColor : m_tabColor);
        if (actHidden) r.drawRect(Rect{btn.x, b.y, btn.w, 2.0f}, m_accentColor);
        r.drawText("...", btn.x + btn.w * 0.5f, b.y + hh * 0.5f,
                   m_fontSize, m_fontFamily,
                   actHidden ? palette().white : m_textColor,
                   Align::Center, VertAlign::Middle);
    }

    r.drawRect(Rect{b.x, b.y + hh - 1.0f, b.w, 1.0f}, palette().track);
    if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, 0.0f);
    }
}

// ------------------------------------------------------------------
// ListItem / ListBox
// ------------------------------------------------------------------
ListItem::ListItem() = default;
ListItem::~ListItem() = default;

ListBox::ListBox() = default;
ListBox::~ListBox() = default;

void ListBox::setItems(const std::vector<std::string>& items) {
    m_items = items;
    m_resolved = true;
    if (m_selected >= static_cast<int>(m_items.size()))
        m_selected = static_cast<int>(m_items.size()) - 1;
    clampScroll();
}

const std::string& ListBox::selectedLabel() const {
    static const std::string kEmpty;
    if (m_selected < 0 || m_selected >= static_cast<int>(m_items.size()))
        return kEmpty;
    return m_items[static_cast<size_t>(m_selected)];
}

void ListBox::setSelectedIndex(int index) {
    m_wantedSelected = index;
    m_selected = index;
    if (m_selected >= static_cast<int>(m_items.size()))
        m_selected = static_cast<int>(m_items.size()) - 1;
    if (m_selected < 0 && !m_items.empty()) m_selected = 0;
    if (m_selected >= 0) ensureVisible(m_selected);
}

void ListBox::rebuildItems() {
    if (m_resolved) return;   // the <item> list is static once parsed
    m_items.clear();
    for (const auto& c : children()) {
        if (auto* li = dynamic_cast<ListItem*>(c.get()))
            m_items.push_back(li->label());
    }
    m_resolved = true;
    if (m_selected >= static_cast<int>(m_items.size()))
        m_selected = static_cast<int>(m_items.size()) - 1;
    if (m_wantedSelected >= 0 && !m_items.empty())
        m_selected = std::min(m_wantedSelected,
                              static_cast<int>(m_items.size()) - 1);
}

void ListBox::layout(float parentWidth, float parentHeight) {
    rebuildItems();
    clampScroll();
    Widget::layout(parentWidth, parentHeight);
}

void ListBox::clampScroll() {
    const float viewH = std::max(bounds().h, 0.0f);
    const float total = m_rowH * static_cast<float>(m_items.size());
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(total - viewH, 0.0f));
}

void ListBox::ensureVisible(int row) {
    if (row < 0) return;
    const float y = static_cast<float>(row) * m_rowH;
    const float viewH = std::max(bounds().h, 0.0f);
    if (y + m_rowH - m_scroll > viewH) m_scroll = y + m_rowH - viewH;
    if (y < m_scroll) m_scroll = y;
    clampScroll();
}

int ListBox::rowAt(float localY) const {
    if (m_rowH <= 0.0f || m_items.empty()) return -1;
    const int row = static_cast<int>((localY + m_scroll) / m_rowH);
    return (row >= 0 && row < static_cast<int>(m_items.size())) ? row : -1;
}

void ListBox::commit(int row) {
    if (row < 0 || row >= static_cast<int>(m_items.size())) return;
    const bool changed = (m_selected != row);
    m_selected = row;
    if (changed && m_onChanged) m_onChanged(m_selected);
}

void ListBox::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    commit(rowAt(static_cast<float>(p.y) - o.y));
}

void ListBox::onPointerMove(const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    m_hover = rowAt(static_cast<float>(p.y) - o.y);
}

void ListBox::onPointerScroll(float, float dy) {
    m_scroll -= dy * m_rowH * 3.0f;   // one notch = three rows
    clampScroll();
}

void ListBox::onKeyDown(KeyCode key, uint32_t) {
    if (m_items.empty()) return;
    const int last = static_cast<int>(m_items.size()) - 1;
    int row = m_selected < 0 ? 0 : m_selected;
    switch (key) {
        case Key_Up:    row = std::max(row - 1, 0); break;
        case Key_Down:  row = std::min(row + 1, last); break;
        case Key_Home:  row = 0; break;
        case Key_End:   row = last; break;
        case Key_Enter:
        case Key_Space: commit(row); return;
        default: return;
    }
    commit(row);
    ensureVisible(row);
}

void ListBox::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bg.a > 0) r.drawRect(b, m_bg);
    if (m_border.a > 0) r.drawRectBorder(b, m_border, 1.0f, 4.0f);

    r.beginFrame(b);   // rows are clipped to the list frame
    const Font* f = r.resolveFont(m_fontFam, m_fontSize);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const float y = b.y + static_cast<float>(i) * m_rowH - m_scroll;
        if (y + m_rowH < b.y) continue;
        if (y > b.y + b.h) break;
        const Rect row{b.x, y, b.w, m_rowH};
        const bool sel = static_cast<int>(i) == m_selected;
        const bool hov = static_cast<int>(i) == m_hover && !sel;
        if (sel) {
            r.drawRect(row, m_highlight);
            r.drawRect({b.x, y, 2.0f, m_rowH}, m_border);
        } else if (hov) {
            Color soft = m_highlight;
            soft.a = 120;
            r.drawRect(row, soft);
        }
        if (f) {
            r.drawTextShifted(m_items[i].c_str(), b.x + 8.0f,
                              y + (m_rowH - m_fontSize) * 0.5f, m_fontSize, *f,
                              sel ? palette().white : m_text);
        }
    }
    r.endFrame();
}

// ------------------------------------------------------------------
// TableColumn / TableRow / Table
// ------------------------------------------------------------------
TableColumn::TableColumn() = default;
TableColumn::~TableColumn() = default;

TableCell::TableCell() = default;
TableCell::~TableCell() = default;

TableRow::TableRow() = default;
TableRow::~TableRow() = default;

void TableRow::setCells(const std::string& semicolonSeparated) {
    m_cells.clear();
    std::string cur;
    for (char ch : semicolonSeparated) {
        if (ch == ';') { m_cells.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    m_cells.push_back(cur);
}

Table::Table() = default;
Table::~Table() = default;

void Table::setColumns(const std::vector<Column>& cols) {
    m_cols = cols;
    m_resolved = true;
}

void Table::setRows(const std::vector<std::vector<std::string>>& rows) {
    m_rows = rows;
    m_resolved = true;
    if (m_selected >= static_cast<int>(m_rows.size()))
        m_selected = static_cast<int>(m_rows.size()) - 1;
    resort();
    clampScroll();
}

std::string Table::cell(int row, int col) const {
    if (row < 0 || row >= static_cast<int>(m_order.size())) return {};
    const size_t orig = static_cast<size_t>(m_order[static_cast<size_t>(row)]);
    if (orig >= m_rows.size()) return {};
    const auto& r = m_rows[orig];
    if (col < 0 || static_cast<size_t>(col) >= r.size()) return {};
    return r[static_cast<size_t>(col)];
}

bool Table::cellEditable(int row, int col) const {
    (void)row;
    return col >= 0 && col < static_cast<int>(m_cols.size()) &&
           m_cols[static_cast<size_t>(col)].editable;
}

void Table::setSelectedIndex(int index) {
    m_wantedSelected = index;
    m_selected = index;
    if (m_selected >= static_cast<int>(m_rows.size()))
        m_selected = static_cast<int>(m_rows.size()) - 1;
    if (m_selected < 0 && !m_rows.empty()) m_selected = 0;
    for (size_t i = 0; i < m_order.size(); ++i)
        if (m_order[i] == m_selected) { ensureVisible(static_cast<int>(i)); break; }
}

void Table::rebuildCells() {
    if (m_resolved) return;   // the <th>/<tr> grid is static once parsed
    m_cols.clear();
    m_rows.clear();
    for (const auto& c : children()) {
        if (auto* th = dynamic_cast<TableColumn*>(c.get()))
            m_cols.push_back({th->label(), th->width(), th->resizable(),
                              th->editable(), th->scrollMode()});
    }
    for (const auto& c : children()) {
        auto* tr = dynamic_cast<TableRow*>(c.get());
        if (!tr) continue;
        // Cells come from <cell> children when present, else cells="a;b".
        std::vector<std::string> row;
        bool anyCell = false;
        for (const auto& cc : tr->children()) {
            auto* cell = dynamic_cast<TableCell*>(cc.get());
            if (!cell) continue;
            anyCell = true;
            row.push_back(cell->text());
        }
        if (!anyCell) row = tr->cells();
        m_rows.push_back(std::move(row));
    }
    // Fold row/cell level flags up onto their columns.
    auto colIndexByLabel = [&](const std::string& label) {
        for (size_t i = 0; i < m_cols.size(); ++i)
            if (m_cols[i].label == label) return static_cast<int>(i);
        return -1;
    };
    size_t ri = 0;
    for (const auto& c : children()) {
        auto* tr = dynamic_cast<TableRow*>(c.get());
        if (!tr) { continue; }
        if (tr->hasRowResizable())
            for (auto& col : m_cols) col.resizable = tr->rowResizable();
        if (tr->hasRowScroll())
            for (auto& col : m_cols) col.scroll = tr->rowScrollMode();
        std::string cur;
        auto flush = [&](const std::string& name) {
            if (name.empty()) return;
            const int ci = colIndexByLabel(name);
            if (ci >= 0) m_cols[static_cast<size_t>(ci)].editable = true;
        };
        for (char ch : tr->editableColumns()) {
            if (ch == ';') { flush(cur); cur.clear(); }
            else cur += ch;
        }
        flush(cur);
        // Per-cell overrides: <cell editable resizable scrollingText>.
        size_t ci = 0;
        for (const auto& cc : tr->children()) {
            auto* cell = dynamic_cast<TableCell*>(cc.get());
            if (!cell) continue;
            if (ci < m_cols.size()) {
                if (cell->hasEditable()) m_cols[ci].editable = cell->editable();
                if (cell->hasResizable()) m_cols[ci].resizable = cell->resizable();
                if (cell->hasScroll()) m_cols[ci].scroll = cell->scrollMode();
            }
            ++ci;
        }
        ++ri;
    }
    m_resolved = true;
    if (m_selected >= static_cast<int>(m_rows.size()))
        m_selected = static_cast<int>(m_rows.size()) - 1;
    if (m_wantedSelected >= 0 && !m_rows.empty())
        m_selected = std::min(m_wantedSelected,
                              static_cast<int>(m_rows.size()) - 1);
    resort();
}

void Table::resort() {
    m_order.resize(m_rows.size());
    for (size_t i = 0; i < m_order.size(); ++i) m_order[i] = static_cast<int>(i);
    if (m_sortCol < 0 || m_sortCol >= static_cast<int>(m_cols.size())) return;
    const size_t col = static_cast<size_t>(m_sortCol);
    const bool asc = m_sortAsc;
    auto at = [&](int orig) -> std::string {
        const auto& r = m_rows[static_cast<size_t>(orig)];
        return col < r.size() ? r[col] : std::string();
    };
    auto numeric = [](const std::string& s, double& out) {
        if (s.empty()) return false;
        size_t pos = 0;
        try { out = std::stod(s, &pos); } catch (...) { return false; }
        return pos == s.size();
    };
    std::stable_sort(m_order.begin(), m_order.end(), [&](int a, int b) {
        const std::string sa = at(a), sb = at(b);
        double da = 0.0, db = 0.0;
        const bool cmp = numeric(sa, da) && numeric(sb, db) ? (da < db)
                         : (std::lexicographical_compare(
                               sa.begin(), sa.end(), sb.begin(), sb.end(),
                               [](char x, char y) {
                                   return std::tolower(static_cast<unsigned char>(x)) <
                                          std::tolower(static_cast<unsigned char>(y));
                               }));
        return asc ? cmp : !cmp && sa != sb;
    });
}

void Table::sortBy(int col) {
    if (col < 0 || col >= static_cast<int>(m_cols.size())) return;
    if (m_sortCol == col) m_sortAsc = !m_sortAsc;
    else { m_sortCol = col; m_sortAsc = true; }
    resort();
}

void Table::layout(float parentWidth, float parentHeight) {
    rebuildCells();
    clampScroll();
    Widget::layout(parentWidth, parentHeight);
}

void Table::clampScroll() {
    const float viewH = std::max(bounds().h - m_headH, 0.0f);
    const float total = m_rowH * static_cast<float>(m_rows.size());
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(total - viewH, 0.0f));
}

void Table::ensureVisible(int row) {
    if (row < 0) return;
    const float y = static_cast<float>(row) * m_rowH;
    const float viewH = std::max(bounds().h - m_headH, 0.0f);
    if (y + m_rowH - m_scroll > viewH) m_scroll = y + m_rowH - viewH;
    if (y < m_scroll) m_scroll = y;
    clampScroll();
}

int Table::rowAt(float localY) const {
    if (localY < m_headH) return -1;   // header strip
    if (m_rowH <= 0.0f || m_rows.empty()) return -1;
    const int row = static_cast<int>((localY - m_headH + m_scroll) / m_rowH);
    return (row >= 0 && row < static_cast<int>(m_rows.size())) ? row : -1;
}

float Table::columnW(int col) const {
    if (col < 0 || col >= static_cast<int>(m_cols.size())) return 0.0f;
    if (m_cols[col].width > 0.0f) return m_cols[col].width;
    // Zero-width columns share whatever the explicit ones leave over.
    float used = 0.0f;
    int flexible = 0;
    for (const auto& c : m_cols) {
        if (c.width > 0.0f) used += c.width;
        else ++flexible;
    }
    if (flexible == 0) return 0.0f;
    return std::max((bounds().w - used) / static_cast<float>(flexible), 40.0f);
}

float Table::columnX(int col) const {
    float x = 0.0f;
    for (int i = 0; i < col && i < static_cast<int>(m_cols.size()); ++i)
        x += columnW(i);
    return x;
}

int Table::colAt(float localX) const {
    float x = 0.0f;
    for (int i = 0; i < static_cast<int>(m_cols.size()); ++i) {
        const float w = columnW(i);
        if (localX >= x && localX < x + w) return i;
        x += w;
    }
    return -1;
}

// A column whose right header edge sits within `kGrab` of localX and is
// resizable; the last column has no trailing handle. -1 when none.
int Table::colAtEdge(float localX) const {
    constexpr float kGrab = 4.0f;
    float x = 0.0f;
    for (int i = 0; i + 1 < static_cast<int>(m_cols.size()); ++i) {
        x += columnW(i);
        if (m_cols[static_cast<size_t>(i)].resizable &&
            std::fabs(localX - x) <= kGrab)
            return i;
    }
    return -1;
}

// Turn flexible (width==0) columns into concrete pixels so a resize drag
// moves one column without the others reflowing under the pointer.
void Table::resolveWidths() {
    for (auto& c : m_cols)
        if (c.width <= 0.0f) c.width = columnW(static_cast<int>(&c - m_cols.data()));
}

void Table::beginEdit(int visRow, int col) {
    if (visRow < 0 || col < 0 || col >= static_cast<int>(m_cols.size())) return;
    if (!m_cols[static_cast<size_t>(col)].editable) return;
    m_editing = true;
    m_editOrig = m_order[static_cast<size_t>(visRow)];
    m_editCol = col;
    const auto& row = m_rows[static_cast<size_t>(m_editOrig)];
    m_editText = static_cast<size_t>(col) < row.size() ? row[static_cast<size_t>(col)] : "";
    m_editCaret = m_editText.size();
}

void Table::endEdit(bool save) {
    if (!m_editing) return;
    if (save && m_editOrig >= 0 && m_editOrig < static_cast<int>(m_rows.size())) {
        auto& row = m_rows[static_cast<size_t>(m_editOrig)];
        while (static_cast<int>(row.size()) <= m_editCol) row.push_back("");
        row[static_cast<size_t>(m_editCol)] = m_editText;
        resort();
        clampScroll();
    }
    m_editing = false;
    m_editOrig = -1;
    m_editCol = -1;
    m_editText.clear();
    m_editCaret = 0;
}

// Insert one UTF-8 code point at the caret (the caret always sits on a
// boundary, so backspace stays correct).
void Table::insertEdit(uint32_t ch) {
    char buf[4];
    int n = 0;
    if (ch < 0x80) {
        buf[n++] = static_cast<char>(ch);
    } else if (ch < 0x800) {
        buf[n++] = static_cast<char>(0xC0 | (ch >> 6));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    } else if (ch < 0x10000) {
        buf[n++] = static_cast<char>(0xE0 | (ch >> 12));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    } else {
        buf[n++] = static_cast<char>(0xF0 | (ch >> 18));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
        buf[n++] = static_cast<char>(0x80 | (ch & 0x3F));
    }
    m_editText.insert(m_editCaret, std::string(buf, static_cast<size_t>(n)));
    m_editCaret += static_cast<size_t>(n);
}

void Table::commit(int row) {
    if (row < 0 || row >= static_cast<int>(m_order.size())) return;
    const int orig = m_order[static_cast<size_t>(row)];
    const bool changed = (m_selected != orig);
    m_selected = orig;
    if (changed && m_onChanged) m_onChanged(m_selected);
}

void Table::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (ly < m_headH) {
        // Header: a grab on a resizable column edge starts a width drag,
        // otherwise a click sorts by that column.
        const int edge = colAtEdge(lx);
        if (edge >= 0) {
            resolveWidths();
            m_resizeCol = edge;
            m_resizeGrabX = lx;
            m_resizeStartW = columnW(edge);
            return;
        }
        if (!m_sortable) return;
        for (int c = 0; c < static_cast<int>(m_cols.size()); ++c)
            if (lx >= columnX(c) && lx < columnX(c) + columnW(c)) { sortBy(c); return; }
        return;
    }
    const int row = rowAt(ly);
    const int col = colAt(lx);
    // Clicking an editable cell edits it in place (and selects the row).
    if (row >= 0 && col >= 0 && m_cols[static_cast<size_t>(col)].editable) {
        commit(row);
        beginEdit(row, col);
        return;
    }
    // Any other click leaves an in-progress edit (saved) and selects the row.
    if (m_editing) endEdit(true);
    commit(row);
}

void Table::onPointerUp(MouseButton) {
    m_resizeCol = -1;
}

void Table::onPointerMove(const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (m_resizeCol >= 0) {
        const float dx = lx - m_resizeGrabX;
        m_cols[static_cast<size_t>(m_resizeCol)].width =
            std::max(40.0f, m_resizeStartW + dx);
        clampScroll();
        return;
    }
    m_hover = rowAt(ly);
    m_hoverCol = ly < m_headH ? -1 : colAt(lx);
}

void Table::onPointerScroll(float, float dy) {
    m_scroll -= dy * m_rowH * 3.0f;   // one notch = three rows
    clampScroll();
}

void Table::onChar(uint32_t ch) {
    if (!m_editing) return;
    if (ch < 32 || ch == 127) return;   // control chars
    insertEdit(ch);
}

void Table::onFocus(bool focused) {
    if (!focused && m_editing) endEdit(true);
}

void Table::onTick(float dt) {
    Widget::onTick(dt);
    if (dt > 0.0f) m_phase += dt;
}

void Table::onKeyDown(KeyCode key, uint32_t) {
    if (m_editing) {
        switch (key) {
            case Key_Enter:
            case Key_KP_Enter:
                endEdit(true);
                return;
            case Key_Escape:
                endEdit(false);
                return;
            case Key_Backspace: {
                if (m_editCaret > 0) {
                    const size_t prev = utf8PrevBoundary(m_editText, m_editCaret);
                    m_editText.erase(prev, m_editCaret - prev);
                    m_editCaret = prev;
                }
                return;
            }
            case Key_Delete: {
                if (m_editCaret < m_editText.size()) {
                    const size_t next = utf8NextBoundary(m_editText, m_editCaret);
                    m_editText.erase(m_editCaret, next - m_editCaret);
                }
                return;
            }
            case Key_Left:
                if (m_editCaret > 0) m_editCaret = utf8PrevBoundary(m_editText, m_editCaret);
                return;
            case Key_Right:
                if (m_editCaret < m_editText.size())
                    m_editCaret = utf8NextBoundary(m_editText, m_editCaret);
                return;
            case Key_Home: m_editCaret = 0; return;
            case Key_End:  m_editCaret = m_editText.size(); return;
            default: return;
        }
    }
    if (m_order.empty()) return;
    const int last = static_cast<int>(m_order.size()) - 1;
    int pos = 0;
    for (size_t i = 0; i < m_order.size(); ++i)
        if (m_order[i] == m_selected) { pos = static_cast<int>(i); break; }
    switch (key) {
        case Key_Up:    pos = std::max(pos - 1, 0); break;
        case Key_Down:  pos = std::min(pos + 1, last); break;
        case Key_Home:  pos = 0; break;
        case Key_End:   pos = last; break;
        case Key_F2: {
            // Keyboard entry into the selected row's first editable column.
            if (m_selected < 0) return;
            for (size_t i = 0; i < m_order.size(); ++i) {
                if (m_order[i] != m_selected) continue;
                for (int c = 0; c < static_cast<int>(m_cols.size()); ++c)
                    if (m_cols[static_cast<size_t>(c)].editable) {
                        beginEdit(static_cast<int>(i), c);
                        return;
                    }
                return;
            }
            return;
        }
        default: return;
    }
    commit(pos);
    ensureVisible(pos);
}

void Table::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    m_marqueeActive = false;
    Rect b = bounds();
    if (m_bg.a > 0) r.drawRect(b, m_bg);

    const Font* f = r.resolveFont(m_fontFam, m_fontSize);

    // ---- rows (clipped below the header) --------------------------------
    r.beginFrame(Rect{b.x, b.y + m_headH, b.w, std::max(b.h - m_headH, 0.0f)});
    for (size_t i = 0; i < m_order.size(); ++i) {
        const float y = b.y + m_headH + static_cast<float>(i) * m_rowH - m_scroll;
        if (y + m_rowH < b.y + m_headH) continue;
        if (y > b.y + b.h) break;
        const int orig = m_order[i];
        const Rect row{b.x, y, b.w, m_rowH};
        const bool sel = orig == m_selected;
        const bool hov = static_cast<int>(i) == m_hover && !sel;
        if (sel) {
            r.drawRect(row, m_highlight);
            r.drawRect({b.x, y, 2.0f, m_rowH}, m_border);
        } else if (hov) {
            Color soft = m_highlight;
            soft.a = 120;
            r.drawRect(row, soft);
        } else if (i % 2 == 1) {
            const auto up = [](uint8_t v) { return static_cast<uint8_t>(v + 8); };
            r.drawRect(row, Color{up(m_bg.r), up(m_bg.g), up(m_bg.b), 255});
        }
        if (f) {
            const auto* src = orig < static_cast<int>(m_rows.size())
                                  ? &m_rows[static_cast<size_t>(orig)] : nullptr;
            for (int c = 0; c < static_cast<int>(m_cols.size()); ++c) {
                const float cx = b.x + columnX(c);
                const float cw = columnW(c);
                const float avail = std::max(cw - 12.0f, 1.0f);
                const float ty = y + (m_rowH - m_fontSize) * 0.5f;
                const Color col = sel ? palette().white : m_text;
                const bool editing = m_editing && orig == m_editOrig && c == m_editCol;

                if (editing) {
                    // Edit box: highlight the cell, draw its text + caret.
                    r.drawRect({cx, y, cw, m_rowH}, Color{255, 255, 255, 18});
                    r.drawRectBorder({cx, y, cw, m_rowH}, m_border, 1.0f, 0.0f);
                    r.beginFrame(Rect{cx + 6.0f, y, avail, m_rowH});
                    r.drawTextShifted(m_editText.c_str(), cx + 6.0f, ty,
                                      m_fontSize, *f, col);
                    const float caretX = cx + 6.0f +
                        f->measureRange(m_editText.c_str(), m_editCaret);
                    r.drawRect({caretX, y + 2.0f, 1.0f, m_rowH - 4.0f}, m_text);
                    r.endFrame();
                    continue;
                }

                const std::string text =
                    src && static_cast<size_t>(c) < src->size() ? (*src)[c] : "";
                if (text.empty()) continue;
                const float textW = r.textWidth(text.c_str(), *f);

                // Marquee when the cell overflows and its column scrolls.
                const CellScroll sm = m_cols[static_cast<size_t>(c)].scroll;
                const bool hovered = static_cast<int>(i) == m_hover && c == m_hoverCol;
                const bool doScroll = textW > avail &&
                    (sm == CellScroll::Automatic ||
                     (sm == CellScroll::OnHover && hovered));
                if (!doScroll) {
                    r.drawTextShifted(text.c_str(), cx + 6.0f, ty, m_fontSize,
                                      *f, col);
                    continue;
                }
                m_marqueeActive = true;
                const float extra = textW - avail;
                constexpr float kSpeed = 28.0f, kPause = 40.0f;
                float off = std::fmod(m_phase * kSpeed, extra + kPause);
                if (off > extra) off = extra;
                r.beginFrame(Rect{cx + 6.0f, y, avail, m_rowH});
                r.drawTextShifted(text.c_str(), cx + 6.0f - off, ty, m_fontSize,
                                  *f, col);
                r.endFrame();
            }
        }
    }
    r.endFrame();

    // ---- header strip ----------------------------------------------------
    const Rect head{b.x, b.y, b.w, m_headH};
    r.drawRect(head, m_headerBg);
    if (f) {
        for (int c = 0; c < static_cast<int>(m_cols.size()); ++c) {
            const float cx = b.x + columnX(c);
            r.drawTextShifted(m_cols[c].label.c_str(), cx + 6.0f,
                              b.y + (m_headH - m_fontSize) * 0.5f, m_fontSize,
                              *f, m_text);
            if (m_sortable && c == m_sortCol) {
                // Small arrow: up when ascending, down when descending.
                const float ax = cx + columnW(c) - 14.0f;
                const float ay = b.y + m_headH * 0.5f;
                const Color ac = palette().focus;
                if (m_sortAsc)
                    r.drawTriangle({ax, ay + 3.0f}, {ax + 6.0f, ay + 3.0f},
                                   {ax + 3.0f, ay - 4.0f}, ac);
                else
                    r.drawTriangle({ax, ay - 3.0f}, {ax + 6.0f, ay - 3.0f},
                                   {ax + 3.0f, ay + 4.0f}, ac);
            }
        }
    }

    // ---- frame + column separators ---------------------------------------
    r.drawRectBorder(b, m_border, 1.0f, 4.0f);
    for (int c = 1; c < static_cast<int>(m_cols.size()); ++c)
        r.drawLine({b.x + columnX(c), b.y}, {b.x + columnX(c), b.y + b.h},
                   Color{60, 60, 60, 255}, 1.0f);
}

// ------------------------------------------------------------------
// TreeItem / TreeView
// ------------------------------------------------------------------
TreeItem::TreeItem() = default;
TreeItem::~TreeItem() = default;

TreeView::TreeView() = default;
TreeView::~TreeView() = default;

void TreeView::flatten(Widget* parent, int depth) {
    for (const auto& c : parent->children()) {
        auto* node = dynamic_cast<TreeItem*>(c.get());
        if (!node) continue;
        m_rows.push_back({node, depth});
        if (node->expanded()) flatten(node, depth + 1);
    }
}

void TreeView::layout(float parentWidth, float parentHeight) {
    m_rows.clear();
    flatten(this, 0);
    if (!m_wantedApplied && m_wantedSelected >= 0 && !m_rows.empty()) {
        m_selected = std::min(m_wantedSelected, static_cast<int>(m_rows.size()) - 1);
    }
    m_wantedApplied = true;
    // Remember the authored box height the first time we are laid out; every
    // later pass derives from it so a re-expand can grow back to full size.
    if (m_authoredH < 0.0f) m_authoredH = std::max(m_bounds.h, parentHeight);
    if (m_autoHeight) {
        const float need = m_rowH * static_cast<float>(m_rows.size());
        const float target = std::max(0.0f, std::min(m_authoredH, need));
        const float h = m_anim.running()
                            ? m_animOldH + (target - m_animOldH) * m_anim.value()
                            : target;
        if (h > 0.0f) m_bounds = Rect{m_bounds.x, m_bounds.y, m_bounds.w, h};
    }
    clampSelected();
    Widget::layout(parentWidth, parentHeight);
}

void TreeView::clampSelected() {
    if (m_selected >= static_cast<int>(m_rows.size()))
        m_selected = static_cast<int>(m_rows.size()) - 1;
    const float viewH = std::max(bounds().h, 0.0f);
    const float total = m_rowH * static_cast<float>(m_rows.size());
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(total - viewH, 0.0f));
}

void TreeView::ensureVisible(int row) {
    if (row < 0) return;
    const float y = static_cast<float>(row) * m_rowH;
    const float viewH = std::max(bounds().h, 0.0f);
    if (y + m_rowH - m_scroll > viewH) m_scroll = y + m_rowH - viewH;
    if (y < m_scroll) m_scroll = y;
    clampSelected();
}

int TreeView::rowAt(float localY) const {
    if (m_rowH <= 0.0f || m_rows.empty()) return -1;
    const int row = static_cast<int>((localY + m_scroll) / m_rowH);
    return (row >= 0 && row < static_cast<int>(m_rows.size())) ? row : -1;
}

bool TreeView::toggleAt(float localX, const Row& row) const {
    // The triangle occupies the [depth*indent, depth*indent + indent) slice.
    const float x0 = static_cast<float>(row.depth) * m_indent;
    return localX >= x0 && localX < x0 + m_indent;
}

void TreeView::rebuildAnimated() {
    // The height tween runs from wherever the box is right now.
    m_animOldH = m_bounds.h;
    // Snapshot the layout we are leaving (positions in pixels, pre-scroll).
    m_animOldY.clear();
    m_animOldRows = m_rows;
    for (size_t i = 0; i < m_rows.size(); ++i)
        m_animOldY[m_rows[i].node] = static_cast<float>(i) * m_rowH;

    const TreeItem* selected = m_selected >= 0 && m_selected < static_cast<int>(m_rows.size())
                                   ? m_rows[static_cast<size_t>(m_selected)].node : nullptr;
    m_rows.clear();
    flatten(this, 0);
    m_selected = -1;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        if (m_rows[static_cast<size_t>(i)].node == selected) { m_selected = i; break; }
    }
    clampSelected();

    m_anim.start(0.0f, 1.0f, 0.18f, TweenEase::EaseOutCubic);
}

void TreeView::toggleRow(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    TreeItem* node = m_rows[static_cast<size_t>(row)].node;
    ensureLoaded(node);
    if (node->children().empty()) return;
    node->setExpanded(!node->expanded());
    // The row we toggled keeps its screen slot; its (dis)appearing children
    // slide out of / into it. Remember that anchor, then re-flatten.
    m_animToggleY = static_cast<float>(row) * m_rowH;
    rebuildAnimated();
}

bool TreeView::hasExpandable(const TreeItem* node) const {
    if (!node) return false;
    if (!node->children().empty()) return true;
    return node->folder() && !node->path().empty() && !node->childrenLoaded();
}

void TreeView::ensureLoaded(TreeItem* node) {
    if (!node || node->childrenLoaded() || node->path().empty()) return;
    node->setChildrenLoaded(true);   // even on error: do not rescan per frame

    std::error_code ec;
    if (!std::filesystem::is_directory(node->path(), ec)) return;

    std::vector<std::pair<std::string, bool>> entries;   // name, is-dir
    for (const auto& e : std::filesystem::directory_iterator(node->path(), ec)) {
        if (ec) break;
        const std::string name = e.path().filename().string();
        if (!name.empty() && name[0] == '.') continue;   // hide dotfiles
        entries.emplace_back(name, e.is_directory(ec));
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) {
                  if (a.second != b.second) return a.second;   // dirs first
                  return a.first < b.first;
              });

    for (const auto& [name, isDir] : entries) {
        auto item = std::make_shared<TreeItem>();
        item->setLabel(name);
        item->setFolder(isDir);
        item->setExpanded(false);
        item->setPath((std::filesystem::path(node->path()) / name).string());
        item->setChildrenLoaded(!isDir);   // files never scan
        node->addChild(item);
    }
}

bool TreeView::loadDirectory(const std::string& path) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::is_directory(p, ec)) return false;

    m_rows.clear();
    auto kids = children();
    for (const auto& c : kids) removeChild(c);
    m_selected = -1;

    auto root = std::make_shared<TreeItem>();
    root->setLabel(p.filename().empty() ? p.string() : p.filename().string());
    root->setFolder(true);
    root->setExpanded(true);
    root->setPath(p.string());
    root->setChildrenLoaded(false);
    addChild(root);
    ensureLoaded(root.get());
    return true;
}

void TreeView::onTick(float dt) {
    Widget::onTick(dt);
    m_anim.update(dt);
}

void TreeView::commit(int row) {
    if (row < 0 || row >= static_cast<int>(m_rows.size())) return;
    const bool changed = (m_selected != row);
    m_selected = row;
    if (changed && m_onChanged) m_onChanged(m_selected);
}

const std::string& TreeView::selectedLabel() const {
    static const std::string kEmpty;
    TreeItem* node = selectedNode();
    return node ? node->label() : kEmpty;
}

TreeItem* TreeView::selectedNode() const {
    if (m_selected < 0 || m_selected >= static_cast<int>(m_rows.size()))
        return nullptr;
    return m_rows[static_cast<size_t>(m_selected)].node;
}

void TreeView::setSelectedIndex(int index) {
    m_wantedSelected = index;
    m_selected = index;
    m_wantedApplied = true;
    clampSelected();
    if (m_selected >= 0) ensureVisible(m_selected);
}

void TreeView::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    const int row = rowAt(ly);
    if (row < 0) return;
    const Row& r = m_rows[static_cast<size_t>(row)];
    if (m_showChecks) {
        const float rowY = static_cast<float>(row) * m_rowH - m_scroll;
        const Rect box = checkRect(contentX(r.depth), rowY);
        if (lx >= box.x && lx <= box.x + box.w && ly >= box.y && ly <= box.y + box.h) {
            r.node->setChecked(!r.node->checked());
            return;
        }
    }
    if (toggleAt(lx, r) && hasExpandable(r.node)) {
        toggleRow(row);
        return;
    }
    commit(row);
}

float TreeView::contentX(int depth) const {
    return static_cast<float>(depth) * m_indent + m_indent;
}

Rect TreeView::checkRect(float baseX, float rowY) const {
    return Rect{baseX, rowY + (m_rowH - m_checkSize) * 0.5f, m_checkSize, m_checkSize};
}

void TreeView::drawGuides(Rasterizer& r, const Rect& b) const {
    const Color gc = palette().guide;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const int d = m_rows[static_cast<size_t>(i)].depth;
        if (d <= 0) continue;
        int pi = -1;
        for (int j = i - 1; j >= 0; --j)
            if (m_rows[static_cast<size_t>(j)].depth == d - 1) { pi = j; break; }
        if (pi < 0) continue;
        const float gx = b.x + static_cast<float>(d - 1) * m_indent + m_indent * 0.5f;
        const float pcy = b.y + static_cast<float>(pi) * m_rowH + m_rowH * 0.5f - m_scroll;
        const float ccy = b.y + static_cast<float>(i) * m_rowH + m_rowH * 0.5f - m_scroll;
        r.drawLine({gx, pcy}, {gx, ccy}, gc, 1.0f);
        r.drawLine({gx, ccy}, {b.x + contentX(d), ccy}, gc, 1.0f);
    }
}

int TreeView::checkedCount() const {
    int n = 0;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        for (const auto& c : w->children()) {
            if (auto* it = dynamic_cast<TreeItem*>(c.get())) {
                if (it->checked()) ++n;
                walk(c.get());
            }
        }
    };
    walk(const_cast<TreeView*>(this));
    return n;
}

std::string TreeView::checkedLabels() const {
    std::string out;
    std::function<void(Widget*)> walk = [&](Widget* w) {
        for (const auto& c : w->children()) {
            if (auto* it = dynamic_cast<TreeItem*>(c.get())) {
                if (it->checked()) {
                    if (!out.empty()) out += ", ";
                    out += it->label();
                }
                walk(c.get());
            }
        }
    };
    walk(const_cast<TreeView*>(this));
    return out;
}

void TreeView::onPointerMove(const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    m_hover = rowAt(static_cast<float>(p.y) - o.y);
}

void TreeView::onPointerScroll(float, float dy) {
    m_scroll -= dy * m_rowH * 3.0f;
    clampSelected();
}

void TreeView::onKeyDown(KeyCode key, uint32_t) {
    if (m_rows.empty()) return;
    const int last = static_cast<int>(m_rows.size()) - 1;
    int row = m_selected < 0 ? 0 : m_selected;
    switch (key) {
        case Key_Up:    row = std::max(row - 1, 0); break;
        case Key_Down:  row = std::min(row + 1, last); break;
        case Key_Home:  row = 0; break;
        case Key_End:   row = last; break;
        case Key_Right: {
            TreeItem* node = m_rows[static_cast<size_t>(row)].node;
            ensureLoaded(node);
            if (node->children().empty() || node->expanded()) return;
            node->setExpanded(true);
            m_animToggleY = static_cast<float>(row) * m_rowH;
            rebuildAnimated();
            return;
        }
        case Key_Left: {
            TreeItem* node = m_rows[static_cast<size_t>(row)].node;
            if (!node->children().empty() && node->expanded()) {
                node->setExpanded(false);
                m_animToggleY = static_cast<float>(row) * m_rowH;
                rebuildAnimated();
                return;
            }
            if (auto* parent = dynamic_cast<TreeItem*>(node->parent())) {
                for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
                    if (m_rows[static_cast<size_t>(i)].node == parent) { commit(i); break; }
            }
            return;
        }
        case Key_Enter:
        case Key_Space:
            toggleRow(row);
            return;
        default: return;
    }
    commit(row);
    ensureVisible(row);
}

void TreeView::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bg.a > 0) r.drawRect(b, m_bg);
    if (m_border.a > 0) r.drawRectBorder(b, m_border, 1.0f, 4.0f);

    r.beginFrame(b);
    const Font* f = r.resolveFont(m_fontFam, m_fontSize);

    const bool anim = m_anim.running();
    const float t = anim ? m_anim.value() : 1.0f;
    auto lerp = [](float a, float c, float k) { return a + (c - a) * k; };

    if (m_showGuides && !anim) drawGuides(r, b);

    // One row at an interpolated vertical slot; `fade` scales its alpha so
    // appearing/vanishing rows blend instead of blinking.
    auto emit = [&](const TreeItem* node, int depth, float relY, float fade,
                    bool sel, bool hov) {
        const float y = b.y + relY - m_scroll;
        if (y + m_rowH < b.y || y > b.y + b.h) return;
        auto scale = [&](Color c) { c.a = static_cast<uint8_t>(c.a * fade); return c; };
        if (sel) r.drawRect({b.x, y, b.w, m_rowH}, scale(m_highlight));
        else if (hov) {
            Color soft = m_highlight;
            soft.a = 120;
            r.drawRect({b.x, y, b.w, m_rowH}, scale(soft));
        }
        // Content columns start after the triangle slice: [checkbox][icon].
        float cx = b.x + contentX(depth);
        if (m_showChecks) {
            const Rect box = checkRect(cx, y);
            r.drawRect(box, scale(palette().surfaceLo));
            r.drawRectBorder(box, scale(m_checkBorder), 1.0f, 2.0f);
            if (node->checked()) {
                r.drawRoundedRect(Rect{box.x + 2.0f, box.y + 2.0f,
                                       box.w - 4.0f, box.h - 4.0f},
                                  scale(m_checkFill), 1.5f);
                const Color tick = scale(palette().white);
                r.drawLine({box.x + 3.0f, box.y + box.h * 0.55f},
                           {box.x + box.w * 0.42f, box.y + box.h - 3.0f}, tick, 1.5f);
                r.drawLine({box.x + box.w * 0.42f, box.y + box.h - 3.0f},
                           {box.x + box.w - 2.0f, box.y + 2.5f}, tick, 1.5f);
            }
            cx += m_checkSize + 4.0f;
        }
        if (m_showIcons) {
            const float iy = y + (m_rowH - m_iconSize) * 0.5f;
            const float s = m_iconSize;
            if (node->folder()) {
                r.drawRect(Rect{cx, iy + s * 0.28f, s, s * 0.72f}, scale(m_folderColor));
                r.drawRect(Rect{cx, iy + s * 0.05f, s * 0.5f, s * 0.3f}, scale(m_folderColor));
            } else {
                r.drawRect(Rect{cx + s * 0.12f, iy, s * 0.76f, s}, scale(m_fileColor));
                r.drawTriangle({cx + s * 0.62f, iy},
                               {cx + s * 0.88f, iy + s * 0.28f},
                               {cx + s * 0.62f, iy + s * 0.28f},
                               scale(palette().textBright));
            }
            cx += m_iconSize + 4.0f;
        }
        cx += 8.0f;
        if (f) {
            const Color tc = sel
                ? scale(palette().white)
                : scale(m_text);
            r.drawTextShifted(node->label().c_str(), cx,
                              y + (m_rowH - m_fontSize) * 0.5f, m_fontSize, *f, tc);
        }
        if (hasExpandable(node)) {
            // Triangles as primitives: system fonts cannot be trusted to
            // carry the ▸/▾ code points.
            const float tx = b.x + static_cast<float>(depth) * m_indent + 3.0f;
            const float cy = y + m_rowH * 0.5f;
            const float s = 4.0f;
            const Color ac = scale(m_accent);
            if (node->expanded())
                r.drawTriangle({tx, cy - s * 0.6f}, {tx + 2 * s, cy - s * 0.6f},
                               {tx + s, cy + s * 0.8f}, ac);
            else
                r.drawTriangle({tx, cy - s}, {tx + 2 * s, cy}, {tx, cy + s}, ac);
        }
    };

    if (anim) {
        // Rows that just vanished slide into the toggled parent and fade out.
        for (size_t i = 0; i < m_animOldRows.size(); ++i) {
            const Row& old = m_animOldRows[i];
            bool still = false;
            for (const auto& nr : m_rows)
                if (nr.node == old.node) { still = true; break; }
            if (still) continue;
            const float oldY = static_cast<float>(i) * m_rowH;
            emit(old.node, old.depth, lerp(oldY, m_animToggleY, t), 1.0f - t,
                 false, false);
        }
    }

    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& row = m_rows[i];
        const bool sel = static_cast<int>(i) == m_selected;
        const bool hov = static_cast<int>(i) == m_hover && !sel;
        float relY = static_cast<float>(i) * m_rowH;
        float fade = 1.0f;
        if (anim) {
            auto it = m_animOldY.find(row.node);
            const float oldY = it != m_animOldY.end() ? it->second : m_animToggleY;
            fade = it != m_animOldY.end() ? 1.0f : t;   // new rows fade in
            relY = lerp(oldY, static_cast<float>(i) * m_rowH, t);
        }
        emit(row.node, row.depth, relY, fade, sel, hov);
    }
    r.endFrame();
}

// ------------------------------------------------------------------
// MenuPopup: floating menu list with submenu levels
// ------------------------------------------------------------------
MenuPopup::MenuPopup() = default;
MenuPopup::~MenuPopup() = default;

void MenuPopup::fadeIn() {
    m_fade.start(0.0f, 1.0f, 0.12f, TweenEase::EaseOutCubic);
}

void MenuPopup::onTick(float dt) {
    Widget::onTick(dt);
    if (m_fade.running()) m_fade.update(dt);
}

void MenuPopup::rebuildRows() {
    m_rows.clear();
    const Level& top = m_stack.back();
    if (m_stack.size() > 1) {
        Row back;
        back.back = true;
        m_rows.push_back(back);
    }
    for (const auto& e : top.entries) {
        Row row;
        if (e.separator) row.separator = true;
        else row.entry = &e;
        m_rows.push_back(row);
    }
}

void MenuPopup::setEntries(const std::vector<MenuEntry>& items) {
    m_stack.clear();
    m_stack.push_back(Level{{}, items});
    m_hover = -1;
    rebuildRows();
}

void MenuPopup::pushLevel(const MenuEntry& e) {
    m_stack.push_back(Level{e.label, e.sub});
    m_hover = -1;
    rebuildRows();
    Rect b = bounds();
    b.h = contentHeight();
    setBounds(b);
}

void MenuPopup::popLevel() {
    if (m_stack.size() <= 1) return;
    m_stack.pop_back();
    m_hover = -1;
    rebuildRows();
    Rect b = bounds();
    b.h = contentHeight();
    setBounds(b);
}

int MenuPopup::rowAt(float localY) const {
    if (m_rowH <= 0.0f || m_rows.empty()) return -1;
    const int row = static_cast<int>(localY / m_rowH);
    return (row >= 0 && row < static_cast<int>(m_rows.size())) ? row : -1;
}

void MenuPopup::onPointerMove(const Offset2D& p) {
    const Offset2D local{p.x - bounds().x, p.y - bounds().y};
    const int row = rowAt(local.y);
    m_hover = (row >= 0 && !m_rows[static_cast<size_t>(row)].separator) ? row : -1;
}

void MenuPopup::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D local{p.x - bounds().x, p.y - bounds().y};
    const int row = rowAt(local.y);
    if (row < 0) return;
    const Row& r = m_rows[static_cast<size_t>(row)];
    if (r.separator) return;
    if (r.back) { popLevel(); return; }
    const MenuEntry* e = r.entry;
    if (!e) return;
    if (!e->sub.empty()) { pushLevel(*e); return; }
    if (m_onSelect) m_onSelect(e->command);
}

void MenuPopup::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;
    const float fade = m_fade.value();
    if (fade <= 0.0f) return;

    auto tint = [&](Color c) {
        c.a = static_cast<uint8_t>(static_cast<float>(c.a) * fade);
        return c;
    };
    r.drawRect(b, tint(m_panel));
    r.drawRectBorder(b, tint(m_border), 1.0f, 4.0f);

    const Font* f = r.resolveFont("sans", m_fontSize);
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const float y = b.y + static_cast<float>(i) * m_rowH;
        const Rect row{b.x, y, b.w, m_rowH};
        const Row& rw = m_rows[i];
        if (rw.separator) {
            r.drawLine({b.x + 6.0f, y + m_rowH * 0.5f},
                       {b.x + b.w - 6.0f, y + m_rowH * 0.5f},
                       tint(m_sepColor), 1.0f);
            continue;
        }
        const bool hi = static_cast<int>(i) == m_hover;
        if (hi) r.drawRect(row, tint(m_highlight));
        std::string label;
        if (rw.back)
            label = std::string("\xe2\x80\xb9 ") + m_stack[m_stack.size() - 2].title;
        else if (rw.entry)
            label = rw.entry->label;
        if (f && !label.empty())
            r.drawTextShifted(label.c_str(), b.x + 10.0f,
                              y + (m_rowH - static_cast<float>(m_fontSize)) * 0.5f,
                              m_fontSize, *f, tint(m_text));
        // ▶ marker for submenu rows, on the right edge.
        if (rw.entry && !rw.entry->sub.empty()) {
            const float cy = y + m_rowH * 0.5f;
            const float cx = b.x + b.w - 12.0f;
            r.drawTriangle({cx - 3.0f, cy - 4.0f}, {cx - 3.0f, cy + 4.0f},
                           {cx + 4.0f, cy}, tint(m_text));
        }
    }
}

// ------------------------------------------------------------------
// MenuItem / Menu / MenuBar / ContextMenu
// ------------------------------------------------------------------
MenuItem::MenuItem() = default;
MenuItem::~MenuItem() = default;

MenuEntry MenuItem::buildEntry(const Widget& w) {
    MenuEntry e;
    if (const auto* mi = dynamic_cast<const MenuItem*>(&w)) {
        e.label = mi->label();
        e.command = mi->command();
        e.separator = mi->separator();
    }
    for (const auto& c : w.children()) {
        if (dynamic_cast<MenuItem*>(c.get()))
            e.sub.push_back(buildEntry(*c));
    }
    return e;
}

Menu::Menu() = default;
Menu::~Menu() = default;

MenuBar::MenuBar() = default;
MenuBar::~MenuBar() = default;

void MenuBar::layout(float parentWidth, float parentHeight) {
    const Rect b = bounds();
    const float pw = b.w > 0.0f ? b.w : parentWidth;
    const float ph = b.h > 0.0f ? b.h : parentHeight;
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    m_widths.clear();
    for (const auto& c : children()) {
        auto* menu = dynamic_cast<Menu*>(c.get());
        if (!menu) { m_widths.push_back(0.0f); continue; }
        const float tw = f ? static_cast<float>(
            f->measureRange(menu->label().c_str(), menu->label().size())) : 60.0f;
        m_widths.push_back(tw + 2.0f * m_pad);
        menu->setBounds(Rect{0.0f, 0.0f, 0.0f, ph});   // hit-transparent
        menu->layout(pw, ph);
    }
}

void MenuBar::openMenu(size_t index) {
    const auto& kids = children();
    if (index >= kids.size()) return;
    auto* menu = dynamic_cast<Menu*>(kids[index].get());
    if (!menu) return;

    std::vector<MenuEntry> entries;
    for (const auto& c : menu->children()) {
        if (auto* item = dynamic_cast<MenuItem*>(c.get()))
            entries.push_back(MenuItem::buildEntry(*item));
    }
    if (entries.empty()) return;

    if (!m_popup) m_popup = std::make_shared<MenuPopup>();
    m_popup->setEntries(entries);
    m_popup->fadeIn();
    m_popup->setVisible(true);

    // Float just under the clicked label, in absolute screen space.
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    float mx = 0.0f;
    for (size_t i = 0; i < index && i < m_widths.size(); ++i) mx += m_widths[i];
    float w = 150.0f;
    const Font* f = FontManager::instance().get("sans", 13);
    if (f) {
        for (const auto& e : entries) {
            const float tw = static_cast<float>(
                f->measureRange(e.label.c_str(), e.label.size()));
            if (tw + 44.0f > w) w = tw + 44.0f;
        }
    }
    m_popup->setBounds(Rect{o.x + mx, o.y + b.h, w, m_popup->contentHeight()});
    m_popup->setOnSelect([this](const std::string& cmd) {
        if (Kernel* k = kernel()) k->closePopup();
        if (m_popup) m_popup->setVisible(false);
        if (m_onCommand && !cmd.empty()) m_onCommand(cmd);
        if (Kernel* k = kernel()) k->invokeBinding(cmd);
    });
    if (Kernel* k = kernel()) k->openPopup(m_popup);
}

void MenuBar::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (ly < 0.0f || ly > b.h) return;
    float x = 0.0f;
    size_t i = 0;
    for (const auto& c : children()) {
        const float w = i < m_widths.size() ? m_widths[i] : 0.0f;
        if (dynamic_cast<Menu*>(c.get()) && lx >= x && lx <= x + w) {
            openMenu(i);
            return;
        }
        x += w;
        ++i;
    }
}

void MenuBar::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);
    r.drawLine({b.x, b.y + b.h - 1.0f}, {b.x + b.w, b.y + b.h - 1.0f},
               palette().border, 1.0f);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    float x = 0.0f;
    size_t i = 0;
    for (const auto& c : children()) {
        const auto* menu = dynamic_cast<const Menu*>(c.get());
        const float w = i < m_widths.size() ? m_widths[i] : 0.0f;
        if (menu && w > 0.0f) {
            r.drawTextShifted(menu->label().c_str(), b.x + x + m_pad,
                              b.y + (b.h - static_cast<float>(m_fontSize)) * 0.5f,
                              m_fontSize, *f, m_fgColor);
        }
        x += w;
        ++i;
    }
}

ContextMenu::ContextMenu() = default;
ContextMenu::~ContextMenu() = default;

void ContextMenu::openAt(float x, float y) {
    std::vector<MenuEntry> entries;
    for (const auto& c : children()) {
        if (auto* item = dynamic_cast<MenuItem*>(c.get()))
            entries.push_back(MenuItem::buildEntry(*item));
    }
    if (entries.empty()) return;

    if (!m_popup) m_popup = std::make_shared<MenuPopup>();
    m_popup->setEntries(entries);
    m_popup->fadeIn();
    m_popup->setVisible(true);

    float w = 150.0f;
    const Font* f = FontManager::instance().get("sans", 13);
    if (f) {
        for (const auto& e : entries) {
            const float tw = static_cast<float>(
                f->measureRange(e.label.c_str(), e.label.size()));
            if (tw + 44.0f > w) w = tw + 44.0f;
        }
    }
    m_popup->setBounds(Rect{x, y, w, m_popup->contentHeight()});
    m_popup->setOnSelect([this](const std::string& cmd) {
        if (Kernel* k = kernel()) k->closePopup();
        if (m_popup) m_popup->setVisible(false);
        if (m_onCommand && !cmd.empty()) m_onCommand(cmd);
        if (Kernel* k = kernel()) k->invokeBinding(cmd);
    });
    if (Kernel* k = kernel()) k->openPopup(m_popup);
}

} // namespace ogui
