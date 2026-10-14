#include "ogui/xmlparser.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"
#include "ogui/font.hpp"

#include <fstream>
#include <cctype>
#include <algorithm>

namespace ogui {

// Colour attribute value: "#rrggbb" literals, or "@name" resolved against the
// active theme palette (unknown names silently fall back to the token default,
// which Palette::token already handles by returning window).
static Color colorOf(const std::string& v) {
    if (!v.empty() && v[0] == '@') return palette().token(v.substr(1));
    return hexColor(v);
}

namespace {
// Defined below with the other attribute helpers; needed by <default>.
void parseFontAttr(const std::string& v, std::string& family, int& size);
bool toFloat(const std::string& s, float& out);
} // namespace

// ------------------------------------------------------------------
// XmlParser
// ------------------------------------------------------------------
XmlParser::XmlParser() = default;
XmlParser::~XmlParser() = default;

void XmlParser::parseDocument() {
    skipWhitespace();

    // Find the root element. <screen> is the normal layout root; <manifest>
    // is a data-only list of screens; <theme> is a stylesheet (only valid as
    // the root of a parseTheme() document). XML declaration / BOM / comments
    // are skipped by skipWhitespace().
    std::string rootTag;
    if (m_pos + 2 <= m_len && m_text[m_pos] == '<' && m_text[m_pos + 1] != '/') {
        const size_t save = m_pos + 1;
        m_pos = save;
        rootTag = parseTagName();
        m_pos = save - 1;
    }
    const bool themeRoot = m_themeTarget && rootTag == "theme";
    if (rootTag != "screen" && rootTag != "manifest" && !themeRoot) {
        // No recognised root tag; treat entire text as a label.
        std::string txt = parseChars();
        m_root = createWidget("label", Style{}, {});
        if (m_root) {
            if (auto lbl = static_cast<Label*>(m_root.get())) {
                lbl->setText(txt);
            }
        }
        return;
    }
    m_pos += 1 + rootTag.size(); // skip '<' + tag name

    // Parse attributes of the root element.
    std::map<std::string, std::string> attrs = parseAttributes();

    // Consume the '>' that ends the start tag (or the "/>" of a self-closing
    // root); otherwise the child loop below would spin forever on it.
    bool rootSelfClosing = false;
    if (m_pos < m_len && m_text[m_pos] == '/') {
        rootSelfClosing = true;
        ++m_pos;
        while (m_pos < m_len && m_text[m_pos] != '>') ++m_pos;
    }
    if (m_pos < m_len && m_text[m_pos] == '>') ++m_pos;

    // Default id for the root element.
    if (attrs.find("id") == attrs.end()) {
        attrs["id"] = rootTag;
    }

    // create the root widget
    std::shared_ptr<Widget> root;
    Style base = m_theme ? m_theme->defaultStyle : Style{};
    base.isVisible = true;
    if (themeRoot) {
        // <theme base="dark|light"> re-seeds the stylesheet over a preset.
        const auto b = attrs.find("base");
        if (b != attrs.end() && (b->second == "light" || b->second == "claro"))
            *m_themeTarget = lightTheme();
        else if (b != attrs.end() && (b->second == "dark" || b->second == "escuro"))
            *m_themeTarget = darkTheme();
        root = std::make_shared<Widget>();
        root->setId(attrs["id"]);
    } else {
        root = createWidget(rootTag, base, attrs);
        if (!root) {
            // Unknown root tag: report it, then fall back to a plain container
            // so callers always get a usable tree instead of nullptr.
            addError("unknown root element <" + rootTag + ">");
            root = std::make_shared<Widget>();
            root->setId(attrs["id"]);
        }
    }
    root->setRawAttributes(attrs);

    if (rootSelfClosing) {
        m_root = root;
        return;
    }

    // push the root onto the stack
    m_stack.push_back({root, rootTag, base, attrs, false, std::string()});

    // parse children until </rootTag>
    const std::string closeTag = "</" + rootTag + ">";
    while (m_pos < m_len) {
        skipWhitespace();
        if (m_pos + closeTag.size() <= m_len &&
            m_text.compare(m_pos, closeTag.size(), closeTag) == 0) {
            m_pos += closeTag.size();
            break;
        }
        parseElement();
    }

    // Close any elements left open (unbalanced document) before popping.
    while (m_stack.size() > 1) {
        m_stack.pop_back();
    }
    if (!m_stack.empty()) m_stack.pop_back();
    m_root = root;
}

void XmlParser::parseElement() {
    skipWhitespace();

    if (m_pos + 2 <= m_len && m_text[m_pos] == '<' && m_text[m_pos+1] == '/') {
        // closing tag: skip '</', the name, then the '>'
        m_pos += 2;
        std::string tag = parseTagName();
        while (m_pos < m_len && m_text[m_pos] != '>') ++m_pos;
        if (m_pos < m_len) ++m_pos;
        parseClosing(tag);
        return;
    }

    if (m_pos < m_len && m_text[m_pos] == '<') {
        ++m_pos; // skip '<'
    } else {
        return;
    }

    std::string tag = parseTagName();
    std::map<std::string, std::string> attrs = parseAttributes();

    bool selfClosing = false;
    if (m_pos < m_len && m_text[m_pos] == '/') {
        selfClosing = true;
        ++m_pos;
    }
    // Consume the '>' ending the start tag (self-closing or not).
    if (m_pos < m_len && m_text[m_pos] == '>') ++m_pos;

    // <style name="..."> defines a reusable style; it is not a widget.
    if (tag == "style") {
        parseStyleDef(attrs, selfClosing);
        return;
    }

    // <font family="..." source="file.ttf"> registers a font family with the
    // FontManager; like <style> it is data-only and yields no widget.
    if (tag == "font") {
        auto find = [&](const char* k) -> std::string {
            auto it = attrs.find(k);
            return it == attrs.end() ? std::string() : it->second;
        };
        const std::string family = find("family");
        std::string path = find("source");
        if (path.empty()) path = find("path");
        if (!family.empty() && !path.empty())
            FontManager::instance().setFontPath(family, path);
        else
            addError("font: family and source are required");
        return;
    }

    // <default .../> restyles the theme's base Style (only inside <theme>).
    if (tag == "default") {
        if (!m_themeTarget) {
            addError("default: only valid inside a <theme> stylesheet");
            skipElement(tag, selfClosing);
            return;
        }
        Style& d = m_themeTarget->defaultStyle;
        if (auto v = attrs.find("font"); v != attrs.end())
            parseFontAttr(v->second, d.fontFamily, d.fontSize);
        if (auto v = attrs.find("fontsize"); v != attrs.end()) {
            float f = 0.0f;
            if (toFloat(v->second, f)) d.fontSize = static_cast<int>(f);
        }
        if (auto v = attrs.find("color"); v != attrs.end()) d.textColor = colorOf(v->second);
        if (auto v = attrs.find("background"); v != attrs.end()) d.backgroundColor = colorOf(v->second);
        if (auto v = attrs.find("border"); v != attrs.end()) { d.borderColor = colorOf(v->second); d.borderWidth = 1.0f; }
        if (auto v = attrs.find("borderwidth"); v != attrs.end()) toFloat(v->second, d.borderWidth);
        if (auto v = attrs.find("radius"); v != attrs.end()) toFloat(v->second, d.radius);
        if (!selfClosing) skipElement(tag, false);
        return;
    }

    // <palette .../> re-colours the theme being built (only inside <theme>).
    if (tag == "palette") {
        if (!m_themeTarget) {
            addError("palette: only valid inside a <theme> stylesheet");
            skipElement(tag, selfClosing);
            return;
        }
        for (const auto& kv : attrs) {
            if (kv.first == "id") continue;
            if (!m_themeTarget->palette.setToken(kv.first, colorOf(kv.second)))
                addWarning("palette: unknown token \"" + kv.first + "\"");
        }
        if (!selfClosing) skipElement(tag, false);
        return;
    }

    // <stylesheet path="..."/> pulls an external <theme> file into this
    // document: its named styles merge into the parser, its <font> entries
    // register, and its <palette> (if any) is installed app-wide.
    if (tag == "stylesheet") {
        auto it = attrs.find("path");
        if (it == attrs.end() || it->second.empty()) it = attrs.find("source");
        if (it == attrs.end() || it->second.empty()) {
            addError("stylesheet: path is required");
            skipElement(tag, selfClosing);
            return;
        }
        XmlParser sub;
        ThemeResult tr = sub.parseThemeFile(it->second, activeTheme());
        for (const auto& e : tr.errors)
            addError("stylesheet " + it->second + ": " + e);
        for (const auto& [name, props] : tr.theme.namedStyles)
            m_namedStyles[name] = props;
        for (const auto& [tag2, props] : tr.theme.elementStyles)
            m_elementStyles[tag2] = props;
        // Install the (possibly palette-edited) theme so @tokens downstream
        // resolve against it. Identity with activeTheme() is a no-op.
        setActiveTheme(tr.theme);
        if (m_theme) m_theme = &activeTheme();
        if (!selfClosing) skipElement(tag, false);
        return;
    }

    // <script src="file.lua"/> (or inline <script>code</script>) hands a
    // Lua chunk to the host's script handler; the parser runs nothing.
    if (tag == "script") {
        auto it = attrs.find("src");
        if (it == attrs.end() || it->second.empty()) it = attrs.find("path");
        if (it != attrs.end() && !it->second.empty()) {
            if (m_result) m_result->scripts.emplace_back(it->second, true);
            if (m_scriptHandler) m_scriptHandler(it->second, true);
            if (!selfClosing) skipElement(tag, false);
            return;
        }
        if (selfClosing) {
            addError("script: src attribute or inline code is required");
            return;
        }
        // Inline body: captured raw up to </script> (Lua may contain <, >,
        // &; no entity decoding applies).
        const size_t end = m_text.find("</script>", m_pos);
        if (end == std::string::npos) {
            addError("script: unterminated inline <script>");
            m_pos = m_len;
            return;
        }
        const std::string code = m_text.substr(m_pos, end - m_pos);
        m_pos = end + 9;   // strlen("</script>")
        while (m_pos < m_len && m_text[m_pos] != '>') ++m_pos;
        if (m_pos < m_len) ++m_pos;
        if (m_result) m_result->scripts.emplace_back(code, false);
        if (m_scriptHandler) m_scriptHandler(code, false);
        return;
    }

    auto widget = createWidget(tag,
                               m_stack.empty() ? Style{} : m_stack.back().style,
                               attrs);
    if (!widget) {
        // Unknown tag: report it and skip the whole element so the rest of
        // the document still parses.
        addError("unknown element <" + tag + ">");
        skipElement(tag, selfClosing);
        return;
    }
    // Keep the raw attribute map so data-only tags can be read back.
    widget->setRawAttributes(attrs);

    if (selfClosing) {
        // attach directly to parent
        if (!m_stack.empty()) m_stack.back().widget->addChild(widget);
        return;
    }

    // open element: attach to parent, push, fill, parse children, close
    if (!m_stack.empty() && m_stack.back().widget) {
        m_stack.back().widget->addChild(widget);
    }
    m_stack.push_back({widget, tag, m_stack.empty() ? Style{} : m_stack.back().style, attrs, false, std::string()});
    while (m_pos < m_len) {
        skipWhitespace();
        if (m_pos + 2 <= m_len && m_text[m_pos] == '<' && m_text[m_pos+1] == '/') {
            m_pos += 2;
            std::string close = parseTagName();
            // skip the '>' that terminates the closing tag
            while (m_pos < m_len && m_text[m_pos] != '>') ++m_pos;
            if (m_pos < m_len) ++m_pos;
            if (close == tag) {
                applyContent(m_stack.back().widget, m_stack.back().content);
                m_stack.pop_back();
                break;
            }
            // mismatched close: keep going (already consumed above)
        } else if (m_pos < m_len && m_text[m_pos] != '<') {
            // Raw text content (e.g. <cell>Zoe</cell>): collect up to the
            // next tag so leaf data widgets can read their value.
            while (m_pos < m_len && m_text[m_pos] != '<') {
                m_stack.back().content += m_text[m_pos];
                ++m_pos;
            }
        } else {
            parseElement();
        }
    }
}

void XmlParser::parseClosing(const std::string&) {
    if (!m_stack.empty()) m_stack.pop_back();
}

void XmlParser::skipElement(const std::string& tag, bool selfClosing) {
    if (selfClosing) return;   // start tag already consumed

    // Walk forward counting nested <tag ...> vs </tag> until balanced.
    int depth = 1;
    while (m_pos < m_len && depth > 0) {
        if (m_text[m_pos] != '<') { ++m_pos; continue; }

        bool closing = (m_pos + 1 < m_len && m_text[m_pos + 1] == '/');
        size_t nameStart = m_pos + (closing ? 2 : 1);
        size_t nameEnd = nameStart;
        while (nameEnd < m_len && m_text[nameEnd] != '>' && m_text[nameEnd] != ' ' &&
               m_text[nameEnd] != '/' && m_text[nameEnd] != '\n' &&
               m_text[nameEnd] != '\r' && m_text[nameEnd] != '\t') {
            ++nameEnd;
        }
        std::string name = m_text.substr(nameStart, nameEnd - nameStart);

        // Advance past the '>'.
        size_t gt = m_text.find('>', m_pos);
        if (gt == std::string::npos) { m_pos = m_len; return; }

        if (closing) {
            if (name == tag) --depth;
        } else {
            bool selfClose = false;
            for (size_t i = nameEnd; i < gt; ++i) {
                if (m_text[i] == '/') { selfClose = true; break; }
            }
            if (!selfClose && name == tag) ++depth;
        }
        m_pos = gt + 1;
    }
}

void XmlParser::skipWhitespace() {
    // Whitespace, comments, processing instructions and DOCTYPE are all
    // skipped at element boundaries so a commented-out block or a leading
    // `<?xml ...?>` never reaches parseElement() as a bogus tag.
    for (;;) {
        while (m_pos < m_len &&
               std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
            ++m_pos;
        }
        if (m_pos + 4 <= m_len && m_text.compare(m_pos, 4, "<!--") == 0) {
            size_t end = m_text.find("-->", m_pos + 4);
            if (end == std::string::npos) { m_pos = m_len; return; }
            m_pos = end + 3;
            continue;
        }
        if (m_pos + 2 <= m_len && m_text[m_pos] == '<' &&
            (m_text[m_pos + 1] == '?' || m_text[m_pos + 1] == '!')) {
            size_t end = m_text.find('>', m_pos);
            if (end == std::string::npos) { m_pos = m_len; return; }
            m_pos = end + 1;
            continue;
        }
        return;
    }
}

std::string XmlParser::parseUntil(char c) {
    size_t start = m_pos;
    while (m_pos < m_len && m_text[m_pos] != c) {
        if (m_text[m_pos] == '&') {
            if (m_pos + 3 < m_len && m_text[m_pos+1] == '#') {
                // incomplete entity; treat as literal
            }
        }
        ++m_pos;
    }
    return m_text.substr(start, m_pos - start);
}

std::string XmlParser::parseTagName() {
    skipWhitespace();
    size_t start = m_pos;
    while (m_pos < m_len && m_text[m_pos] != ' ' && m_text[m_pos] != '>' && m_text[m_pos] != '/') {
        ++m_pos;
    }
    return m_text.substr(start, m_pos - start);
}

std::map<std::string, std::string> XmlParser::parseAttributes() {
    std::map<std::string, std::string> attrs;
    skipWhitespace();
    // Stop at '>' or at the '/' of a self-closing tag; the '/' is left for
    // the caller so it can detect self-closing.
    while (m_pos < m_len && m_text[m_pos] != '>' && m_text[m_pos] != '/') {
        size_t start = m_pos;
        while (m_pos < m_len && m_text[m_pos] != '=' && m_text[m_pos] != ' ' &&
               m_text[m_pos] != '>' && m_text[m_pos] != '/' && m_text[m_pos] != '\n' &&
               m_text[m_pos] != '\r' && m_text[m_pos] != '\t') {
            ++m_pos;
        }
        std::string name = m_text.substr(start, m_pos - start);
        // Attribute names are matched case-insensitively (the proposal mixes
        // camelCase like scrollingText/editableCells with the lowercased keys
        // the rest of the parser already uses). Tag names stay exact.
        for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        skipWhitespace();

        if (m_pos < m_len && m_text[m_pos] == '=') {
            ++m_pos; // skip '='
            skipWhitespace();
            if (m_pos < m_len && m_text[m_pos] == '"') {
                ++m_pos;
                std::string val = parseUntil('"');
                if (m_pos < m_len && m_text[m_pos] == '"') ++m_pos;
                attrs[name] = val;
            } else {
                // unquoted value
                size_t vstart = m_pos;
                while (m_pos < m_len && m_text[m_pos] != ' ' && m_text[m_pos] != '>' &&
                       m_text[m_pos] != '/' && !std::isspace(static_cast<unsigned char>(m_text[m_pos]))) {
                    ++m_pos;
                }
                attrs[name] = m_text.substr(vstart, m_pos - vstart);
            }
        } else if (name == "font") {
            // Legacy pseudo-syntax (no '='):
            //   font sans 20 color "#66ee66"
            // Becomes font="sans" fontsize="20" color="#66ee66".
            auto readToken = [this]() -> std::string {
                skipWhitespace();
                size_t s = m_pos;
                while (m_pos < m_len && !std::isspace(static_cast<unsigned char>(m_text[m_pos])) &&
                       m_text[m_pos] != '>' && m_text[m_pos] != '/') {
                    ++m_pos;
                }
                return m_text.substr(s, m_pos - s);
            };

            std::string family = readToken();
            if (!family.empty()) attrs["font"] = family;

            skipWhitespace();
            if (m_pos < m_len && std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) {
                attrs["fontsize"] = readToken();
                skipWhitespace();
            }
            // Optional trailing: color "#rrggbb"
            if (m_pos + 5 <= m_len && m_text.compare(m_pos, 5, "color") == 0) {
                m_pos += 5;
                skipWhitespace();
                if (m_pos < m_len && m_text[m_pos] == '"') {
                    ++m_pos;
                    std::string val = parseUntil('"');
                    if (m_pos < m_len && m_text[m_pos] == '"') ++m_pos;
                    attrs["color"] = val;
                }
            }
        } else if (!name.empty()) {
            attrs[name] = "true";
        } else {
            // Defensive: no progress possible, bail out.
            break;
        }

        skipWhitespace();
    }
    return attrs;
}

std::string XmlParser::parseString() {
    size_t start = m_pos;
    while (m_pos < m_len && m_text[m_pos] != ' ') {
        ++m_pos;
    }
    return m_text.substr(start, m_pos - start);
}

std::string XmlParser::parseChars() {
    size_t start = m_pos;
    while (m_pos < m_len && m_text[m_pos] != '<') {
        ++m_pos;
    }
    return m_text.substr(start, m_pos - start);
}

std::shared_ptr<Widget> XmlParser::createWidget(const std::string& tag,
                                                const Style& style,
                                                const std::map<std::string, std::string>& attrs) {
    auto w = makeWidget(tag, style);
    if (!w) {
        return nullptr;
    }
    applyAttributes(w, tag, withNamedStyle(tag, attrs));
    return w;
}

// ---- <style name|element="..."> --------------------------------------------
void XmlParser::parseStyleDef(const std::map<std::string, std::string>& attrs,
                              bool selfClosing) {
    // <style element="button"> styles every button of the document; a named
    // <style name="p"> is opt-in through style="p". Element wins the target
    // lookup; name is the classic path.
    auto nameIt = attrs.find("name");
    auto elemIt = attrs.find("element");
    const bool isElement =
        (nameIt == attrs.end() || nameIt->second.empty()) &&
        elemIt != attrs.end() && !elemIt->second.empty();
    if (!isElement && (nameIt == attrs.end() || nameIt->second.empty())) {
        addWarning("<style> without a name or element attribute ignored");
        if (!selfClosing) skipElement("style", false);
        return;
    }
    std::map<std::string, std::string> props;
    for (const auto& kv : attrs) {
        if (kv.first == "name" || kv.first == "element") continue;
        props[kv.first] = kv.second;
    }
    auto& table = isElement ? m_elementStyles : m_namedStyles;
    const std::string key = isElement ? elemIt->second : nameIt->second;
    table[key] = std::move(props);

    // Non-self-closing <style> bodies may carry nested <set> declarations.
    if (!selfClosing) {
        while (m_pos < m_len) {
            skipWhitespace();
            if (m_pos + 8 <= m_len && m_text.compare(m_pos, 8, "</style>") == 0) {
                m_pos += 8;
                break;
            }
            if (m_pos + 4 <= m_len && m_text.compare(m_pos, 4, "<set") == 0) {
                m_pos += 4;
                auto setAttrs = parseAttributes();
                while (m_pos < m_len && m_text[m_pos] != '>') ++m_pos;
                if (m_pos < m_len) ++m_pos;
                for (const auto& kv : setAttrs) {
                    table[key][kv.first] = kv.second;
                }
            } else {
                // Unknown content inside <style>: skip one element/text run.
                size_t lt = m_text.find('<', m_pos);
                if (lt == std::string::npos) { m_pos = m_len; break; }
                m_pos = lt;
                if (m_pos + 2 <= m_len && m_text[m_pos + 1] == '/') {
                    skipElement("", false);   // closing tag: consume it
                    break;
                }
                size_t gt = m_text.find('>', m_pos);
                if (gt == std::string::npos) { m_pos = m_len; break; }
                m_pos = gt + 1;
            }
        }
    }
}

std::map<std::string, std::string> XmlParser::withNamedStyle(
    const std::string& tag,
    const std::map<std::string, std::string>& attrs) const {
    // Cascade: element style (lowest) -> named styles in list order -> the
    // element's own attributes (highest).
    std::map<std::string, std::string> merged;
    auto elem = m_elementStyles.find(tag);
    if (elem != m_elementStyles.end()) merged = elem->second;

    auto it = attrs.find("style");
    if (it != attrs.end() && !it->second.empty()) {
        std::string tok;
        auto flush = [this, &merged, &tok]() {
            if (tok.empty()) return;
            auto named = m_namedStyles.find(tok);
            if (named == m_namedStyles.end()) {
                addWarning("unknown style \"" + tok + "\"");
            } else {
                for (const auto& kv : named->second) merged[kv.first] = kv.second;
            }
            tok.clear();
        };
        for (char c : it->second) {
            if (c == ',' || c == ' ' || c == ';' || c == '|') flush();
            else tok.push_back(c);
        }
        flush();
    }

    for (const auto& kv : attrs) {
        if (kv.first == "style") continue;
        merged[kv.first] = kv.second;
    }
    return merged;
}

// ---- attribute application -------------------------------------------------
namespace {

bool toFloat(const std::string& s, float& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && *end == '\0';
}

bool toInt(const std::string& s, int& out) {
    float f = 0.0f;
    if (!toFloat(s, f)) return false;
    out = static_cast<int>(f);
    return true;
}

bool toBool(const std::string& s) {
    return s == "true" || s == "1" || s == "yes" || s == "on";
}

// Parses the legacy `font family size` pseudo-syntax used by early layouts:
//   font="sans 22"   -> family=sans, size=22
void parseFontAttr(const std::string& v, std::string& family, int& size) {
    size_t sp = v.find(' ');
    if (sp == std::string::npos) {
        family = v;
        return;
    }
    family = v.substr(0, sp);
    int sz = 0;
    if (toInt(v.substr(sp + 1), sz) && sz > 0) size = sz;
}

} // namespace

void XmlParser::applyAttributes(const std::shared_ptr<Widget>& w,
                                const std::string& tag,
                                const std::map<std::string, std::string>& attrs) {
    if (!w) return;

    auto get = [&](const char* key) -> const std::string* {
        auto it = attrs.find(key);
        return it == attrs.end() ? nullptr : &it->second;
    };
    // ---- identity ----
    if (const std::string* v = get("id")) w->setId(*v);
    if (const std::string* v = get("onclick")) w->setBinding(*v);
    if (const std::string* v = get("bind")) w->setBinding(*v);
    if (const std::string* v = get("contextmenu")) w->setContextMenuName(*v);
    if (const std::string* v = get("tooltip")) w->setTooltip(*v);

    // ---- geometry (layout happens later; bounds seed the widget) ----
    // x/y/w/h accept pixels, "NN%" (relative to the parent's content size)
    // or "fill" (w/h: take the whole parent content size). Percent and fill
    // components are re-resolved on every layout pass, so resizing the
    // screen/window reflows the tree.
    float x = 0, y = 0, wd = 0, ht = 0;
    Widget::GeomMode xm = Widget::GeomAbsolute, ym = Widget::GeomAbsolute,
                     wm = Widget::GeomAbsolute, hm = Widget::GeomAbsolute;
    auto parseGeom = [&](const std::string* s, float& val,
                         Widget::GeomMode& mode) -> bool {
        if (!s) return false;
        if (*s == "fill") { mode = Widget::GeomFill; return true; }
        if (!s->empty() && s->back() == '%') {
            mode = Widget::GeomPercent;
            return toFloat(s->substr(0, s->size() - 1), val);
        }
        return toFloat(*s, val);
    };
    bool hasX = parseGeom(get("x"), x, xm);
    bool hasY = parseGeom(get("y"), y, ym);
    bool hasW = parseGeom(get("width"), wd, wm);
    bool hasH = parseGeom(get("height"), ht, hm);
    if (xm || ym || wm || hm) {
        w->setGeometrySpec(Rect{x, y, wd, ht}, xm, ym, wm, hm);
    }
    // anchor="left,right,top,bottom" (l/r/t/b accepted, space or comma
    // separated): pin the described edges' margins across parent resizes.
    if (const std::string* v = get("anchor")) {
        uint8_t bits = 0;
        std::string tok;
        auto flush = [&tok, &bits]() {
            if (tok == "l" || tok == "left") bits |= Widget::AnchorLeft;
            else if (tok == "r" || tok == "right") bits |= Widget::AnchorRight;
            else if (tok == "t" || tok == "top") bits |= Widget::AnchorTop;
            else if (tok == "b" || tok == "bottom") bits |= Widget::AnchorBottom;
            tok.clear();
        };
        for (char c : *v) {
            if (c == ',' || c == ' ' || c == ';' || c == '|') flush();
            else if (c >= 'A' && c <= 'Z') tok.push_back(char(c - 'A' + 'a'));
            else tok.push_back(c);
        }
        flush();
        if (bits) w->setAnchor(bits);
    }
    // minwidth/maxwidth/minheight/maxheight (px) clamp the resolved size on
    // every layout pass, so percent/fill/anchor widths keep a pixel floor
    // or ceiling.
    {
        float minW = 0, minH = 0, maxW = 0, maxH = 0;
        bool hasLimits = false;
        if (const std::string* v = get("minwidth")) { toFloat(*v, minW); hasLimits = true; }
        if (const std::string* v = get("minheight")) { toFloat(*v, minH); hasLimits = true; }
        if (const std::string* v = get("maxwidth")) { toFloat(*v, maxW); hasLimits = true; }
        if (const std::string* v = get("maxheight")) { toFloat(*v, maxH); hasLimits = true; }
        if (hasLimits) w->setSizeLimits(minW, minH, maxW, maxH);
    }
    // weight="N" makes the widget share a column's leftover height or a
    // row's leftover width proportionally (see Column/Row::layout).
    if (const std::string* v = get("weight")) {
        float wt = 0.0f;
        if (toFloat(*v, wt) && wt > 0.0f) w->setWeight(wt);
    }
    // Seed the bounds with the numeric values; relative components are
    // corrected by the first layout pass.
    if (hasW || hasH) {
        Rect b = w->bounds();
        w->setBounds(Rect{hasX ? x : b.x, hasY ? y : b.y,
                          hasW ? wd : b.w, hasH ? ht : b.h});
    } else if (hasX || hasY) {
        Rect b = w->bounds();
        w->setBounds(Rect{hasX ? x : b.x, hasY ? y : b.y, b.w, b.h});
    }
    w->clampToBounds();

    // ---- common style attrs (a single applyStyle: it replaces the style) ----
    Style st;
    bool hasStyle = false;
    if (const std::string* v = get("visible")) { st.isVisible = toBool(*v); hasStyle = true; }
    if (const std::string* v = get("background"))      { st.backgroundColor = colorOf(*v); hasStyle = true; }
    if (const std::string* v = get("color"))   { st.textColor = colorOf(*v); hasStyle = true; }
    if (const std::string* v = get("border"))  { st.borderColor = colorOf(*v); st.borderWidth = 1.0f; hasStyle = true; }
    if (const std::string* v = get("bordercolor")) { st.borderColor = colorOf(*v); hasStyle = true; }
    if (const std::string* v = get("borderwidth")) { toFloat(*v, st.borderWidth); hasStyle = true; }
    if (const std::string* v = get("radius"))  { toFloat(*v, st.radius); hasStyle = true; }
    if (const std::string* v = get("padding")) { toFloat(*v, st.padding); hasStyle = true; }
    if (const std::string* v = get("margin"))  { toFloat(*v, st.margin); hasStyle = true; }
    if (const std::string* v = get("gap"))     { toFloat(*v, st.gap); hasStyle = true; }
    if (const std::string* v = get("font")) {
        parseFontAttr(*v, st.fontFamily, st.fontSize);
        hasStyle = true;
    }
    if (const std::string* v = get("fontsize")) { toInt(*v, st.fontSize); hasStyle = true; }
    if (const std::string* v = get("shadow")) {
        // "blur" | "blur,dy" | "blur,dx,dy" (blur 0 disables the shadow).
        std::vector<float> nums;
        std::string tok;
        auto flush = [&tok, &nums]() {
            if (tok.empty()) return;
            nums.push_back(std::strtof(tok.c_str(), nullptr));
            tok.clear();
        };
        for (char c : *v) {
            if (c == ',' || c == ' ' || c == ';' || c == '|') flush();
            else tok.push_back(c);
        }
        flush();
        if (!nums.empty()) {
            st.shadowBlur = nums[0];
            if (nums.size() >= 2) st.shadowDy = nums[1];
            if (nums.size() >= 3) st.shadowDx = nums[2];
            hasStyle = true;
        }
    }
    if (const std::string* v = get("shadowcolor")) { st.shadowColor = colorOf(*v); hasStyle = true; }
    if (const std::string* v = get("gradient")) {
        st.gradientColor = colorOf(*v);
        st.hasGradient = true;
        if (const std::string* d = get("gradientdirection"))
            st.gradientHorizontal = (*d == "horizontal" || *d == "left" || *d == "right");
        hasStyle = true;
    }
    if (hasStyle) w->applyStyle(st);

    // ---- typed widget attributes ----
    if (auto* p = dynamic_cast<Label*>(w.get())) {
        if (const std::string* v = get("text")) p->setText(*v);
        if (const std::string* v = get("wrap")) p->setWrap(toBool(*v));
        if (hasStyle) { p->setFontFamily(st.fontFamily); p->setFontSize(st.fontSize); p->setTextColor(st.textColor); }
        if (const std::string* v = get("align")) {
            p->setHAlign(*v == "center" ? Align::Center : (*v == "right" ? Align::Right : Align::Left));
        }
    } else if (auto* b = dynamic_cast<Button*>(w.get())) {
        if (const std::string* v = get("text")) b->setText(*v);
        if (const std::string* v = get("align")) {
            b->setHAlign(*v == "left" ? Align::Left
                                      : (*v == "right" ? Align::Right : Align::Center));
        }
        if (const std::string* v = get("textpadding")) {
            float pad = 0.0f;
            toFloat(*v, pad);
            b->setTextPadding(pad);
        }
        if (hasStyle) {
            b->setFontFamily(st.fontFamily); b->setFontSize(st.fontSize);
            b->setTextColor(st.textColor); b->setBgColor(st.backgroundColor);
            b->setBorderColor(st.borderColor); b->setBorderWidth(st.borderWidth);
            b->setRadius(st.radius);
        }
    } else if (auto* si = dynamic_cast<SearchInput*>(w.get())) {
        // Before the TextInput branch: SearchInput inherits from it.
        if (const std::string* v = get("text")) si->setText(*v);
        if (const std::string* v = get("placeholder")) si->setPlaceholder(*v);
        if (hasStyle) {
            si->setBgColor(st.backgroundColor); si->setFgColor(st.textColor);
        }
    } else if (auto* t = dynamic_cast<TextInput*>(w.get())) {
        if (const std::string* v = get("text")) t->setText(*v);
        if (const std::string* v = get("placeholder")) t->setPlaceholder(*v);
        if (hasStyle) {
            t->setBgColor(st.backgroundColor); t->setFgColor(st.textColor);
            t->setBorderColor(st.borderColor); t->setBorderWidth(st.borderWidth);
            t->setRadius(st.radius); t->setFontSize(st.fontSize);
        }
    } else if (auto* win = dynamic_cast<Window*>(w.get())) {
        if (const std::string* v = get("title")) win->setTitle(*v);
        if (const std::string* v = get("titlevisible")) win->setTitleVisible(toBool(*v));
        if (hasStyle) {
            win->setBgColor(st.backgroundColor); win->setBorderColor(st.borderColor);
            win->setBorderWidth(st.borderWidth); win->setRadius(st.radius);
        }
    } else if (auto* pan = dynamic_cast<Panel*>(w.get())) {
        if (hasStyle) {
            pan->setBgColor(st.backgroundColor); pan->setBorderColor(st.borderColor);
            pan->setBorderWidth(st.borderWidth); pan->setRadius(st.radius);
        }
    } else if (auto* sv = dynamic_cast<ScrollView*>(w.get())) {
        // Applied before the children are parsed, so they only settle after
        // the first layout pass (clampScroll() keeps them then).
        if (const std::string* v = get("scrollstep")) {
            float step = 40.0f;
            toFloat(*v, step);
            sv->setScrollStep(step);
        }
        float sx = sv->scrollPosition().x, sy = sv->scrollPosition().y;
        bool hasScroll = false;
        if (const std::string* v = get("scrollx")) { toFloat(*v, sx); hasScroll = true; }
        if (const std::string* v = get("scrolly")) { toFloat(*v, sy); hasScroll = true; }
        if (hasScroll) sv->setScroll(sx, sy);
        if (hasStyle) {
            sv->setBgColor(st.backgroundColor); sv->setBorderColor(st.borderColor);
            sv->setBorderWidth(st.borderWidth);
        }
    } else if (auto* img = dynamic_cast<Image*>(w.get())) {
        if (const std::string* v = get("source")) img->setImagePath(*v);
        if (const std::string* v = get("tile")) img->setTile(toBool(*v));
    } else if (auto* grp = dynamic_cast<Group*>(w.get())) {
        if (const std::string* v = get("name")) grp->setGroupName(*v);
        if (hasStyle) {
            grp->setBgColor(st.backgroundColor); grp->setBorderColor(st.borderColor);
            grp->setBorderWidth(st.borderWidth);
        }
    } else if (auto* cb = dynamic_cast<Checkbox*>(w.get())) {
        if (const std::string* v = get("text")) cb->setText(*v);
        if (const std::string* v = get("checked")) cb->setChecked(toBool(*v));
        if (const std::string* v = get("color")) cb->setTextColor(colorOf(*v));
        if (const std::string* v = get("background")) cb->setCheckColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) cb->setBoxColor(colorOf(*v));
        else if (const std::string* v = get("border")) cb->setBoxColor(colorOf(*v));
        if (hasStyle) { cb->setFontFamily(st.fontFamily); cb->setFontSize(st.fontSize); }
    } else if (auto* rb = dynamic_cast<RadioButton*>(w.get())) {
        if (const std::string* v = get("text")) rb->setText(*v);
        if (const std::string* v = get("checked")) rb->setChecked(toBool(*v));
        if (const std::string* v = get("color")) rb->setTextColor(colorOf(*v));
        if (const std::string* v = get("accent")) rb->setDotColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) rb->setCircleColor(colorOf(*v));
        if (const std::string* v = get("group")) rb->setGroup(*v);
        if (hasStyle) { rb->setFontFamily(st.fontFamily); rb->setFontSize(st.fontSize); }
    } else if (auto* sep = dynamic_cast<Separator*>(w.get())) {
        if (const std::string* v = get("thickness")) {
            float t = 1.0f;
            toFloat(*v, t);
            sep->setThickness(t);
        }
        if (const std::string* v = get("color")) sep->setLineColor(colorOf(*v));
        else if (const std::string* v = get("background")) sep->setLineColor(colorOf(*v));
    } else if (auto* pr = dynamic_cast<ProgressBar*>(w.get())) {
        if (const std::string* v = get("indeterminate")) pr->setIndeterminate(toBool(*v));
        if (const std::string* v = get("value")) {
            float val = 0.0f;
            toFloat(*v, val);
            pr->setValue(val);
        }
        if (const std::string* v = get("color")) pr->setBarColor(colorOf(*v));
        if (const std::string* v = get("background")) pr->setBgColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) pr->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) {
            pr->setBorderColor(colorOf(*v));
            pr->setBorderWidth(1.0f);
        }
        if (const std::string* v = get("borderwidth")) {
            float bw = 1.0f;
            toFloat(*v, bw);
            pr->setBorderWidth(bw);
        }
        if (const std::string* v = get("radius")) {
            float rad = 4.0f;
            toFloat(*v, rad);
            pr->setRadius(rad);
        }
    } else if (auto* sl = dynamic_cast<Slider*>(w.get())) {
        float lo = sl->minimum(), hi = sl->maximum(), val = sl->value();
        bool hasRange = false;
        if (const std::string* v = get("min")) { toFloat(*v, lo); hasRange = true; }
        if (const std::string* v = get("max")) { toFloat(*v, hi); hasRange = true; }
        if (hasRange) sl->setRange(lo, hi);
        if (const std::string* v = get("value")) { toFloat(*v, val); sl->setValue(val); }
        if (const std::string* v = get("color")) sl->setBarColor(colorOf(*v));
        if (const std::string* v = get("background")) sl->setTrackColor(colorOf(*v));
        if (const std::string* v = get("thumbcolor")) sl->setThumbColor(colorOf(*v));
    } else if (auto* sb = dynamic_cast<SpinBox*>(w.get())) {
        float lo = sb->minimum(), hi = sb->maximum(), val = sb->value();
        bool hasRange = false;
        if (const std::string* v = get("min")) { toFloat(*v, lo); hasRange = true; }
        if (const std::string* v = get("max")) { toFloat(*v, hi); hasRange = true; }
        if (hasRange) sb->setRange(lo, hi);
        if (const std::string* v = get("value")) { toFloat(*v, val); sb->setValue(val); }
        if (const std::string* v = get("step")) { float s = 1.0f; toFloat(*v, s); sb->setStep(s); }
        if (const std::string* v = get("decimals")) { int d = 0; toInt(*v, d); sb->setDecimals(d); }
        if (const std::string* v = get("wrap")) sb->setWrap(toBool(*v));
        if (const std::string* v = get("prefix")) sb->setPrefix(*v);
        if (const std::string* v = get("suffix")) sb->setSuffix(*v);
        if (const std::string* v = get("text")) sb->setPrefix(*v);   // convenience label
        if (const std::string* v = get("background")) sb->setBgColor(colorOf(*v));
        if (const std::string* v = get("color")) sb->setTextColor(colorOf(*v));
        if (const std::string* v = get("buttoncolor")) sb->setButtonColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) sb->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) sb->setBorderColor(colorOf(*v));
        if (hasStyle) { sb->setFontFamily(st.fontFamily); sb->setFontSize(st.fontSize); }
    } else if (auto* os = dynamic_cast<OptionSpin*>(w.get())) {
        if (const std::string* v = get("options")) {
            std::vector<std::string> opts;
            size_t p = 0;
            while (p <= v->size()) {
                size_t q = v->find(',', p);
                if (q == std::string::npos) q = v->size();
                size_t a = p, b = q;
                while (a < b && std::isspace(static_cast<unsigned char>((*v)[a]))) ++a;
                while (b > a && std::isspace(static_cast<unsigned char>((*v)[b - 1]))) --b;
                if (b > a) opts.push_back(v->substr(a, b - a));
                if (q == v->size()) break;
                p = q + 1;
            }
            os->setOptions(std::move(opts));
        }
        if (const std::string* v = get("value")) os->setValue(*v);
        if (const std::string* v = get("index")) { int i = 0; toInt(*v, i); os->setIndex(i); }
        if (const std::string* v = get("wrap")) os->setWrap(toBool(*v));
        if (const std::string* v = get("background")) os->setBgColor(colorOf(*v));
        if (const std::string* v = get("color")) os->setTextColor(colorOf(*v));
        if (const std::string* v = get("buttoncolor")) os->setButtonColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) os->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) os->setBorderColor(colorOf(*v));
        if (hasStyle) { os->setFontFamily(st.fontFamily); os->setFontSize(st.fontSize); }
    } else if (auto* rs = dynamic_cast<RangeSlider*>(w.get())) {
        float lo = rs->minimum(), hi = rs->maximum();
        bool hasRange = false;
        if (const std::string* v = get("min")) { toFloat(*v, lo); hasRange = true; }
        if (const std::string* v = get("max")) { toFloat(*v, hi); hasRange = true; }
        if (hasRange) rs->setRange(lo, hi);
        if (const std::string* v = get("low")) { float x = 0; toFloat(*v, x); rs->setLow(x); }
        if (const std::string* v = get("high")) { float x = 0; toFloat(*v, x); rs->setHigh(x); }
        if (const std::string* v = get("color")) rs->setBarColor(colorOf(*v));
        if (const std::string* v = get("background")) rs->setTrackColor(colorOf(*v));
        if (const std::string* v = get("thumbcolor")) rs->setThumbColor(colorOf(*v));
    } else if (auto* sp = dynamic_cast<Splitter*>(w.get())) {
        if (const std::string* v = get("orientation")) sp->setVertical(*v != "horizontal");
        if (const std::string* v = get("vertical")) sp->setVertical(toBool(*v));
        if (const std::string* v = get("split")) { float f = 0.5f; toFloat(*v, f); sp->setSplit(f); }
        if (const std::string* v = get("handlesize")) { float h = 6.0f; toFloat(*v, h); sp->setHandleSize(h); }
        if (const std::string* v = get("handlecolor")) sp->setHandleColor(colorOf(*v));
    } else if (auto* gd = dynamic_cast<Grid*>(w.get())) {
        if (const std::string* v = get("columns")) gd->setCols(*v);
        if (const std::string* v = get("gap")) { float g = 4.0f; toFloat(*v, g); gd->setGap(g); }
        if (const std::string* v = get("rowheight")) { float rh = 0.0f; toFloat(*v, rh); gd->setRowHeight(rh); }
    } else if (auto* bd = dynamic_cast<Badge*>(w.get())) {
        if (const std::string* v = get("text")) bd->setText(*v);
        if (const std::string* v = get("count")) { int c = -1; toInt(*v, c); bd->setCount(c); }
        if (const std::string* v = get("removable")) bd->setRemovable(toBool(*v));
        if (const std::string* v = get("background")) bd->setBgColor(colorOf(*v));
        if (const std::string* v = get("color")) bd->setFgColor(colorOf(*v));
    } else if (auto* spn = dynamic_cast<Spinner*>(w.get())) {
        if (const std::string* v = get("text")) spn->setText(*v);
        if (const std::string* v = get("variant")) spn->setVariant(*v);
        if (const std::string* v = get("speed")) { float s = 8.0f; toFloat(*v, s); spn->setSpeed(s); }
        if (const std::string* v = get("color")) spn->setFgColor(colorOf(*v));
    } else if (auto* acc = dynamic_cast<Accordion*>(w.get())) {
        if (const std::string* v = get("exclusive")) acc->setExclusive(toBool(*v));
        if (const std::string* v = get("gap")) { float g = 4.0f; toFloat(*v, g); acc->setGap(g); }
    } else if (auto* sec = dynamic_cast<AccordionSection*>(w.get())) {
        if (const std::string* v = get("header")) sec->setHeader(*v);
        else if (const std::string* v = get("text")) sec->setHeader(*v);
        if (const std::string* v = get("expanded")) sec->setExpanded(toBool(*v));
        if (const std::string* v = get("headerheight")) { float h = 24.0f; toFloat(*v, h); sec->setHeaderHeight(h); }
    } else if (auto* md = dynamic_cast<Modal*>(w.get())) {
        if (const std::string* v = get("title")) md->setTitle(*v);
        if (const std::string* v = get("open")) md->setOpen(toBool(*v));
        if (const std::string* v = get("panelwidth")) { float f = 340.0f; toFloat(*v, f); md->setPanelSize(f, md->panelSize().h); }
        if (const std::string* v = get("panelheight")) { float f = 160.0f; toFloat(*v, f); md->setPanelSize(md->panelSize().w, f); }
    } else if (auto* sl = dynamic_cast<Sparkline*>(w.get())) {
        if (const std::string* v = get("values")) sl->setValues(*v);
        if (const std::string* v = get("label")) sl->setLabel(*v);
        if (const std::string* v = get("max")) { int n = 60; toInt(*v, n); sl->setMaxSamples(n); }
        if (const std::string* v = get("color")) sl->setLineColor(colorOf(*v));
        if (const std::string* v = get("background")) sl->setBgColor(colorOf(*v));
    } else if (auto* mn = dynamic_cast<Menu*>(w.get())) {
        if (const std::string* v = get("label")) mn->setLabel(*v);
        if (const std::string* v = get("text")) mn->setLabel(*v);
    } else if (auto* mi = dynamic_cast<MenuItem*>(w.get())) {
        if (const std::string* v = get("label")) mi->setLabel(*v);
        if (const std::string* v = get("text")) mi->setLabel(*v);
        if (const std::string* v = get("command")) mi->setCommand(*v);
    } else if (auto* cp = dynamic_cast<ColorPicker*>(w.get())) {
        if (const std::string* v = get("color")) cp->setColorHex(*v);
    } else if (auto* pp = dynamic_cast<PathPicker*>(w.get())) {
        if (const std::string* v = get("value")) pp->setValue(*v);
        if (const std::string* v = get("placeholder")) pp->setPlaceholder(*v);
        if (const std::string* v = get("path")) pp->setStartPath(*v);
        if (const std::string* v = get("filter")) pp->setFilter(*v);
    } else if (auto* re = dynamic_cast<RampEditor*>(w.get())) {
        if (const std::string* v = get("stops")) re->setStopsFromSpec(*v);
    } else if (auto* ce = dynamic_cast<CurveEditor*>(w.get())) {
        if (const std::string* v = get("points")) ce->setPointsFromSpec(*v);
    } else if (auto* tb = dynamic_cast<Tabs*>(w.get())) {
        if (const std::string* v = get("active")) {
            int idx = 0;
            toInt(*v, idx);
            tb->setActive(idx);
        }
        if (const std::string* v = get("scrolling")) tb->setScrollable(toBool(*v));
        if (const std::string* v = get("tabheight")) {
            float th = 28.0f;
            toFloat(*v, th);
            tb->setHeaderHeight(th);
        }
        if (const std::string* v = get("headercolor")) tb->setHeaderColor(colorOf(*v));
        if (const std::string* v = get("tabcolor")) tb->setTabColor(colorOf(*v));
        if (const std::string* v = get("activecolor")) tb->setActiveColor(colorOf(*v));
        if (const std::string* v = get("accent")) tb->setAccentColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) tb->setBorderColor(colorOf(*v));
        if (const std::string* v = get("borderwidth")) {
            float bw = 0.0f;
            toFloat(*v, bw);
            tb->setBorderWidth(bw);
        }
        if (hasStyle) {
            tb->setFontFamily(st.fontFamily); tb->setFontSize(st.fontSize);
            tb->setContentColor(st.backgroundColor); tb->setTextColor(st.textColor);
        }
    } else if (auto* tb = dynamic_cast<Tab*>(w.get())) {
        if (const std::string* v = get("label")) tb->setLabel(*v);
        else if (const std::string* v = get("text")) tb->setLabel(*v);
    } else if (auto* box = dynamic_cast<ComboBox*>(w.get())) {
        if (const std::string* v = get("selected")) {
            int idx = 0;
            toInt(*v, idx);
            box->setSelectedIndex(idx);
        }
        if (const std::string* v = get("rowheight")) {
            float rh = 24.0f;
            toFloat(*v, rh);
            box->setRowHeight(rh);
        }
        if (const std::string* v = get("color")) box->setTextColor(colorOf(*v));
        if (const std::string* v = get("background")) box->setBgColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) box->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) box->setBorderColor(colorOf(*v));
        if (const std::string* v = get("accent")) box->setArrowColor(colorOf(*v));
        if (const std::string* v = get("highlight")) box->setHighlightColor(colorOf(*v));
        if (const std::string* v = get("panel")) box->setPanelColor(colorOf(*v));
        if (hasStyle) { box->setFontFamily(st.fontFamily); box->setFontSize(st.fontSize); }
    } else if (auto* opt = dynamic_cast<ComboOption*>(w.get())) {
        if (const std::string* v = get("label")) opt->setLabel(*v);
        else if (const std::string* v = get("text")) opt->setLabel(*v);
        if (const std::string* v = get("value")) opt->setValue(*v);
    } else if (auto* cs = dynamic_cast<Console*>(w.get())) {
        if (const std::string* v = get("rowheight")) {
            float rh = 18.0f;
            toFloat(*v, rh);
            cs->setRowHeight(rh);
        }
        if (const std::string* v = get("maxlines")) {
            int n = 200;
            toInt(*v, n);
            cs->setMaxLines(n);
        }
        if (const std::string* v = get("color")) cs->setTextColor(colorOf(*v));
        if (const std::string* v = get("background")) cs->setBgColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) cs->setBorderColor(colorOf(*v));
        if (hasStyle) { cs->setFontFamily(st.fontFamily); cs->setFontSize(st.fontSize); }
    } else if (auto* lb = dynamic_cast<ListBox*>(w.get())) {
        if (const std::string* v = get("rowheight")) {
            float rh = 24.0f;
            toFloat(*v, rh);
            lb->setRowHeight(rh);
        }
        if (const std::string* v = get("selected")) {
            int idx = 0;
            toInt(*v, idx);
            lb->setSelectedIndex(idx);
        }
        if (const std::string* v = get("color")) lb->setTextColor(colorOf(*v));
        if (const std::string* v = get("background")) lb->setBgColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) lb->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) lb->setBorderColor(colorOf(*v));
        if (const std::string* v = get("highlight")) lb->setHighlightColor(colorOf(*v));
        if (hasStyle) { lb->setFontFamily(st.fontFamily); lb->setFontSize(st.fontSize); }
    } else if (auto* li = dynamic_cast<ListItem*>(w.get())) {
        if (const std::string* v = get("label")) li->setLabel(*v);
        else if (const std::string* v = get("text")) li->setLabel(*v);
        if (const std::string* v = get("value")) li->setValue(*v);
    } else if (auto* tb = dynamic_cast<Table*>(w.get())) {
        if (const std::string* v = get("rowheight")) {
            float rh = 22.0f;
            toFloat(*v, rh);
            tb->setRowHeight(rh);
        }
        if (const std::string* v = get("headerheight")) {
            float hh = 24.0f;
            toFloat(*v, hh);
            tb->setHeaderHeight(hh);
        }
        if (const std::string* v = get("selected")) {
            int idx = 0;
            toInt(*v, idx);
            tb->setSelectedIndex(idx);
        }
        if (const std::string* v = get("sortable")) tb->setSortable(*v == "true" || *v == "1");
        if (const std::string* v = get("color")) tb->setTextColor(colorOf(*v));
        if (const std::string* v = get("background")) tb->setBgColor(colorOf(*v));
        if (const std::string* v = get("headerbackground")) tb->setHeaderBgColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) tb->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) tb->setBorderColor(colorOf(*v));
        if (const std::string* v = get("highlight")) tb->setHighlightColor(colorOf(*v));
        if (hasStyle) { tb->setFontFamily(st.fontFamily); tb->setFontSize(st.fontSize); }
    } else if (auto* th = dynamic_cast<TableColumn*>(w.get())) {
        if (const std::string* v = get("label")) th->setLabel(*v);
        else if (const std::string* v = get("text")) th->setLabel(*v);
        if (const std::string* v = get("width")) {
            float cw = 0.0f;
            toFloat(*v, cw);
            th->setWidth(cw);
        }
        if (const std::string* v = get("resizable")) th->setResizable(toBool(*v));
        if (const std::string* v = get("editable")) th->setEditable(toBool(*v));
        if (const std::string* v = get("scrollingtext"))
            th->setScrollMode(*v == "automatic" ? CellScroll::Automatic
                              : *v == "onmouseenter" ? CellScroll::OnHover
                                                     : CellScroll::None);
    } else if (auto* tr = dynamic_cast<TableRow*>(w.get())) {
        if (const std::string* v = get("cells")) tr->setCells(*v);
        if (const std::string* v = get("value")) tr->setValue(*v);
        if (const std::string* v = get("editablecells")) tr->setEditableColumns(*v);
        if (const std::string* v = get("resizable")) tr->setRowResizable(toBool(*v));
        if (const std::string* v = get("scrollingtext"))
            tr->setRowScrollMode(*v == "automatic" ? CellScroll::Automatic
                                 : *v == "onmouseenter" ? CellScroll::OnHover
                                                        : CellScroll::None);
    } else if (auto* cell = dynamic_cast<TableCell*>(w.get())) {
        if (const std::string* v = get("editable")) cell->setEditable(toBool(*v));
        if (const std::string* v = get("resizable")) cell->setResizable(toBool(*v));
        if (const std::string* v = get("scrollingtext"))
            cell->setScrollMode(*v == "automatic" ? CellScroll::Automatic
                                : *v == "onmouseenter" ? CellScroll::OnHover
                                                       : CellScroll::None);
    } else if (auto* tv = dynamic_cast<TreeView*>(w.get())) {
        if (const std::string* v = get("rowheight")) {
            float rh = 22.0f;
            toFloat(*v, rh);
            tv->setRowHeight(rh);
        }
        if (const std::string* v = get("indent")) {
            float ind = 14.0f;
            toFloat(*v, ind);
            tv->setIndent(ind);
        }
        if (const std::string* v = get("autoheight")) tv->setAutoHeight(toBool(*v));
        if (const std::string* v = get("selected")) {
            int idx = 0;
            toInt(*v, idx);
            tv->setSelectedIndex(idx);
        }
        if (const std::string* v = get("color")) tv->setTextColor(colorOf(*v));
        if (const std::string* v = get("background")) tv->setBgColor(colorOf(*v));
        if (const std::string* v = get("bordercolor")) tv->setBorderColor(colorOf(*v));
        else if (const std::string* v = get("border")) tv->setBorderColor(colorOf(*v));
        if (const std::string* v = get("highlight")) tv->setHighlightColor(colorOf(*v));
        if (const std::string* v = get("accent")) tv->setAccentColor(colorOf(*v));
        if (const std::string* v = get("icons")) tv->setShowIcons(toBool(*v));
        if (const std::string* v = get("checks")) tv->setShowChecks(toBool(*v));
        if (const std::string* v = get("guides")) tv->setShowGuides(toBool(*v));
        if (const std::string* v = get("dir")) tv->loadDirectory(*v);
        if (hasStyle) { tv->setFontFamily(st.fontFamily); tv->setFontSize(st.fontSize); }
    } else if (auto* ti = dynamic_cast<TreeItem*>(w.get())) {
        if (const std::string* v = get("label")) ti->setLabel(*v);
        else if (const std::string* v = get("text")) ti->setLabel(*v);
        if (const std::string* v = get("value")) ti->setValue(*v);
        if (const std::string* v = get("expanded")) ti->setExpanded(toBool(*v));
        if (const std::string* v = get("folder")) ti->setFolder(toBool(*v));
        else if (const std::string* v = get("file")) ti->setFolder(!toBool(*v));
        if (const std::string* v = get("checked")) ti->setChecked(toBool(*v));
    } else if (auto* ts = dynamic_cast<ToggleSwitch*>(w.get())) {
        if (const std::string* v = get("checked")) ts->setState(toBool(*v));
        else if (const std::string* v = get("on")) ts->setState(toBool(*v));
        if (const std::string* v = get("text")) ts->setText(*v);
    } else if (auto* col = dynamic_cast<Column*>(w.get())) {
        // Column layout knobs are handled by Style (padding/margin/gap)
        // above; `align` places children horizontally within the column.
        if (const std::string* v = get("align")) {
            col->setChildAlign(*v == "center" ? Align::Center
                                              : (*v == "right" ? Align::Right : Align::Left));
        }
    } else if (auto* row = dynamic_cast<Row*>(w.get())) {
        // `valign` places shorter children against the tallest row member.
        if (const std::string* v = get("valign")) {
            row->setChildValign(*v == "middle" ? VertAlign::Middle
                                               : (*v == "bottom" ? VertAlign::Bottom : VertAlign::Top));
        }
    }

    if (tag == "screen") {
        // nothing extra yet
    }
}

