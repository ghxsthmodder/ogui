// ogui_examples_test.cpp : the documentation examples, executed.
//
// Two jobs:
//   1. examples/hello.xml and examples/counter.xml are the minimal layouts the
//      docs quote; they are parsed, pushed, framed and clicked here, so the
//      behaviour the docs describe is the behaviour the tests assert.
//   2. every fenced ```xml / ```lua block in docs/examples.md is extracted and
//      run: XML must parse without errors, Lua must load and execute its
//      ogui_on_load hook without a script error. A block may opt out with a
//      `<!-- expect-error -->` marker (then it must fail), which is how the
//      "what a mistake looks like" snippets stay honest too.
//
// Blocks are matched with the source tree as the working directory, so
// `<script src="...">` and stylesheet paths resolve the way the demo host
// resolves them.
#include "ogui/kernel.hpp"
#include "ogui/script.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define OG_CHDIR(p) _chdir(p)
#else
#include <unistd.h>
#define OG_CHDIR(p) chdir(p)
#endif

#ifndef OGUI_SOURCE_DIR
#define OGUI_SOURCE_DIR "."
#endif

using namespace ogui;

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";         \
            ++g_failures;                                                \
        }                                                                \
    } while (false)

static InputState idle(float t = 1.0f) {
    InputState st;
    st.time = t;
    // No button held and no press edge: `mouseButton` only names the button
    // that moved this frame, the Kernel reads the edge against `prevInput`.
    st.mouseButton = -1;
    return st;
}

// ---- 1. the example layouts ------------------------------------------------

static std::shared_ptr<Screen> loadScreen(const std::string& relPath,
                                          XmlParser::ParseResult& out) {
    XmlParser p;
    out = p.parseFile(relPath);
    if (!out.root) return nullptr;
    auto s = std::make_shared<Screen>(out.root->id());
    s->widgets.push_back(out.root);
    s->width = 800.0f;
    s->height = 600.0f;
    // What the demo host does: the chunks the parser collected travel with
    // the screen, and the Kernel loads them when it becomes foreground.
    s->scripts = out.scripts;
    return s;
}

static void testHello() {
    XmlParser::ParseResult res;
    auto screen = loadScreen("examples/hello.xml", res);
    CHECK(screen != nullptr);
    if (!screen) return;
    CHECK(res.errors.empty());
    CHECK(res.scripts.empty());          // a pure layout, no behaviour

    Kernel k;
    k.init();
    k.pushScreen(screen);
    k.frame(idle());

    CHECK(k.findWidget("greeting") != nullptr);
    CHECK(k.getText("greeting") == "Boas-vindas!");
    int fired = 0;
    k.bind("onGo", [&] { ++fired; });
    CHECK(k.invokeBinding("onGo"));
    CHECK(fired == 1);
    // The draw list exists and the tree dump sees both widgets.
    CHECK(!k.drawList().vertices.empty());
    CHECK(k.dumpTree().find("Button go") != std::string::npos);
}

static void testCounter() {
    XmlParser::ParseResult res;
    auto screen = loadScreen("examples/counter.xml", res);
    CHECK(screen != nullptr);
    if (!screen) return;
    CHECK(res.errors.empty());
    CHECK(res.scripts.size() == 1);

    Kernel k;
    k.init();
    k.pushScreen(screen);

    InputState st = idle();
    k.frame(st);
    CHECK(k.script().errors().empty());
    if (!k.script().errors().empty()) {
        for (const auto& e : k.script().errors()) std::cerr << "  lua: " << e << "\n";
    }
    // ogui_on_load ran (ogui.log goes to the kernel log, which a <console>
    // would show) and the label carries the initial value.
    bool logged = false;
    for (const auto& line : k.logs())
        if (line.find("counter: loaded") != std::string::npos) logged = true;
    CHECK(logged);
    CHECK(k.getText("count") == "0");

    // Clicking +1 through the real pointer path reaches ogui_on_click, because
    // the button has no onclick= binding and no C++ handler.
    auto* inc = k.findWidget("inc");
    CHECK(inc != nullptr);
    if (!inc) return;
    // absoluteOrigin() already folds the parent offsets in; bounds stay
    // parent-relative, so the centre is origin + size/2.
    const Offset2D o = inc->absoluteOrigin();
    const Rect b = inc->bounds();
    st.mouseX = o.x + b.w * 0.5f;
    st.mouseY = o.y + b.h * 0.5f;
    st.mouseDown[0] = true;
    st.mouseButton = 0;
    st.time += 0.1f;
    k.frame(st);
    st.mouseDown[0] = false;
    st.mouseButton = -1;
    st.time += 0.1f;
    k.frame(st);
    CHECK(k.getText("count") == "1");
    CHECK(k.findWidget("meter") != nullptr);

    // ogui.after(0.5) from ogui_on_show: past its deadline the toast is gone
    // again after 3.5 s, so just assert the timer ran without an error.
    st.time += 1.0f;
    k.frame(st);
    CHECK(k.script().errors().empty());
}

