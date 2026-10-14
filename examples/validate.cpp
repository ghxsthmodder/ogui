// ogui_validate.cpp : check layout files without running the app.
//
//   ./build/ogui_validate ui.xml [more.xml ...]
//   ./build/ogui_validate manifest.xml --manifest
//   ./build/ogui_validate --chdir . manifest.xml --manifest
//
// A layout is parsed with the library's own XmlParser, so what the tool
// reports is exactly what the running app would see: `line:col: message`
// errors, the warnings that produced fallback widgets, and the ids and
// element types that were actually created. `--manifest` walks every screen
// path in a <manifest>; `--theme` validates a <theme> stylesheet.
//
// Nested references (`<stylesheet path>`, `<script src>`, `<font source>`) are
// resolved against the working directory, the same rule the demo host follows,
// so `--chdir` has to point at the directory the app runs from.
//
// Exit code: 0 clean, 1 errors found, 2 the file could not be read.
#include "ogui/xmlparser.hpp"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define OG_CHDIR(p) _chdir(p)
#else
#include <unistd.h>
#define OG_CHDIR(p) chdir(p)
#endif

using namespace ogui;

namespace {

int g_errors = 0;
int g_warnings = 0;

void printDiags(const std::vector<std::string>& v, const char* kind) {
    for (const auto& e : v) {
        std::printf("%s: %s\n", kind, e.c_str());
    }
}

// Count the widgets the parser kept, so a layout that silently degraded to
// plain labels still looks wrong in the summary.
size_t countWidgets(const Widget::Ptr& w) {
    if (!w) return 0;
    size_t n = 1;
    for (const auto& c : w->children()) n += countWidgets(c);
    return n;
}

bool validateFile(const std::string& path, bool asTheme) {
    XmlParser p;
    if (asTheme) {
        XmlParser::ThemeResult r = p.parseThemeFile(path, activeTheme());
        printDiags(r.errors, "error");
        printDiags(r.warnings, "warn");
        g_errors += static_cast<int>(r.errors.size());
        g_warnings += static_cast<int>(r.warnings.size());
        std::printf("%s: %s, %d warning(s)\n", path.c_str(),
                    r.errors.empty() ? "theme ok" : "theme FAILED",
                    static_cast<int>(r.warnings.size()));
        return r.errors.empty();
    }

    XmlParser::ParseResult res = p.parseFile(path);
    if (!res.root) {
        std::printf("%s: unreadable or empty\n", path.c_str());
        ++g_errors;
        return false;
    }
    printDiags(res.errors, "error");
    printDiags(res.warnings, "warn");
    g_errors += static_cast<int>(res.errors.size());
    g_warnings += static_cast<int>(res.warnings.size());
    std::printf("%s: %s — %zu widgets, %zu scripts, %d warning(s)\n",
                path.c_str(), res.errors.empty() ? "ok" : "FAILED",
                countWidgets(res.root), res.scripts.size(),
                static_cast<int>(res.warnings.size()));
    return res.errors.empty();
}

// Every <screen title path="..."/> in a manifest, checked the way the demo
// host would load it.
bool validateManifest(const std::string& path) {
    XmlParser p;
    XmlParser::ParseResult res = p.parseFile(path);
    if (!res.root) {
        std::printf("%s: unreadable or empty\n", path.c_str());
        ++g_errors;
        return false;
    }
    const std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    bool ok = true;
    for (const auto& s : res.root->children()) {
        const auto& attrs = s->rawAttributes();
        auto it = attrs.find("path");
        if (it == attrs.end()) continue;
        ok = validateFile(dir + it->second, false) && ok;
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    bool asTheme = false;
    bool manifest = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--theme") asTheme = true;
        else if (a == "--manifest") manifest = true;
        else if (a == "--chdir" && i + 1 < argc) {
            if (OG_CHDIR(argv[++i]) != 0) {
                std::fprintf(stderr, "cannot enter directory %s\n", argv[i]);
                return 2;
            }
        } else if (a == "-h" || a == "--help") {
            std::printf("usage: ogui_validate [--theme|--manifest] "
                        "[--chdir DIR] file.xml...\n");
            return 0;
        } else files.push_back(a);
    }
    if (files.empty()) {
        std::fprintf(stderr,
                     "usage: ogui_validate [--theme|--manifest] [--chdir DIR] "
                     "file.xml...\n");
        return 2;
    }
    for (const auto& f : files) {
        if (manifest) validateManifest(f);
        else validateFile(f, asTheme);
    }
    std::printf("validate: %d error(s), %d warning(s)\n", g_errors, g_warnings);
    return g_errors ? 1 : 0;
}
