#pragma once
// Internal shared helpers for the widgets translation units. Not a public
// header: it exists so the widgets_*.cpp files stay small and agree on the
// UTF-8 caret/word utilities the text-capable widgets share.
#include "ogui/widgets.hpp"
#include "ogui/clipboard.hpp"
#include "ogui/imagemanager.hpp"
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

} // namespace ogui
