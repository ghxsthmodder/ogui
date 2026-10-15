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

    // ---- templates --------------------------------------------------------
    // <template name="Card" title=""> declares a reusable component: its
    // markup body is stored raw and expanded wherever the name appears as a
    // tag. ${param} placeholders take the instance's attributes, the template
    // attributes are their defaults, and <slot/> receives the instance's
    // children. A tag the widget factory does not know is looked up here.
    struct TemplateDef {
        std::string name;
        // Parameter defaults, keyed by attribute name (already lowercased).
        std::map<std::string, std::string> params;
        std::string body;   // raw markup between <template> and </template>
    };

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

    // Handles <template name="...">...</template> (declares a component).
    void parseTemplateDef(const std::map<std::string, std::string>& attrs,
                          bool selfClosing);

    // Expands a registered template at an instance site and attaches the
    // result to the parent on the stack. Returns false when `tag` names no
    // template, so the caller can still report an unknown element.
    bool instantiateTemplate(const std::string& tag,
                             const std::map<std::string, std::string>& attrs,
                             bool selfClosing);

    // Reads the raw markup between the cursor and </tag>, leaving the cursor
    // after that closing tag.
    std::string takeElementBody(const std::string& tag);

    // ${name} substitution: values come from `params`; a placeholder with no
    // value stays literal and its name is reported in `missing`.
    static std::string substituteParams(
        const std::string& body,
        const std::map<std::string, std::string>& params,
        std::vector<std::string>& missing);

    // Replaces every <slot/> (or <slot>fallback</slot>) with `children`,
    // falling back to the slot's own markup when the instance has none.
    // `hadSlot` reports whether the body carried a slot at all.
    static std::string fillSlots(const std::string& body,
                                 const std::string& children, bool& hadSlot);

    // Parses component markup as a document of its own (its own cursor, so
    // its errors carry the component's line:col) and returns its widgets.
    std::vector<std::shared_ptr<Widget>> expandFragment(
        const std::string& markup, const Style& inherited, ParseResult& out);

    // Prefixes every id in the subtree so a component's ids stay local:
    // instance "start" + body id "title" reads back as "start.title".
    static void namespaceIds(const std::shared_ptr<Widget>& w,
                             const std::string& prefix);

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

    // Factory for concrete widget types. The attributes are passed through so
    // a registered custom widget can read values the parser does not know.
    std::shared_ptr<Widget> makeWidget(const std::string& tag, const Style& style,
                                       const std::map<std::string, std::string>& attrs);

    std::shared_ptr<Widget> m_root = nullptr;

    // Active result being filled (points into the ParseResult returned by
    // parse()/parseFile()).
    ParseResult* m_result = nullptr;

    // Receiver for <script> elements (see setScriptHandler()).
    ScriptHandler m_scriptHandler;

    // Active template expansions, innermost last: seeing a name already on
    // the stack means the template calls itself.
    std::vector<std::string> m_templateStack;

    // Instances per component name in this document: an instance without an
    // id still needs its own id scope, so it takes "<Name>#<n>".
    std::map<std::string, int> m_templateCount;

    // The id scope of each expansion on m_templateStack, innermost last. A
    // component instantiated inside another one nests: "card.row", so the
    // same inner id in two hosts of the same component stays addressable.
    std::vector<std::string> m_templateScopes;
};

// The component catalogue, keyed by tag name. It is global so a shared
// components file parsed before a screen supplies that screen's templates; a
// later <template name="X"> replaces an earlier one.
std::map<std::string, XmlParser::TemplateDef>& templateRegistry();

} // namespace ogui
