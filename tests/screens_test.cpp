// ogui_screens_test.cpp : every screen listed in manifest.xml must parse
// cleanly, and the ids the demo gallery binds to must exist somewhere.
#include "ogui/xmlparser.hpp"
#include "ogui/widget.hpp"

#include <iostream>
#include <set>
#include <string>

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

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";           \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static void collectIds(const Widget& w, std::set<std::string>& out) {
    if (!w.id().empty()) out.insert(w.id());
    for (const auto& c : w.children())
        if (c) collectIds(*c, out);
}

int main() {
    const std::string root = OGUI_SOURCE_DIR;
    // Layouts reference assets and stylesheets with cwd-relative paths
    // (same rule the demo host uses), so run from the project root.
    OG_CHDIR(root.c_str());
    XmlParser manifest;
    auto res = manifest.parseFile(root + "/manifest.xml");
    if (!res.root) {
        std::cerr << "cannot parse manifest.xml\n";
        return 1;
    }

    int screens = 0;
    std::set<std::string> allIds;
    auto parseAll = [&](bool collect) {
        screens = 0;
        for (const auto& s : res.root->children()) {
            const auto& attrs = s->rawAttributes();
            auto path = attrs.find("path");
            if (path == attrs.end()) {
                std::cerr << "manifest entry without path\n";
                ++failures;
                continue;
            }
            XmlParser p;
            auto r = p.parseFile(root + "/" + path->second);
            ++screens;
            CHECK(r.root != nullptr);
            CHECK(r.errors.empty());
            if (!r.errors.empty()) {
                for (const auto& e : r.errors) std::cerr << "  " << e << "\n";
            }
            if (collect && r.root) collectIds(*r.root, allIds);
        }
    };
    parseAll(true);

    // Every layout must also parse under the light preset (@tokens resolve
    // against the active palette, so a missing token would surface here).
    setActiveTheme(lightTheme());
    parseAll(false);
    setActiveTheme(darkTheme());
    CHECK(screens >= 7);

    // Ids the demo gallery binds to (examples/gallery.hpp) or setText()s.
    const char* required[] = {"bg_inst",   "ce_curve", "cp_tint",  "md_confirm",
                              "pp_mesh",   "re_ramp",  "tr_files", "ts_vsync",
                              "lbl_tree",  "lbl_cp",   "lbl_title"};
    for (const char* id : required)
        CHECK(allIds.count(id) != 0);

    // The scene tree and the file tree must not live on the same screen
    // anymore: FileTree got its own layout file.
    XmlParser ft;
    auto ftr = ft.parseFile(root + "/layouts/layout_filetree.xml");
    CHECK(ftr.root != nullptr);
    std::set<std::string> ftIds;
    if (ftr.root) collectIds(*ftr.root, ftIds);
    CHECK(ftIds.count("tr_files") != 0);
    CHECK(ftIds.count("lbl_tree") != 0);

    if (failures) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "screens_test: OK (" << screens << " screens)\n";
    return 0;
}
