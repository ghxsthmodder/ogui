#include "ogui/widgets.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/font.hpp"
#include "ogui/clipboard.hpp"
#include "ogui/imagemanager.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>

namespace ogui {

namespace {
    // Caret positions live in std::string byte offsets, so every move/edit
    // has to land on a UTF-8 boundary or it splits an accented character
    // in half and leaves stray continuation bytes in the text.
    size_t utf8PrevBoundary(const std::string& s, size_t i) {
        if (i == 0) return 0;
        size_t j = i - 1;
        while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
        return j;
    }

    size_t utf8NextBoundary(const std::string& s, size_t i) {
        if (i >= s.size()) return s.size();
        size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        return j;
    }

    // Horizontal padding between the frame and the text (onDraw/layout agree
    // on this, so clicks land on the same glyph the caret renders at).
    constexpr float kTextPadX = 6.0f;

    // Word units for double-click selection and Ctrl+arrow caret moves.
    // Bytes >= 0x80 are UTF-8 lead/continuation bytes: keeping them as word
    // bytes makes every boundary decision land on ASCII, so a "word" slice
    // can never split an accented character.
    bool isWordByte(unsigned char c) {
        return c >= 0x80 || std::isalnum(c) != 0 || c == '_';
    }

    // [begin,end) of the word containing or adjacent to pos; empty when the
    // click sits on a separator with no word to grab.
    std::pair<size_t, size_t> wordRange(const std::string& s, size_t pos) {
        size_t b = pos, e = pos;
        while (b > 0 && isWordByte(static_cast<unsigned char>(s[b - 1]))) --b;
        while (e < s.size() && isWordByte(static_cast<unsigned char>(s[e]))) ++e;
        if (b == e && pos > 0) {   // separator: fall back to the word before
            b = pos;
            while (b > 0 && !isWordByte(static_cast<unsigned char>(s[b - 1]))) --b;
            if (b == pos) b = e = pos;   // trailing separator: nothing to do
        }
        return {b, e};
    }

    size_t wordPrevBoundary(const std::string& s, size_t pos) {
        size_t i = pos;
        while (i > 0 && !isWordByte(static_cast<unsigned char>(s[i - 1]))) --i;
        while (i > 0 && isWordByte(static_cast<unsigned char>(s[i - 1]))) --i;
        return i;
    }

    size_t wordNextBoundary(const std::string& s, size_t pos) {
        size_t i = pos;
        while (i < s.size() && isWordByte(static_cast<unsigned char>(s[i]))) ++i;
        while (i < s.size() && !isWordByte(static_cast<unsigned char>(s[i]))) ++i;
        return i;
    }
} // namespace

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
                   withAlpha(hexColor("#ffffff"), m_pressed ? 6 : 28), 1.0f);
    }
    const bool focus = isFocused();
    if (m_borderWidth > 0.0f || focus) {
        r.drawRectBorder(b, focus ? hexColor("#88bbff") : m_borderColor,
                         focus ? 2.0f : m_borderWidth, m_radius);
    }
    if (!m_text.empty()) {
        r.drawText(m_text.c_str(), b.x + b.w * 0.5f, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   Align::Center, VertAlign::Middle);
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
    if (m_bgColor.a > 0) {
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
}

void Window::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    // Body first, then title bar on top of it.
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);

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
                       ogui::hexColor("#ffffff"), Align::Left, VertAlign::Middle);
        }

        float btnW = 16.0f, btnH = 16.0f;
        float pad = (th - btnH) * 0.5f;
        float bx = b.x + b.w - 3 * (btnW + 4) - 4;
        r.drawRect({bx, b.y + pad, btnW, btnH}, ogui::hexColor("#cc4444"));
        bx = b.x + b.w - 2 * (btnW + 4) - 4;
        r.drawRect({bx, b.y + pad, btnW, btnH}, ogui::hexColor("#cccc44"));
        bx = b.x + b.w - (btnW + 4) - 4;
        r.drawRect({bx, b.y + pad, btnW, btnH}, ogui::hexColor("#44cccc"));

    }
    if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
    }
}

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
// TextInput
// ------------------------------------------------------------------
TextInput::TextInput() = default;
TextInput::~TextInput() = default;

void TextInput::setText(const std::string& text) {
    m_text = text;
    m_caret = m_selAnchor = m_text.size();
    m_selectionStart = m_selectionEnd = m_caret;
    m_scrollX = 0.0f;
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
        r.drawRectBorder(b, hexColor("#88bbff"), 2.0f, m_radius);
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
    constexpr float kSbW = 8.0f;   // TextArea vertical scrollbar width

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
        b.w - kTextPadX * 2.0f > kSbW + 2.0f) {
        build(wrapW - (kSbW + 2.0f));
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
    return Rect{b.x + b.w - kSbW - 2.0f, b.y + kTextPadY, kSbW, viewH()};
}

Rect TextArea::sbThumb() const {
    const Rect trk = sbTrack();
    const float total = contentH();
    if (total <= 0.0f || trk.h <= 0.0f) return trk;
    const float thumbH = std::clamp(trk.h * (viewH() / total), 16.0f, trk.h);
    const float maxScroll = std::max(total - viewH(), 0.0f);
    const float frac = maxScroll > 0.0f ? m_scrollY / maxScroll : 0.0f;
    return Rect{trk.x, trk.y + frac * (trk.h - thumbH), trk.w, thumbH};
}

void TextArea::sbScrollTo(float ly, float grab) {
    const Rect trk = sbTrack();
    const float total = contentH();
    const float maxScroll = std::max(total - viewH(), 0.0f);
    if (trk.h <= 0.0f || total <= 0.0f) { m_scrollY = 0.0f; return; }
    const float thumbH = std::clamp(trk.h * (viewH() / total), 16.0f, trk.h);
    const float span = trk.h - thumbH;
    if (span <= 0.0f) { m_scrollY = 0.0f; return; }
    const float frac = std::clamp((ly - grab - trk.y) / span, 0.0f, 1.0f);
    m_scrollY = frac * maxScroll;
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
    const float sbReserve = sbVisible() ? kSbW + 2.0f : 0.0f;
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
    if (!m_focused) return;
    const bool shift = (mods & Mod_Shift) != 0;
    if (mods & Mod_Ctrl) {           // A/C/X/V/Arrows behave as in TextInput
        TextInput::onKeyDown(key, mods);
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
    TextInput::onKeyDown(key, mods);
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
        r.drawRectBorder(b, hexColor("#88bbff"), 2.0f, m_radius);
    } else if (m_borderWidth > 0.0f) {
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
    }

    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    const float lh = lineHeight();
    const std::vector<Line> ls = lines();

    if (sbVisible()) b.w -= kSbW + 2.0f;   // keep text clear of the scrollbar
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
        const Rect trk = sbTrack();
        Color track = m_bgColor;
        track.a = 90;
        r.drawRoundedRect(trk, track, trk.w * 0.5f);
        const Rect th = sbThumb();
        Color knob = m_borderColor;
        knob.a = static_cast<uint8_t>(m_dragging || m_sbDrag ? 240 : 170);
        r.drawRoundedRect(th, knob, th.w * 0.5f);
    }
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
                const size_t col = (i + j) % nCols;
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
// Checkbox
// ------------------------------------------------------------------
Checkbox::Checkbox() = default;
Checkbox::~Checkbox() = default;