void XmlParser::applyContent(const std::shared_ptr<Widget>& w, const std::string& raw) {
    if (!w) return;
    // Trim leading/trailing whitespace (newlines + indentation around the tag).
    size_t a = raw.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return;   // empty/whitespace-only
    size_t b = raw.find_last_not_of(" \t\r\n");
    const std::string text = raw.substr(a, b - a + 1);
    if (auto* cell = dynamic_cast<TableCell*>(w.get())) {
        cell->setText(text);
    }
}

Style XmlParser::resolveStyle(const std::string&, const Style& base) const {
    return base;
}

Style XmlParser::inheritStyle(const Style& parentStyle) const {
    return parentStyle;
}

std::shared_ptr<Widget> XmlParser::makeWidget(const std::string& tag,
                                              const Style& style) {
    auto& reg = widgetRegistry();
    auto it = reg.find(tag);
    if (it != reg.end()) {
        return it->second(style, {});
    }
    return nullptr;
}

std::string XmlParser::where() const {
    // Incremental: errors cluster near the current position, so resume from
    // the last scan instead of walking the document from byte 0 every time
    // (which made large files with many errors quadratic).
    if (m_wcPos > m_pos) { m_wcPos = 0; m_wcLine = 1; m_wcCol = 1; }
    for (; m_wcPos < m_pos && m_wcPos < m_len; ++m_wcPos) {
        if (m_text[m_wcPos] == '\n') { ++m_wcLine; m_wcCol = 1; }
        else ++m_wcCol;
    }
    return std::to_string(m_wcLine) + ":" + std::to_string(m_wcCol);
}

