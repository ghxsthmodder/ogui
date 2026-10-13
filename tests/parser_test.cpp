// ogui_parser_test.cpp : unit test for the XML layout parser.
// Build: g++ -std=c++20 tests/parser_test.cpp -I../include -L../build -logui -o parser_test
#include "ogui/xmlparser.hpp"
#include <cassert>
#include <iostream>

int main() {
    ogui::XmlParser parser;

    std::string xml = R"(<screen id="main">
  <panel id="bg" x="0" y="0" width="800" height="600" background="#161616"/>
  <window id="win" x="120" y="80" width="560" height="420" title="Teste">
    <button id="b1" text="Ok" x="20" y="20" width="120" height="40"
            background="#4488ff" color="#fff" radius="6"/>
    <label id="l1" text="Hello world" x="20" y="80" width="200" height="30"
           font sans 20 color "#66ee66"/>
  </window>
  <column id="col">
    <button id="b2" text="Second" x="0" y="0" width="100" height="30"/>
  </column>
</screen>)";

    auto res = parser.parse(xml);
    if (!res.root) {
        std::cerr << "parse failed: ";
        for (const auto& e : res.errors) std::cerr << e << ';';
        std::cerr << '\n';
        return 1;
    }

    // The root should be a <screen>.
    assert(res.root->id() == "main");

    int child_count = 0;
    for (const auto& w : res.root->children()) {
        std::cout << "widget: " << w->id() << " type: ";
        if (auto b = std::dynamic_pointer_cast<ogui::Button>(w)) { (void)b; std::cout << "Button"; }
        else if (auto l = std::dynamic_pointer_cast<ogui::Label>(w)) { (void)l; std::cout << "Label"; }
        else if (auto c = std::dynamic_pointer_cast<ogui::Column>(w)) { (void)c; std::cout << "Column"; }
        else if (auto win = std::dynamic_pointer_cast<ogui::Window>(w)) { (void)win; std::cout << "Window"; }
        else if (auto p = std::dynamic_pointer_cast<ogui::Panel>(w)) { (void)p; std::cout << "Panel"; }
        else std::cout << "Unknown";
        std::cout << '\n';
        ++child_count;
    }
    // Direct children of <screen>: panel, window, column.
    assert(child_count == 3);

    // Check nested children inside the window.
    auto win = std::dynamic_pointer_cast<ogui::Window>(res.root->children()[1]);
    assert(win != nullptr);
    assert(win->children().size() == 2);

    // Check column children.
    auto col = std::dynamic_pointer_cast<ogui::Column>(res.root->children()[2]);
    assert(col != nullptr);
    assert(col->children().size() == 1);

    // Attributes must have been applied by the parser.
    assert(res.root->children()[0]->id() == "bg");
    assert(!win->children()[0]->id().empty());
    auto panel = std::dynamic_pointer_cast<ogui::Panel>(res.root->children()[0]);
    assert(panel != nullptr);
    assert(panel->bounds().w == 800.0f && panel->bounds().h == 600.0f);

    // Legacy `font sans 20 color "#66ee66"` pseudo-syntax must have landed.
    auto lbl = std::dynamic_pointer_cast<ogui::Label>(win->children()[1]);
    assert(lbl != nullptr);
    assert(lbl->text() == "Hello world");
    assert(lbl->fontFamily() == "sans");
    assert(lbl->fontSize() == 20);
    // Regression: hexColorHex() truncated a whole strtol() run to 8 bits,
    // which made "#66ee66" come out as gray.
    const ogui::Color& lc = lbl->textColor();
    assert(lc.r == 0x66 && lc.g == 0xee && lc.b == 0x66 && lc.a == 0xff);

    // ---- named styles + onclick ------------------------------------------
    {
        ogui::XmlParser p2;
        std::string xml2 = R"(<screen id="s">
  <style name="primary" background="#4488ff" color="#ffffff" radius="8" fontSize="18"/>
  <button id="go" style="primary" text="Go" x="0" y="0" width="100" height="32" onClick="onGo"/>
</screen>)";
        auto r2 = p2.parse(xml2);
        assert(r2.root != nullptr);
        assert(r2.root->children().size() == 1);
        auto btn = std::dynamic_pointer_cast<ogui::Button>(r2.root->children()[0]);
        assert(btn != nullptr);
        // Element attrs override the named style, named style supplies the rest.
        assert(btn->text() == "Go");
        assert(btn->binding() == "onGo");
        assert(btn->bounds().w == 100.0f);
    }

    // ---- unknown element is reported, the rest still parses --------------
    {
        ogui::XmlParser p3;
        std::string xml3 = R"(<screen id="s">
  <bogus id="x" foo="bar"/>
  <label id="after" text="ok"/>
</screen>)";
        auto r3 = p3.parse(xml3);
        assert(r3.root != nullptr);
        assert(!r3.errors.empty());
        // Error message carries a line:col prefix.
        assert(r3.errors[0].find(':') != std::string::npos);
        assert(r3.root->children().size() == 1);
        assert(r3.root->children()[0]->id() == "after");
    }

    // ---- unknown style produces a warning --------------------------------
    {
        ogui::XmlParser p4;
        std::string xml4 = R"(<screen id="s">
  <button id="b" style="missing" text="x"/>
</screen>)";
        auto r4 = p4.parse(xml4);
        assert(r4.root != nullptr);
        assert(!r4.warnings.empty());
    }

    // ---- label wrap attribute ------------------------------------------
    {
        ogui::XmlParser p5;
        std::string xml5 = R"(<screen id="s">
  <label id="l" text="long text" wrap="true" x="0" y="0" width="120" height="0"/>
  <label id="l2" text="short"/>
</screen>)";
        auto r5 = p5.parse(xml5);
        assert(r5.root != nullptr);
        assert(r5.root->children().size() == 2);
        auto l1 = std::dynamic_pointer_cast<ogui::Label>(r5.root->children()[0]);
        auto l2 = std::dynamic_pointer_cast<ogui::Label>(r5.root->children()[1]);
        assert(l1 != nullptr);
        assert(l2 != nullptr);
        assert(l1->wrap());
        assert(!l2->wrap());          // opt-in
        assert(l1->bounds().w == 120.0f);
        assert(l1->bounds().h == 0.0f);  // h=0 lets Column size it to the text
    }

    // ---- tabs, tab labels, tooltip and visible attributes ----------------
    {
        ogui::XmlParser p6;
        std::string xml6 = R"XML(<screen id="s">
  <tabs id="tb" x="10" y="10" width="300" height="100" active="1" tabHeight="22">
    <tab label="Um" id="t1"/>
    <tab text="Dois" id="t2"/>
  </tabs>
  <button id="b" text="x" tooltip="dica" visible="false"/>
</screen>)XML";
        auto r6 = p6.parse(xml6);
        assert(r6.root != nullptr);
        assert(r6.errors.empty());
        assert(r6.root->children().size() == 2);
        auto tb = std::dynamic_pointer_cast<ogui::Tabs>(r6.root->children()[0]);
        assert(tb != nullptr);
        assert(tb->active() == 1);
        assert(tb->headerHeight() == 22.0f);
        assert(tb->children().size() == 2);
        auto t1 = std::dynamic_pointer_cast<ogui::Tab>(tb->children()[0]);
        auto t2 = std::dynamic_pointer_cast<ogui::Tab>(tb->children()[1]);
        assert(t1 != nullptr && t2 != nullptr);
        assert(t1->label() == "Um");
        assert(t2->label() == "Dois");   // text= works as a label alias
        auto b = std::dynamic_pointer_cast<ogui::Button>(r6.root->children()[1]);
        assert(b != nullptr);
        assert(b->tooltip() == "dica");
        assert(!b->isVisible());
        assert(b->bounds().x == 0.0f && b->bounds().y == 0.0f);
    }

    // ---- manifest.xml: <manifest> root + raw attribute storage -------------
    {
        ogui::XmlParser p7;
        std::string xml7 = R"XML(<?xml version="1.0"?>
<!-- screen list -->
<manifest>
  <screen title="A" path="a.xml"/>
  <screen title="B" path="b/c.xml"/>
</manifest>)XML";
        auto r7 = p7.parse(xml7);
        assert(r7.root != nullptr);
        assert(r7.errors.empty());
        assert(r7.root->children().size() == 2);
        const auto& e0 = r7.root->children()[0];
        const auto& e1 = r7.root->children()[1];
        assert(e0->rawAttribute("title") && *e0->rawAttribute("title") == "A");
        assert(e0->rawAttribute("path") && *e0->rawAttribute("path") == "a.xml");
        assert(e1->rawAttribute("path") && *e1->rawAttribute("path") == "b/c.xml");
        assert(e1->rawAttribute("nope") == nullptr);
    }

    // A normal <screen> root still keeps its attributes on the root widget.
    {
        ogui::XmlParser p8;
        auto r8 = p8.parse(R"XML(<screen id="s" foo="bar"><label id="l"/></screen>)XML");
        assert(r8.root != nullptr);
        assert(r8.root->rawAttribute("foo") && *r8.root->rawAttribute("foo") == "bar");
        assert(r8.root->children().size() == 1);
    }

    std::cout << "parser_test: OK - parsed " << child_count << " top-level widgets\n";
    return 0;
}