void Checkbox::toggle() {
    m_checked = !m_checked;
    if (m_onChanged) m_onChanged(m_checked);
}

void Checkbox::onPointerDown(MouseButton, const Offset2D&) {
    toggle();
}

void Checkbox::onKeyDown(KeyCode key, uint32_t) {
    if (key == Key_Space || key == Key_Enter) toggle();
}

void Checkbox::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    const float side = std::min(b.h, 18.0f);
    const Rect box{b.x, b.y + (b.h - side) * 0.5f, side, side};
    if (m_checked) {
        // Soft accent fill behind the tick so the state reads at a glance.
        r.drawRoundedRect(box, withAlpha(m_checkColor, 40), 3.0f);
    } else if (isHovered()) {
        r.drawRoundedRect(box, withAlpha(m_boxColor, 28), 3.0f);
    }
    r.drawRectBorder(box, isHovered() ? shade(m_boxColor, 1.4f) : m_boxColor,
                     1.0f, 3.0f);

    if (m_checked) {
        // Two strokes make the tick; a filled square reads as "pressed".
        const float x0 = box.x + box.w * 0.20f;
        const float y0 = box.y + box.h * 0.52f;
        const float x1 = box.x + box.w * 0.42f;
        const float y1 = box.y + box.h * 0.76f;
        const float x2 = box.x + box.w * 0.80f;
        const float y2 = box.y + box.h * 0.26f;
        const float t = std::max(1.5f, box.w * 0.14f);
        r.drawLine({x0, y0}, {x1, y1}, m_checkColor, t);
        r.drawLine({x1, y1}, {x2, y2}, m_checkColor, t);
    }

    if (!m_text.empty()) {
        r.drawText(m_text.c_str(), box.x + side + 8.0f, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   Align::Left, VertAlign::Middle);
    }
}

// ------------------------------------------------------------------
// RadioButton
// ------------------------------------------------------------------
RadioButton::RadioButton() = default;
RadioButton::~RadioButton() = default;

void RadioButton::toggle() {
    // Radio semantics: selecting is sticky; the only way to uncheck a
    // selected radio is a sibling in the same group taking over.
    if (m_checked) return;
    if (!m_group.empty() && kernel()) {
        kernel()->eachWidget([this](Widget* w) {
            if (w == this) return;
            if (auto* rb = dynamic_cast<RadioButton*>(w))
                if (rb->group() == m_group) rb->setChecked(false);
        });
    }
    m_checked = true;
    if (m_onChanged) m_onChanged(m_checked);
}

void RadioButton::setChecked(bool c) {
    m_checked = c;
}

void RadioButton::onPointerDown(MouseButton, const Offset2D&) {
    // The Kernel already hit-tested this widget (bounds are parent-relative,
    // so re-checking the absolute point here would mostly miss).
    toggle();
}

void RadioButton::onKeyDown(KeyCode key, uint32_t) {
    if (key == Key_Space || key == Key_Enter) toggle();
}

void RadioButton::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    const float side = std::min(b.h, 18.0f);
    const Rect box{b.x, b.y + (b.h - side) * 0.5f, side, side};

    // Draw circle outline (brighter on hover)
    r.drawRectBorder(box, isHovered() ? shade(m_dotColor, 1.4f) : m_dotColor,
                     2.0f, side * 0.5f);

    // Draw circle fill if checked
    if (m_checked) {
        const float di = side - 6.0f;
        r.drawRoundedRect(Rect{box.x + 3, box.y + 3, di, di}, m_circleColor,
                          di * 0.5f);
    } else if (isHovered()) {
        const float di = side - 8.0f;
        r.drawRoundedRect(Rect{box.x + 4, box.y + 4, di, di},
                          withAlpha(m_circleColor, 46), di * 0.5f);
    }

    // Draw text
    if (!m_text.empty()) {
        r.drawText(m_text.c_str(), box.x + side + 8.0f, b.y + b.h * 0.5f,
                   m_fontSize, m_fontFamily, m_textColor,
                   Align::Left, VertAlign::Middle);
    }

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
// ProgressBar
// ------------------------------------------------------------------
ProgressBar::ProgressBar() = default;
ProgressBar::~ProgressBar() = default;

void ProgressBar::setValue(float v) {
    m_value = std::clamp(v, 0.0f, 1.0f);
}

void ProgressBar::onTick(float dt) {
    Widget::onTick(dt);
    if (m_indeterminate) m_phase += dt * 1.4f;
}

void ProgressBar::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);

    const float inset = m_borderWidth > 0.0f ? m_borderWidth : 0.0f;
    if (m_indeterminate) {
        // Ping-pong a 28% block across the track while ticking.
        const float span = std::max(b.w - inset * 2.0f, 0.0f);
        const float blockW = span * 0.28f;
        const float t = m_phase - std::floor(m_phase * 0.5f) * 2.0f;  // 0..2
        const float tri = t < 1.0f ? t : 2.0f - t;                    // 0..1..0
        Rect fill{b.x + inset + (span - blockW) * tri, b.y + inset,
                  blockW, b.h - inset * 2.0f};
        if (fill.w > 0.0f && fill.h > 0.0f) r.drawRect(fill, m_barColor);
    } else if (m_value > 0.0f) {
        Rect fill{b.x + inset, b.y + inset,
                  (b.w - inset * 2.0f) * m_value, b.h - inset * 2.0f};
        if (fill.w > 0.0f && fill.h > 0.0f) r.drawRect(fill, m_barColor);
    }

    if (m_borderWidth > 0.0f)
        r.drawRectBorder(b, m_borderColor, m_borderWidth, m_radius);
}

// ------------------------------------------------------------------
// Slider
// ------------------------------------------------------------------
Slider::Slider() = default;
Slider::~Slider() = default;

void Slider::setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    m_min = lo;
    m_max = hi;
    m_value = std::clamp(m_value, m_min, m_max);   // no notify: programmatic
}

void Slider::setValue(float v) {
    m_value = std::clamp(v, m_min, m_max);
}

void Slider::commit(float v) {
    const float nv = std::clamp(v, m_min, m_max);
    if (nv == m_value) return;
    m_value = nv;
    if (m_onChanged) m_onChanged(m_value);
}

float Slider::tFromValue() const {
    const float span = m_max - m_min;
    if (span <= 0.0f) return 0.0f;
    return (m_value - m_min) / span;
}

void Slider::valueFromLocalX(float localX) {
    const Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float span = b.w - pad * 2.0f;
    if (span <= 0.0f) {
        commit(m_min);
        return;
    }
    const float t = std::clamp((localX - pad) / span, 0.0f, 1.0f);
    commit(m_min + t * (m_max - m_min));
}

void Slider::onPointerDown(MouseButton, const Offset2D& p) {
    m_dragging = true;
    valueFromLocalX(static_cast<float>(p.x) - absoluteOrigin().x);
}

void Slider::onPointerUp(MouseButton) {
    m_dragging = false;
}

