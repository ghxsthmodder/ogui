// ogui_properties_test.cpp : the typed property registry -- class chains and
// overrides, the generic write/read round-trip over every built-in property,
// per-type value validation, the side-effect-free value check the XML parser
// runs its attributes through, the name drift between what a layout may
// author and what the registry exposes, and the Lua get/set/props surface.
#include "ogui/kernel.hpp"
#include "ogui/properties.hpp"
#include "ogui/script.hpp"
#include "ogui/style.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace ogui;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __LINE__ << "  " #cond "\n";               \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_MSG(cond, msg)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL " << __LINE__ << "  " #cond << "  -- " << (msg) \
                      << "\n";                                                 \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

// A host widget class, declared by the test rather than the library: the
// registry is open, so a subclass gets the same name-based API as a built-in.
class Gadget : public Panel {
public:
    int hits = 0;
    std::string note;
};

class GadgetEx : public Gadget {
public:
    int loud = 3;
};

const PropertyDef* findProp(const Widget& w, const std::string& name) {
    return PropertyRegistry::instance().find(w, name);
}

bool hasName(const Widget& w, const std::string& name) {
    bool found = false;
    w.property(name, &found);
    return found;
}

void testInheritance() {
    Label label;
    CHECK(PropertyRegistry::instance().declares(typeid(Label)));
    const auto* text = findProp(label, "text");
    CHECK(text != nullptr);
    CHECK(text && text->type == PropertyType::String);
    CHECK(text && text->canRead() && text->canWrite());

    // The base table reaches every class: `id` is declared on Widget.
    CHECK(hasName(label, "id"));
    CHECK(hasName(label, "background"));
    CHECK(hasName(label, "maxwidth"));

    // A subclass with an empty own table still resolves through its base.
    TextArea area;
    CHECK(PropertyRegistry::instance().declares(typeid(TextArea)));
    CHECK(hasName(area, "placeholder"));
    CHECK(hasName(area, "text"));
    SearchInput search;
    CHECK(hasName(search, "placeholder"));

    // Two-level chain: Window -> Panel -> Widget.
    Window window;
    CHECK(hasName(window, "title"));      // Window
    CHECK(hasName(window, "bgcolor"));    // Panel
    CHECK(hasName(window, "tooltip"));    // Widget

    // The class field wins over the base declaration it shadows.
    const auto* fontSize = findProp(label, "fontsize");
    CHECK_MSG(fontSize, "label must carry a fontsize property");
    if (fontSize) {
        std::string err;
        CHECK(setWidgetProperty(label, "fontsize", "22", &err));
        CHECK(label.style().fontSize == 22);
        CHECK(label.fontSize() == 22);   // the drawn field moved too
    }
}

void testHostClasses() {
    auto& reg = PropertyRegistry::instance();
    Gadget gadget;
    CHECK(!reg.declares(typeid(Gadget)));   // unknown until the host declares

    reg.declare(typeid(Gadget), typeid(Panel), {
        numberProp<Gadget>(
            "hits", [](const Gadget& g) { return static_cast<double>(g.hits); },
            [](Gadget& g, double v) { g.hits = static_cast<int>(v); return true; },
            "how many times the gadget fired"),
        stringProp<Gadget>(
            "note", [](const Gadget& g) { return g.note; },
            [](Gadget& g, const std::string& v) { g.note = v; return true; },
            "free-form annotation"),
    });
    CHECK(reg.declares(typeid(Gadget)));
    CHECK(hasName(gadget, "note"));
    CHECK(hasName(gadget, "bgcolor"));   // inherited from Panel

    // A derived re-declaration overrides the inherited one by name.
    reg.declare(typeid(GadgetEx), typeid(Gadget), {
        numberProp<GadgetEx>(
            "hits", [](const GadgetEx& g) { return g.hits * 100.0; },
            [](GadgetEx& g, double v) { g.hits = static_cast<int>(v); return true; },
            "hits, reported times a hundred"),
    });
    GadgetEx ex;
    ex.hits = 2;
    CHECK(widgetProperty(ex, "hits") == "200");
    std::string err;
    CHECK(setWidgetProperty(ex, "hits", "5", &err));
    CHECK(ex.hits == 5);
    // Names the derived class does not redeclare still come from the base.
    CHECK(hasName(ex, "note"));

    // Only one entry per name, and the base properties are all still listed.
    int hitsDecls = 0;
    for (const auto* def : reg.list(ex)) {
        if (def->name == "hits") ++hitsDecls;
    }
    CHECK(hitsDecls == 1);
}

