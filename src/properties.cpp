#include "ogui/properties.hpp"
#include "ogui/kernel.hpp"
#include "ogui/style.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace ogui {

namespace {

std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool equalIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && lowerCase(a) == lowerCase(b);
}

// The align vocabulary the XML attributes use, shared by every class that
// places its caption.
const char* alignName(Align a) {
    switch (a) {
        case Align::Left:   return "left";
        case Align::Center: return "center";
        case Align::Right:  return "right";
    }
    return "left";
}

bool alignFromName(const std::string& text, Align& out) {
    if (equalIgnoreCase(text, "left"))   { out = Align::Left; return true; }
    if (equalIgnoreCase(text, "center")) { out = Align::Center; return true; }
    if (equalIgnoreCase(text, "right"))  { out = Align::Right; return true; }
    return false;
}

}  // namespace

const char* propertyTypeName(PropertyType type) {
    switch (type) {
        case PropertyType::String: return "string";
        case PropertyType::Bool:   return "bool";
        case PropertyType::Number: return "number";
        case PropertyType::Color:  return "color";
        case PropertyType::Enum:   return "enum";
    }
    return "value";
}

std::string formatNumber(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
}

bool parseNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    const char* s = text.c_str();
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s) return false;
    while (end != s + text.size() &&
           std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (*end != '\0') return false;
    if (std::isnan(v) || std::isinf(v)) return false;
    out = v;
    return true;
}

bool parseBool(const std::string& text, bool& out) {
    if (equalIgnoreCase(text, "true") || text == "1" ||
        equalIgnoreCase(text, "yes") || equalIgnoreCase(text, "on")) {
        out = true;
        return true;
    }
    if (equalIgnoreCase(text, "false") || text == "0" ||
        equalIgnoreCase(text, "no") || equalIgnoreCase(text, "off")) {
        out = false;
        return true;
    }
    return false;
}

std::string formatColor(const Color& c) {
    char buf[16];
    if (c.a == 255) std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    else std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", c.r, c.g, c.b, c.a);
    return buf;
}

bool parseColorValue(const std::string& text, Color& out) {
    if (text.empty()) return false;
    if (text[0] == '@') {
        // Palette::token() answers an unknown name with `window`, which is the
        // right thing to draw and the wrong thing to accept: a typo'd token
        // has to fail here so the author hears about it.
        const std::string name = text.substr(1);
        if (!palette().hasToken(name)) return false;
        out = palette().token(name);
        return true;
    }
    const char* p = text.c_str();
    if (*p == '#') ++p;
    const size_t n = text.size() - (p - text.c_str());
    if (n != 3 && n != 4 && n != 6 && n != 8) return false;
    for (const char* q = p; *q; ++q) {
        if (!std::isxdigit(static_cast<unsigned char>(*q))) return false;
    }
    out = hexColorC(p);
    return true;
}

bool matchesChoice(const std::vector<std::string>& choices,
                   const std::string& value) {
    for (const auto& c : choices) {
        if (equalIgnoreCase(c, value)) return true;
    }
    return false;
}

std::string joinedChoices(const std::vector<std::string>& choices) {
    std::string out;
    for (size_t i = 0; i < choices.size(); ++i) {
        if (i) out += "|";
        out += choices[i];
    }
    return out;
}

void setValueError(std::string* error, const std::string& name,
                   const std::string& expected, const std::string& value) {
    if (!error) return;
    *error = "property '" + name + "' expects " + expected + ", got '" + value + "'";
}

// ------------------------------------------------------------------
// Registry
// ------------------------------------------------------------------
PropertyRegistry& PropertyRegistry::instance() {
    static PropertyRegistry reg;
    return reg;
}

PropertyRegistry::PropertyRegistry() { loadBuiltins(); }

void PropertyRegistry::declare(const std::type_info& type,
                               const std::type_info& base,
                               const std::vector<PropertyDef>& props) {
    Entry* existing = nullptr;
    for (auto& up : m_entries) {
        if (*up->type == type) { existing = up.get(); break; }
    }
    if (!existing) {
        auto up = std::make_unique<Entry>();
        up->type = &type;
        up->base = &base;
        m_entries.push_back(std::move(up));
        existing = m_entries.back().get();
    }
    for (const auto& def : props) {
        bool replaced = false;
        for (auto& held : existing->props) {
            if (held.name == def.name) { held = def; replaced = true; break; }
        }
        if (!replaced) existing->props.push_back(def);
    }
    // Class chains and resolved lists are cached; rebuild them next lookup.
    m_resolved.clear();
}

const PropertyRegistry::Entry* PropertyRegistry::entryFor(
    const std::type_info& type) const {
    for (const auto& up : m_entries) {
        if (*up->type == type) return up.get();
    }
    return nullptr;
}

bool PropertyRegistry::declares(const std::type_info& type) const {
    return !resolve(type).empty();
}

// Walks type -> base -> ... collecting declarations; the most derived
// declaration of a name wins, so a subclass can retype or reimplement one of
// the properties it inherits.
std::vector<const PropertyDef*> PropertyRegistry::resolve(
    const std::type_info& type) const {
    const std::string key = type.name();
    auto cached = m_resolved.find(key);
    if (cached != m_resolved.end()) return cached->second;

    std::vector<const PropertyDef*> out;
    const Entry* e = entryFor(type);
    for (int depth = 0; e && depth < 32; ++depth) {
        for (const auto& def : e->props) {
            bool shadowed = false;
            for (const auto* held : out) {
                if (held->name == def.name) { shadowed = true; break; }
            }
            if (!shadowed) out.push_back(&def);
        }
        if (!e->base || *e->base == *e->type) break;
        e = entryFor(*e->base);
    }
    m_resolved.emplace(key, out);
    return out;
}

std::vector<const PropertyDef*> PropertyRegistry::list(const Widget& w) const {
    return resolve(typeid(w));
}

const PropertyDef* PropertyRegistry::find(const Widget& w,
                                          const std::string& name) const {
    for (const auto* def : resolve(typeid(w))) {
        if (def->name == name) return def;
    }
    return nullptr;
}

// ------------------------------------------------------------------
// Lookups over a live widget
// ------------------------------------------------------------------
std::string widgetProperty(const Widget& w, const std::string& name, bool* found) {
    const auto* def = PropertyRegistry::instance().find(w, name);
    if (found) *found = def != nullptr;
    if (!def || !def->canRead()) return {};
    return def->read(w);
}