void Slider::onPointerMove(const Offset2D& p) {
    if (!m_dragging) return;
    valueFromLocalX(static_cast<float>(p.x) - absoluteOrigin().x);
}

void Slider::onKeyDown(KeyCode key, uint32_t) {
    const float step = (m_max - m_min) / 20.0f;
    switch (key) {
        case Key_Left:
        case Key_Down:
            commit(m_value - step);
            break;
        case Key_Right:
        case Key_Up:
            commit(m_value + step);
            break;
        case Key_Home:
            commit(m_min);
            break;
        case Key_End:
            commit(m_max);
            break;
        default:
            break;
    }
}

void Slider::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();

    const float pad = m_thumbWidth * 0.5f;
    const float y = b.y + b.h * 0.5f;
    const float x0 = b.x + pad;
    const float x1 = b.x + b.w - pad;
    const bool hot = isHovered() || m_dragging;
    r.drawLine({x0, y}, {x1, y}, hot ? shade(m_trackColor, 1.3f) : m_trackColor,
               m_trackThickness);

    const float t = tFromValue();
    const float x = x0 + (x1 - x0) * t;
    if (t > 0.0f)
        r.drawLine({x0, y}, {x, y}, hot ? shade(m_barColor, 1.2f) : m_barColor,
                   m_trackThickness);

    // Rounded thumb that grows a touch while hovered/dragged.
    const float grow = hot ? 2.0f : 0.0f;
    const Rect thumb{x - pad - grow * 0.5f, b.y + 2.0f - grow * 0.5f,
                     m_thumbWidth + grow, b.h - 4.0f + grow};
    if (m_thumbColor.a > 0)
        r.drawRoundedRect(thumb, hot ? shade(m_thumbColor, 1.15f) : m_thumbColor,
                          3.0f);
}

// ------------------------------------------------------------------
// SpinBox
// ------------------------------------------------------------------
SpinBox::SpinBox() = default;
SpinBox::~SpinBox() = default;

void SpinBox::setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    m_min = lo;
    m_max = hi;
    m_value = std::clamp(m_value, m_min, m_max);   // programmatic: no notify
}

void SpinBox::setValue(float v) {
    m_value = std::clamp(v, m_min, m_max);
}

void SpinBox::stepBy(int dir) {
    float nv = m_value + static_cast<float>(dir) * m_step;
    if (nv > m_max) nv = m_wrap ? m_min : m_max;
    else if (nv < m_min) nv = m_wrap ? m_max : m_min;
    if (nv == m_value) return;
    m_value = nv;
    if (m_onChanged) m_onChanged(m_value);
}

std::string SpinBox::formatValue() const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", m_decimals, static_cast<double>(m_value));
    return m_prefix + buf + m_suffix;
}

void SpinBox::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    // p is screen-absolute; lx/ly are widget-local (origin at 0,0).
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (ly < 0.0f || ly > b.h) return;
    const float btnW = b.h;                 // square buttons on each end
    if (lx >= 0.0f && lx <= btnW) stepBy(-1);              // left = minus
    else if (lx >= b.w - btnW && lx <= b.w) stepBy(+1);    // right = plus
}

void SpinBox::onKeyDown(KeyCode key, uint32_t) {
    switch (key) {
        case Key_Up:
        case Key_Right: stepBy(+1); break;
        case Key_Down:
        case Key_Left:  stepBy(-1); break;
        case Key_Home:  setValue(m_min); if (m_onChanged) m_onChanged(m_value); break;
        case Key_End:   setValue(m_max); if (m_onChanged) m_onChanged(m_value); break;
        default: break;
    }
}

