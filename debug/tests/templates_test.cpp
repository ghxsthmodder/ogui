// ogui_templates_test.cpp : reusable components -- <template> registration,
// ${param} substitution with defaults, <slot/> content, local ids,
// composition, and the error messages that name the component of origin.
#include "ogui/kernel.hpp"
#include "ogui/properties.hpp"
#include "ogui/script.hpp"
#include "ogui/style.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

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

// The catalogue is process-wide (a components file loaded before a screen
// supplies that screen's templates), so each test starts from a clean one.
void resetTemplates() { templateRegistry().clear(); }

std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : " | ") + s;
    return out;
}

bool mentions(const std::vector<std::string>& msgs, const std::string& needle) {
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) return true;
    }
    return false;
}

void testDefaultsAndOverride() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="PrimaryButton" text="OK" variant="4" y="10">
    <button text="${text}" radius="${variant}" x="10" y="${y}" width="120" height="40"/>
  </template>
  <PrimaryButton id="start"/>
  <PrimaryButton id="later" text="Iniciar" y="90" variant="8"/>
</screen>)");
    CHECK_MSG(res.errors.empty(), joined(res.errors));
    CHECK_MSG(res.warnings.empty(), joined(res.warnings));
    const auto& kids = res.root->children();
    CHECK_MSG(kids.size() == 2,
              "n=" + std::to_string(kids.size()) + " err=" + joined(res.errors));
    if (kids.size() != 2) return;

    // A single-root component *is* the instance: no wrapper widget.
    auto* first = dynamic_cast<Button*>(kids[0].get());
    auto* second = dynamic_cast<Button*>(kids[1].get());
    CHECK(first != nullptr);
    CHECK(second != nullptr);
    if (!first || !second) return;
    CHECK(first->text() == "OK");                    // declared default
    CHECK(second->text() == "Iniciar");              // instance wins
    CHECK(first->style().radius == 4);
    CHECK(second->style().radius == 8);
    // An instance that moves one axis keeps the geometry the body authored.
    CHECK(first->bounds().y == 10);
    CHECK(second->bounds().x == 10);
    CHECK(second->bounds().y == 90);
    CHECK(second->bounds().w == 120);
    CHECK(second->bounds().h == 40);
    CHECK(first->id() == "start");
    // `variant` is a parameter, not an attribute the widget understands.
    CHECK(second->style().borderWidth == 0);
    // Diagnostics can name the component a widget came from.
    CHECK(kids[0]->rawAttributes().count("_template") == 1);
    CHECK(kids[0]->rawAttributes().at("_template") == "PrimaryButton");
}

void testRequiredParameter() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Note">
    <label text="${title}" x="10" y="10" width="200" height="24"/>
  </template>
  <Note id="n1" title="Here"/>
  <Note id="n2"/>
</screen>)");
    CHECK(res.errors.size() == 1);
    CHECK_MSG(mentions(res.errors, "required parameter 'title'"),
              joined(res.errors));
    CHECK_MSG(mentions(res.errors, "Note"), joined(res.errors));
    // The unexpanded placeholder stays in the tree rather than dropping it.
    CHECK(res.root->children().size() == 2);
}

void testSlots() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Card" title="untitled">
    <panel x="0" y="0" width="300" height="200">
      <label id="head" text="${title}" x="8" y="8" width="280" height="20"/>
      <slot><label id="empty" text="nothing inside"/></slot>
    </panel>
  </template>
  <Card id="a" title="First">
    <button id="go" text="Go" x="8" y="40" width="100" height="30"/>
  </Card>
  <Card id="b" title="Second"/>
  <template name="Boxed">
    <panel/>
  </template>
  <Boxed id="c">
    <label text="dropped"/>
  </Boxed>
