#include "widgets_internal.hpp"

namespace ogui {

// ------------------------------------------------------------------
// TextInput
// ------------------------------------------------------------------
TextInput::TextInput() = default;
TextInput::~TextInput() = default;

void TextInput::setText(const std::string& text) {
    m_text = text;
    m_caret = m_selAnchor = m_text.size();
    m_selectionStart = m_selectionEnd = m_caret;
    m_scrollX = 0.0f;
    if (Kernel* kn = kernel()) kn->invalidate();
}

std::string TextInput::selectedText() const {
    if (m_selectionStart >= m_selectionEnd) return std::string();
    return m_text.substr(m_selectionStart, m_selectionEnd - m_selectionStart);
}

void TextInput::setSelection(size_t start, size_t end) {
    if (end > m_text.size()) end = m_text.size();
    if (start > end) start = end;
    m_selectionStart = start;
    m_selectionEnd = end;
    m_selAnchor = start;
    m_caret = end;
    syncState();
    if (Kernel* kn = kernel()) kn->invalidate();
}

void TextInput::syncState() {
    if (m_caret > m_text.size()) m_caret = m_text.size();
    if (m_selAnchor > m_text.size()) m_selAnchor = m_text.size();
    m_selectionStart = std::min(m_caret, m_selAnchor);
    m_selectionEnd = std::max(m_caret, m_selAnchor);

    // Keep the caret on screen: onDraw clips the line to the frame and
    // shifts it by m_scrollX, so a long line still stays reachable.
    const float viewW = bounds().w - kTextPadX * 2.0f - insetLeft() - insetRight();
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    if (!f || viewW <= 0.0f) {
        m_scrollX = 0.0f;
        return;
    }
    const float caretX = f->measureRange(m_text.c_str(), m_caret);
    if (caretX - m_scrollX > viewW) m_scrollX = caretX - viewW;
    if (caretX < m_scrollX) m_scrollX = caretX;
    if (m_scrollX < 0.0f) m_scrollX = 0.0f;
}

size_t TextInput::indexAtX(float lx) const {
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    if (!f) return m_text.empty() ? 0 : m_text.size();
    const float tx = lx - kTextPadX - insetLeft() + m_scrollX;
    return f->indexFromX(m_text.c_str(), tx);
}

void TextInput::deleteSelection() {
    if (!hasSelection()) return;
    m_text.erase(m_selectionStart, m_selectionEnd - m_selectionStart);
    m_caret = m_selAnchor = m_selectionStart;
    syncState();
}

void TextInput::insertText(const std::string& s) {
    if (s.empty()) return;
    if (hasSelection()) {
        m_text.erase(m_selectionStart, m_selectionEnd - m_selectionStart);
        m_caret = m_selAnchor = m_selectionStart;
    }
    m_text.insert(m_caret, s);
    m_caret = m_selAnchor = m_caret + s.size();
    syncState();
}

void TextInput::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;   // right/middle must not move the caret
    m_focused = true;
    const Offset2D o = absoluteOrigin();
    m_lastMove = {static_cast<float>(p.x) - o.x, static_cast<float>(p.y) - o.y};
    const size_t pos = indexAtX(m_lastMove.x);
    Kernel* k = kernel();
    const uint32_t mods = k ? k->mods() : 0;
    const int clicks = k ? k->clickCount() : 1;
    if (mods & Mod_Shift) {
        m_caret = pos;               // extend from the existing anchor
    } else if (clicks >= 3) {
        m_selAnchor = 0;             // triple click: whole line
        m_caret = m_text.size();
    } else if (clicks == 2) {
        const auto [b, e] = wordRange(m_text, pos);   // double: word
        m_selAnchor = b;
        m_caret = e;
    } else {
        m_caret = m_selAnchor = pos;
    }
    m_dragging = true;
    syncState();
}

void TextInput::onPointerScroll(float dx, float dy) {
    // Horizontal wheel scroll of the text slice; the caret auto-scroll
    // re-arms on the next edit or key move.
    const float step = std::max(static_cast<float>(m_fontSize), 12.0f);
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    const float textW = f ? f->measureRange(m_text.c_str(), m_text.size()) : 0.0f;
    const float viewW = std::max(bounds().w - kTextPadX * 2.0f - insetLeft() - insetRight(), 0.0f);
    const float max = std::max(textW - viewW, 0.0f);
    const float d = (dy != 0.0f ? dy : dx) * step;
    m_scrollX = std::clamp(m_scrollX - d, 0.0f, max);
}