void SpinBox::onDraw(Rasterizer& r) const {
    // One widget, not three: a single rounded frame; the buttons are flat
    // inset regions behind hairline separators.
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);
    if (isFocused()) {
        r.drawRectBorder(b, hexColor("#88bbff"), 2.0f, m_radius);
    } else if (m_borderColor.a > 0) {
        r.drawRectBorder(b, isHovered() ? shade(m_borderColor, 1.3f)
                                        : m_borderColor, 1.0f, m_radius);
    }

    const float btnW = b.h;
    const Rect minus{b.x, b.y, btnW, b.h};
    const Rect plus{b.x + b.w - btnW, b.y, btnW, b.h};

    // Per-button hover: the pointer region under the cursor gets a soft tint.
    bool hoverMinus = false, hoverPlus = false;
    if (isHovered() && kernel()) {
        const Offset2D m = kernel()->mousePos();
        hoverMinus = m.x >= minus.x && m.x <= minus.x + minus.w;
        hoverPlus = m.x >= plus.x && m.x <= plus.x + plus.w;
    }
    if (hoverMinus) {
        r.drawRoundedRect(Rect{b.x + 2, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }
    if (hoverPlus) {
        r.drawRoundedRect(Rect{plus.x, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }

    Color sep = m_borderColor;
    sep.a = 140;
    r.drawLine({minus.x + minus.w, b.y + 4}, {minus.x + minus.w, b.y + b.h - 4},
               sep, 1.0f);
    r.drawLine({plus.x, b.y + 4}, {plus.x, b.y + b.h - 4}, sep, 1.0f);

    // A single centred glyph on each button keeps it font-independent.
    // Plain '-' (not U+2212): the atlas only packs ASCII + Latin-1.
    r.drawText("-", minus.x + minus.w * 0.5f, minus.y + minus.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    r.drawText("+", plus.x + plus.w * 0.5f, plus.y + plus.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);

    r.drawText(formatValue().c_str(), b.x + b.w * 0.5f, b.y + b.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
}

// ------------------------------------------------------------------
// OptionSpin
// ------------------------------------------------------------------
OptionSpin::OptionSpin() = default;
OptionSpin::~OptionSpin() = default;

void OptionSpin::setOptions(std::vector<std::string> opts) {
    m_options = std::move(opts);
    m_index = m_options.empty() ? 0 : std::clamp(m_index, 0, static_cast<int>(m_options.size()) - 1);
}

void OptionSpin::setIndex(int i) {
    if (m_options.empty()) { m_index = 0; return; }
    m_index = std::clamp(i, 0, static_cast<int>(m_options.size()) - 1);
}

std::string OptionSpin::value() const {
    if (m_index < 0 || m_index >= static_cast<int>(m_options.size())) return std::string();
    return m_options[static_cast<size_t>(m_index)];
}

void OptionSpin::setValue(const std::string& v) {
    for (size_t i = 0; i < m_options.size(); ++i)
        if (m_options[i] == v) { m_index = static_cast<int>(i); return; }
}

void OptionSpin::stepBy(int dir) {
    if (m_options.empty()) return;
    const int n = static_cast<int>(m_options.size());
    int ni = m_index + dir;
    if (ni < 0) ni = m_wrap ? n - 1 : 0;
    else if (ni >= n) ni = m_wrap ? 0 : n - 1;
    if (ni == m_index) return;
    m_index = ni;
    if (m_onChanged) m_onChanged(m_index, m_options[static_cast<size_t>(m_index)]);
}

void OptionSpin::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    // p is screen-absolute; lx/ly are widget-local (origin at 0,0).
    const float lx = static_cast<float>(p.x) - o.x;
    const float ly = static_cast<float>(p.y) - o.y;
    if (ly < 0.0f || ly > b.h) return;
    const float btnW = b.h;                 // square arrows on each end
    if (lx >= 0.0f && lx <= btnW) stepBy(-1);              // [ < ]
    else if (lx >= b.w - btnW && lx <= b.w) stepBy(+1);    // [ > ]
}

void OptionSpin::onKeyDown(KeyCode key, uint32_t) {
    switch (key) {
        case Key_Up:
        case Key_Right: stepBy(+1); break;
        case Key_Down:
        case Key_Left:  stepBy(-1); break;
        case Key_Home:  setIndex(0);
                        if (!m_options.empty() && m_onChanged)
                            m_onChanged(m_index, m_options[static_cast<size_t>(m_index)]);
                        break;
        case Key_End:   setIndex(static_cast<int>(m_options.size()) - 1);
                        if (!m_options.empty() && m_onChanged)
                            m_onChanged(m_index, m_options[static_cast<size_t>(m_index)]);
                        break;
        default: break;
    }
}

void OptionSpin::onDraw(Rasterizer& r) const {
    // Same unified frame as SpinBox, with arrows instead of +/-.
    if (!m_style.isVisible) return;
    Rect b = bounds();
    if (m_bgColor.a > 0) r.drawRoundedRect(b, m_bgColor, m_radius);
    if (isFocused()) {
        r.drawRectBorder(b, hexColor("#88bbff"), 2.0f, m_radius);
    } else if (m_borderColor.a > 0) {
        r.drawRectBorder(b, isHovered() ? shade(m_borderColor, 1.3f)
                                        : m_borderColor, 1.0f, m_radius);
    }

    const float btnW = b.h;
    const Rect back{b.x, b.y, btnW, b.h};
    const Rect fwd{b.x + b.w - btnW, b.y, btnW, b.h};

    bool hoverBack = false, hoverFwd = false;
    if (isHovered() && kernel()) {
        const Offset2D m = kernel()->mousePos();
        hoverBack = m.x >= back.x && m.x <= back.x + back.w;
        hoverFwd = m.x >= fwd.x && m.x <= fwd.x + fwd.w;
    }
    if (hoverBack) {
        r.drawRoundedRect(Rect{b.x + 2, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }
    if (hoverFwd) {
        r.drawRoundedRect(Rect{fwd.x, b.y + 2, btnW - 2, b.h - 4},
                          withAlpha(shade(m_buttonColor, 1.6f), 110), 3.0f);
    }

    Color sep = m_borderColor;
    sep.a = 140;
    r.drawLine({back.x + back.w, b.y + 4}, {back.x + back.w, b.y + b.h - 4}, sep, 1.0f);
    r.drawLine({fwd.x, b.y + 4}, {fwd.x, b.y + b.h - 4}, sep, 1.0f);

    r.drawText("<", back.x + back.w * 0.5f, back.y + back.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    r.drawText(">", fwd.x + fwd.w * 0.5f, fwd.y + fwd.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);

    r.beginFrame(Rect{back.x + back.w, b.y, fwd.x - (back.x + back.w), b.h});
    r.drawText(value().c_str(), b.x + b.w * 0.5f, b.y + b.h * 0.5f,
               m_fontSize, m_fontFam, m_textColor, Align::Center, VertAlign::Middle);
    r.endFrame();
}

// ------------------------------------------------------------------
// RangeSlider
// ------------------------------------------------------------------
RangeSlider::RangeSlider() = default;
RangeSlider::~RangeSlider() = default;

void RangeSlider::setRange(float lo, float hi) {
    if (hi < lo) std::swap(lo, hi);
    m_min = lo;
    m_max = hi;
    m_low = std::clamp(m_low, m_min, m_max);
    m_high = std::clamp(m_high, m_min, m_max);
    if (m_low > m_high) std::swap(m_low, m_high);
}

void RangeSlider::setLow(float v) {
    const float nv = std::clamp(v, m_min, m_high);
    if (nv != m_low) { m_low = nv; if (m_onChanged) m_onChanged(m_low, m_high); }
}

void RangeSlider::setHigh(float v) {
    const float nv = std::clamp(v, m_low, m_max);
    if (nv != m_high) { m_high = nv; if (m_onChanged) m_onChanged(m_low, m_high); }
}

float RangeSlider::valueFromLocalX(float localX) const {
    const Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float span = b.w - pad * 2.0f;
    if (span <= 0.0f) return m_min;
    const float t = std::clamp((localX - pad) / span, 0.0f, 1.0f);
    return m_min + t * (m_max - m_min);
}

float RangeSlider::thumbX(float value) const {
    const Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float span = b.w - pad * 2.0f;
    const float t = (m_max - m_min) > 0.0f ? (value - m_min) / (m_max - m_min) : 0.0f;
    return b.x + pad + span * t;
}

void RangeSlider::commit(float low, float high) {
    low = std::clamp(low, m_min, m_max);
    high = std::clamp(high, m_min, m_max);
    if (low > high) std::swap(low, high);
    if (low == m_low && high == m_high) return;
    m_low = low;
    m_high = high;
    if (m_onChanged) m_onChanged(m_low, m_high);
}

void RangeSlider::onPointerDown(MouseButton, const Offset2D& p) {
    const Offset2D o = absoluteOrigin();
    const float lx = static_cast<float>(p.x) - o.x;
    const float v = valueFromLocalX(lx);
    // Grab whichever thumb is closer to the click.
    m_drag = (std::fabs(v - m_low) <= std::fabs(v - m_high)) ? 0 : 1;
    if (m_drag == 0) commit(v, m_high);
    else commit(m_low, v);
}

void RangeSlider::onPointerUp(MouseButton) {
    m_drag = -1;
}

void RangeSlider::onPointerMove(const Offset2D& p) {
    if (m_drag < 0) return;
    const Offset2D o = absoluteOrigin();
    const float v = valueFromLocalX(static_cast<float>(p.x) - o.x);
    if (m_drag == 0) commit(v, std::max(m_high, v));
    else commit(std::min(m_low, v), v);
}

void RangeSlider::onKeyDown(KeyCode key, uint32_t) {
    const float step = (m_max - m_min) / 20.0f;
    switch (key) {
        case Key_Left:  commit(m_low - step, m_high); break;
        case Key_Right: commit(m_low + step, m_high); break;
        case Key_Down:  commit(m_low, m_high - step); break;
        case Key_Up:    commit(m_low, m_high + step); break;
        case Key_Home:  commit(m_min, m_high); break;
        case Key_End:   commit(m_low, m_max); break;
        default: break;
    }
}

void RangeSlider::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    Rect b = bounds();
    const float pad = m_thumbWidth * 0.5f;
    const float y = b.y + b.h * 0.5f;
    const float x0 = b.x + pad;
    const float x1 = b.x + b.w - pad;
    const bool hot = isHovered() || m_drag >= 0;
    r.drawLine({x0, y}, {x1, y}, hot ? shade(m_trackColor, 1.3f) : m_trackColor,
               m_trackThickness);

    const float lx = thumbX(m_low);
    const float hx = thumbX(m_high);
    r.drawLine({lx, y}, {hx, y}, m_barColor, m_trackThickness);

    // Rounded thumbs; the grabbed one grows and brightens.
    const float ty = b.y + 2.0f, th = b.h - 4.0f;
    for (int i = 0; i < 2; ++i) {
        const float tx = i == 0 ? lx : hx;
        const bool active = m_drag == i;
        const float grow = active ? 3.0f : (isHovered() ? 1.5f : 0.0f);
        r.drawRoundedRect(Rect{tx - pad - grow * 0.5f, ty - grow * 0.5f,
                               m_thumbWidth + grow, th + grow},
                          active ? shade(m_thumbColor, 1.25f) : m_thumbColor,
                          3.0f);
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
        r.drawRectBorder(b, m_open ? hexColor("#88bbff") : m_border,
                         m_open ? 2.0f : 1.0f, 4.0f);

    const Font* f = r.resolveFont(m_fontFam, m_fontSize);
    const float midY = b.y + (b.h - m_fontSize) * 0.5f;
    if (f) {
        const std::string& label = selectedLabel();
        if (!label.empty()) {
            r.drawTextShifted(label.c_str(), b.x + 8.0f, midY, m_fontSize, *f,
                              m_text);
        }
        // Arrow at the right edge; flips when the popup is open.
        const Rect arrow{b.x + b.w - m_arrowWidth, b.y, m_arrowWidth, b.h};
        const float arrowX = arrow.x + (arrow.w - m_fontSize) * 0.5f;
        const char* glyph = m_open ? "\xE2\x96\xB2" : "\xE2\x96\xBE";  // up/down
        r.drawTextShifted(glyph, arrowX, midY, m_fontSize, *f,
                          isHovered() || m_open ? shade(m_arrow, 1.4f)
                                                : m_arrow);
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
                   act ? hexColor("#ffffff") : m_textColor,
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
                   actHidden ? hexColor("#ffffff") : m_textColor,
                   Align::Center, VertAlign::Middle);
    }

    r.drawRect(Rect{b.x, b.y + hh - 1.0f, b.w, 1.0f}, hexColor("#333333"));
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
                              sel ? ogui::hexColor("#ffffff") : m_text);
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
                const Color col = sel ? ogui::hexColor("#ffffff") : m_text;
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
                const Color ac = ogui::hexColor("#88bbff");
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
    if (node->children().empty()) return;
    node->setExpanded(!node->expanded());
    // The row we toggled keeps its screen slot; its (dis)appearing children
    // slide out of / into it. Remember that anchor, then re-flatten.
    m_animToggleY = static_cast<float>(row) * m_rowH;
    rebuildAnimated();
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
    if (toggleAt(lx, r) && !r.node->children().empty()) {
        toggleRow(row);
        return;
    }
    commit(row);
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
        case Key_Right:
            if (m_rows[static_cast<size_t>(row)].node->children().empty()) return;
            if (m_rows[static_cast<size_t>(row)].node->expanded()) return;
            m_rows[static_cast<size_t>(row)].node->setExpanded(true);
            m_animToggleY = static_cast<float>(row) * m_rowH;
            rebuildAnimated();
            return;
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
    const float textPad = m_indent + 8.0f;

    const bool anim = m_anim.running();
    const float t = anim ? m_anim.value() : 1.0f;
    auto lerp = [](float a, float c, float k) { return a + (c - a) * k; };

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
        if (f) {
            const Color tc = sel
                ? scale(ogui::hexColor("#ffffff"))
                : scale(m_text);
            r.drawTextShifted(node->label().c_str(),
                              b.x + textPad + static_cast<float>(depth) * m_indent,
                              y + (m_rowH - m_fontSize) * 0.5f, m_fontSize, *f, tc);
        }
        if (!node->children().empty()) {
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
// ToggleSwitch
// ------------------------------------------------------------------
ToggleSwitch::ToggleSwitch() = default;
ToggleSwitch::~ToggleSwitch() = default;

void ToggleSwitch::setState(bool state) {
    m_checked = state;
    if (m_onChanged) m_onChanged(state);
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
        const Color col = m_removeHot ? hexColor("#ff8888") : m_fgColor;
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
    r.drawRectBorder(head, hexColor("#3f4047"), 1.0f, 0.0f);

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
    Color dim = hexColor("#000000");
    dim.a = 140;
    r.drawRect(b, dim);
    const Rect pr = panelRect();
    r.drawRoundedRect(pr, hexColor("#262626"), 6.0f);
    r.drawRectBorder(pr, hexColor("#555555"), 1.0f, 6.0f);
    if (!m_title.empty()) {
        const Font* f = r.resolveFont("sans", 14);
        if (f) {
            r.drawTextShifted(m_title.c_str(), pr.x + 12.0f, pr.y + 8.0f,
                              14, *f, hexColor("#eeeeee"));
        }
    }
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
    r.drawRectBorder(b, hexColor("#3a3a3a"), 1.0f, 0.0f);
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
               hexColor("#3a3b44"), 1.0f);
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

// ------------------------------------------------------------------
// ColorPopup / ColorPicker
// ------------------------------------------------------------------
ColorPopup::ColorPopup() = default;
ColorPopup::~ColorPopup() = default;

void ColorPopup::setHSV(float h, float s, float v) {
    m_h = h; m_s = s; m_v = v;
}

void ColorPopup::hsvToRgb(float h, float s, float v,
                          float& r, float& g, float& b) {
    const float c = v * s;
    const float hp = h * 6.0f;
    const float xx = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    const float m = v - c;
    float rr = 0.0f, gg = 0.0f, bb = 0.0f;
    if (hp < 1.0f) { rr = c; gg = xx; }
    else if (hp < 2.0f) { rr = xx; gg = c; }
    else if (hp < 3.0f) { gg = c; bb = xx; }
    else if (hp < 4.0f) { gg = xx; bb = c; }
    else if (hp < 5.0f) { rr = xx; bb = c; }
    else { rr = c; bb = xx; }
    r = rr + m; g = gg + m; b = bb + m;
}

Rect ColorPopup::svRect() const {
    const Rect b = bounds();
    return Rect{b.x + 8.0f, b.y + 8.0f, b.w - 16.0f, b.h - 60.0f};
}

Rect ColorPopup::hueRect() const {
    const Rect b = bounds();
    return Rect{b.x + 8.0f, b.y + b.h - 44.0f, b.w - 16.0f, 12.0f};
}

void ColorPopup::pick(const Offset2D& p, bool fromHue) {
    if (fromHue) {
        const Rect q = hueRect();
        float t = q.w > 0.0f ? (static_cast<float>(p.x) - q.x) / q.w : 0.0f;
        t = std::clamp(t, 0.0f, 0.9999f);
        m_h = t;
    } else {
        const Rect q = svRect();
        float sx = q.w > 0.0f ? (static_cast<float>(p.x) - q.x) / q.w : 0.0f;
        float sy = q.h > 0.0f ? (static_cast<float>(p.y) - q.y) / q.h : 0.0f;
        m_s = std::clamp(sx, 0.0f, 1.0f);
        m_v = std::clamp(1.0f - sy, 0.0f, 1.0f);
    }
    if (m_onChanged) m_onChanged(m_h, m_s, m_v);
}

void ColorPopup::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Rect sv = svRect(), hue = hueRect();
    const float px = static_cast<float>(p.x), py = static_cast<float>(p.y);
    if (px >= hue.x && px <= hue.x + hue.w && py >= hue.y && py <= hue.y + hue.h) {
        m_drag = 2;
        pick(p, true);
    } else if (px >= sv.x && px <= sv.x + sv.w && py >= sv.y && py <= sv.y + sv.h) {
        m_drag = 1;
        pick(p, false);
    }
}

void ColorPopup::onPointerMove(const Offset2D& p) {
    if (m_drag == 2) pick(p, true);
    else if (m_drag == 1) pick(p, false);
}

void ColorPopup::onPointerUp(MouseButton) {
    m_drag = 0;
}

void ColorPopup::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_panel);
    r.drawRectBorder(b, m_border, 1.0f, 4.0f);

    // SV square as a coarse grid of flat cells (cheap, no shader).
    const Rect sv = svRect();
    constexpr int kCols = 12, kRows = 9;
    const float cw = sv.w / kCols, ch = sv.h / kRows;
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kCols; ++i) {
            float rr, gg, bb;
            hsvToRgb(m_h, static_cast<float>(i) / (kCols - 1),
                     1.0f - static_cast<float>(j) / (kRows - 1), rr, gg, bb);
            Color c;
            c.r = static_cast<uint8_t>(rr * 255.0f);
            c.g = static_cast<uint8_t>(gg * 255.0f);
            c.b = static_cast<uint8_t>(bb * 255.0f);
            r.drawRect({sv.x + static_cast<float>(i) * cw,
                        sv.y + static_cast<float>(j) * ch, cw + 1.0f, ch + 1.0f}, c);
        }
    }
    r.drawRectBorder(sv, m_border, 1.0f, 0.0f);
    {
        const float px = sv.x + m_s * sv.w;
        const float py = sv.y + (1.0f - m_v) * sv.h;
        r.drawRectBorder({px - 4.0f, py - 4.0f, 8.0f, 8.0f},
                         hexColor("#ffffff"), 1.5f, 4.0f);
    }

    // Hue strip.
    const Rect hue = hueRect();
    constexpr int kSeg = 12;
    for (int i = 0; i < kSeg; ++i) {
        float rr, gg, bb;
        hsvToRgb(static_cast<float>(i) / kSeg, 1.0f, 1.0f, rr, gg, bb);
        Color c;
        c.r = static_cast<uint8_t>(rr * 255.0f);
        c.g = static_cast<uint8_t>(gg * 255.0f);
        c.b = static_cast<uint8_t>(bb * 255.0f);
        r.drawRect({hue.x + static_cast<float>(i) * hue.w / kSeg, hue.y,
                    hue.w / kSeg + 1.0f, hue.h}, c);
    }
    r.drawRectBorder(hue, m_border, 1.0f, 2.0f);
    {
        const float px = hue.x + m_h * hue.w;
        r.drawRect({px - 2.0f, hue.y - 2.0f, 4.0f, hue.h + 4.0f},
                   hexColor("#ffffff"));
    }

    // Current colour + hex label.
    float rr, gg, bb;
    hsvToRgb(m_h, m_s, m_v, rr, gg, bb);
    Color cur;
    cur.r = static_cast<uint8_t>(rr * 255.0f);
    cur.g = static_cast<uint8_t>(gg * 255.0f);
    cur.b = static_cast<uint8_t>(bb * 255.0f);
    const Rect chip{b.x + 8.0f, b.y + b.h - 24.0f, 16.0f, 16.0f};
    r.drawRect(chip, cur);
    r.drawRectBorder(chip, m_border, 1.0f, 2.0f);
    char hex[8];
    std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", cur.r, cur.g, cur.b);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (f)
        r.drawTextShifted(hex, chip.x + 22.0f, b.y + b.h - 22.0f,
                          m_fontSize, *f, hexColor("#cccccc"));
}

ColorPicker::ColorPicker() = default;
ColorPicker::~ColorPicker() = default;

void ColorPicker::setColorHex(const std::string& hex) {
    m_color = hexColor(hex);
    // rgb -> hsv so the popup opens on the right spot.
    const float r = m_color.r / 255.0f, g = m_color.g / 255.0f,
                b = m_color.b / 255.0f;
    const float mx = std::max({r, g, b}), mn = std::min({r, g, b});
    const float d = mx - mn;
    float h = 0.0f;
    if (d > 0.0f) {
        if (mx == r) h = std::fmod((g - b) / d, 6.0f);
        else if (mx == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    m_h = h;
    m_s = mx > 0.0f ? d / mx : 0.0f;
    m_v = mx;
}

std::string ColorPicker::colorHex() const {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", m_color.r, m_color.g,
                  m_color.b);
    return buf;
}

void ColorPicker::applyHsv() {
    float rr, gg, bb;
    ColorPopup::hsvToRgb(m_h, m_s, m_v, rr, gg, bb);
    m_color.r = static_cast<uint8_t>(rr * 255.0f);
    m_color.g = static_cast<uint8_t>(gg * 255.0f);
    m_color.b = static_cast<uint8_t>(bb * 255.0f);
}

void ColorPicker::onPointerDown(MouseButton btn, const Offset2D&) {
    if (btn != Mouse_Left) return;
    if (!m_popup) m_popup = std::make_shared<ColorPopup>();
    m_popup->setHSV(m_h, m_s, m_v);
    m_popup->setVisible(true);
    m_popup->setOnChanged([this](float h, float s, float v) {
        m_h = h; m_s = s; m_v = v;
        applyHsv();
        if (m_onChanged) m_onChanged(colorHex());
        if (Kernel* k = kernel()) k->invokeBinding(binding());
    });
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    m_popup->setBounds(Rect{o.x, o.y + b.h + 2.0f, 180.0f, 170.0f});
    if (Kernel* k = kernel()) k->openPopup(m_popup);
}

void ColorPicker::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    const Rect chip{b.x + 4.0f, b.y + (b.h - 18.0f) * 0.5f, 18.0f, 18.0f};
    r.drawRect(chip, m_color);
    r.drawRectBorder(chip, hexColor("#666666"), 1.0f, 3.0f);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (f) {
        const std::string hex = colorHex();
        r.drawTextShifted(hex.c_str(), chip.x + 26.0f,
                          b.y + (b.h - static_cast<float>(m_fontSize)) * 0.5f,
                          m_fontSize, *f, hexColor("#cccccc"));
    }
}

// ------------------------------------------------------------------
// PathPicker
// ------------------------------------------------------------------
PathPicker::PathPicker() = default;
PathPicker::~PathPicker() = default;

void PathPicker::listDir(const std::string& dir) {
    std::vector<MenuEntry> entries;
    if (!m_popup) m_popup = std::make_shared<MenuPopup>();

    if (dir != "/" && !dir.empty()) {
        MenuEntry up;
        up.label = "..";
        up.command = "d:..";
        entries.push_back(up);
    }

    std::error_code ec;
    std::vector<std::string> dirs, files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (e.is_directory(ec)) dirs.push_back(name);
        else if (e.is_regular_file(ec)) {
            if (!m_filter.empty() &&
                e.path().extension().string() != m_filter)
                continue;
            files.push_back(name);
        }
        if (dirs.size() + files.size() >= 40) break;   // keep the menu sane
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    for (const auto& d : dirs) {
        if (entries.size() >= 40) break;
        MenuEntry e;
        e.label = d + "/";
        e.command = "d:" + d;
        entries.push_back(e);
    }
    for (const auto& fl : files) {
        if (entries.size() >= 40) break;
        MenuEntry e;
        e.label = fl;
        e.command = "f:" + fl;
        entries.push_back(e);
    }

    m_popup->setEntries(entries);
    m_popup->fadeIn();
    m_popup->setVisible(true);
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const float h = std::min(m_popup->contentHeight(), 340.0f);
    m_popup->setBounds(Rect{o.x, o.y + b.h + 2.0f,
                            std::max(b.w, 220.0f), h});
    m_popup->setOnSelect([this, dir](const std::string& cmd) {
        if (!cmd.empty() && cmd[0] == 'd' && cmd.size() > 1 && cmd[1] == ':') {
            std::string next;
            if (cmd.substr(2) == "..") {
                next = dir;
                while (next.size() > 1 && next.back() != '/') next.pop_back();
                if (next.size() > 1) next.pop_back();
            } else {
                next = dir;
                if (!next.empty() && next.back() != '/') next += '/';
                next += cmd.substr(2);
            }
            listDir(next);   // stay open, show the new folder
            return;
        }
        if (!cmd.empty() && cmd[0] == 'f' && cmd.size() > 1 && cmd[1] == ':') {
            std::string path = dir;
            if (!path.empty() && path.back() != '/') path += '/';
            path += cmd.substr(2);
            m_value = path;
            if (Kernel* k = kernel()) k->closePopup();
            if (m_popup) m_popup->setVisible(false);
            if (m_onChanged) m_onChanged(m_value);
            if (Kernel* k = kernel()) k->invokeBinding(binding());
        }
    });
    if (Kernel* k = kernel()) k->openPopup(m_popup);
}

void PathPicker::openBrowser() {
    std::string dir = m_start;
    if (!m_value.empty()) {
        std::filesystem::path p(m_value);
        if (p.has_parent_path()) dir = p.parent_path().string();
    }
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec)) dir = ".";
    listDir(dir);
}