void testRoundTrip() {
    // Every built-in property that reads and writes must accept its own
    // current value back unchanged: the canonical form has to parse.
    const std::vector<Widget::Ptr> samples{
        std::make_shared<Label>(),        std::make_shared<Button>(),
        std::make_shared<Image>(),        std::make_shared<Panel>(),
        std::make_shared<Window>(),       std::make_shared<ScrollView>(),
        std::make_shared<ScrollableVerticalContainer>(),
        std::make_shared<ScrollableHorizontalContainer>(),
        std::make_shared<TextInput>(),    std::make_shared<SearchInput>(),
        std::make_shared<TextArea>(),     std::make_shared<Group>(),
        std::make_shared<Checkbox>(),     std::make_shared<RadioButton>(),
        std::make_shared<ToggleSwitch>(), std::make_shared<Separator>(),
        std::make_shared<ProgressBar>(),  std::make_shared<Slider>(),
        std::make_shared<RangeSlider>(),  std::make_shared<SpinBox>(),
        std::make_shared<OptionSpin>(),   std::make_shared<Splitter>(),
        std::make_shared<Badge>(),        std::make_shared<StatusBar>(),
        std::make_shared<Tabs>(),         std::make_shared<ComboBox>(),
        std::make_shared<ListBox>(),      std::make_shared<ListItem>(),
        std::make_shared<TreeView>(),     std::make_shared<TreeItem>(),
        std::make_shared<Table>(),        std::make_shared<Keybind>(),
        std::make_shared<CommandPalette>(),
        std::make_shared<Console>(),      std::make_shared<Modal>(),
        std::make_shared<Column>(),       std::make_shared<Row>(),
        std::make_shared<Grid>(),         std::make_shared<Accordion>(),
        std::make_shared<AccordionSection>(), std::make_shared<Tab>(),
        std::make_shared<ComboOption>(),  std::make_shared<CrumbItem>(),
        std::make_shared<Breadcrumb>(),   std::make_shared<CommandAction>(),
        std::make_shared<TableColumn>(),  std::make_shared<TableRow>(),
        std::make_shared<TableCell>(),    std::make_shared<TreeTable>(),
        std::make_shared<TreeTableNode>(), std::make_shared<PropertyItem>(),
        std::make_shared<PropertyGrid>(), std::make_shared<Menu>(),
        std::make_shared<MenuItem>(),     std::make_shared<MenuBar>(),
        std::make_shared<ContextMenu>(),  std::make_shared<ColorPicker>(),
        std::make_shared<PathPicker>(),   std::make_shared<RampEditor>(),
        std::make_shared<CurveEditor>(),  std::make_shared<Spinner>(),
        std::make_shared<Sparkline>(),
    };

    auto& reg = PropertyRegistry::instance();
    size_t exercised = 0;
    for (const auto& sample : samples) {
        Widget* w = sample.get();
        CHECK_MSG(reg.declares(typeid(*w)),
                  std::string("no property table for ") + Kernel::typeName(w));
        for (const auto* def : reg.list(*w)) {
            if (!def->canRead() || !def->canWrite()) continue;
            const std::string before = def->read(*w);
            std::string error;
            const bool ok = setWidgetProperty(*w, def->name, before, &error);
            CHECK_MSG(ok, w->id() + "." + def->name + ": " + error);
            if (!ok) continue;
            const std::string after = def->read(*w);
            CHECK_MSG(after == before,
                      Kernel::typeName(w) + "." + def->name + " read '" + after +
                          "' after writing '" + before + "'");
            ++exercised;
        }
    }
    std::cout << "round-trip sweep: " << exercised << " properties\n";
    CHECK_MSG(exercised > 1500, "round-trip sweep covered only " +
                                    std::to_string(exercised) + " properties");
}

void testFactoryCoverage() {
    // Every tag the XML factory can create must resolve to a declared class,
    // so `ogui.props` and a name-based `ogui.set` answer for any widget a
    // layout can hold.
    auto& reg = PropertyRegistry::instance();
    size_t tags = 0;
    for (auto& [tag, create] : widgetRegistry()) {
        Widget::Ptr w = create(Style{}, {});
        ++tags;
        CHECK_MSG(reg.declares(typeid(*w)),
                  tag + " -> " + Kernel::typeName(w.get()) +
                      " has no property table");
        CHECK_MSG(!reg.list(*w).empty(), tag + " exposes no properties");
    }
    CHECK_MSG(tags > 50, "the factory only offered " + std::to_string(tags) + " tags");
}