void TextInput::onPointerUp(MouseButton) {
    m_dragging = false;
}

void TextInput::onPointerMove(const Offset2D& p) {
    if (!m_dragging) return;
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (lx == m_lastMove.x && ly == m_lastMove.y) return;
    m_lastMove = {lx, ly};
    m_caret = indexAtX(lx);
    syncState();
}

void TextInput::onKeyDown(KeyCode key, uint32_t mods) {
    const std::string before = m_text;
    onKeyEdit(key, mods);
    if (m_text != before) announceChange();
    // Single-line field: Enter commits the text (a multi-line TextArea
    // overrides onKeyDown and turns Enter into a newline instead).
    if (key == Key_Enter || key == Key_KP_Enter) announceSubmit();
}

void TextInput::onKeyEdit(KeyCode key, uint32_t mods) {
    if (!m_focused) return;
    const bool shift = (mods & Mod_Shift) != 0;
    const bool ctrl = (mods & Mod_Ctrl) != 0;

    if (ctrl) {
        switch (key) {
            case Key_A:
                m_selAnchor = 0;
                m_caret = m_text.size();
                syncState();
                return;
            case Key_C:
                Clipboard::instance().setText(selectedText());
                return;
            case Key_X:
                Clipboard::instance().setText(selectedText());
                deleteSelection();
                return;
            case Key_Left:
            case Key_Right: {
                // Word-wise caret; Shift extends the selection. Without
                // shift an active selection collapses to its near end first,
                // like the standard editors do.
                size_t from = m_caret;
                if (!shift && hasSelection())
                    from = key == Key_Left ? m_selectionStart : m_selectionEnd;
                const size_t next = key == Key_Left
                    ? wordPrevBoundary(m_text, from)
                    : wordNextBoundary(m_text, from);
                m_caret = next;
                if (!shift) m_selAnchor = m_caret;
                syncState();
                return;
            }
            case Key_V: {
                std::string clip = Clipboard::instance().text();
                // Single line: drop control characters (CR/LF/TAB) so a
                // multi-line paste cannot break the layout. Bytes >= 0x80
                // stay, they are UTF-8 continuation/lead bytes.
                clip.erase(std::remove_if(clip.begin(), clip.end(),
                                          [](unsigned char c) {
                                              return c < 0x20 || c == 0x7F;
                                          }),
                           clip.end());
                insertText(clip);
                return;
            }
            default:
                break;
        }
    }

    switch (key) {
        case Key_Left: {
            const size_t next = utf8PrevBoundary(m_text, m_caret);
            m_caret = next;
            if (!shift) m_selAnchor = m_caret;
            syncState();
            break;
        }
        case Key_Right: {
            const size_t next = utf8NextBoundary(m_text, m_caret);
            m_caret = next;
            if (!shift) m_selAnchor = m_caret;
            syncState();
            break;
        }
        case Key_Home:
            m_caret = 0;
            if (!shift) m_selAnchor = m_caret;
            syncState();
            break;
        case Key_End:
            m_caret = m_text.size();
            if (!shift) m_selAnchor = m_caret;
            syncState();
            break;
        case Key_Backspace:
            if (hasSelection()) {
                deleteSelection();
            } else if (m_caret > 0) {
                const size_t start = utf8PrevBoundary(m_text, m_caret);
                m_text.erase(start, m_caret - start);
                m_caret = m_selAnchor = start;
                syncState();
            }
            break;
        case Key_Delete:
            if (hasSelection()) {
                deleteSelection();
            } else if (m_caret < m_text.size()) {
                const size_t end = utf8NextBoundary(m_text, m_caret);
                m_text.erase(m_caret, end - m_caret);
                m_selAnchor = m_caret;
                syncState();
            }
            break;
        default:
            break;
    }
}

void TextInput::onKeyUp(KeyCode) {}

void TextInput::onChar(uint32_t ch) {
    if (!m_focused) return;
    if (ch < 32 || ch == 127) return;   // control chars

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
    insertText(std::string(buf, static_cast<size_t>(n)));
    announceChange();
}