void PathPicker::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    const float lx = static_cast<float>(p.x) - o.x + b.x;
    const Rect button{b.x + b.w - 30.0f, b.y + 2.0f, 28.0f, b.h - 4.0f};
    if (lx >= button.x && lx <= button.x + button.w) {
        openBrowser();
        return;
    }
    (void)p;
}

void PathPicker::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);
    r.drawRectBorder(b, m_borderColor, 1.0f, 4.0f);
    const Font* f = r.resolveFont("sans", m_fontSize);
    if (!f) return;
    const float ty = b.y + (b.h - static_cast<float>(m_fontSize)) * 0.5f;
    if (!m_value.empty()) {
        r.drawTextShifted(m_value.c_str(), b.x + 8.0f, ty, m_fontSize, *f,
                          m_fgColor);
    } else if (!m_placeholder.empty()) {
        Color dim = m_fgColor;
        dim.a = 120;
        r.drawTextShifted(m_placeholder.c_str(), b.x + 8.0f, ty, m_fontSize,
                          *f, dim);
    }
    const Rect button{b.x + b.w - 30.0f, b.y + 2.0f, 28.0f, b.h - 4.0f};
    r.drawRect(button, hexColor("#2f3340"));
    r.drawText("...", button.x + button.w * 0.5f,
               button.y + button.h * 0.5f, m_fontSize, "sans",
               hexColor("#dddddd"), Align::Center, VertAlign::Middle);
}

