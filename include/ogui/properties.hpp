#pragma once
// Typed property registry: one place that says which named properties a
// widget class carries, what type each one is, how to read and write it and
// what counts as a valid value.
//
// The XML parser reads authored attributes into the concrete setters it knows;
// everything that has to work by NAME at runtime -- C++ `Widget::setProperty`,
// the Lua `ogui.get`/`ogui.set`/`ogui.props`, tooling that lists what a widget
// exposes -- resolves through this registry instead of growing another
// dynamic_cast ladder. Values travel as strings, the same form XML and Lua
// scripts use, and each declaration converts and validates them, so a bad
// value reports what was expected rather than silently doing nothing.
#include "ogui/api.hpp"
#include "ogui/ogui_export.hpp"
#include <functional>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <vector>

namespace ogui {

class Widget;

enum class PropertyType { String, Bool, Number, Color, Enum };

// "string" / "bool" / "number" / "color" / "enum" -- the name used in error
// messages and reported by the introspection APIs.
const char* propertyTypeName(PropertyType type);

struct PropertyDef {
    std::string name;          // lowercase, exactly as authored in XML
    PropertyType type = PropertyType::String;
    std::string description;   // what it does, for docs and tooling
    std::string defaultValue;  // canonical string form, empty when unknown
    std::vector<std::string> choices;  // Enum: the accepted values

    // Canonical string form of the current value; empty read = write-only.
    std::function<std::string(const Widget&)> read;
    // Applies a value; returns false and fills `error` when the value is not
    // valid for this property. Empty write = read-only.
    std::function<bool(Widget&, const std::string& value, std::string* error)> write;

    bool canRead() const { return static_cast<bool>(read); }
    bool canWrite() const { return static_cast<bool>(write); }
};

// Registry of per-class property tables, keyed by the widget's dynamic type
// and walked up the declared base chain (most derived declaration wins).
class OGUI_API PropertyRegistry {
public:
    static PropertyRegistry& instance();

    // Adds (or replaces, by name) the properties of one class. `base` is the
    // class whose properties it inherits; pass typeid(Widget) for a direct
    // subclass of the base. The built-in classes register on first use, hosts
    // call this themselves for their own widget subclasses.
    void declare(const std::type_info& type, const std::type_info& base,
                 const std::vector<PropertyDef>& props);

    // Resolved over the chain: every property the widget exposes, most derived
    // first, overridden names collapsed to the derived declaration.
    std::vector<const PropertyDef*> list(const Widget& w) const;
    const PropertyDef* find(const Widget& w, const std::string& name) const;
    // True when this class or one of its bases declared a table; widgets that
    // answer false are opaque to the registry (a host class that declared
    // nothing) and the lookup API reports that instead of "unknown property".
    bool declares(const std::type_info& type) const;

private:
    PropertyRegistry();
    void loadBuiltins();

    // Entries are heap-owned: the resolved lists hand out pointers into their
    // property vectors and must survive later declarations.
    struct Entry {
        const std::type_info* type = nullptr;
        const std::type_info* base = nullptr;
        std::vector<PropertyDef> props;
    };

    const Entry* entryFor(const std::type_info& type) const;
    std::vector<const PropertyDef*> resolve(const std::type_info& type) const;