</screen>)");
    CHECK_MSG(res.errors.empty(), joined(res.errors));
    const auto& kids = res.root->children();
    CHECK(kids.size() == 3);
    if (kids.size() != 3) return;

    // a: the instance's button replaced the slot's fallback.
    CHECK(kids[0]->children().size() == 2);
    if (kids[0]->children().size() != 2) return;
    const auto& head = kids[0]->children()[0];
    const auto* body = dynamic_cast<Button*>(kids[0]->children()[1].get());
    CHECK(head->id() == "a.head");
    CHECK(body != nullptr);
    CHECK(body && body->text() == "Go");
    CHECK(body && body->id() == "a.go");

    // b: no children, so the fallback markup stands.
    CHECK(kids[1]->children().size() == 2);
    if (kids[1]->children().size() == 2) {
        auto* fb = dynamic_cast<Label*>(kids[1]->children()[1].get());
        CHECK(fb != nullptr);
        CHECK(fb && fb->id() == "b.empty");   // the fallback is scoped too
    }

    // c: a slot-less component says its children were dropped.
    CHECK_MSG(mentions(res.warnings, "no <slot/>"), joined(res.warnings));
    CHECK(kids[2]->children().empty());
}

void testLocalIds() {
    resetTemplates();
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Row2">
    <panel x="0" y="0" width="400" height="40">
      <label id="left" text="L" x="0" y="0" width="100" height="20"/>
      <row id="right">
        <label id="deep" text="D" x="0" y="0" width="100" height="20"/>
      </row>
    </panel>
  </template>
  <Row2 id="top"/>
  <Row2/>
</screen>)");
    CHECK_MSG(res.errors.empty(), joined(res.errors));
    auto screen = std::make_shared<Screen>("s");
    screen->widgets.push_back(res.root);
    screen->width = 800;
    screen->height = 600;
    k.pushScreen(screen);
    InputState st;
    st.mouseButton = -1;
    k.frame(st);

    // Ids inside a component are scoped by the instance: no collisions, and
    // the host addresses them through the same id API as its own widgets. The
    // scope is the instance id, at every depth.
    CHECK(k.findWidget("top.left") != nullptr);
    CHECK(k.findWidget("top.right") != nullptr);
    CHECK(k.findWidget("top.deep") != nullptr);
    CHECK(k.findWidget("left") == nullptr);
    // An unnamed instance scopes under its own generated name, so the second
    // copy of the component does not shadow the first.
    CHECK(k.findWidget("Row2#1.left") != nullptr);
    CHECK(k.findWidget("Row2#1.deep") != nullptr);
    CHECK(k.findWidget("Row2#2.left") == nullptr);
    // The generated tree is complete, so the ids the layout needs exist.
    if (auto* w = k.findWidget("top")) CHECK(w->children().size() == 2);
}

void testComposition() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Chip" text="?">
    <badge id="b" text="${text}" x="0" y="0" width="60" height="20"/>
  </template>
  <template name="TagList" label="tags">
    <column id="col" x="10" y="10" width="200" height="200">
      <label id="name" text="${label}" x="0" y="0" width="200" height="20"/>
      <Chip text="first"/>
      <Chip id="second" text="second"/>
      <slot/>
    </column>
  </template>
  <TagList id="tags" label="Colors">
    <Chip id="extra" text="from outside"/>
  </TagList>
  <TagList id="more" label="Sizes">
    <Chip id="extra" text="also outside"/>
  </TagList>
</screen>)");
    CHECK_MSG(res.errors.empty(), joined(res.errors));
    const auto& kids = res.root->children();
    CHECK(kids.size() == 2);
    if (kids.size() != 2) return;
    auto* col = dynamic_cast<Column*>(kids[0].get());
    CHECK(col != nullptr);
    if (!col) return;
    CHECK(col->id() == "tags");
    CHECK(col->children().size() == 4);
    if (col->children().size() != 4) return;
    CHECK(col->children()[0]->id() == "tags.name");
    // A component inside a component is scoped by its host, so the same inner
    // id in two hosts of one component never collides.
    CHECK(col->children()[1]->id() == "tags.Chip#1.b");
    CHECK(col->children()[2]->id() == "tags.second");
    // A child handed to the slot lands inside the host's scope too.
    auto* extra = dynamic_cast<Badge*>(col->children()[3].get());
    CHECK(extra != nullptr);
    CHECK(extra && extra->id() == "tags.extra");
    CHECK(extra && extra->text() == "from outside");

    auto* col2 = dynamic_cast<Column*>(kids[1].get());
    CHECK(col2 != nullptr);
    if (!col2) return;
    CHECK(col2->id() == "more");
    CHECK(col2->children().size() == 4);
    if (col2->children().size() != 4) return;
    CHECK(col2->children()[1]->id() == "more.Chip#2.b");
    CHECK(col2->children()[3]->id() == "more.extra");
    CHECK(col2->children()[3] != col->children()[3]);
}

