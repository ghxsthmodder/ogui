#pragma once
#include "ogui/api.hpp"
#include "ogui/style.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <optional>
#include <functional>

namespace ogui {

// ------------------------------------------------------------------
// XML parser: builds a widget tree from a layout + style document
// The parser is intentionally dependency-free: it uses a tiny hand-rolled
// tokenizer so that a game linking against ogui ships a single static lib.
// ------------------------------------------------------------------
class XmlParser {
public:
    struct ParseResult {
        std::shared_ptr<Widget> root;   // the screen root <screen>
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        // <script> chunks ({payload, isFile}) in document order; hosts
        // move them onto the Screen so the Kernel loads them on switch.
        std::vector<std::pair<std::string, bool>> scripts;
    };

    XmlParser();
    ~XmlParser();

    // Parse a string.
    ParseResult parse(const std::string& xml, const Theme* theme = nullptr);

    // Parse a file (reads the file into memory).
    ParseResult parseFile(const std::string& path, const Theme* theme = nullptr);

    // ---- stylesheets ------------------------------------------------------
    // A <theme> document restyles the whole app: <palette> overrides tokens,
    // <style name> defines reusable attribute sets shared by every screen,
    // <font> registers families. parseTheme layers the file over `base`.
    struct ThemeResult {
        Theme theme;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ThemeResult parseTheme(const std::string& xml, const Theme& base);
    ThemeResult parseThemeFile(const std::string& path, const Theme& base);

    // ---- scripting --------------------------------------------------------
    // <script src="file.lua"/> and inline <script>code</script> elements
    // are forwarded to this handler while parsing (payload, isFile); the
    // parser itself never runs them. Hosts wire it to a ScriptHost, e.g.
    //     parser.setScriptHandler([&](const std::string& p, bool f) {
    //         f ? kernel.loadScriptFile(p) : kernel.loadScriptString(p); });
    using ScriptHandler =
        std::function<void(const std::string& payload, bool isFile)>;
    void setScriptHandler(ScriptHandler h) { m_scriptHandler = std::move(h); }

private:
    std::string m_text;
    const Theme* m_theme = nullptr;
    // Non-null while parsing a <theme> stylesheet: <palette> writes here.
    Theme* m_themeTarget = nullptr;

    size_t m_pos = 0;
    size_t m_len = 0;

    // Cached cursor for where() (see its incremental scan).
    mutable size_t m_wcPos = 0;
    mutable size_t m_wcLine = 1;
    mutable size_t m_wcCol = 1;

    // Element stack; the top is the widget currently being filled.
    struct ParseState {
        std::shared_ptr<Widget> widget;
        std::string tag;
        Style style;
        std::map<std::string, std::string> attrs;
        bool isSelfClosing = false;
        std::string content;   // raw text between the open and close tags
    };
    std::vector<ParseState> m_stack;

    void parseDocument();
    void parseElement();
    void parseClosing(const std::string& tag);
    // Skips an element body (and its nested content) after an error.
    void skipElement(const std::string& tag, bool selfClosing);
    void skipWhitespace();
    std::string parseUntil(char c);
    std::string parseTagName();
    std::map<std::string, std::string> parseAttributes();
    std::string parseString();
    std::string parseChars();

    // Build a widget of the given type, fill it from attrs + style.
    std::shared_ptr<Widget> createWidget(const std::string& tag,
                                         const Style& style,
                                         const std::map<std::string, std::string>& attrs);

    // Push a message into the active ParseResult (tagged with line:col).
    void addError(const std::string& msg) const;
    void addWarning(const std::string& msg) const;
    std::string where() const;          // "line:col" at m_pos

    // Applies id/geometry/style/typed attributes to a freshly built widget.
    void applyAttributes(const std::shared_ptr<Widget>& w,
                         const std::string& tag,
                         const std::map<std::string, std::string>& attrs);

    // Applies an element's raw text content (trimmed) to a widget that
    // carries a text payload, e.g. <cell>Zoe</cell>.
    void applyContent(const std::shared_ptr<Widget>& w, const std::string& raw);

    // Handles <style name="...">...</style> (defines a reusable attr set).
    void parseStyleDef(const std::map<std::string, std::string>& attrs,
                       bool selfClosing);

    // Named styles collected from <style> elements (name -> attributes).
    std::map<std::string, std::map<std::string, std::string>> m_namedStyles;

    // Element-type styles from <style element="..."> (tag -> attributes);
    // they sit under named styles in the cascade.
    std::map<std::string, std::map<std::string, std::string>> m_elementStyles;

    // Returns attrs merged over the element style for `tag` and the named
    // styles referenced by style="a,b" (later names win; own attrs win all).
    std::map<std::string, std::string> withNamedStyle(
        const std::string& tag,
        const std::map<std::string, std::string>& attrs) const;

    // Resolve a named style (e.g. "button") into a merged Style.
    Style resolveStyle(const std::string& name, const Style& base) const;

    // Inherit a child style from its parent (for a child without explicit <style>).
    Style inheritStyle(const Style& parentStyle) const;

    // Factory for concrete widget types.
    std::shared_ptr<Widget> makeWidget(const std::string& tag, const Style& style);

    std::shared_ptr<Widget> m_root = nullptr;

    // Active result being filled (points into the ParseResult returned by
    // parse()/parseFile()).
    ParseResult* m_result = nullptr;

    // Receiver for <script> elements (see setScriptHandler()).
    ScriptHandler m_scriptHandler;
};

} // namespace ogui