void XmlParser::addError(const std::string& msg) const {
    if (m_result) m_result->errors.push_back(where() + ": " + msg);
}

void XmlParser::addWarning(const std::string& msg) const {
    if (m_result) m_result->warnings.push_back(where() + ": " + msg);
}

XmlParser::ParseResult XmlParser::parse(const std::string& xml, const Theme* theme) {
    m_text = xml;
    m_len = m_text.size();
    m_pos = 0;
    m_theme = theme;
    if (theme) setActiveTheme(*theme);
    m_root = nullptr;
    m_stack.clear();
    m_themeTarget = nullptr;
    m_namedStyles.clear();
    m_elementStyles.clear();
    if (theme) {
        m_namedStyles = theme->namedStyles;
        m_elementStyles = theme->elementStyles;
    }

    XmlParser::ParseResult res;
    m_result = &res;
    parseDocument();
    m_result = nullptr;
    res.root = m_root;
    return res;
}

XmlParser::ParseResult XmlParser::parseFile(const std::string& path, const Theme* theme) {
    ParseResult res;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        res.errors.push_back("cannot open file: " + path);
        return res;
    }
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    f.close();

    m_text = data;
    m_len = m_text.size();
    m_pos = 0;
    m_theme = theme;
    if (theme) setActiveTheme(*theme);
    m_root = nullptr;
    m_stack.clear();
    m_themeTarget = nullptr;
    m_namedStyles.clear();
    m_elementStyles.clear();
    if (theme) {
        m_namedStyles = theme->namedStyles;
        m_elementStyles = theme->elementStyles;
    }

    m_result = &res;
    parseDocument();
    m_result = nullptr;
    res.root = m_root;
    return res;
}

// ---- stylesheets -----------------------------------------------------------
XmlParser::ThemeResult XmlParser::parseTheme(const std::string& xml, const Theme& base) {
    ThemeResult res;
    m_text = xml;
    m_len = m_text.size();
    m_pos = 0;
    m_theme = nullptr;
    m_root = nullptr;
    m_stack.clear();
    m_namedStyles = base.namedStyles;
    m_elementStyles = base.elementStyles;
    Theme target = base;
    m_themeTarget = &target;

    ParseResult pr;
    m_result = &pr;
    parseDocument();
    m_result = nullptr;
    m_themeTarget = nullptr;

    res.errors = std::move(pr.errors);
    res.warnings = std::move(pr.warnings);
    target.namedStyles = std::move(m_namedStyles);
    target.elementStyles = std::move(m_elementStyles);
    res.theme = std::move(target);
    return res;
}

XmlParser::ThemeResult XmlParser::parseThemeFile(const std::string& path, const Theme& base) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        ThemeResult res;
        res.theme = base;
        res.errors.push_back("cannot open file: " + path);
        return res;
    }
    std::string data((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    f.close();
    return parseTheme(data, base);
}

} // namespace ogui