void TextInput::onFocus(bool focused) {
    m_focused = focused;
    if (!focused) m_dragging = false;
}

void TextInput::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);
    if (m_focused) {
        // Focus ring: thicker accent border.
        r.drawRectBorder(b, palette().focus, 2.0f, m_radius);
    } else if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
    }

    const float midY = b.y + b.h * 0.5f;
    const float textX = b.x + kTextPadX + insetLeft() - m_scrollX;
    const Font* f = r.resolveFont("sans", m_fontSize);

    r.beginFrame(b);   // text, selection and caret stay inside the widget

    // Selection highlight goes under the text.
    if (f && hasSelection()) {
        const float x0 = textX + f->measureRange(m_text.c_str(), m_selectionStart);
        const float x1 = textX + f->measureRange(m_text.c_str(), m_selectionEnd);
        Color sel = m_borderColor;
        sel.a = 90;
        r.drawRect({x0, b.y + 3.0f, x1 - x0, b.h - 6.0f}, sel);
    }

    if (!m_text.empty()) {
        r.drawText(m_text.c_str(), textX, midY, m_fontSize, "sans", m_fgColor,
                   Align::Left, VertAlign::Middle);
    } else if (!m_placeholder.empty()) {
        Color ph = m_fgColor;
        ph.a = 120;
        r.drawText(m_placeholder.c_str(), textX, midY, m_fontSize, "sans", ph,
                   Align::Left, VertAlign::Middle);
    }

    if (m_focused) {
        float cx = textX;
        if (f) cx += f->measureRange(m_text.c_str(), m_caret);
        r.drawRect({cx, b.y + 6.0f, 1.0f, b.h - 12.0f}, m_fgColor);
    }
    r.endFrame();
}

// ------------------------------------------------------------------
// TextArea
// ------------------------------------------------------------------
namespace {
    constexpr float kTextPadY = 4.0f;
    constexpr float kSbW = 8.0f;       // scrollbar track width
    constexpr float kSbMargin = 2.0f;  // track inset from the widget edge
    constexpr float kSbMinThumb = 16.0f;
    constexpr float kSbReserve = kSbW + kSbMargin;  // width kept clear for the bar

    // Thumb size for a viewport inside a track of the given height.
    float sbThumbH(float trackH, float viewH, float total) {
        if (total <= 0.0f) return trackH;
        return std::clamp(trackH * (viewH / total), kSbMinThumb, trackH);
    }

    // Scroll position -> thumb rect inside track (absolute coordinates).
    Rect sbThumbRect(const Rect& trk, float viewH, float total, float scroll) {
        if (trk.h <= 0.0f) return trk;
        const float thumbH = sbThumbH(trk.h, viewH, total);
        const float maxScroll = std::max(total - viewH, 0.0f);
        const float frac = maxScroll > 0.0f ? scroll / maxScroll : 0.0f;
        return Rect{trk.x, trk.y + frac * (trk.h - thumbH), trk.w, thumbH};
    }

    // Pointer y + grab offset -> the scroll value that puts the thumb there.
    float sbScrollAt(const Rect& trk, float viewH, float total, float ly,
                     float grab) {
        const float maxScroll = std::max(total - viewH, 0.0f);
        if (trk.h <= 0.0f || total <= 0.0f || maxScroll <= 0.0f) return 0.0f;
        const float span = trk.h - sbThumbH(trk.h, viewH, total);
        if (span <= 0.0f) return 0.0f;
        return std::clamp((ly - grab - trk.y) / span, 0.0f, 1.0f) * maxScroll;
    }

    // Byte index inside one line where a measured x lands (the font has no
    // length-limited indexFromX, so walk the code points).
    size_t lineByteAt(const std::string& text, size_t lineStart, size_t lineLen,
                      const Font* f, float tx) {
        if (!f || lineLen == 0 || tx <= 0.0f) return 0;
        size_t local = 0;
        float acc = 0.0f;
        while (local < lineLen) {
            size_t next = utf8NextBoundary(text, lineStart + local) - lineStart;
            if (next > lineLen) next = lineLen;
            const float adv = f->measureRange(text.c_str() + lineStart + local,
                                              next - local);
            if (acc + adv * 0.5f > tx) break;
            acc += adv;
            local = next;
        }
        return local;
    }
} // namespace