// ------------------------------------------------------------------
// RampEditor
// ------------------------------------------------------------------
RampEditor::RampEditor() = default;
RampEditor::~RampEditor() = default;

void RampEditor::setStops(const std::vector<Stop>& stops) {
    m_stops = stops;
    std::sort(m_stops.begin(), m_stops.end(),
              [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
}

bool RampEditor::setStopsFromSpec(const std::string& spec) {
    std::vector<Stop> out;
    size_t i = 0;
    while (i < spec.size()) {
        while (i < spec.size() && std::isspace(static_cast<unsigned char>(spec[i]))) ++i;
        size_t j = i;
        while (j < spec.size() && !std::isspace(static_cast<unsigned char>(spec[j]))) ++j;
        if (j > i) {
            const std::string tok = spec.substr(i, j - i);
            const size_t colon = tok.find(':');
            if (colon == std::string::npos) return false;
            Stop s;
            s.pos = std::clamp(static_cast<float>(std::atof(tok.substr(0, colon).c_str())), 0.0f, 1.0f);
            s.color = hexColor(tok.substr(colon + 1));
            out.push_back(s);
        }
        i = j;
    }
    if (out.empty()) return false;
    setStops(out);
    return true;
}

void RampEditor::clear() {
    m_stops.clear();
}

Color RampEditor::sample(float pos) const {
    if (m_stops.empty()) return hexColor("#ffffff");
    std::vector<Stop> sorted = m_stops;
    std::sort(sorted.begin(), sorted.end(),
              [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
    pos = std::clamp(pos, 0.0f, 1.0f);
    if (pos <= sorted.front().pos) return sorted.front().color;
    if (pos >= sorted.back().pos) return sorted.back().color;
    for (size_t i = 1; i < sorted.size(); ++i) {
        if (pos <= sorted[i].pos) {
            const Stop& a = sorted[i - 1];
            const Stop& b = sorted[i];
            const float span = b.pos - a.pos;
            const float t = span > 0.0f ? (pos - a.pos) / span : 0.0f;
            Color c;
            c.r = static_cast<uint8_t>(a.color.r + (b.color.r - a.color.r) * t);
            c.g = static_cast<uint8_t>(a.color.g + (b.color.g - a.color.g) * t);
            c.b = static_cast<uint8_t>(a.color.b + (b.color.b - a.color.b) * t);
            c.a = 255;
            return c;
        }
    }
    return sorted.back().color;
}

void RampEditor::notify() {
    if (m_onChanged) m_onChanged();
}

void RampEditor::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f) return;
    const float lx = static_cast<float>(p.x) - o.x;   // 0-based local
    // Grab the nearest existing stop within a small radius, else add one.
    int best = -1;
    float bestDist = 8.0f;
    for (int i = 0; i < static_cast<int>(m_stops.size()); ++i) {
        const float tx = m_stops[i].pos * b.w;
        const float d = std::fabs(tx - lx);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best >= 0) {
        m_drag = best;
    } else {
        Stop s;
        s.pos = std::clamp(lx / b.w, 0.0f, 1.0f);
        s.color = sample(s.pos);
        m_stops.push_back(s);
        m_drag = static_cast<int>(m_stops.size()) - 1;
        notify();
    }
}

void RampEditor::onPointerUp(MouseButton) {
    if (m_drag >= 0) {
        m_drag = -1;
        std::sort(m_stops.begin(), m_stops.end(),
                  [](const Stop& a, const Stop& b) { return a.pos < b.pos; });
        notify();
    }
}

void RampEditor::onPointerMove(const Offset2D& p) {
    if (m_drag < 0 || m_drag >= static_cast<int>(m_stops.size())) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f) return;
    const float lx = static_cast<float>(p.x) - o.x;
    m_stops[m_drag].pos = std::clamp(lx / b.w, 0.0f, 1.0f);
}

void RampEditor::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    // Gradient bar: one thin vertical slice per step, colour sampled at centre.
    constexpr float kStep = 3.0f;
    for (float x = 0.0f; x < b.w; x += kStep) {
        const float frac = (x + kStep * 0.5f) / b.w;
        r.drawRect(Rect{b.x + x, b.y, kStep + 1.0f, b.h}, sample(frac));
    }
    r.drawRectBorder(b, m_borderColor, 1.0f, 0.0f);
    // Stop markers: a tick down the bar plus a filled diamond at the base.
    for (const Stop& s : m_stops) {
        const float cx = b.x + s.pos * b.w;
        r.drawLine({cx, b.y}, {cx, b.y + b.h - 6.0f}, hexColor("#00000080"), 1.0f);
        const float cy = b.y + b.h - 5.0f;
        const float k = 5.0f;
        r.drawTriangle({cx - k, cy}, {cx, cy - k}, {cx + k, cy}, s.color);
        r.drawTriangle({cx - k, cy}, {cx, cy + k}, {cx + k, cy}, s.color);
        r.drawRectBorder(Rect{cx - k, cy - k, k * 2.0f, k * 2.0f}, m_thumbColor, 1.0f, 0.0f);
    }
}