void testRecursionGuards() {
    resetTemplates();
    XmlParser p;
    auto self = p.parse(R"(<screen id="s">
  <template name="Loop"><Loop/></template>
  <Loop id="x"/>
</screen>)");
    CHECK_MSG(mentions(self.errors, "expands inside itself"),
              joined(self.errors));
    CHECK(self.root->children().empty());

    // A chain of distinct components deeper than the cap is refused too.
    std::string doc = "<screen id=\"s\">";
    for (int i = 0; i < 20; ++i) {
        doc += "<template name=\"T" + std::to_string(i) + "\">";
        if (i + 1 < 20) doc += "<T" + std::to_string(i + 1) + "/>";
        else doc += "<label text=\"bottom\"/>";
        doc += "</template>";
    }
    doc += "<T0 id=\"root\"/></screen>";
    XmlParser q;
    auto deep = q.parse(doc);
    CHECK_MSG(mentions(deep.errors, "more than 16 components nested"),
              joined(deep.errors));
}

void testBodyErrorsNameTheComponent() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Card">
    <panel x="0" y="0" width="100" height="100">
      <buton text="typo"/>
    </panel>
  </template>
  <Card id="a"/>
</screen>)");
    CHECK_MSG(mentions(res.errors, "unknown element <buton>"), joined(res.errors));
    CHECK_MSG(mentions(res.errors, "while expanding <Card>"), joined(res.errors));
    // The typo does not swallow the rest of the document.
    CHECK(res.root->children().size() == 1);
    if (!res.root->children().empty()) {
        // The panel stands with its children intact; only the bad element went.
        auto* panel = dynamic_cast<Panel*>(res.root->children()[0].get());
        CHECK(panel != nullptr);
    }
}

void testStylesFlowThrough() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <style name="muted" color="#888888"/>
  <style element="Callout" radius="6"/>
  <template name="Callout" text="note">
    <label text="${text}" x="10" y="10" width="200" height="24"/>
  </template>
  <Callout id="a" style="muted" background="#123456"/>
</screen>)");
    CHECK_MSG(res.errors.empty(), joined(res.errors));
    const auto& kids = res.root->children();
    CHECK(kids.size() == 1);
    if (kids.size() != 1) return;
    auto* lbl = dynamic_cast<Label*>(kids[0].get());
    CHECK(lbl != nullptr);
    if (!lbl) return;
    // The instance's own attributes and its stylesheets reach the generated
    // root, overriding what the component body authored.
    CHECK(lbl->style().backgroundColor.r == 0x12);
    CHECK(lbl->style().textColor.r == 0x88);
    CHECK(lbl->style().radius == 6);
    // Same path through the typed registry.
    CHECK(widgetProperty(*lbl, "background") == "#123456");
}

void testScriptsPerInstance() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Note" text="hi">
    <label id="body" text="${text}" x="10" y="10" width="200" height="24"/>
    <script>ogui.set("note.body", "text", "from script")</script>
  </template>
  <Note id="note"/>
</screen>)");
    CHECK_MSG(mentions(res.warnings, "once per instance"), joined(res.warnings));
    CHECK(res.scripts.size() == 1);
    CHECK_MSG(res.scripts.size() == 1 && res.scripts[0].first.find("note.body") !=
                                                  std::string::npos,
              res.scripts.empty() ? "" : res.scripts[0].first);
    resetTemplates();
}