    std::vector<std::unique_ptr<Entry>> m_entries;
    // Resolved chains are cached; lookups are const, so the cache is mutable.
    mutable std::unordered_map<std::string, std::vector<const PropertyDef*>>
        m_resolved;
};

// ---- value conversion (shared by the helpers below and by callers that want
// to convert by hand) -------------------------------------------------------
// Values travel as strings, the form XML and Lua scripts use: numbers format
// as %.6g (integers without a fraction), colours as #rrggbb (plus alpha when
// it is not opaque), booleans as true/false.
OGUI_API std::string formatNumber(double v);
OGUI_API bool parseNumber(const std::string& text, double& out);
OGUI_API bool parseBool(const std::string& text, bool& out);
OGUI_API std::string formatColor(const Color& c);
// "#rgb[a]"/"#rrggbb[aa]" literals and "@token" palette references.
OGUI_API bool parseColorValue(const std::string& text, Color& out);
OGUI_API bool matchesChoice(const std::vector<std::string>& choices,
                            const std::string& value);
OGUI_API std::string joinedChoices(const std::vector<std::string>& choices);
// "property 'x' expects <what>, got 'value'" (or the registry's message for an
// unknown / read-only / unwritable name when `error` is non-null).
OGUI_API void setValueError(std::string* error, const std::string& name,
                            const std::string& expected,
                            const std::string& value);

// ---- declaration helpers ---------------------------------------------------
// Each takes lambdas over the concrete widget class; the registry only ever
// calls them with a widget it matched to that class. Pass nullptr for `read`
// (write-only) or `write` (read-only). Numbers format as %.6g, colours as
// #rrggbb (plus alpha when it is not opaque), booleans as true/false.
template <class T>
PropertyDef numberProp(std::string name, std::function<double(const T&)> read,
                       std::function<bool(T&, double)> write,
                       std::string description = {}) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = PropertyType::Number;
    def.description = std::move(description);
    if (read) {
        def.read = [read](const Widget& w) {
            return formatNumber(read(static_cast<const T&>(w)));
        };
    }
    if (write) {
        def.write = [write, n = def.name](Widget& w, const std::string& value,
                                          std::string* error) {
            double v = 0.0;
            if (!parseNumber(value, v)) {
                setValueError(error, n, "number", value);
                return false;
            }
            return write(static_cast<T&>(w), v);
        };
    }
    return def;
}

template <class T>
PropertyDef boolProp(std::string name, std::function<bool(const T&)> read,
                     std::function<bool(T&, bool)> write,
                     std::string description = {}) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = PropertyType::Bool;
    def.description = std::move(description);
    if (read) {
        def.read = [read](const Widget& w) {
            return read(static_cast<const T&>(w)) ? "true" : "false";
        };
    }
    if (write) {
        def.write = [write, n = def.name](Widget& w, const std::string& value,
                                          std::string* error) {
            bool v = false;
            if (!parseBool(value, v)) {
                setValueError(error, n, "true/false/1/0/yes/no/on/off", value);
                return false;
            }
            return write(static_cast<T&>(w), v);
        };
    }
    return def;
}

template <class T>
PropertyDef stringProp(std::string name, std::function<std::string(const T&)> read,
                       std::function<bool(T&, const std::string&)> write,
                       std::string description = {},
                       std::vector<std::string> choices = {}) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = choices.empty() ? PropertyType::String : PropertyType::Enum;
    def.description = std::move(description);
    def.choices = choices;
    if (read) {
        def.read = [read](const Widget& w) {
            return read(static_cast<const T&>(w));
        };
    }
    if (write) {
        def.write = [write, n = def.name, choices](
                        Widget& w, const std::string& value,
                        std::string* error) {
            if (!choices.empty() && !matchesChoice(choices, value)) {
                setValueError(error, n, joinedChoices(choices), value);
                return false;
            }
            return write(static_cast<T&>(w), value);
        };
    }
    return def;
}

template <class T>
PropertyDef colorProp(std::string name, std::function<Color(const T&)> read,
                      std::function<bool(T&, const Color&)> write,
                      std::string description = {}) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = PropertyType::Color;
    def.description = std::move(description);
    if (read) {
        def.read = [read](const Widget& w) {
            return formatColor(read(static_cast<const T&>(w)));
        };
    }
    if (write) {
        def.write = [write, n = def.name](Widget& w, const std::string& value,
                                          std::string* error) {
            Color c{};
            if (!parseColorValue(value, c)) {
                setValueError(error, n, "\"#rrggbb\", \"#rrggbbaa\" or \"@token\"",
                              value);
                return false;
            }
            return write(static_cast<T&>(w), c);
        };
    }
    return def;
}

// ---- lookups over a live widget -------------------------------------------
// Current value in canonical string form; `found` reports whether the widget
// exposes the name at all (a write-only property reads back as empty).
OGUI_API std::string widgetProperty(const Widget& w, const std::string& name,
                                    bool* found = nullptr);
// Applies a value. False with a message in `error` when the name is unknown to
// this class, the value does not parse, or the widget rejected it. Invalidates
// the kernel so the change reaches the next draw list.
OGUI_API bool setWidgetProperty(Widget& w, const std::string& name,
                                const std::string& value,
                                std::string* error = nullptr);
// Names of every property the widget exposes, sorted.
OGUI_API std::vector<std::string> widgetPropertyNames(const Widget& w);

}  // namespace ogui