void testNameDrift() {
    // The parser and the registry describe one contract from two sides: what a
    // layout may write, a script must be able to read and write back. Tracing
    // says which attribute names applyAttributes() actually asks about, so this
    // check needs no list kept by hand -- a `get("...")` no property declares
    // for the widget being built fails here instead of widening the gap in
    // silence (and a name that only some classes carry is judged per class,
    // because the widget the parser built is the one asked about).
    XmlParser p;
    p.setAttributeTrace(true);
    auto& reg = PropertyRegistry::instance();
    size_t names = 0;
    for (auto& [tag, create] : widgetRegistry()) {
        const std::string xml =
            "<screen id=\"s\"><" + tag + " id=\"w\"/></screen>";
        auto res = p.parse(xml);
        if (!res.root || res.root->children().empty()) continue;
        const Widget& w = *res.root->children()[0];
        for (const std::string& name : p.tracedAttributes(tag)) {
            ++names;
            CHECK_MSG(reg.find(w, name) != nullptr,
                      "<" + tag + "> authors \"" + name + "\" on a " +
                          Kernel::typeName(&w) +
                          ", but no property declares it");
        }
    }
    CHECK_MSG(names > 1500,
              "the trace only saw " + std::to_string(names) + " names");
}

void testValidation() {
    Button button;
    Label label;
    ComboBox combo;
    Keybind keybind;
    std::string error;

    error.clear();
    CHECK(!setWidgetProperty(button, "width", "wide", &error));
    CHECK_MSG(error.find("expects number") != std::string::npos, error);

    CHECK(!setWidgetProperty(button, "enabled", "maybe", &error));
    CHECK_MSG(error.find("true/false") != std::string::npos, error);

    CHECK(!setWidgetProperty(button, "background", "reddish", &error));
    CHECK_MSG(error.find("#rrggbb") != std::string::npos, error);

    // Enum values are checked against the declared choices, case-insensitively.
    CHECK(!setWidgetProperty(label, "align", "middle-ish", &error));
    CHECK_MSG(error.find("left|center|right") != std::string::npos, error);
    CHECK(setWidgetProperty(label, "align", "CENTER", &error));
    CHECK(label.hAlign() == Align::Center);

    // An unknown name says so, and names what the class does expose.
    CHECK(!setWidgetProperty(button, "nope", "1", &error));
    CHECK_MSG(error.find("no property 'nope' on Button") != std::string::npos,
              error);
    CHECK_MSG(error.find("align") != std::string::npos, error);

    // Read-only properties refuse writes instead of pretending to work.
    CHECK(!setWidgetProperty(combo, "count", "3", &error));
    CHECK_MSG(error.find("read-only") != std::string::npos, error);
    CHECK(!setWidgetProperty(button, "pressed", "true", &error));

    // A widget-level rejection travels as an error too.
    CHECK(!setWidgetProperty(keybind, "value", "not a combo", &error));
    CHECK_MSG(error.find("value") != std::string::npos, error);

    // Palette tokens and short hex forms both parse.
    CHECK(setWidgetProperty(button, "background", "@accent", &error));
    const Color bg = button.style().backgroundColor;
    CHECK(bg.r == palette().accent.r && bg.g == palette().accent.g &&
          bg.b == palette().accent.b);
    CHECK(setWidgetProperty(button, "bordercolor", "#f00", &error));
    CHECK(button.style().borderColor.r == 255);
    CHECK(button.style().borderColor.g == 0);

    // The number formatters survive their own output.
    CHECK(setWidgetProperty(button, "radius", "3.5", &error));
    CHECK(widgetProperty(button, "radius") == "3.5");
    CHECK(!setWidgetProperty(button, "radius", "3.5.1", &error));
    CHECK(!setWidgetProperty(button, "radius", "nan", &error));
    CHECK(!setWidgetProperty(button, "radius", "inf", &error));

    // The vocabularies the composite widgets add.
    TableCell cell;
    CHECK(!setWidgetProperty(cell, "scrollingtext", "always", &error));
    CHECK_MSG(error.find("none|automatic|onmouseenter") != std::string::npos,
              error);
    CHECK(setWidgetProperty(cell, "scrollingtext", "onMouseEnter", &error));
    CHECK(cell.scrollMode() == CellScroll::OnHover);

    Row row;
    CHECK(setWidgetProperty(row, "valign", "middle", &error));
    CHECK(row.childValign() == VertAlign::Middle);

    Spinner spinner;
    CHECK(!setWidgetProperty(spinner, "variant", "dots", &error));
    CHECK_MSG(error.find("glyph|blocks") != std::string::npos, error);

    // The registry defines the vocabulary: the alias the XML parser happens to
    // accept is not a property value, and the error names the ones that are.
    PropertyItem item;
    CHECK(!setWidgetProperty(item, "type", "checkbox", &error));
    CHECK_MSG(error.find("bool|number|text|enum|color") != std::string::npos,
              error);
    CHECK(setWidgetProperty(item, "type", "enum", &error));
    CHECK(widgetProperty(item, "type") == "enum");

    // A spec the widget keeps no copy of stays write-only; the shape it
    // resolved to reads back through its own property.
    Grid grid;
    CHECK(!findProp(grid, "columns")->canRead());
    CHECK(setWidgetProperty(grid, "columns", "120,1fr,40%", &error));
    CHECK(widgetProperty(grid, "colcount") == "3");
    CHECK(!setWidgetProperty(grid, "colcount", "2", &error));
    CHECK_MSG(error.find("read-only") != std::string::npos, error);

    // A class that draws from its own fields shadows the inherited colour.
    Sparkline spark;
    CHECK(setWidgetProperty(spark, "background", "#ff2233", &error));
    CHECK(spark.bgColor().r == 0xff);
    CHECK(spark.style().backgroundColor.r != 0xff);
}