void testLuaThroughTheKernel() {
    if (!ScriptHost::available()) return;
    resetTemplates();
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Gauge" cap="loading">
    <panel id="wrap" x="10" y="90" width="220" height="60">
      <progress id="bar" x="0" y="0" width="200" height="20"/>
      <label id="cap" text="${cap}" x="0" y="24" width="200" height="16"/>
      <button id="go" text="run" x="0" y="40" width="80" height="20"/>
    </panel>
  </template>
  <Gauge id="cpu" cap="busy"/>
  <label id="out" text="init" x="10" y="200" width="400" height="24"/>
  <script>
function ogui_on_load()
  ogui.set("out", "text", ogui.get("cpu.cap", "text"))
  ogui.on("cpu", "click", function(id, info)
    ogui.set("out", "text", id .. "/" .. info.target)
  end)
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
    // A component's inner widget is addressed through its scoped id.
    CHECK_MSG(k.getText("out") == "busy", k.getText("out"));

    // A click on the component's interior bubbles to the instance root, which
    // is how a component exposes its events.
    InputState st;
    st.mouseButton = -1;
    k.frame(st);
    st.mouseX = 40;
    st.mouseY = 140;
    st.mouseButton = 0;
    st.mouseDown[0] = true;
    k.frame(st);
    st.mouseDown[0] = false;
    st.mouseButton = -1;
    k.frame(st);
    CHECK_MSG(k.getText("out") == "cpu/cpu.go", k.getText("out"));
}

void testUnknownTagStillUnknown() {
    resetTemplates();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <Whatever id="w"/>
</screen>)");
    CHECK_MSG(mentions(res.errors, "unknown element <Whatever>"),
              joined(res.errors));
}

void testDefinitionErrors() {
    resetTemplates();
    XmlParser p;
    auto noName = p.parse(R"(<screen id="s">
  <template><label/></template>
</screen>)");
    CHECK_MSG(mentions(noName.errors, "template: name is required"),
              joined(noName.errors));

    resetTemplates();
    XmlParser q;
    auto empty = q.parse(R"(<screen id="s">
  <template name="Nothing"></template>
  <Nothing/>
</screen>)");
    CHECK_MSG(mentions(empty.errors, "empty body"), joined(empty.errors));
    CHECK_MSG(mentions(empty.errors, "unknown element <Nothing>"),
              joined(empty.errors));

    resetTemplates();
    XmlParser r;
    auto unterminated = r.parse(R"(<screen id="s">
  <template name="Open"><label text="x"/>
</screen>)");
    CHECK_MSG(mentions(unterminated.errors, "unterminated <template>"),
              joined(unterminated.errors));
}

void testFragmentAndAdd() {
    // Templates also expand inside a fragment handed to the script API.
    resetTemplates();
    Kernel k;
    k.init();
    XmlParser p;
    auto res = p.parse(R"(<screen id="s">
  <template name="Tag" text="t">
    <panel id="p" x="10" y="10" width="100" height="60">
      <label id="l" text="${text}" x="0" y="0" width="100" height="20"/>
    </panel>
  </template>
  <panel id="host" x="10" y="50" width="300" height="200"/>
</screen>)");
    auto screen = std::make_shared<Screen>("s");
    screen->widgets.push_back(res.root);
    screen->width = 800;
    screen->height = 600;
    k.pushScreen(screen);
    InputState st;
    st.mouseButton = -1;
    k.frame(st);
    if (!ScriptHost::available()) return;
    std::string error;
    const bool ran = k.script().loadString(
        "local id, err = ogui.add(\"host\", '<Tag id=\"t1\" text=\"added\"/>')\n"
        "if not id then error(\"add failed: \" .. tostring(err)) end",
        "add", &error);
    CHECK_MSG(ran, error);
    k.frame(st);
    auto* added = k.findWidget("t1.l");
    CHECK_MSG(k.findWidget("t1") != nullptr, "instance root not found");
    CHECK_MSG(added != nullptr, k.script().errors().empty() ? "" :
                                                        k.script().errors().front());
    CHECK(k.getText("t1.l") == "added");
}

}  // namespace

int main() {
    testDefaultsAndOverride();
    testRequiredParameter();
    testSlots();
    testLocalIds();
    testComposition();
    testRecursionGuards();
    testBodyErrorsNameTheComponent();
    testStylesFlowThrough();
    testScriptsPerInstance();
    testLuaThroughTheKernel();
    testUnknownTagStillUnknown();
    testDefinitionErrors();
    testFragmentAndAdd();
    resetTemplates();

    std::cout << (failures ? "TEMPLATE TEST FAILED" : "template test OK") << "\n";
    return failures ? 1 : 0;
}