// ------------------------------------------------------------------
// CurveEditor
// ------------------------------------------------------------------
CurveEditor::CurveEditor() = default;
CurveEditor::~CurveEditor() = default;

void CurveEditor::setPoints(const Point pts[4]) {
    for (int i = 0; i < 4; ++i) {
        m_pts[i].x = std::clamp(pts[i].x, 0.0f, 1.0f);
        m_pts[i].y = std::clamp(pts[i].y, 0.0f, 1.0f);
    }
}

bool CurveEditor::setPointsFromSpec(const std::string& spec) {
    Point tmp[4];
    int n = 0;
    size_t i = 0;
    while (i < spec.size() && n < 4) {
        while (i < spec.size() && (std::isspace(static_cast<unsigned char>(spec[i])) || spec[i] == ',')) ++i;
        size_t c = spec.find(',', i);
        if (c == std::string::npos) break;
        tmp[n].x = std::atof(spec.substr(i, c - i).c_str());
        size_t d = spec.find_first_of(" ,", c + 1);
        if (d == std::string::npos) d = spec.size();
        tmp[n].y = std::atof(spec.substr(c + 1, d - c - 1).c_str());
        ++n;
        i = d;
    }
    if (n != 4) return false;
    setPoints(tmp);
    return true;
}

float CurveEditor::evaluate(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    const float mt = 1.0f - t;
    const float y = mt * mt * mt * m_pts[0].y
                  + 3.0f * mt * mt * t * m_pts[1].y
                  + 3.0f * mt * t * t * m_pts[2].y
                  + t * t * t * m_pts[3].y;
    return std::clamp(y, 0.0f, 1.0f);
}