TextArea::TextArea() = default;
TextArea::~TextArea() = default;

void TextArea::setText(const std::string& text) {
    TextInput::setText(text);
    m_scrollY = 0.0f;
    syncState();
}

std::vector<TextArea::Line> TextArea::lines() const {
    std::vector<Line> out;
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    const Rect b = bounds();
    const float lh = lineHeight();
    const float viewH = std::max(b.h - kTextPadY * 2.0f, 0.0f);
    float wrapW = std::max(b.w - kTextPadX * 2.0f, 40.0f);

    // Physical line -> display lines no wider than wrapW. Words longer than
    // the whole line are hard-broken by code points; spaces at a break are
    // dropped (but kept as indentation at a line start).
    auto wrapPhysical = [&](size_t start, size_t len, float w) {
        if (!f || len == 0) { out.push_back({start, len}); return; }
        const size_t end = start + len;
        const float spaceW = std::max(f->measureRange(" ", 1), 0.5f);

        struct Tok { size_t s, e; bool space; };
        std::vector<Tok> toks;
        for (size_t i = start; i < end; ) {
            const bool sp = (m_text[i] == ' ');
            size_t j = i;
            while (j < end && (m_text[j] == ' ') == sp) ++j;
            toks.push_back({i, j, sp});
            i = j;
        }

        size_t lineStart = start;
        size_t lastWordEnd = start;
        float x = 0.0f;
        bool empty = true;   // nothing painted on the current display line
        size_t ti = 0;
        while (ti < toks.size()) {
            const Tok& t = toks[ti];
            if (t.space) { x += spaceW * static_cast<float>(t.e - t.s); ++ti; continue; }
            const float ww = f->measureRange(m_text.c_str() + t.s, t.e - t.s);
            if (ww > w) {   // oversized word: flush, then chunk it
                if (!empty) {
                    out.push_back({lineStart, lastWordEnd - lineStart});
                    lineStart = t.s; x = 0.0f;
                }
                size_t c = t.s;
                size_t chunkStart = c;
                float acc = 0.0f;
                while (c < t.e) {
                    size_t nxt = utf8NextBoundary(m_text, c);
                    if (nxt > t.e) nxt = t.e;
                    const float cw = f->measureRange(m_text.c_str() + c, nxt - c);
                    if (acc + cw > w && c > chunkStart) {
                        out.push_back({chunkStart, c - chunkStart});
                        chunkStart = c; acc = 0.0f;
                    }
                    acc += cw;
                    c = nxt;
                }
                lineStart = chunkStart; x = acc;
                lastWordEnd = t.e; empty = false;
                ++ti; continue;
            }
            if (!empty && x + ww > w) {
                out.push_back({lineStart, lastWordEnd - lineStart});  // break: drop trailing spaces
                lineStart = t.s;
                x = ww;
            } else {
                x += ww;
            }
            lastWordEnd = t.e;
            empty = false;
            ++ti;
        }
        if (lineStart < end) out.push_back({lineStart, end - lineStart});
    };

    auto build = [&](float wrapW_) {
        out.clear();
        size_t start = 0;
        for (size_t i = 0; i <= m_text.size(); ++i) {
            if (i == m_text.size() || m_text[i] == '\n') {
                wrapPhysical(start, i - start, wrapW_);
                start = i + 1;
            }
        }
    };

    build(wrapW);
    // Reserve room for the scrollbar once the content is known to overflow,
    // so wrapped text never slides under the bar.
    if (lh * static_cast<float>(out.size()) - viewH > 1.0f &&
        b.w - kTextPadX * 2.0f > kSbReserve) {
        build(wrapW - (kSbReserve));
    }
    return out;
}

int TextArea::lineOf(size_t index) const {
    const std::vector<Line> ls = lines();
    for (int i = 0; i < static_cast<int>(ls.size()); ++i)
        if (index <= ls[i].start + ls[i].len) return i;
    return static_cast<int>(ls.size()) - 1;
}

float TextArea::lineHeight() const {
    return std::max(textLineHeight(m_fontSize, "sans"), 1.0f);
}