void testValueCheck() {
    // checkWidgetProperty() is the same verdict setWidgetProperty() reaches
    // for a malformed value, with nothing applied: the XML parser asks it
    // about attributes it has already set, so a re-write would double-apply
    // them (and clobber the forms only the parser understands).
    Button button;
    const double width0 = std::stod(widgetProperty(button, "width"));
    std::string error;

    CHECK(checkWidgetProperty(button, "width", "wide", &error) ==
          PropertyCheck::Rejected);
    CHECK_MSG(error.find("'width' expects number") != std::string::npos, error);
    CHECK(checkWidgetProperty(button, "width", "123", &error) ==
          PropertyCheck::Accepted);
    CHECK(error.empty());
    CHECK(std::stod(widgetProperty(button, "width")) == width0);

    // Unknown names are not malformed values: an attribute the registry does
    // not carry is the parser's own, and reporting it would call valid
    // authoring a mistake.
    CHECK(checkWidgetProperty(button, "nope", "1", &error) ==
          PropertyCheck::Unknown);
    // Any text is a valid value for a string property.
    CHECK(checkWidgetProperty(button, "id", "@@ whatever", &error) ==
          PropertyCheck::Accepted);
    CHECK(checkWidgetProperty(button, "enabled", "maybe", &error) ==
          PropertyCheck::Rejected);
    Label label;
    CHECK(checkWidgetProperty(label, "align", "middle-ish", &error) ==
          PropertyCheck::Rejected);
    CHECK_MSG(error.find("left|center|right") != std::string::npos, error);
    CHECK(checkWidgetProperty(label, "align", "RIGHT", &error) ==
          PropertyCheck::Accepted);
    // A setter that rejects a well-formed value of its own accord is not
    // predicted here -- only the declared type and choices are.
    Keybind keybind;
    CHECK(checkWidgetProperty(keybind, "value", "not a combo", &error) ==
          PropertyCheck::Accepted);
    // Read-only is about writing, not about the value's form.
    ComboBox combo;
    CHECK(checkWidgetProperty(combo, "count", "3", &error) ==
          PropertyCheck::Accepted);
    CHECK(checkWidgetProperty(combo, "count", "three", &error) ==
          PropertyCheck::Rejected);

    // ---- palette tokens ---------------------------------------------------
    // Palette::token() has to keep answering an unknown name with `window`
    // (a live theme draws something), so membership is a separate question.
    CHECK(palette().hasToken("accent"));
    CHECK(palette().hasToken("ACCENT"));
    CHECK(palette().hasToken("textBright"));
    CHECK(!palette().hasToken("nope"));
    CHECK(!palette().hasToken(""));
    Color c{};
    CHECK(parseColorValue("@accent", c));
    CHECK(c.r == palette().accent.r);
    CHECK(!parseColorValue("@nope", c));
    CHECK(parseColorValue("#f00", c) && c.r == 255 && c.g == 0);
    CHECK(!parseColorValue("reddish", c));
    // The colour property says which half of its grammar the value missed.
    CHECK(checkWidgetProperty(button, "background", "@nope", &error) ==
          PropertyCheck::Rejected);
    CHECK_MSG(error.find("'background' expects") != std::string::npos, error);
}