std::vector<std::string> widgetPropertyNames(const Widget& w) {
    std::vector<std::string> names;
    for (const auto* def : PropertyRegistry::instance().list(w)) {
        names.push_back(def->name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool setWidgetProperty(Widget& w, const std::string& name,
                       const std::string& value, std::string* error) {
    if (error) error->clear();
    const auto& reg = PropertyRegistry::instance();
    const auto* def = reg.find(w, name);
    if (!def) {
        if (error) {
            *error = "no property '" + name + "' on " + Kernel::typeName(&w);
            const auto names = widgetPropertyNames(w);
            if (!names.empty()) {
                error->append(" (properties: ");
                for (size_t i = 0; i < names.size(); ++i) {
                    if (i) error->append(", ");
                    if (i == 12) { error->append("..."); break; }
                    error->append(names[i]);
                }
                error->append(")");
            }
        }
        return false;
    }
    if (!def->canWrite()) {
        if (error) *error = "property '" + name + "' is read-only";
        return false;
    }
    if (!def->write(w, value, error)) {
        if (error && error->empty()) {
            *error = "property '" + name + "' rejected '" + value + "'";
        }
        return false;
    }
    if (Kernel* k = w.kernel()) k->invalidate();
    return true;
}

PropertyCheck checkWidgetProperty(const Widget& w, const std::string& name,
                                  const std::string& value,
                                  std::string* error) {
    if (error) error->clear();
    const auto* def = PropertyRegistry::instance().find(w, name);
    if (!def) return PropertyCheck::Unknown;
    const std::string& n = def->name;
    switch (def->type) {
    case PropertyType::Bool: {
        bool v = false;
        if (parseBool(value, v)) return PropertyCheck::Accepted;
        setValueError(error, n, "true/false/1/0/yes/no/on/off", value);
        return PropertyCheck::Rejected;
    }
    case PropertyType::Number: {
        double v = 0.0;
        if (parseNumber(value, v)) return PropertyCheck::Accepted;
        setValueError(error, n, "number", value);
        return PropertyCheck::Rejected;
    }
    case PropertyType::Color: {
        Color c{};
        if (parseColorValue(value, c)) return PropertyCheck::Accepted;
        setValueError(error, n,
                      "\"#rrggbb\", \"#rrggbbaa\" or \"@token\"", value);
        return PropertyCheck::Rejected;
    }
    case PropertyType::Enum:
        if (matchesChoice(def->choices, value)) return PropertyCheck::Accepted;
        setValueError(error, n, joinedChoices(def->choices), value);
        return PropertyCheck::Rejected;
    case PropertyType::String:
        break;
    }
    return PropertyCheck::Accepted;
}

// ------------------------------------------------------------------
// Widget facade
// ------------------------------------------------------------------
std::string Widget::property(const std::string& name, bool* found) const {
    return widgetProperty(*this, name, found);
}

bool Widget::setProperty(const std::string& name, const std::string& value,
                         std::string* error) {
    return setWidgetProperty(*this, name, value, error);
}

std::vector<std::string> Widget::propertyNames() const {
    return widgetPropertyNames(*this);
}

// ==================================================================
// The built-in declarations: every property of every class shipped with
// the library, in one table. This is the catalogue a tool or a script sees
// when it asks a widget what it exposes, and the validator every
// name-based write goes through.
// ==================================================================
namespace {

// ---- reusable shapes ------------------------------------------------

// The text a widget draws, shared by the caption-carrying classes.
template <class T>
PropertyDef textProp(const char* doc = "the caption the widget draws") {
    return stringProp<T>(
        "text", [](const T& w) { return w.text(); },
        [](T& w, const std::string& v) { w.setText(v); return true; }, doc);
}

// The selected row/option index of the choice widgets. They share the concept,
// not a base class, so each one declares it through its own accessors.
template <class T>
PropertyDef selectedProp(int (T::*get)() const, void (T::*set)(int)) {
    return numberProp<T>(
        "selected", [get](const T& w) { return static_cast<double>((w.*get)()); },
        [set](T& w, double v) { (w.*set)(static_cast<int>(v)); return true; },
        "index of the selected entry (-1 for none)");
}

// A class that keeps its own font fields re-declares these over the
// Style-backed base pair: a write moves the Style and the field the rasterizer
// reads, as the parser does. nullptr for a setter the class does not have.
template <class T>
std::vector<PropertyDef> fontProps(void (T::*setFamily)(const std::string&),
                                   void (T::*setSize)(int)) {
    std::vector<PropertyDef> out;
    if (setFamily) {
        out.push_back(stringProp<T>(
            "font", [](const T& w) { return w.style().fontFamily; },
            [setFamily](T& w, const std::string& v) {
                Style s = w.style();
                s.fontFamily = v;
                w.applyStyle(s);
                (w.*setFamily)(v);
                return true;
            },
            "font family used for the widget's text"));
    }
    if (setSize) {
        out.push_back(numberProp<T>(
            "fontsize", [](const T& w) {
                return static_cast<double>(w.style().fontSize);
            },
            [setSize](T& w, double v) {
                const int px = static_cast<int>(v);
                Style s = w.style();
                s.fontSize = px;
                w.applyStyle(s);
                (w.*setSize)(px);
                return true;
            },
            "text size in logical pixels"));
    }
    return out;
}

void addAll(std::vector<PropertyDef>& into, std::vector<PropertyDef>&& more) {
    for (auto& def : more) into.push_back(std::move(def));
}

// ---- enum vocabularies --------------------------------------------------

const char* vertAlignName(VertAlign a) {
    switch (a) {
        case VertAlign::Top:    return "top";
        case VertAlign::Middle: return "middle";
        case VertAlign::Bottom: return "bottom";
    }
    return "top";
}

bool vertAlignFromName(const std::string& text, VertAlign& out) {
    if (equalIgnoreCase(text, "top"))    { out = VertAlign::Top; return true; }
    if (equalIgnoreCase(text, "middle")) { out = VertAlign::Middle; return true; }
    if (equalIgnoreCase(text, "bottom")) { out = VertAlign::Bottom; return true; }
    return false;
}

// The marquee modes keep the words the XML attributes use.
const char* scrollName(CellScroll m) {
    switch (m) {
        case CellScroll::None:      return "none";
        case CellScroll::Automatic: return "automatic";
        case CellScroll::OnHover:   return "onmouseenter";
    }
    return "none";
}

bool scrollFromName(const std::string& text, CellScroll& out) {
    if (equalIgnoreCase(text, "none"))         { out = CellScroll::None; return true; }
    if (equalIgnoreCase(text, "automatic"))    { out = CellScroll::Automatic; return true; }
    if (equalIgnoreCase(text, "onmouseenter")) { out = CellScroll::OnHover; return true; }
    return false;
}

// The canonical name of a property-grid kind; the setter also accepts the
// aliases `PropertyItem::kindFromName` documents, and reads back as one of
// these five.
const char* kindName(PropertyItem::Kind k) {
    switch (k) {
        case PropertyItem::Kind::Bool:   return "bool";
        case PropertyItem::Kind::Number: return "number";
        case PropertyItem::Kind::Text:   return "text";
        case PropertyItem::Kind::Enum:   return "enum";
        case PropertyItem::Kind::Color:  return "color";
    }
    return "text";
}

// ---- multi-value strings -------------------------------------------------
// The attributes that carry several values in one string (`cells`, `options`,
// `path`) join and split with the separator their setter uses, so the value a
// property reads is always one the same property accepts back.

std::string joinList(const std::vector<std::string>& items, char sep) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += sep;
        out += items[i];
    }
    return out;
}

std::vector<std::string> splitList(const std::string& text, char sep) {
    std::vector<std::string> out;
    if (text.empty()) return out;
    size_t start = 0;
    while (true) {
        const size_t at = text.find(sep, start);
        out.push_back(text.substr(
            start, at == std::string::npos ? std::string::npos : at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    return out;
}

// ---- geometry -------------------------------------------------------

enum GeomField { GeomX, GeomY, GeomW, GeomH };

float geomAt(const Rect& r, GeomField field) {
    switch (field) {
        case GeomX: return r.x;
        case GeomY: return r.y;
        case GeomW: return r.w;
        case GeomH: return r.h;
    }
    return 0.0f;
}

// Writing moves the bounds AND pins the geometry spec to absolute, the same
// way `ogui.set` does, so a percent or fill component cannot undo the value on
// the next layout pass.
PropertyDef geomProp(const char* name, GeomField field, const char* doc) {
    return numberProp<Widget>(
        name, [field](const Widget& w) { return geomAt(w.bounds(), field); },
        [field](Widget& w, double v) {
            const float fv = static_cast<float>(v);
            Rect b = w.bounds();
            Rect spec = w.geometrySpec();
            switch (field) {
                case GeomX: b.x = spec.x = fv; break;
                case GeomY: b.y = spec.y = fv; break;
                case GeomW: b.w = spec.w = fv; break;
                case GeomH: b.h = spec.h = fv; break;
            }
            w.setGeometrySpec(spec, Widget::GeomAbsolute, Widget::GeomAbsolute,
                              Widget::GeomAbsolute, Widget::GeomAbsolute);
            w.setBounds(b);
            w.clampToBounds();
            return true;
        },
        doc);
}

enum LimitField { LimitMinW, LimitMinH, LimitMaxW, LimitMaxH };

float limitAt(const Rect& r, LimitField field) {
    switch (field) {
        case LimitMinW: return r.x;
        case LimitMinH: return r.y;
        case LimitMaxW: return r.w;
        case LimitMaxH: return r.h;
    }
    return 0.0f;
}

PropertyDef limitProp(const char* name, LimitField field, const char* doc) {
    return numberProp<Widget>(
        name,
        [field](const Widget& w) { return limitAt(w.sizeLimits(), field); },
        [field](Widget& w, double v) {
            const Rect l = w.sizeLimits();
            const float fv = static_cast<float>(v);
            float minW = l.x, minH = l.y, maxW = l.w, maxH = l.h;
            switch (field) {
                case LimitMinW: minW = fv; break;
                case LimitMinH: minH = fv; break;
                case LimitMaxW: maxW = fv; break;
                case LimitMaxH: maxH = fv; break;
            }
            w.setSizeLimits(minW, minH, maxW, maxH);
            w.clampToBounds();
            return true;
        },
        doc);
}

// ---- style-backed colours and spacing --------------------------------
// The Style holds the authored base value; applyVisualColor/Float pushes it
// into whichever field the widget actually draws with, so a registry write and
// an XML attribute end up in the same place.
PropertyDef styleColorProp(const char* name, Color Style::*field,
                           const char* visualKey, const char* doc) {
    auto def = colorProp<Widget>(
        name, [field](const Widget& w) { return static_cast<Color>(w.style().*field); },
        [field, visualKey](Widget& w, const Color& c) {
            Style s = w.style();
            s.*field = c;
            w.applyStyle(s);
            if (visualKey) w.applyVisualColor(visualKey, c);
            return true;
        },
        doc);
    def.defaultValue = formatColor((Style{}.*field));
    return def;
}

PropertyDef styleFloatProp(const char* name, float Style::*field,
                           const char* visualKey, const char* doc) {
    auto def = numberProp<Widget>(
        name, [field](const Widget& w) { return static_cast<double>(w.style().*field); },
        [field, visualKey](Widget& w, double v) {
            Style s = w.style();
            s.*field = static_cast<float>(v);
            w.applyStyle(s);
            if (visualKey) w.applyVisualFloat(visualKey, static_cast<float>(v));
            return true;
        },
        doc);
    def.defaultValue = formatNumber(Style{}.*field);
    return def;
}

PropertyDef styleIntProp(const char* name, int Style::*field, const char* doc) {
    auto def = numberProp<Widget>(
        name, [field](const Widget& w) { return static_cast<double>(w.style().*field); },
        [field](Widget& w, double v) {
            Style s = w.style();
            s.*field = static_cast<int>(v);
            w.applyStyle(s);
            return true;
        },
        doc);
    def.defaultValue = formatNumber(Style{}.*field);
    return def;
}

// ---- per-class tables -------------------------------------------------

std::vector<PropertyDef> widgetProps() {
    std::vector<PropertyDef> props{
        stringProp<Widget>("id", [](const Widget& w) { return w.id(); },
                           [](Widget& w, const std::string& v) {
                               w.setId(v);
                               return true;
                           },
                           "lookup key of this widget within its screen"),
        stringProp<Widget>("onclick", [](const Widget& w) { return w.binding(); },
                           [](Widget& w, const std::string& v) {
                               w.setBinding(v);
                               return true;
                           },
                           "action name fired on click (Kernel::bind / ogui.on)"),
        stringProp<Widget>("contextmenu",
                           [](const Widget& w) { return w.contextMenuName(); },
                           [](Widget& w, const std::string& v) {
                               w.setContextMenuName(v);
                               return true;
                           },
                           "id of the <contextmenu> popped on right-click"),
        stringProp<Widget>("tooltip", [](const Widget& w) { return w.tooltip(); },
                           [](Widget& w, const std::string& v) {
                               w.setTooltip(v);
                               return true;
                           },
                           "text shown while the pointer rests on the widget"),
        boolProp<Widget>("focusable",
                         [](const Widget& w) { return w.isFocusable(); },
                         [](Widget& w, bool v) {
                             w.setFocusable(v);
                             return true;
                         },
                         "whether Tab can stop on this widget"),
        stringProp<Widget>("accessible",
                           [](const Widget& w) { return w.accessibleName(); },
                           [](Widget& w, const std::string& v) {
                               w.setAccessibleName(v);
                               return true;
                           },
                           "accessible name for tooling (no visual effect)"),
        boolProp<Widget>(
            "visible", [](const Widget& w) { return w.style().isVisible; },
            [](Widget& w, bool v) {
                w.setVisible(v);
                return true;
            },
            "this widget's own visibility flag (an ancestor still hides it)"),
        boolProp<Widget>("enabled", [](const Widget& w) { return w.isEnabled(); },
                         [](Widget& w, bool v) {
                             w.setEnabled(v);
                             return true;
                         },
                         "false makes the widget inert: no input, no focus"),
        geomProp("x", GeomX, "left edge, relative to the parent"),
        geomProp("y", GeomY, "top edge, relative to the parent"),
        geomProp("width", GeomW, "width in logical units"),
        geomProp("height", GeomH, "height in logical units"),
        limitProp("minwidth", LimitMinW, "smallest width the layout may resolve"),
        limitProp("minheight", LimitMinH, "smallest height the layout may resolve"),
        limitProp("maxwidth", LimitMaxW, "largest width the layout may resolve"),
        limitProp("maxheight", LimitMaxH, "largest height the layout may resolve"),
        numberProp<Widget>(
            "weight", [](const Widget& w) { return w.weight(); },
            [](Widget& w, double v) {
                w.setWeight(static_cast<float>(v));
                return true;
            },
            "flex share of a column/row's leftover space (0 = authored size)"),
        styleColorProp("background", &Style::backgroundColor, "background",
                       "fill colour"),
        styleColorProp("color", &Style::textColor, "color",
                       "foreground / text colour"),
        styleColorProp("bordercolor", &Style::borderColor, "bordercolor",
                       "border colour"),
        // `border` is the shorthand the parser also reads: colour plus 1px.
        colorProp<Widget>(
            "border", [](const Widget& w) { return w.style().borderColor; },
            [](Widget& w, const Color& c) {
                Style s = w.style();
                s.borderColor = c;
                s.borderWidth = 1.0f;
                w.applyStyle(s);
                w.applyVisualColor("border", c);
                w.applyVisualFloat("borderwidth", 1.0f);
                return true;
            },
            "border colour, and 1px borderwidth"),
        styleFloatProp("borderwidth", &Style::borderWidth, "borderwidth",
                       "border thickness in logical pixels"),
        styleFloatProp("radius", &Style::radius, "radius", "corner radius"),
        styleFloatProp("padding", &Style::padding, nullptr,
                       "inner spacing"),
        styleFloatProp("margin", &Style::margin, nullptr, "outer spacing"),
        styleFloatProp("gap", &Style::gap, nullptr,
                       "child spacing for column/row stacks"),
        styleColorProp("shadowcolor", &Style::shadowColor, nullptr,
                       "drop shadow tint"),
        // `gradient` reads as the second ramp colour, opaque when unset.
        colorProp<Widget>(
            "gradient",
            [](const Widget& w) { return w.style().hasGradient ? w.style().gradientColor
                                                                : Color{}; },
            [](Widget& w, const Color& c) {
                Style s = w.style();
                s.gradientColor = c;
                s.hasGradient = true;
                w.applyStyle(s);
                return true;
            },
            "second colour of the fill gradient"),
        styleIntProp("fontsize", &Style::fontSize, "text size in logical pixels"),
        stringProp<Widget>(
            "font", [](const Widget& w) { return w.style().fontFamily; },
            [](Widget& w, const std::string& v) {
                Style s = w.style();
                s.fontFamily = v;
                w.applyStyle(s);
                return true;
            },
            "font family name"),
    };
    return props;
}

std::vector<PropertyDef> labelProps() {
    std::vector<PropertyDef> props{
        textProp<Label>(),
        boolProp<Label>(
            "wrap", [](const Label& l) { return l.wrap(); },
            [](Label& l, bool v) {
                l.setWrap(v);
                return true;
            },
            "break the text across lines instead of clipping it"),
        stringProp<Label>(
            "align",
            [](const Label& l) { return std::string(alignName(l.hAlign())); },
            [](Label& l, const std::string& v) {
                Align a = Align::Left;
                if (!alignFromName(v, a)) return false;
                l.setHAlign(a);
                return true;
            },
            "horizontal text placement", {"left", "center", "right"}),
    };
    addAll(props, fontProps<Label>(&Label::setFontFamily, &Label::setFontSize));
    addAll(props, {colorProp<Label>(
        "textcolor", [](const Label& l) { return l.textColor(); },
        [](Label& l, const Color& c) {
            l.setTextColor(c);
            return true;
        },
        "text colour as the widget holds it")});
    return props;
}

std::vector<PropertyDef> buttonProps() {
    std::vector<PropertyDef> props{
        textProp<Button>(),
        stringProp<Button>(
            "align",
            [](const Button& b) { return std::string(alignName(b.hAlign())); },
            [](Button& b, const std::string& v) {
                Align a = Align::Center;
                if (!alignFromName(v, a)) return false;
                b.setHAlign(a);
                return true;
            },
            "horizontal text placement", {"left", "center", "right"}),
        numberProp<Button>(
            "textpadding", [](const Button& b) { return b.textPadding(); },
            [](Button& b, double v) {
                b.setTextPadding(static_cast<float>(v));
                return true;
            },
            "inset used by the left/right text alignments"),
        boolProp<Button>(
            "pressed", [](const Button& b) { return b.pressed(); }, nullptr,
            "while the pointer is held down on it"),
    };
    addAll(props, fontProps<Button>(&Button::setFontFamily, &Button::setFontSize));
    return props;
}

std::vector<PropertyDef> imageProps() {
    return {
        stringProp<Image>(
            "source", [](const Image& i) { return i.imagePath(); },
            [](Image& i, const std::string& v) {
                i.setImagePath(v);
                return true;
            },
            "image file drawn into the bounds"),
        boolProp<Image>(
            "tile", [](const Image& i) { return i.tile(); },
            [](Image& i, bool v) {
                i.setTile(v);
                return true;
            },
            "repeat at native size instead of stretching"),
        numberProp<Image>(
            "zoom", [](const Image& i) { return i.zoom(); },
            [](Image& i, double v) {
                i.setZoom(static_cast<float>(v));
                return true;
            },
            "magnification 1..8: the bounds show that much less of the picture"),
        boolProp<Image>(
            "pinchzoom", [](const Image& i) { return i.pinchZoom(); },
            [](Image& i, bool v) {
                i.setPinchZoom(v);
                return true;
            },
            "a two-finger pinch scales this image instead of scrolling"),
    };
}

std::vector<PropertyDef> windowProps() {
    return {
        stringProp<Window>(
            "title", [](const Window& w) { return w.title(); },
            [](Window& w, const std::string& v) {
                w.setTitle(v);
                return true;
            },
            "caption in the title bar"),
        boolProp<Window>(
            "titlevisible", [](const Window& w) { return w.titleVisible(); },
            [](Window& w, bool v) {
                w.setTitleVisible(v);
                return true;
            },
            "hide the title bar (and its child offset)"),
    };
}

std::vector<PropertyDef> scrollviewProps() {
    return {
        numberProp<ScrollView>(
            "scrollx", [](const ScrollView& s) { return s.scrollPosition().x; },
            [](ScrollView& s, double v) {
                s.setScroll(static_cast<float>(v), s.scrollPosition().y);
                return true;
            },
            "content scrolled horizontally (<= 0)"),
        numberProp<ScrollView>(
            "scrolly", [](const ScrollView& s) { return s.scrollPosition().y; },
            [](ScrollView& s, double v) {
                s.setScroll(s.scrollPosition().x, static_cast<float>(v));
                return true;
            },
            "content scrolled vertically (<= 0)"),
        numberProp<ScrollView>(
            "scrollstep", [](const ScrollView& s) { return s.scrollStep(); },
            [](ScrollView& s, double v) {
                s.setScrollStep(static_cast<float>(v));
                return true;
            },
            "pixels moved per wheel notch"),
    };
}

std::vector<PropertyDef> textinputProps() {
    std::vector<PropertyDef> props{
        textProp<TextInput>("the text in the field"),
        stringProp<TextInput>(
            "placeholder", [](const TextInput& t) { return t.placeholder(); },
            [](TextInput& t, const std::string& v) {
                t.setPlaceholder(v);
                return true;
            },
            "greyed text shown while the field is empty"),
        numberProp<TextInput>(
            "caret", [](const TextInput& t) { return static_cast<double>(t.caret()); },
            [](TextInput& t, double v) {
                const size_t pos = static_cast<size_t>(v < 0 ? 0 : v);
                t.setSelection(pos, pos);
                return true;
            },
            "insertion point as a byte offset into text"),
    };
    addAll(props, fontProps<TextInput>(nullptr, &TextInput::setFontSize));
    return props;
}

std::vector<PropertyDef> checkboxProps() {
    return {
        textProp<Checkbox>("caption next to the box"),
        boolProp<Checkbox>(
            "checked", [](const Checkbox& c) { return c.checked(); },
            [](Checkbox& c, bool v) {
                c.setChecked(v);
                return true;
            },
            "tick state (programmatic writes do not fire change)"),
        boolProp<Checkbox>(
            "value", [](const Checkbox& c) { return c.checked(); },
            [](Checkbox& c, bool v) {
                c.setChecked(v);
                return true;
            },
            "alias of checked"),
    };
}

std::vector<PropertyDef> radiobuttonProps() {
    return {
        textProp<RadioButton>("caption next to the dot"),
        boolProp<RadioButton>(
            "checked", [](const RadioButton& r) { return r.checked(); },
            [](RadioButton& r, bool v) {
                r.setChecked(v);
                return true;
            },
            "whether this option is the selected one"),
        stringProp<RadioButton>(
            "group", [](const RadioButton& r) { return r.group(); },
            [](RadioButton& r, const std::string& v) {
                r.setGroup(v);
                return true;
            },
            "exclusive group this button belongs to"),
        colorProp<RadioButton>(
            "accent", [](const RadioButton& r) { return r.dotColor(); },
            [](RadioButton& r, const Color& c) {
                r.setDotColor(c);
                return true;
            },
            "filled dot colour"),
        colorProp<RadioButton>(
            "bordercolor", [](const RadioButton& r) { return r.circleColor(); },
            [](RadioButton& r, const Color& c) {
                r.setCircleColor(c);
                return true;
            },
            "ring colour"),
    };
}

std::vector<PropertyDef> toggleswitchProps() {
    return {
        textProp<ToggleSwitch>("caption next to the pill"),
        boolProp<ToggleSwitch>(
            "state", [](const ToggleSwitch& s) { return s.state(); },
            [](ToggleSwitch& s, bool v) {
                s.setState(v);
                return true;
            },
            "on/off position (programmatic writes do not fire change)"),
        boolProp<ToggleSwitch>(
            "checked", [](const ToggleSwitch& s) { return s.state(); },
            [](ToggleSwitch& s, bool v) {
                s.setState(v);
                return true;
            },
            "alias of state"),
    };
}

std::vector<PropertyDef> separatorProps() {
    return {
        numberProp<Separator>(
            "thickness", [](const Separator& s) { return s.thickness(); },
            [](Separator& s, double v) {
                s.setThickness(static_cast<float>(v));
                return true;
            },
            "rule width in logical pixels"),
        colorProp<Separator>(
            "linecolor", [](const Separator& s) { return s.lineColor(); },
            [](Separator& s, const Color& c) {
                s.setLineColor(c);
                return true;
            },
            "rule colour as the widget holds it"),
    };
}

std::vector<PropertyDef> progressbarProps() {
    return {
        numberProp<ProgressBar>(
            "value", [](const ProgressBar& p) { return p.value(); },
            [](ProgressBar& p, double v) {
                p.setValue(static_cast<float>(v));
                return true;
            },
            "fill fraction, 0 to 1"),
        boolProp<ProgressBar>(
            "indeterminate",
            [](const ProgressBar& p) { return p.indeterminate(); },
            [](ProgressBar& p, bool v) {
                p.setIndeterminate(v);
                return true;
            },
            "slide a block instead of showing a fraction"),
        colorProp<ProgressBar>(
            "barcolor", [](const ProgressBar& p) { return p.barColor(); },
            [](ProgressBar& p, const Color& c) {
                p.setBarColor(c);
                return true;
            },
            "filled portion colour"),
    };
}

std::vector<PropertyDef> sliderProps() {
    return {
        numberProp<Slider>(
            "value", [](const Slider& s) { return s.value(); },
            [](Slider& s, double v) {
                s.setValue(static_cast<float>(v));
                return true;
            },
            "thumb position inside the range"),
        numberProp<Slider>(
            "min", [](const Slider& s) { return s.minimum(); },
            [](Slider& s, double v) {
                s.setRange(static_cast<float>(v), s.maximum());
                return true;
            },
            "low end of the range"),
        numberProp<Slider>(
            "max", [](const Slider& s) { return s.maximum(); },
            [](Slider& s, double v) {
                s.setRange(s.minimum(), static_cast<float>(v));
                return true;
            },
            "high end of the range"),
    };
}

std::vector<PropertyDef> rangesliderProps() {
    return {
        numberProp<RangeSlider>(
            "low", [](const RangeSlider& r) { return r.low(); },
            [](RangeSlider& r, double v) {
                r.setLow(static_cast<float>(v));
                return true;
            },
            "lower handle position"),
        numberProp<RangeSlider>(
            "high", [](const RangeSlider& r) { return r.high(); },
            [](RangeSlider& r, double v) {
                r.setHigh(static_cast<float>(v));
                return true;
            },
            "upper handle position"),
        numberProp<RangeSlider>(
            "min", [](const RangeSlider& r) { return r.minimum(); },
            [](RangeSlider& r, double v) {
                r.setRange(static_cast<float>(v), r.maximum());
                return true;
            },
            "low end of the range"),
        numberProp<RangeSlider>(
            "max", [](const RangeSlider& r) { return r.maximum(); },
            [](RangeSlider& r, double v) {
                r.setRange(r.minimum(), static_cast<float>(v));
                return true;
            },
            "high end of the range"),
    };
}

std::vector<PropertyDef> spinboxProps() {
    return {
        numberProp<SpinBox>(
            "value", [](const SpinBox& s) { return s.value(); },
            [](SpinBox& s, double v) {
                s.setValue(static_cast<float>(v));
                return true;
            },
            "current number"),
        numberProp<SpinBox>(
            "min", [](const SpinBox& s) { return s.minimum(); },
            [](SpinBox& s, double v) {
                s.setRange(static_cast<float>(v), s.maximum());
                return true;
            },
            "low end of the range"),
        numberProp<SpinBox>(
            "max", [](const SpinBox& s) { return s.maximum(); },
            [](SpinBox& s, double v) {
                s.setRange(s.minimum(), static_cast<float>(v));
                return true;
            },
            "high end of the range"),
        numberProp<SpinBox>(
            "decimals", [](const SpinBox& s) { return s.decimals(); },
            [](SpinBox& s, double v) {
                s.setDecimals(static_cast<int>(v));
                return true;
            },
            "digits shown after the decimal point"),
        stringProp<SpinBox>(
            "prefix", [](const SpinBox& s) { return s.prefix(); },
            [](SpinBox& s, const std::string& v) {
                s.setPrefix(v);
                return true;
            },
            "text drawn before the number"),
        stringProp<SpinBox>(
            "suffix", [](const SpinBox& s) { return s.suffix(); },
            [](SpinBox& s, const std::string& v) {
                s.setSuffix(v);
                return true;
            },
            "text drawn after the number"),
    };
}

std::vector<PropertyDef> optionspinProps() {
    return {
        selectedProp(&OptionSpin::index, &OptionSpin::setIndex),
        stringProp<OptionSpin>(
            "value", [](const OptionSpin& o) { return o.value(); },
            [](OptionSpin& o, const std::string& v) {
                o.setValue(v);
                return true;
            },
            "text of the selected option"),
        boolProp<OptionSpin>(
            "wrap", [](const OptionSpin& o) { return o.wrap(); },
            [](OptionSpin& o, bool v) {
                o.setWrap(v);
                return true;
            },
            "cycle past the first/last option"),
    };
}

std::vector<PropertyDef> badgeProps() {
    return {
        textProp<Badge>("caption inside the pill"),
        numberProp<Badge>(
            "count", [](const Badge& b) { return static_cast<double>(b.count()); },
            [](Badge& b, double v) {
                b.setCount(static_cast<int>(v));
                return true;
            },
            "trailing number (< 0 hides it)"),
        boolProp<Badge>(
            "removable", [](const Badge& b) { return b.removable(); },
            [](Badge& b, bool v) {
                b.setRemovable(v);
                return true;
            },
            "draw the [x] button that fires the binding"),
    };
}

std::vector<PropertyDef> statusbarProps() {
    return {
        stringProp<StatusBar>(
            "left", [](const StatusBar& s) { return s.left(); },
            [](StatusBar& s, const std::string& v) {
                s.setLeft(v);
                return true;
            },
            "text in the left slot"),
        stringProp<StatusBar>(
            "center", [](const StatusBar& s) { return s.center(); },
            [](StatusBar& s, const std::string& v) {
                s.setCenter(v);
                return true;
            },
            "text in the centre slot"),
        stringProp<StatusBar>(
            "right", [](const StatusBar& s) { return s.right(); },
            [](StatusBar& s, const std::string& v) {
                s.setRight(v);
                return true;
            },
            "text in the right slot"),
    };
}

std::vector<PropertyDef> tabsProps() {
    return {
        selectedProp(&Tabs::active, &Tabs::setActive),
        numberProp<Tabs>(
            "tabheight", [](const Tabs& t) { return t.headerHeight(); },
            [](Tabs& t, double v) {
                t.setHeaderHeight(static_cast<float>(v));
                return true;
            },
            "height of the header strip"),
        boolProp<Tabs>(
            "scrolling", [](const Tabs& t) { return t.scrollable(); },
            [](Tabs& t, bool v) {
                t.setScrollable(v);
                return true;
            },
            "scroll the header strip instead of truncating it"),
    };
}

std::vector<PropertyDef> comboboxProps() {
    return {
        selectedProp(&ComboBox::selectedIndex, &ComboBox::setSelectedIndex),
        numberProp<ComboBox>(
            "count", [](const ComboBox& c) { return static_cast<double>(c.count()); },
            nullptr, "number of options"),
        stringProp<ComboBox>(
            "value", [](const ComboBox& c) { return c.selectedLabel(); }, nullptr,
            "text of the selected option"),
        boolProp<ComboBox>(
            "open", [](const ComboBox& c) { return c.popupOpen(); }, nullptr,
            "while the dropdown is showing"),
    };
}

std::vector<PropertyDef> listboxProps() {
    return {
        selectedProp(&ListBox::selectedIndex, &ListBox::setSelectedIndex),
        numberProp<ListBox>(
            "count", [](const ListBox& l) { return static_cast<double>(l.count()); },
            nullptr, "number of rows"),
        stringProp<ListBox>(
            "value", [](const ListBox& l) { return l.selectedLabel(); }, nullptr,
            "text of the selected row"),
    };
}

std::vector<PropertyDef> listitemProps() {
    return {
        stringProp<ListItem>(
            "label", [](const ListItem& i) { return i.label(); },
            [](ListItem& i, const std::string& v) {
                i.setLabel(v);
                return true;
            },
            "text shown for the row"),
        stringProp<ListItem>(
            "value", [](const ListItem& i) { return i.value(); },
            [](ListItem& i, const std::string& v) {
                i.setValue(v);
                return true;
            },
            "value the row carries"),
    };
}

std::vector<PropertyDef> treeitemProps() {
    return {
        stringProp<TreeItem>(
            "label", [](const TreeItem& i) { return i.label(); },
            [](TreeItem& i, const std::string& v) {
                i.setLabel(v);
                return true;
            },
            "text shown for the node"),
        stringProp<TreeItem>(
            "value", [](const TreeItem& i) { return i.value(); },
            [](TreeItem& i, const std::string& v) {
                i.setValue(v);
                return true;
            },
            "value the node carries"),
        boolProp<TreeItem>(
            "expanded", [](const TreeItem& i) { return i.expanded(); },
            [](TreeItem& i, bool v) {
                i.setExpanded(v);
                return true;
            },
            "whether the children are shown"),
        boolProp<TreeItem>(
            "folder", [](const TreeItem& i) { return i.folder(); },
            [](TreeItem& i, bool v) {
                i.setFolder(v);
                return true;
            },
            "folder icon instead of a file icon"),
        boolProp<TreeItem>(
            "checked", [](const TreeItem& i) { return i.checked(); },
            [](TreeItem& i, bool v) {
                i.setChecked(v);
                return true;
            },
            "checkbox state of the node"),
    };
}

std::vector<PropertyDef> treeviewProps() {
    std::vector<PropertyDef> props{
        selectedProp(&TreeView::selectedIndex, &TreeView::setSelectedIndex),
        numberProp<TreeView>(
            "count", [](const TreeView& t) { return static_cast<double>(t.rowCount()); },
            nullptr, "number of visible rows"),
        stringProp<TreeView>(
            "value", [](const TreeView& t) { return t.selectedLabel(); }, nullptr,
            "text of the selected node"),
        boolProp<TreeView>(
            "checks", [](const TreeView& t) { return t.showChecks(); },
            [](TreeView& t, bool v) {
                t.setShowChecks(v);
                return true;
            },
            "draw a checkbox in every row"),
        boolProp<TreeView>(
            "icons", [](const TreeView& t) { return t.showIcons(); },
            [](TreeView& t, bool v) {
                t.setShowIcons(v);
                return true;
            },
            "draw a folder or file glyph per row"),
        boolProp<TreeView>(
            "guides", [](const TreeView& t) { return t.showGuides(); },
            [](TreeView& t, bool v) {
                t.setShowGuides(v);
                return true;
            },
            "connect each node to its parent with a dotted rule"),
        stringProp<TreeView>(
            "checkedlabels", [](const TreeView& t) { return t.checkedLabels(); }, nullptr,
            "text of the ticked nodes, `;`-separated"),
        numberProp<TreeView>(
            "rowheight", [](const TreeView& t) { return t.rowHeight(); },
            [](TreeView& t, double v) {
                t.setRowHeight(static_cast<float>(v));
                return true;
            },
            "height of one row"),
        numberProp<TreeView>(
            "indent", [](const TreeView& t) { return t.indent(); },
            [](TreeView& t, double v) {
                t.setIndent(static_cast<float>(v));
                return true;
            },
            "horizontal inset per depth level"),
        colorProp<TreeView>(
            "highlight", [](const TreeView& t) { return t.highlightColor(); },
            [](TreeView& t, const Color& c) {
                t.setHighlightColor(c);
                return true;
            },
            "fill behind the selected row"),
        colorProp<TreeView>(
            "accent", [](const TreeView& t) { return t.accentColor(); },
            [](TreeView& t, const Color& c) {
                t.setAccentColor(c);
                return true;
            },
            "tint of the toggles, checks and guide lines"),
    };
    addAll(props,
           fontProps<TreeView>(&TreeView::setFontFamily, &TreeView::setFontSize));
    return props;
}

std::vector<PropertyDef> tableprops() {
    std::vector<PropertyDef> props{
        selectedProp(&Table::selectedIndex, &Table::setSelectedIndex),
        numberProp<Table>(
            "rowcount", [](const Table& t) { return static_cast<double>(t.rowCount()); },
            nullptr, "number of rows"),
        numberProp<Table>(
            "columncount",
            [](const Table& t) { return static_cast<double>(t.columnCount()); },
            nullptr, "number of columns"),
        boolProp<Table>(
            "sortable", [](const Table& t) { return t.sortable(); },
            [](Table& t, bool v) {
                t.setSortable(v);
                return true;
            },
            "clicking a header reorders the rows"),
        numberProp<Table>(
            "sortcolumn", [](const Table& t) {
                return static_cast<double>(t.sortColumn());
            },
            nullptr, "column the rows are sorted by (-1 = document order)"),
        boolProp<Table>(
            "sortascending", [](const Table& t) { return t.sortAscending(); }, nullptr,
            "direction of the current sort"),
        numberProp<Table>(
            "rowheight", [](const Table& t) { return t.rowHeight(); },
            [](Table& t, double v) {
                t.setRowHeight(static_cast<float>(v));
                return true;
            },
            "height of one row"),
        numberProp<Table>(
            "headerheight", [](const Table& t) { return t.headerHeight(); },
            [](Table& t, double v) {
                t.setHeaderHeight(static_cast<float>(v));
                return true;
            },
            "height of the header strip"),
        colorProp<Table>(
            "headerbackground", [](const Table& t) { return t.headerBgColor(); },
            [](Table& t, const Color& c) {
                t.setHeaderBgColor(c);
                return true;
            },
            "fill colour of the header strip"),
        colorProp<Table>(
            "highlight", [](const Table& t) { return t.highlightColor(); },
            [](Table& t, const Color& c) {
                t.setHighlightColor(c);
                return true;
            },
            "fill behind the selected row"),
    };
    addAll(props, fontProps<Table>(&Table::setFontFamily, &Table::setFontSize));
    return props;
}

std::vector<PropertyDef> keybindProps() {
    return {
        stringProp<Keybind>(
            "value", [](const Keybind& k) { return k.value(); },
            [](Keybind& k, const std::string& v) {
                if (!k.setValue(v)) {
                    return false;
                }
                return true;
            },
            "key combo such as \"ctrl+shift+k\" (\"\" clears)"),
        boolProp<Keybind>(
            "capturing", [](const Keybind& k) { return k.capturing(); }, nullptr,
            "while it is recording a key press"),
    };
}

std::vector<PropertyDef> commandpaletteProps() {
    std::vector<PropertyDef> props{
        selectedProp(&CommandPalette::selectedIndex,
                     &CommandPalette::setSelectedIndex),
        stringProp<CommandPalette>(
            "query", [](const CommandPalette& p) { return p.query(); },
            [](CommandPalette& p, const std::string& v) {
                p.setQuery(v);
                return true;
            },
            "text the list is filtered by"),
        stringProp<CommandPalette>(
            "placeholder", [](const CommandPalette& p) { return p.placeholder(); },
            [](CommandPalette& p, const std::string& v) {
                p.setPlaceholder(v);
                return true;
            },
            "text shown while the query is empty"),
        numberProp<CommandPalette>(
            "count", [](const CommandPalette& p) {
                return static_cast<double>(p.count());
            },
            nullptr, "number of commands"),
        numberProp<CommandPalette>(
            "matches", [](const CommandPalette& p) {
                return static_cast<double>(p.matchCount());
            },
            nullptr, "rows the query leaves visible"),
        numberProp<CommandPalette>(
            "historycount", [](const CommandPalette& p) {
                return static_cast<double>(p.historyCount());
            },
            nullptr, "commands already run"),
        numberProp<CommandPalette>(
            "maxhistory", [](const CommandPalette& p) {
                return static_cast<double>(p.maxHistory());
            },
            [](CommandPalette& p, double v) {
                p.setMaxHistory(static_cast<int>(v));
                return true;
            },
            "how many runs are kept"),
        numberProp<CommandPalette>(
            "rowheight", [](const CommandPalette& p) { return p.rowHeight(); },
            [](CommandPalette& p, double v) {
                p.setRowHeight(static_cast<float>(v));
                return true;
            },
            "height of one result row"),
        numberProp<CommandPalette>(
            "headerheight", [](const CommandPalette& p) { return p.headerHeight(); },
            [](CommandPalette& p, double v) {
                p.setHeaderHeight(static_cast<float>(v));
                return true;
            },
            "height of the search field"),
    };
    addAll(props, fontProps<CommandPalette>(&CommandPalette::setFontFamily,
                                           &CommandPalette::setFontSize));
    return props;
}

std::vector<PropertyDef> consoleProps() {
    return {
        numberProp<Console>(
            "linecount", [](const Console& c) {
                return static_cast<double>(c.lineCount());
            },
            nullptr, "log lines currently held"),
        numberProp<Console>(
            "maxlines", [](const Console& c) { return static_cast<double>(c.maxLines()); },
            [](Console& c, double v) {
                c.setMaxLines(static_cast<int>(v));
                return true;
            },
            "line cap; oldest lines are dropped"),
    };
}

std::vector<PropertyDef> modalProps() {
    return {
        stringProp<Modal>(
            "title", [](const Modal& m) { return m.title(); },
            [](Modal& m, const std::string& v) {
                m.setTitle(v);
                return true;
            },
            "caption of the dialog panel"),
        boolProp<Modal>(
            "open", [](const Modal& m) { return m.isOpen(); },
            [](Modal& m, bool v) {
                m.setOpen(v);
                return true;
            },
            "whether the dialog and its dim are showing"),
        numberProp<Modal>(
            "panelwidth", [](const Modal& m) { return m.panelSize().w; },
            [](Modal& m, double v) {
                m.setPanelSize(static_cast<float>(v), m.panelSize().h);
                return true;
            },
            "width of the dialog panel"),
        numberProp<Modal>(
            "panelheight", [](const Modal& m) { return m.panelSize().h; },
            [](Modal& m, double v) {
                m.setPanelSize(m.panelSize().w, static_cast<float>(v));
                return true;
            },
            "height of the dialog panel"),
    };
}

std::vector<PropertyDef> splitterProps() {
    return {
        numberProp<Splitter>(
            "split", [](const Splitter& s) { return s.split(); },
            [](Splitter& s, double v) {
                s.setSplit(static_cast<float>(v));
                return true;
            },
            "divider position as a fraction (0..1)"),
        boolProp<Splitter>(
            "vertical", [](const Splitter& s) { return s.isVertical(); },
            [](Splitter& s, bool v) {
                s.setVertical(v);
                return true;
            },
            "vertical divider (two panes side by side)"),
    };
}

std::vector<PropertyDef> groupProps() {
    return {
        stringProp<Group>(
            "name", [](const Group& g) { return g.groupName(); },
            [](Group& g, const std::string& v) {
                g.setGroupName(v);
                return true;
            },
            "caption drawn in the frame"),
    };
}

std::vector<PropertyDef> panelProps() {
    return {
        colorProp<Panel>(
            "bgcolor", [](const Panel& p) { return p.bgColor(); },
            [](Panel& p, const Color& c) {
                p.setBgColor(c);
                return true;
            },
            "fill colour as the panel holds it"),
    };
}

// ---- stacks and grids ----------------------------------------------------

std::vector<PropertyDef> columnProps() {
    return {
        stringProp<Column>(
            "align", [](const Column& c) { return std::string(alignName(c.childAlign())); },
            [](Column& c, const std::string& v) {
                Align a = Align::Left;
                if (!alignFromName(v, a)) return false;
                c.setChildAlign(a);
                return true;
            },
            "horizontal placement of the children",
            {"left", "center", "right"}),
    };
}

std::vector<PropertyDef> rowProps() {
    return {
        stringProp<Row>(
            "valign",
            [](const Row& r) { return std::string(vertAlignName(r.childValign())); },
            [](Row& r, const std::string& v) {
                VertAlign a = VertAlign::Top;
                if (!vertAlignFromName(v, a)) return false;
                r.setChildValign(a);
                return true;
            },
            "vertical placement of the shorter children",
            {"top", "middle", "bottom"}),
        boolProp<Row>(
            "wrap", [](const Row& r) { return r.wrap(); },
            [](Row& r, bool v) {
                r.setWrap(v);
                return true;
            },
            "flow children onto extra lines when they overflow"),
    };
}

// `columns` parses a track spec and keeps no copy of the text, so it is
// write-only; the resolved shape reads back as `colcount`.
std::vector<PropertyDef> gridProps() {
    return {
        stringProp<Grid>(
            "columns", nullptr,
            [](Grid& g, const std::string& v) {
                g.setCols(v);
                return true;
            },
            "track spec: a bare count (\"3\") or a list (\"120,1fr,40%\")"),
        numberProp<Grid>(
            "colcount", [](const Grid& g) { return static_cast<double>(g.colCount()); },
            nullptr, "number of tracks the spec resolved to"),
        numberProp<Grid>(
            "gap", [](const Grid& g) { return g.gap(); },
            [](Grid& g, double v) {
                g.setGap(static_cast<float>(v));
                return true;
            },
            "spacing between tracks"),
        numberProp<Grid>(
            "rowheight", [](const Grid& g) { return g.rowHeight(); },
            [](Grid& g, double v) {
                g.setRowHeight(static_cast<float>(v));
                return true;
            },
            "fixed row height (0 sizes rows to their content)"),
    };
}

std::vector<PropertyDef> accordionProps() {
    return {
        boolProp<Accordion>(
            "exclusive", [](const Accordion& a) { return a.exclusive(); },
            [](Accordion& a, bool v) {
                a.setExclusive(v);
                return true;
            },
            "opening one section closes the others"),
        numberProp<Accordion>(
            "gap", [](const Accordion& a) { return a.gap(); },
            [](Accordion& a, double v) {
                a.setGap(static_cast<float>(v));
                return true;
            },
            "spacing between sections"),
    };
}

std::vector<PropertyDef> accordionsectionProps() {
    return {
        stringProp<AccordionSection>(
            "header", [](const AccordionSection& s) { return s.header(); },
            [](AccordionSection& s, const std::string& v) {
                s.setHeader(v);
                return true;
            },
            "caption of the clickable strip"),
        boolProp<AccordionSection>(
            "expanded", [](const AccordionSection& s) { return s.expanded(); },
            [](AccordionSection& s, bool v) {
                s.setExpanded(v);
                return true;
            },
            "whether the section's content is shown"),
        numberProp<AccordionSection>(
            "headerheight", [](const AccordionSection& s) {
                return static_cast<double>(s.headerHeight());
            },
            [](AccordionSection& s, double v) {
                s.setHeaderHeight(static_cast<float>(v));
                return true;
            },
            "height of the header strip"),
    };
}

// ---- captions of the composite leaf widgets -------------------------------

std::vector<PropertyDef> tabProps() {
    return {
        stringProp<Tab>(
            "label", [](const Tab& t) { return t.label(); },
            [](Tab& t, const std::string& v) {
                t.setLabel(v);
                return true;
            },
            "text on the header button"),
    };
}

std::vector<PropertyDef> combooptionProps() {
    return {
        stringProp<ComboOption>(
            "label", [](const ComboOption& o) { return o.label(); },
            [](ComboOption& o, const std::string& v) {
                o.setLabel(v);
                return true;
            },
            "text shown in the list"),
        stringProp<ComboOption>(
            "value", [](const ComboOption& o) { return o.value(); },
            [](ComboOption& o, const std::string& v) {
                o.setValue(v);
                return true;
            },
            "value the option carries"),
    };
}

std::vector<PropertyDef> crumbitemProps() {
    return {
        stringProp<CrumbItem>(
            "label", [](const CrumbItem& c) { return c.label(); },
            [](CrumbItem& c, const std::string& v) {
                c.setLabel(v);
                return true;
            },
            "text of this segment"),
    };
}

std::vector<PropertyDef> commandactionProps() {
    return {
        stringProp<CommandAction>(
            "name", [](const CommandAction& a) { return a.name(); },
            [](CommandAction& a, const std::string& v) {
                a.setName(v);
                return true;
            },
            "text the palette matches and runs"),
        stringProp<CommandAction>(
            "hint", [](const CommandAction& a) { return a.hint(); },
            [](CommandAction& a, const std::string& v) {
                a.setHint(v);
                return true;
            },
            "description shown beside the name"),
        stringProp<CommandAction>(
            "key", [](const CommandAction& a) { return a.key(); },
            [](CommandAction& a, const std::string& v) {
                a.setKey(v);
                return true;
            },
            "shortcut printed at the right edge"),
    };
}

// ---- table parts ----------------------------------------------------------

std::vector<PropertyDef> tablecolumnProps() {
    return {
        stringProp<TableColumn>(
            "label", [](const TableColumn& c) { return c.label(); },
            [](TableColumn& c, const std::string& v) {
                c.setLabel(v);
                return true;
            },
            "text drawn in the header cell"),
        numberProp<TableColumn>(
            "width", [](const TableColumn& c) { return c.width(); },
            [](TableColumn& c, double v) {
                c.setWidth(static_cast<float>(v));
                return true;
            },
            "column width (0 shares the leftover space)"),
        boolProp<TableColumn>(
            "resizable", [](const TableColumn& c) { return c.resizable(); },
            [](TableColumn& c, bool v) {
                c.setResizable(v);
                return true;
            },
            "drag the header edge to resize"),
        boolProp<TableColumn>(
            "editable", [](const TableColumn& c) { return c.editable(); },
            [](TableColumn& c, bool v) {
                c.setEditable(v);
                return true;
            },
            "double-click opens a cell editor"),
        stringProp<TableColumn>(
            "scrollingtext",
            [](const TableColumn& c) { return std::string(scrollName(c.scrollMode())); },
            [](TableColumn& c, const std::string& v) {
                CellScroll m = CellScroll::None;
                if (!scrollFromName(v, m)) return false;
                c.setScrollMode(m);
                return true;
            },
            "marquee for text wider than the column",
            {"none", "automatic", "onmouseenter"}),
    };
}

std::vector<PropertyDef> tablerowProps() {
    return {
        stringProp<TableRow>(
            "cells", [](const TableRow& r) { return joinList(r.cells(), ';'); },
            [](TableRow& r, const std::string& v) {
                r.setCells(v);
                return true;
            },
            "cell texts, `;`-separated"),
        stringProp<TableRow>(
            "value", [](const TableRow& r) { return r.value(); },
            [](TableRow& r, const std::string& v) {
                r.setValue(v);
                return true;
            },
            "value the row carries"),
        stringProp<TableRow>(
            "editablecells",
            [](const TableRow& r) { return r.editableColumns(); },
            [](TableRow& r, const std::string& v) {
                r.setEditableColumns(v);
                return true;
            },
            "column indexes that open an editor (\"0,2\", \"all\")"),
        boolProp<TableRow>(
            "resizable", [](const TableRow& r) { return r.rowResizable(); },
            [](TableRow& r, bool v) {
                r.setRowResizable(v);
                return true;
            },
            "row-wide default for resizable"),
        stringProp<TableRow>(
            "scrollingtext",
            [](const TableRow& r) { return std::string(scrollName(r.rowScrollMode())); },
            [](TableRow& r, const std::string& v) {
                CellScroll m = CellScroll::None;
                if (!scrollFromName(v, m)) return false;
                r.setRowScrollMode(m);
                return true;
            },
            "row-wide default for scrollingtext",
            {"none", "automatic", "onmouseenter"}),
    };
}

std::vector<PropertyDef> tablecellProps() {
    return {
        textProp<TableCell>("text drawn in the cell"),
        boolProp<TableCell>(
            "editable", [](const TableCell& c) { return c.editable(); },
            [](TableCell& c, bool v) {
                c.setEditable(v);
                return true;
            },
            "this cell opens an editor"),
        boolProp<TableCell>(
            "resizable", [](const TableCell& c) { return c.resizable(); },
            [](TableCell& c, bool v) {
                c.setResizable(v);
                return true;
            },
            "this cell's column may be dragged"),
        stringProp<TableCell>(
            "scrollingtext",
            [](const TableCell& c) { return std::string(scrollName(c.scrollMode())); },
            [](TableCell& c, const std::string& v) {
                CellScroll m = CellScroll::None;
                if (!scrollFromName(v, m)) return false;
                c.setScrollMode(m);
                return true;
            },
            "marquee for this cell",
            {"none", "automatic", "onmouseenter"}),
    };
}

// ---- tree table -----------------------------------------------------------

std::vector<PropertyDef> treetablenodeProps() {
    return {
        stringProp<TreeTableNode>(
            "label", [](const TreeTableNode& n) { return n.label(); },
            [](TreeTableNode& n, const std::string& v) {
                n.setLabel(v);
                return true;
            },
            "text of the tree column"),
        stringProp<TreeTableNode>(
            "cells", [](const TreeTableNode& n) { return joinList(n.cells(), ';'); },
            [](TreeTableNode& n, const std::string& v) {
                n.setCells(v);
                return true;
            },
            "extra column texts, `;`-separated"),
        boolProp<TreeTableNode>(
            "expanded", [](const TreeTableNode& n) { return n.expanded(); },
            [](TreeTableNode& n, bool v) {
                n.setExpanded(v);
                return true;
            },
            "whether the children are shown"),
    };
}

std::vector<PropertyDef> treetableProps() {
    std::vector<PropertyDef> props{
        selectedProp(&TreeTable::selectedIndex, &TreeTable::setSelectedIndex),
        numberProp<TreeTable>(
            "rowcount", [](const TreeTable& t) {
                return static_cast<double>(t.rowCount());
            },
            nullptr, "number of visible rows"),
        numberProp<TreeTable>(
            "columncount", [](const TreeTable& t) {
                return static_cast<double>(t.columnCount());
            },
            nullptr, "number of columns"),
        boolProp<TreeTable>(
            "sortable", [](const TreeTable& t) { return t.sortable(); },
            [](TreeTable& t, bool v) {
                t.setSortable(v);
                return true;
            },
            "clicking a header reorders the nodes"),
        numberProp<TreeTable>(
            "rowheight", [](const TreeTable& t) { return t.rowHeight(); },
            [](TreeTable& t, double v) {
                t.setRowHeight(static_cast<float>(v));
                return true;
            },
            "height of one row"),
        numberProp<TreeTable>(
            "headerheight", [](const TreeTable& t) { return t.headerHeight(); },
            [](TreeTable& t, double v) {
                t.setHeaderHeight(static_cast<float>(v));
                return true;
            },
            "height of the header strip"),
        numberProp<TreeTable>(
            "indent", [](const TreeTable& t) { return t.indent(); },
            [](TreeTable& t, double v) {
                t.setIndent(static_cast<float>(v));
                return true;
            },
            "horizontal inset per depth level"),
        colorProp<TreeTable>(
            "headerbackground", [](const TreeTable& t) { return t.headerBgColor(); },
            [](TreeTable& t, const Color& c) {
                t.setHeaderBgColor(c);
                return true;
            },
            "fill colour of the header strip"),
        colorProp<TreeTable>(
            "highlight", [](const TreeTable& t) { return t.highlightColor(); },
            [](TreeTable& t, const Color& c) {
                t.setHighlightColor(c);
                return true;
            },
            "fill behind the selected row"),
        colorProp<TreeTable>(
            "accent", [](const TreeTable& t) { return t.accentColor(); },
            [](TreeTable& t, const Color& c) {
                t.setAccentColor(c);
                return true;
            },
            "tint of the toggles and guide lines"),
        numberProp<TreeTable>(
            "sortcolumn", [](const TreeTable& t) {
                return static_cast<double>(t.sortColumn());
            },
            nullptr, "column the rows are sorted by (-1 = document order)"),
        boolProp<TreeTable>(
            "sortascending", [](const TreeTable& t) { return t.sortAscending(); },
            nullptr, "direction of the current sort"),
    };
    addAll(props, fontProps<TreeTable>(&TreeTable::setFontFamily, &TreeTable::setFontSize));
    return props;
}

// ---- property grid --------------------------------------------------------

std::vector<PropertyDef> propertyitemProps() {
    return {
        stringProp<PropertyItem>(
            "name", [](const PropertyItem& p) { return p.name(); },
            [](PropertyItem& p, const std::string& v) {
                p.setName(v);
                return true;
            },
            "label shown in the left column"),
        stringProp<PropertyItem>(
            "category", [](const PropertyItem& p) { return p.group(); },
            [](PropertyItem& p, const std::string& v) {
                p.setGroup(v);
                return true;
            },
            "group header the item sits under"),
        stringProp<PropertyItem>(
            "type", [](const PropertyItem& p) { return std::string(kindName(p.kind())); },
            [](PropertyItem& p, const std::string& v) {
                p.setKind(PropertyItem::kindFromName(v));
                return true;
            },
            "editor the grid builds for the value",
            {"bool", "number", "text", "enum", "color"}),
        stringProp<PropertyItem>(
            "value", [](const PropertyItem& p) { return p.value(); },
            [](PropertyItem& p, const std::string& v) {
                p.setValue(v);
                return true;
            },
            "value as text"),
        boolProp<PropertyItem>(
            "readonly", [](const PropertyItem& p) { return p.readOnly(); },
            [](PropertyItem& p, bool v) {
                p.setReadOnly(v);
                return true;
            },
            "the grid refuses edits of this item"),
        stringProp<PropertyItem>(
            "options", [](const PropertyItem& p) { return joinList(p.options(), ';'); },
            [](PropertyItem& p, const std::string& v) {
                p.setOptions(splitList(v, ';'));
                return true;
            },
            "`;`-separated choices for an enum item"),
    };
}

std::vector<PropertyDef> propertygridProps() {
    return {
        selectedProp(&PropertyGrid::selectedIndex, &PropertyGrid::setSelectedIndex),
        numberProp<PropertyGrid>(
            "count", [](const PropertyGrid& g) { return static_cast<double>(g.count()); },
            nullptr, "number of items collected"),
        numberProp<PropertyGrid>(
            "rowheight", [](const PropertyGrid& g) { return g.rowHeight(); },
            [](PropertyGrid& g, double v) {
                g.setRowHeight(static_cast<float>(v));
                return true;
            },
            "height of one row"),
        stringProp<PropertyGrid>(
            "error", [](const PropertyGrid& g) { return g.errorText(); }, nullptr,
            "message of the last rejected edit"),
        colorProp<PropertyGrid>(
            "labelcolor", [](const PropertyGrid& g) { return g.labelColor(); },
            [](PropertyGrid& g, const Color& c) {
                g.setLabelColor(c);
                return true;
            },
            "text colour of the left column"),
        colorProp<PropertyGrid>(
            "valuecolor", [](const PropertyGrid& g) { return g.valueColor(); },
            [](PropertyGrid& g, const Color& c) {
                g.setValueColor(c);
                return true;
            },
            "text colour of the values"),
        colorProp<PropertyGrid>(
            "groupcolor", [](const PropertyGrid& g) { return g.groupColor(); },
            [](PropertyGrid& g, const Color& c) {
                g.setGroupColor(c);
                return true;
            },
            "fill colour of a group header"),
        colorProp<PropertyGrid>(
            "highlight", [](const PropertyGrid& g) { return g.highlightColor(); },
            [](PropertyGrid& g, const Color& c) {
                g.setHighlightColor(c);
                return true;
            },
            "fill behind the selected row"),
        // The grid draws from its own fields, not from Style, so these shadow
        // the inherited colour properties and land on what is on screen.
        colorProp<PropertyGrid>(
            "background", [](const PropertyGrid& g) { return g.bgColor(); },
            [](PropertyGrid& g, const Color& c) {
                g.setBgColor(c);
                return true;
            },
            "fill colour"),
        colorProp<PropertyGrid>(
            "bordercolor", [](const PropertyGrid& g) { return g.borderColor(); },
            [](PropertyGrid& g, const Color& c) {
                g.setBorderColor(c);
                return true;
            },
            "border colour"),
    };
}

// ---- breadcrumbs ----------------------------------------------------------

std::vector<PropertyDef> breadcrumbProps() {
    return {
        stringProp<Breadcrumb>(
            "path",
            [](const Breadcrumb& b) { return joinList(b.segments(), '/'); },
            [](Breadcrumb& b, const std::string& v) {
                b.setPath(v);
                return true;
            },
            "`/`-separated segments"),
        stringProp<Breadcrumb>(
            "separator", [](const Breadcrumb& b) { return b.separator(); },
            [](Breadcrumb& b, const std::string& v) {
                b.setSeparator(v);
                return true;
            },
            "text drawn between two segments"),
        numberProp<Breadcrumb>(
            "current", [](const Breadcrumb& b) { return static_cast<double>(b.current()); },
            [](Breadcrumb& b, double v) {
                b.setCurrent(static_cast<int>(v));
                return true;
            },
            "index of the highlighted segment (-1 = last)"),
        colorProp<Breadcrumb>(
            "dimcolor", [](const Breadcrumb& b) { return b.dimColor(); },
            [](Breadcrumb& b, const Color& c) {
                b.setDimColor(c);
                return true;
            },
            "colour of the segments before the current one"),
        colorProp<Breadcrumb>(
            "accent", [](const Breadcrumb& b) { return b.accentColor(); },
            [](Breadcrumb& b, const Color& c) {
                b.setAccentColor(c);
                return true;
            },
            "colour of the separators"),
    };
}

// ---- menus -----------------------------------------------------------------

std::vector<PropertyDef> menuProps() {
    return {
        stringProp<Menu>(
            "label", [](const Menu& m) { return m.label(); },
            [](Menu& m, const std::string& v) {
                m.setLabel(v);
                return true;
            },
            "caption on the bar"),
    };
}

std::vector<PropertyDef> menuitemProps() {
    return {
        stringProp<MenuItem>(
            "label", [](const MenuItem& i) { return i.label(); },
            [](MenuItem& i, const std::string& v) {
                i.setLabel(v);
                return true;
            },
            "text of the row"),
        stringProp<MenuItem>(
            "command", [](const MenuItem& i) { return i.command(); },
            [](MenuItem& i, const std::string& v) {
                i.setCommand(v);
                return true;
            },
            "name reported when the row is picked"),
        boolProp<MenuItem>(
            "separator", [](const MenuItem& i) { return i.separator(); },
            [](MenuItem& i, bool v) {
                i.setSeparator(v);
                return true;
            },
            "draw a rule instead of a command"),
    };
}

// ---- pickers and editors ----------------------------------------------------

std::vector<PropertyDef> colorpickerProps() {
    return {
        colorProp<ColorPicker>(
            "color", [](const ColorPicker& c) { return c.color(); },
            [](ColorPicker& c, const Color& v) {
                c.setColorHex(formatColor(v));
                return true;
            },
            "colour in the swatch"),
    };
}

std::vector<PropertyDef> pathpickerProps() {
    return {
        stringProp<PathPicker>(
            "value", [](const PathPicker& p) { return p.value(); },
            [](PathPicker& p, const std::string& v) {
                p.setValue(v);
                return true;
            },
            "path shown in the field"),
        stringProp<PathPicker>(
            "placeholder", [](const PathPicker& p) { return p.placeholder(); },
            [](PathPicker& p, const std::string& v) {
                p.setPlaceholder(v);
                return true;
            },
            "text shown while the value is empty"),
        stringProp<PathPicker>(
            "path", [](const PathPicker& p) { return p.startPath(); },
            [](PathPicker& p, const std::string& v) {
                p.setStartPath(v);
                return true;
            },
            "directory the browser opens in"),
        stringProp<PathPicker>(
            "filter", [](const PathPicker& p) { return p.filter(); },
            [](PathPicker& p, const std::string& v) {
                p.setFilter(v);
                return true;
            },
            "extension the browser lists (\".png\")"),
    };
}

std::vector<PropertyDef> rampeditorProps() {
    return {
        stringProp<RampEditor>(
            "stops",
            [](const RampEditor& r) {
                std::string out;
                for (const auto& s : r.stops()) {
                    if (!out.empty()) out += ' ';
                    out += formatNumber(s.pos) + ":" + formatColor(s.color);
                }
                return out;
            },
            [](RampEditor& r, const std::string& v) {
                if (v.empty()) {
                    r.clear();
                    return true;
                }
                return r.setStopsFromSpec(v);
            },
            "ramp as \"pos:#rrggbb pos:#rrggbb ...\""),
        numberProp<RampEditor>(
            "stopcount", [](const RampEditor& r) {
                return static_cast<double>(r.stopCount());
            },
            nullptr, "number of stops"),
    };
}

std::vector<PropertyDef> curveeditorProps() {
    return {
        stringProp<CurveEditor>(
            "points",
            [](const CurveEditor& c) {
                std::string out;
                for (int i = 0; i < 4; ++i) {
                    if (i) out += ' ';
                    out += formatNumber(c.point(i).x) + "," + formatNumber(c.point(i).y);
                }
                return out;
            },
            [](CurveEditor& c, const std::string& v) {
                return c.setPointsFromSpec(v);
            },
            "four \"x,y\" pairs, normalised, y up"),
    };
}

std::vector<PropertyDef> spinnerProps() {
    return {
        stringProp<Spinner>(
            "text", [](const Spinner& s) { return s.text(); },
            [](Spinner& s, const std::string& v) {
                s.setText(v);
                return true;
            },
            "caption drawn beside the animation"),
        stringProp<Spinner>(
            "variant", [](const Spinner& s) { return std::string(s.variant()); },
            [](Spinner& s, const std::string& v) {
                if (v != "glyph" && v != "blocks") return false;
                s.setVariant(v);
                return true;
            },
            "which glyph set is spun", {"glyph", "blocks"}),
        numberProp<Spinner>(
            "speed", [](const Spinner& s) { return s.speed(); },
            [](Spinner& s, double v) {
                s.setSpeed(static_cast<float>(v));
                return true;
            },
            "frames per second"),
    };
}

std::vector<PropertyDef> sparklineProps() {
    return {
        stringProp<Sparkline>(
            "values",
            [](const Sparkline& s) {
                std::string out;
                for (float v : s.values()) {
                    if (!out.empty()) out += ';';
                    out += formatNumber(v);
                }
                return out;
            },
            [](Sparkline& s, const std::string& v) {
                s.setValues(v);
                return true;
            },
            "samples, `;`- or `,`-separated"),
        stringProp<Sparkline>(
            "label", [](const Sparkline& s) { return s.label(); },
            [](Sparkline& s, const std::string& v) {
                s.setLabel(v);
                return true;
            },
            "caption drawn over the plot"),
        numberProp<Sparkline>(
            "max", [](const Sparkline& s) { return static_cast<double>(s.maxSamples()); },
            [](Sparkline& s, double v) {
                s.setMaxSamples(static_cast<int>(v));
                return true;
            },
            "samples kept before the oldest is dropped"),
        // The sparkline has no Style-backed fields at all, so `color` and
        // `background` name the pen and the plot fill it actually draws with.
        colorProp<Sparkline>(
            "color", [](const Sparkline& s) { return s.lineColor(); },
            [](Sparkline& s, const Color& c) {
                s.setLineColor(c);
                return true;
            },
            "colour of the line"),
        colorProp<Sparkline>(
            "background", [](const Sparkline& s) { return s.bgColor(); },
            [](Sparkline& s, const Color& c) {
                s.setBgColor(c);
                return true;
            },
            "fill colour behind the plot"),
    };
}

}  // namespace

void PropertyRegistry::loadBuiltins() {
    // Declares on `*this`: the constructor runs while instance()'s local
    // static is still initializing, so re-entering instance() would recurse.
    auto& self = *this;
    self.declare(typeid(Widget), typeid(Widget), widgetProps());
    self.declare(typeid(Label), typeid(Widget), labelProps());
    self.declare(typeid(Button), typeid(Widget), buttonProps());
    self.declare(typeid(Image), typeid(Widget), imageProps());
    self.declare(typeid(Panel), typeid(Widget), panelProps());
    self.declare(typeid(Window), typeid(Panel), windowProps());
    self.declare(typeid(ScrollView), typeid(Widget), scrollviewProps());
    self.declare(typeid(TextInput), typeid(Widget), textinputProps());
    self.declare(typeid(SearchInput), typeid(TextInput), {});
    self.declare(typeid(TextArea), typeid(TextInput), {});
    self.declare(typeid(Group), typeid(Widget), groupProps());
    self.declare(typeid(Checkbox), typeid(Widget), checkboxProps());
    self.declare(typeid(RadioButton), typeid(Widget), radiobuttonProps());
    self.declare(typeid(ToggleSwitch), typeid(Widget), toggleswitchProps());
    self.declare(typeid(Separator), typeid(Widget), separatorProps());
    self.declare(typeid(ProgressBar), typeid(Widget), progressbarProps());
    self.declare(typeid(Slider), typeid(Widget), sliderProps());
    self.declare(typeid(RangeSlider), typeid(Widget), rangesliderProps());
    self.declare(typeid(SpinBox), typeid(Widget), spinboxProps());
    self.declare(typeid(OptionSpin), typeid(Widget), optionspinProps());
    self.declare(typeid(Splitter), typeid(Widget), splitterProps());
    self.declare(typeid(Badge), typeid(Widget), badgeProps());
    self.declare(typeid(StatusBar), typeid(Widget), statusbarProps());
    self.declare(typeid(Tabs), typeid(Widget), tabsProps());
    self.declare(typeid(ComboBox), typeid(Widget), comboboxProps());
    self.declare(typeid(ListBox), typeid(Widget), listboxProps());
    self.declare(typeid(ListItem), typeid(Widget), listitemProps());
    self.declare(typeid(TreeView), typeid(Widget), treeviewProps());
    self.declare(typeid(TreeItem), typeid(Widget), treeitemProps());
    self.declare(typeid(Table), typeid(Widget), tableprops());
    self.declare(typeid(Keybind), typeid(Widget), keybindProps());
    self.declare(typeid(CommandPalette), typeid(Widget), commandpaletteProps());
    self.declare(typeid(Console), typeid(Widget), consoleProps());
    self.declare(typeid(Modal), typeid(Widget), modalProps());

    // Stacks, grids and the composite widgets: every class the XML factory can
    // create is declared, so a name a layout authors is a name a script can
    // read and write.
    self.declare(typeid(Column), typeid(Widget), columnProps());
    self.declare(typeid(Row), typeid(Widget), rowProps());
    self.declare(typeid(Grid), typeid(Widget), gridProps());
    self.declare(typeid(Accordion), typeid(Widget), accordionProps());
    self.declare(typeid(AccordionSection), typeid(Widget), accordionsectionProps());
    self.declare(typeid(Tab), typeid(Widget), tabProps());
    self.declare(typeid(ComboOption), typeid(Widget), combooptionProps());
    self.declare(typeid(CrumbItem), typeid(Widget), crumbitemProps());
    self.declare(typeid(Breadcrumb), typeid(Widget), breadcrumbProps());
    self.declare(typeid(CommandAction), typeid(Widget), commandactionProps());
    self.declare(typeid(TableColumn), typeid(Widget), tablecolumnProps());
    self.declare(typeid(TableRow), typeid(Widget), tablerowProps());
    self.declare(typeid(TableCell), typeid(Widget), tablecellProps());
    self.declare(typeid(TreeTable), typeid(Widget), treetableProps());
    self.declare(typeid(TreeTableNode), typeid(Widget), treetablenodeProps());
    self.declare(typeid(PropertyItem), typeid(Widget), propertyitemProps());
    self.declare(typeid(PropertyGrid), typeid(Widget), propertygridProps());
    self.declare(typeid(Menu), typeid(Widget), menuProps());
    self.declare(typeid(MenuItem), typeid(Widget), menuitemProps());
    self.declare(typeid(MenuBar), typeid(Widget), {});
    self.declare(typeid(ContextMenu), typeid(Widget), {});
    self.declare(typeid(ColorPicker), typeid(Widget), colorpickerProps());
    self.declare(typeid(PathPicker), typeid(Widget), pathpickerProps());
    self.declare(typeid(RampEditor), typeid(Widget), rampeditorProps());
    self.declare(typeid(CurveEditor), typeid(Widget), curveeditorProps());
    self.declare(typeid(Spinner), typeid(Widget), spinnerProps());
    self.declare(typeid(Sparkline), typeid(Widget), sparklineProps());
}

}  // namespace ogui