float TextArea::contentH() const {
    return lineHeight() * static_cast<float>(lines().size());
}

float TextArea::viewH() const {
    return std::max(bounds().h - kTextPadY * 2.0f, 0.0f);
}

Rect TextArea::sbTrack() const {
    const Rect b = bounds();
    // Local space: hit-tested against pointer-minus-absoluteOrigin, drawn
    // translated by the shifted bounds origin (see onDraw).
    return Rect{b.w - kSbW - kSbMargin, kTextPadY, kSbW, viewH()};
}

Rect TextArea::sbThumb() const {
    return sbThumbRect(sbTrack(), viewH(), contentH(), m_scrollY);
}

void TextArea::sbScrollTo(float ly, float grab) {
    m_scrollY = sbScrollAt(sbTrack(), viewH(), contentH(), ly, grab);
}

void TextArea::syncState() {
    if (m_caret > m_text.size()) m_caret = m_text.size();
    if (m_selAnchor > m_text.size()) m_selAnchor = m_text.size();
    m_selectionStart = std::min(m_caret, m_selAnchor);
    m_selectionEnd = std::max(m_caret, m_selAnchor);

    const std::vector<Line> ls = lines();
    const int li = lineOf(m_caret);
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    if (!f) { m_scrollX = 0.0f; m_scrollY = 0.0f; return; }

    m_caretX = f->measureRange(m_text.c_str() + ls[li].start,
                               m_caret - ls[li].start);

    const Rect b = bounds();
    const float sbReserve = sbVisible() ? kSbReserve : 0.0f;
    const float viewW = b.w - kTextPadX * 2.0f - sbReserve;
    if (viewW > 0.0f) {
        if (m_caretX - m_scrollX > viewW) m_scrollX = m_caretX - viewW;
        if (m_caretX < m_scrollX) m_scrollX = m_caretX;
        if (m_scrollX < 0.0f) m_scrollX = 0.0f;
    }
    const float lh = lineHeight();
    const float viewH = b.h - kTextPadY * 2.0f;
    const float caretY = static_cast<float>(li) * lh;
    if (viewH > 0.0f) {
        if (caretY - m_scrollY + lh > viewH) m_scrollY = caretY + lh - viewH;
        if (caretY < m_scrollY) m_scrollY = caretY;
        if (m_scrollY < 0.0f) m_scrollY = 0.0f;
    }
}

size_t TextArea::indexAtPoint(float lx, float ly) const {
    const std::vector<Line> ls = lines();
    const float lh = lineHeight();
    int li = static_cast<int>((ly - kTextPadY + m_scrollY) / lh);
    if (li < 0) li = 0;
    if (li >= static_cast<int>(ls.size())) li = static_cast<int>(ls.size()) - 1;
    const Line& ln = ls[static_cast<size_t>(li)];
    const Font* f = FontManager::instance().get("sans", m_fontSize);
    return ln.start + lineByteAt(m_text, ln.start, ln.len, f,
                                 lx - kTextPadX + m_scrollX);
}

void TextArea::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;   // right/middle must not select text
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (sbVisible()) {
        const Rect trk = sbTrack();
        if (lx >= trk.x && lx <= trk.x + trk.w && ly >= trk.y && ly <= trk.y + trk.h) {
            m_focused = true;
            m_sbDrag = true;
            const Rect th = sbThumb();
            if (ly >= th.y && ly <= th.y + th.h) {
                m_sbGrab = ly - th.y;
            } else {
                // Clicked the empty track: jump the thumb center to the point.
                m_sbGrab = th.h * 0.5f;
                sbScrollTo(ly, m_sbGrab);
            }
            return;
        }
    }
    m_focused = true;
    m_lastMove = {lx, ly};
    const size_t pos = indexAtPoint(lx, ly);
    Kernel* k = kernel();
    const uint32_t mods = k ? k->mods() : 0;
    const int clicks = k ? k->clickCount() : 1;
    if (mods & Mod_Shift) {
        m_caret = pos;                       // extend from the existing anchor
    } else if (clicks >= 3) {
        m_selAnchor = 0;                     // triple click: all text
        m_caret = m_text.size();
    } else if (clicks == 2) {
        const auto [b, e] = wordRange(m_text, pos);   // double: word
        m_selAnchor = b;
        m_caret = e;
    } else {
        m_caret = m_selAnchor = pos;
    }
    m_dragging = true;
    syncState();
}