void testFacadeAndKernel() {
    Kernel k;
    k.init();
    XmlParser parser;
    auto res = parser.parse(R"(<screen id="s">
  <label id="lbl" text="Hi" x="10" y="10" width="200" height="24"/>
</screen>)");
    auto screen = std::make_shared<Screen>("s");
    screen->widgets.push_back(res.root);
    screen->width = 800;
    screen->height = 600;
    k.pushScreen(screen);
    InputState st;
    st.mouseButton = -1;
    k.frame(st);

    Widget* w = k.findWidget("lbl");
    CHECK(w != nullptr);
    if (!w) return;

    std::string error;
    CHECK(w->setProperty("text", "Hello", &error));
    CHECK(k.getText("lbl") == "Hello");

    // The facade and the free functions are the same path.
    CHECK(w->property("text") == "Hello");
    CHECK(widgetProperty(*w, "text") == "Hello");

    // Writing the id moves the widget within the kernel's index.
    CHECK(w->setProperty("id", "greeting"));
    CHECK(k.findWidget("greeting") == w);
    CHECK(k.findWidget("lbl") == nullptr);

    // Names come back sorted and complete.
    const auto names = w->propertyNames();
    CHECK(names.size() > 20);
    CHECK(std::find(names.begin(), names.end(), "wrap") != names.end());
    CHECK(std::find(names.begin(), names.end(), "textpadding") == names.end());

    // A property with no meaning for this class is reported as missing.
    bool found = true;
    CHECK(w->property("low", &found).empty());
    CHECK(!found);
}

void testLua() {
    if (!ScriptHost::available()) return;

    Kernel k;
    k.init();
    XmlParser parser;
    auto res = parser.parse(R"(<screen id="s">
  <label id="lbl" text="Hi" x="10" y="10" width="200" height="24"/>
  <button id="btn" text="Go" x="10" y="50" width="120" height="40"/>
  <label id="out" text="init" x="10" y="120" width="400" height="24"/>
  <script>
function ogui_on_load()
  ogui.set("lbl", "wrap", true)
  ogui.set("lbl", "fontsize", 22)
  ogui.set("lbl", "align", "right")
  local p = ogui.props("btn")
  ogui.set("out", "text",
    p.text.type .. "/" .. tostring(p.pressed.writable) .. "/" ..
    tostring(ogui.get("lbl", "wrap")) .. "/" ..
    ("%d"):format(ogui.get("lbl", "fontsize")) .. "/" ..
    tostring(ogui.get("lbl", "nope")))
  local ok = pcall(ogui.set, "lbl", "align", "sideways")
  if not ok then ogui.set("out", "text", ogui.get("out", "text") .. "/rejected") end
  local ok2, msg = pcall(ogui.set, "lbl", "wrap", "perhaps")
  if not ok2 and msg:find("expects") then
    ogui.set("out", "text", ogui.get("out", "text") .. "/typed")
  end
  if ogui.props("ghost") ~= nil then
    ogui.set("out", "text", ogui.get("out", "text") .. "/bogus")
  end
end
  </script>
</screen>)");
    auto screen = std::make_shared<Screen>("s");
    screen->widgets.push_back(res.root);
    screen->scripts = std::move(res.scripts);
    screen->width = 800;
    screen->height = 600;
    k.pushScreen(screen);

    CHECK_MSG(k.script().errors().empty(),
              k.script().errors().empty() ? "" : k.script().errors().front());

    auto* lbl = dynamic_cast<Label*>(k.findWidget("lbl"));
    CHECK(lbl != nullptr);
    if (lbl) {
        CHECK(lbl->wrap());
        CHECK(lbl->fontSize() == 22);
        CHECK(lbl->hAlign() == Align::Right);
    }
    CHECK_MSG(k.getText("out") == "string/false/true/22/nil/rejected/typed",
              k.getText("out"));
}

}  // namespace

int main() {
    testInheritance();
    testHostClasses();
    testRoundTrip();
    testFactoryCoverage();
    testNameDrift();
    testValidation();
    testValueCheck();
    testFacadeAndKernel();
    testLua();

    std::cout << (failures ? "PROPERTY TEST FAILED" : "property test OK") << "\n";
    return failures ? 1 : 0;
}