void CurveEditor::notify() {
    if (m_onChanged) m_onChanged();
}

void CurveEditor::onPointerDown(MouseButton btn, const Offset2D& p) {
    if (btn != Mouse_Left) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;
    const float px = static_cast<float>(p.x) - o.x;   // 0-based local pixels
    const float py = static_cast<float>(p.y) - o.y;
    int best = -1;
    float bestDist = 10.0f;
    for (int i = 0; i < 4; ++i) {
        const float hx = m_pts[i].x * b.w;
        const float hy = (1.0f - m_pts[i].y) * b.h;
        const float d = std::sqrt((hx - px) * (hx - px) + (hy - py) * (hy - py));
        if (d < bestDist) { bestDist = d; best = i; }
    }
    m_drag = best;
}

void CurveEditor::onPointerUp(MouseButton) {
    if (m_drag >= 0) { m_drag = -1; notify(); }
}

void CurveEditor::onPointerMove(const Offset2D& p) {
    if (m_drag < 0) return;
    const Offset2D o = absoluteOrigin();
    const Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;
    m_pts[m_drag].x = std::clamp((static_cast<float>(p.x) - o.x) / b.w, 0.0f, 1.0f);
    m_pts[m_drag].y = std::clamp(1.0f - (static_cast<float>(p.y) - o.y) / b.h, 0.0f, 1.0f);
}

void CurveEditor::onDraw(Rasterizer& r) const {
    if (!m_style.isVisible) return;
    const Rect b = bounds();
    r.drawRect(b, m_bgColor);

    auto toPx = [&](const Point& pt) {
        return Offset2D{ b.x + pt.x * b.w, b.y + (1.0f - pt.y) * b.h };
    };

    // Unit grid at quarter steps.
    for (int i = 1; i < 4; ++i) {
        const float gx = b.x + b.w * (i / 4.0f);
        const float gy = b.y + b.h * (i / 4.0f);
        r.drawLine({gx, b.y}, {gx, b.y + b.h}, m_gridColor, 1.0f);
        r.drawLine({b.x, gy}, {b.x + b.w, gy}, m_gridColor, 1.0f);
    }

    // Control handles P0-P1 and P2-P3.
    r.drawLine(toPx(m_pts[0]), toPx(m_pts[1]), m_handleColor, 1.0f);
    r.drawLine(toPx(m_pts[2]), toPx(m_pts[3]), m_handleColor, 1.0f);

    // The cubic Bezier, sampled as short segments.
    Point prev = m_pts[0];
    for (int s = 1; s <= 48; ++s) {
        const float t = s / 48.0f;
        const float mt = 1.0f - t;
        Point cur;
        cur.x = mt * mt * mt * m_pts[0].x + 3.0f * mt * mt * t * m_pts[1].x
              + 3.0f * mt * t * t * m_pts[2].x + t * t * t * m_pts[3].x;
        cur.y = mt * mt * mt * m_pts[0].y + 3.0f * mt * mt * t * m_pts[1].y
              + 3.0f * mt * t * t * m_pts[2].y + t * t * t * m_pts[3].y;
        r.drawLine(toPx(prev), toPx(cur), m_curveColor, 2.0f);
        prev = cur;
    }

    // Draggable nodes: endpoints cyan, control points yellow.
    for (int i = 0; i < 4; ++i) {
        const Offset2D c = toPx(m_pts[i]);
        const bool end = (i == 0 || i == 3);
        const float k = (m_drag == i) ? 6.0f : 4.0f;
        r.drawRect(Rect{c.x - k, c.y - k, k * 2.0f, k * 2.0f},
                   end ? m_endColor : m_ctrlColor);
    }
    r.drawRectBorder(b, hexColor("#3a3b44"), 1.0f, 0.0f);
}

} // namespace ogui
