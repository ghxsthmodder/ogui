#include "ogui/clipboard.hpp"

namespace ogui {

Clipboard& Clipboard::instance() {
    static Clipboard clip;
    return clip;
}

void Clipboard::setHandlers(Get get, Set set) {
    m_get = std::move(get);
    m_set = std::move(set);
}

std::string Clipboard::text() const {
    if (m_get) return m_get();
    return m_fallback;
}

void Clipboard::setText(const std::string& text) {
    if (m_set) {
        m_set(text);
        return;
    }
    m_fallback = text;
}

} // namespace ogui
