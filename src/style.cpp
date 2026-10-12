#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include <cstring>

namespace ogui {

// ---- Style helpers --------------------------------------------------------
Style* Style::setFont(const std::string& family, int size) {
    fontFamily = family;
    fontSize = size;
    return this;
}

Style* Style::setColor(const std::string& hex, Color* out) {
    Color c = hexColor(hex);
    if (out) *out = c;
    return this;
}

} // namespace ogui
