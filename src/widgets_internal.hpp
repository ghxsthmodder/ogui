#pragma once
// Internal shared helpers for the widgets translation units. Not a public
// header: it exists so the widgets_*.cpp files stay small and agree on the
// UTF-8 caret/word utilities the text-capable widgets share.
#include "ogui/widgets.hpp"
#include "ogui/clipboard.hpp"
#include "ogui/imagemanager.hpp"
#include "ogui/rasterizer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <filesystem>

namespace ogui {

    // Caret positions live in std::string byte offsets, so every move/edit
    // has to land on a UTF-8 boundary or it splits an accented character
    // in half and leaves stray continuation bytes in the text.
    inline size_t utf8PrevBoundary(const std::string& s, size_t i) {
        if (i == 0) return 0;
        size_t j = i - 1;
        while (j > 0 && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) --j;
        return j;
    }

    inline size_t utf8NextBoundary(const std::string& s, size_t i) {
        if (i >= s.size()) return s.size();
        size_t j = i + 1;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80) ++j;
        return j;
    }

    // Horizontal padding between the frame and the text (onDraw/layout agree
    // on this, so clicks land on the same glyph the caret renders at).
    inline constexpr float kTextPadX = 6.0f;

    // Word units for double-click selection and Ctrl+arrow caret moves.
    // Bytes >= 0x80 are UTF-8 lead/continuation bytes: keeping them as word
    // bytes makes every boundary decision land on ASCII, so a "word" slice
    // can never split an accented character.
    inline bool isWordByte(unsigned char c) {
        return c >= 0x80 || std::isalnum(c) != 0 || c == '_';
    }

    // [begin,end) of the word containing or adjacent to pos; empty when the
    // click sits on a separator with no word to grab.
    inline std::pair<size_t, size_t> wordRange(const std::string& s, size_t pos) {
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

    inline size_t wordPrevBoundary(const std::string& s, size_t pos) {
        size_t i = pos;
        while (i > 0 && !isWordByte(static_cast<unsigned char>(s[i - 1]))) --i;
        while (i > 0 && isWordByte(static_cast<unsigned char>(s[i - 1]))) --i;
        return i;
    }

    inline size_t wordNextBoundary(const std::string& s, size_t pos) {
        size_t i = pos;
        while (i < s.size() && isWordByte(static_cast<unsigned char>(s[i]))) ++i;
        while (i < s.size() && !isWordByte(static_cast<unsigned char>(s[i]))) ++i;
        return i;
    }

    // Vertical scrollbar geometry shared by the scrollable list widgets:
    // an 8px rounded bar overlaying the right edge of the viewport.
    inline constexpr float kBarW = 8.0f;
    inline constexpr float kBarMargin = 2.0f;
    inline constexpr float kBarMinThumb = 16.0f;

    inline float barThumbH(float trackH, float viewH, float total) {
        if (total <= 0.0f) return trackH;
        return std::clamp(trackH * (viewH / total), kBarMinThumb, trackH);
    }

    // Scroll position -> thumb rect inside track (absolute coordinates).
    inline Rect barThumbRect(const Rect& trk, float viewH, float total, float scroll) {
        if (trk.h <= 0.0f) return trk;
        const float thumbH = barThumbH(trk.h, viewH, total);
        const float maxScroll = std::max(total - viewH, 0.0f);
        const float frac = maxScroll > 0.0f ? scroll / maxScroll : 0.0f;
        return Rect{trk.x, trk.y + frac * (trk.h - thumbH), trk.w, thumbH};
    }

    // Pointer y + grab offset -> the scroll value that puts the thumb there.
    inline float barScrollAt(const Rect& trk, float viewH, float total,
                             float ly, float grab) {
        const float maxScroll = std::max(total - viewH, 0.0f);
        if (trk.h <= 0.0f || total <= 0.0f || maxScroll <= 0.0f) return 0.0f;
        const float span = trk.h - barThumbH(trk.h, viewH, total);
        if (span <= 0.0f) return 0.0f;
        return std::clamp((ly - grab - trk.y) / span, 0.0f, 1.0f) * maxScroll;
    }

    // Bars are hit-tested in the widget's LOCAL space (pointer minus
    // absoluteOrigin) but drawn while the Kernel has shifted bounds() into
    // absolute space; translate the local rects by the shifted origin.
    inline Rect offsetRect(const Rect& r, float dx, float dy) {
        return Rect{r.x + dx, r.y + dy, r.w, r.h};
    }

    inline void drawVerticalBar(Rasterizer& r, const Rect& trk, const Rect& thumb,
                                bool active) {
        Color track = palette().surface;
        track.a = 90;
        r.drawRoundedRect(trk, track, trk.w * 0.5f);
        Color knob = palette().borderStrong;
        knob.a = active ? 240 : 170;
        r.drawRoundedRect(thumb, knob, thumb.w * 0.5f);
    }

} // namespace ogui