void TextArea::onPointerUp(MouseButton) {
    m_sbDrag = false;
    TextInput::onPointerUp(static_cast<MouseButton>(0));
}

void TextArea::onPointerMove(const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    if (m_sbDrag) {
        sbScrollTo(static_cast<float>(p.y) - o.y, m_sbGrab);
        return;
    }
    if (!m_dragging) return;
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (lx == m_lastMove.x && ly == m_lastMove.y) return;
    m_lastMove = {lx, ly};
    m_caret = indexAtPoint(lx, ly);
    syncState();
}

void TextArea::onKeyDown(KeyCode key, uint32_t mods) {
    const std::string before = m_text;
    onKeyEdit(key, mods);
    if (m_text != before) announceChange();
}

void TextArea::onKeyEdit(KeyCode key, uint32_t mods) {
    if (!m_focused) return;
    const bool shift = (mods & Mod_Shift) != 0;
    if (mods & Mod_Ctrl) {           // A/C/X/V/Arrows behave as in TextInput
        TextInput::onKeyEdit(key, mods);
        return;
    }

    const std::vector<Line> ls = lines();
    const int li = lineOf(m_caret);
    auto setCaret = [&](size_t pos) {
        m_caret = pos;
        if (!shift) m_selAnchor = pos;
        syncState();
    };

    switch (key) {
        case Key_Enter:
        case Key_KP_Enter:
            insertText("\n");
            return;
        case Key_Up:
        case Key_Down: {
            int tl = li + (key == Key_Down ? 1 : -1);
            if (tl < 0) { setCaret(0); return; }
            if (tl >= static_cast<int>(ls.size())) { setCaret(m_text.size()); return; }
            const Line& ln = ls[static_cast<size_t>(tl)];
            const Font* f = FontManager::instance().get("sans", m_fontSize);
            setCaret(ln.start + lineByteAt(m_text, ln.start, ln.len, f, m_caretX));
            return;
        }
        case Key_Home: setCaret(ls[static_cast<size_t>(li)].start); return;
        case Key_End:  setCaret(ls[static_cast<size_t>(li)].start +
                                ls[static_cast<size_t>(li)].len); return;
        default: break;
    }
    TextInput::onKeyEdit(key, mods);
}

void TextArea::onPointerScroll(float, float dy) {
    // Free wheel scrolling, clamped to the content; the caret only re-arms
    // its auto-scroll on the next edit or key move.
    const float lh = lineHeight();
    const float total = lh * static_cast<float>(lines().size());
    const float viewH = std::max(bounds().h - kTextPadY * 2.0f, 0.0f);
    m_scrollY = std::clamp(m_scrollY - dy * lh, 0.0f, std::max(total - viewH, 0.0f));
}