// ---- 2. the fenced blocks in docs/examples.md ------------------------------

struct Block {
    std::string lang;
    std::string body;
    int line = 0;
    bool expectError = false;
};

static std::vector<Block> extractBlocks(const std::string& path) {
    std::vector<Block> out;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "cannot open " << path << "\n";
        ++g_failures;
        return out;
    }
    std::string line;
    Block cur;
    bool in = false;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        const size_t p = line.find_first_not_of(" \t");
        if (p == std::string::npos) {
            if (in) cur.body += "\n";
            continue;
        }
        if (line.compare(p, 3, "```") == 0) {
            if (!in) {
                cur.lang = line.substr(p + 3);
                cur.body.clear();
                cur.line = n;
                cur.expectError = false;
                in = true;
            } else {
                if (cur.lang == "xml" || cur.lang == "lua" ||
                    cur.lang == "xml expect-error" || cur.lang == "lua expect-error") {
                    cur.expectError = cur.lang.find("expect-error") != std::string::npos;
                    cur.lang.resize(3);            // keep just "xml" / "lua"
                    out.push_back(cur);
                }
                in = false;
            }
            continue;
        }
        if (in) cur.body += line + "\n";
    }
    return out;
}

// A block that is not a whole document (a widget fragment quoted to show one
// attribute) still has to parse: wrap it in a screen.
static std::string wrapIfFragment(const std::string& body) {
    const size_t p = body.find_first_not_of(" \t\r\n");
    if (p == std::string::npos) return body;
    if (body.compare(p, 7, "<screen") == 0 || body.compare(p, 9, "<manifest") == 0 ||
        body.compare(p, 6, "<theme") == 0)
        return body;
    return "<screen id=\"fragment\">\n" + body + "\n</screen>\n";
}

static void testDocBlocks(const std::string& path) {
    const std::vector<Block> blocks = extractBlocks(path);
    CHECK(!blocks.empty());
    int checked = 0;
    for (const auto& b : blocks) {
        if (b.lang == "xml") {
            XmlParser p;
            const XmlParser::ParseResult res = p.parse(wrapIfFragment(b.body));
            const bool failed = !res.root || !res.errors.empty();
            if (b.expectError) {
                CHECK(failed);
                std::cerr << path << ":" << b.line << " (expected error) ";
                for (const auto& e : res.errors) std::cerr << e << "; ";
                std::cerr << "\n";
            } else {
                if (failed) {
                    std::cerr << path << ":" << b.line << ": ";
                    for (const auto& e : res.errors) std::cerr << e << "; ";
                    std::cerr << "\n";
                }
                CHECK(!failed);
            }
            ++checked;
        } else if (b.lang == "lua") {
            Kernel k;
            k.init();
            auto s = std::make_shared<Screen>("doc");
            s->width = 400.0f;
            s->height = 300.0f;
            k.pushScreen(s);
            k.frame(idle());
            k.loadScriptString(b.body, "doc-block");
            const bool failed = !k.script().errors().empty();
            if (b.expectError) CHECK(failed);
            else CHECK(!failed);
            if (failed) {
                std::cerr << path << ":" << b.line << ": ";
                for (const auto& e : k.script().errors()) std::cerr << e << "; ";
                std::cerr << "\n";
            }
            ++checked;
        }
    }
    std::cout << path << ": " << checked << " doc blocks checked\n";
}

int main() {
    const std::string root = OGUI_SOURCE_DIR;
    OG_CHDIR(root.c_str());

    testHello();
    testCounter();
    testDocBlocks(root + "/docs/examples.md");

    std::cout << (g_failures ? "EXAMPLES FAILED" : "examples: all checks passed")
              << " (" << g_failures << " failures)\n";
    return g_failures ? 1 : 0;
}