void TextArea::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);
    if (m_focused) {
        // Focus ring: thicker accent border.
        r.drawRectBorder(b, palette().focus, 2.0f, m_radius);
    } else if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
    }

    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    const float lh = lineHeight();
    const std::vector<Line> ls = lines();

    if (sbVisible()) b.w -= kSbReserve;   // keep text clear of the scrollbar
    r.beginFrame(b);   // lines are clipped to the frame
    const float textX = b.x + kTextPadX - m_scrollX;

    if (m_text.empty() && !m_placeholder.empty()) {
        Color ph = m_fgColor;
        ph.a = 120;
        std::string one = m_placeholder;
        one.erase(std::remove(one.begin(), one.end(), '\n'), one.end());
        r.drawTextShifted(one.c_str(), textX, b.y + kTextPadY, m_fontSize, *f, ph);
    }

    for (size_t i = 0; i < ls.size(); ++i) {
        const float ly = b.y + kTextPadY + static_cast<float>(i) * lh - m_scrollY;
        if (ly + lh < b.y || ly > b.y + b.h) continue;
        const size_t lstart = ls[i].start;
        const size_t lend = ls[i].start + ls[i].len;

        if (hasSelection()) {
            const size_t s0 = std::max(m_selectionStart, lstart);
            const size_t s1 = std::min(m_selectionEnd, lend);
            if (s0 < s1) {
                const float x0 = textX + f->measureRange(m_text.c_str() + lstart,
                                                         s0 - lstart);
                const float x1 = textX + f->measureRange(m_text.c_str() + lstart,
                                                         s1 - lstart);
                Color sel = m_borderColor;
                sel.a = 90;
                r.drawRect({x0, ly, x1 - x0, lh}, sel);
            }
        }

        if (ls[i].len > 0) {
            const std::string s = m_text.substr(lstart, ls[i].len);
            r.drawTextShifted(s.c_str(), textX, ly, m_fontSize, *f, m_fgColor);
        }
    }

    if (m_focused && !ls.empty()) {
        const int li = lineOf(m_caret);
        const float ly = b.y + kTextPadY + static_cast<float>(li) * lh - m_scrollY;
        const float cx = textX + f->measureRange(m_text.c_str() + ls[li].start,
                                                 m_caret - ls[li].start);
        r.drawRect({cx, ly, 1.0f, lh}, m_fgColor);
    }
    r.endFrame();

    // Vertical scrollbar: only when the text overflows the viewport. Drawn
    // outside the text clip so the thumb stays crisp over the frame edge.
    if (sbVisible()) {
        // b.x/b.y are the shifted (absolute) origin during this draw call;
        // sbTrack()/sbThumb() are local, so translate them before drawing.
        const Rect trk = offsetRect(sbTrack(), b.x, b.y);
        Color track = m_bgColor;
        track.a = 90;
        r.drawRoundedRect(trk, track, trk.w * 0.5f);
        const Rect th = offsetRect(sbThumb(), b.x, b.y);
        Color knob = m_borderColor;
        knob.a = static_cast<uint8_t>(m_dragging || m_sbDrag ? 240 : 170);
        r.drawRoundedRect(th, knob, th.w * 0.5f);
    }
}

// ------------------------------------------------------------------
// SearchInput
// ------------------------------------------------------------------
SearchInput::SearchInput() = default;
SearchInput::~SearchInput() = default;

Rect SearchInput::clearRect() const {
    const Rect b = bounds();
    return Rect{b.x + b.w - kIconW - 6.0f, b.y + b.h * 0.5f - 6.0f,
                12.0f, 12.0f};
}

void SearchInput::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn == Mouse_Left && !text().empty()) {
        const Offset2D o = absoluteOrigin();
        const Rect b = bounds();
        const Offset2D l{static_cast<float>(p.x) - o.x + b.x,
                         static_cast<float>(p.y) - o.y + b.y};
        const Rect q = clearRect();
        if (l.x >= q.x && l.x <= q.x + q.w &&
            l.y >= q.y && l.y <= q.y + q.h) {
            setText("");
            return;   // consume; the kernel click fires the filter binding
        }
    }
    TextInput::onPointerDown(btn, p);
}

void SearchInput::onDraw(Rasterizer& r) const {
    TextInput::onDraw(r);
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const float midY = b.y + b.h * 0.5f;
    const Color ink = m_fgColor;

    // Magnifier: ring + handle (the ring is an 8-segment polygon).
    const float cx = b.x + 6.0f + 5.0f, cy = midY - 1.0f, rad = 4.0f;
    Offset2D prev{cx + rad, cy};
    for (int i = 1; i <= 8; ++i) {
        const float a = 0.7853982f * static_cast<float>(i);
        Offset2D pt{cx + rad * std::cos(a), cy + rad * std::sin(a)};
        r.drawLine(prev, pt, ink, 1.5f);
        prev = pt;
    }
    r.drawLine({cx + rad * 0.72f, cy + rad * 0.72f},
               {cx + rad + 3.0f, cy + rad + 3.0f}, ink, 1.5f);

    if (!text().empty()) {
        const Rect q = clearRect();
        r.drawLine({q.x + 1.0f, q.y + 1.0f},
                   {q.x + q.w - 1.0f, q.y + q.h - 1.0f}, ink, 1.5f);
        r.drawLine({q.x + q.w - 1.0f, q.y + 1.0f},
                   {q.x + 1.0f, q.y + q.h - 1.0f}, ink, 1.5f);
    }
}

} // namespace ogui
