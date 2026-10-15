// ogui_asset_test.cpp : one seam for every byte the library opens.
//
// A layout, a theme, a script, a font and an image are all read the same way:
// ogui::readFile() asks the host's asset reader first and the filesystem
// second. That is what lets an Android host ship them inside the APK
// (AAssetManager) instead of next to the binary. What this checks is that a
// host really can own those names, that a name it declines still comes off the
// disk, and that a host which installs nothing reads files exactly as before.
#include "ogui/assets.hpp"
#include "ogui/font.hpp"
#include "ogui/imagemanager.hpp"
#include "ogui/script.hpp"
#include "ogui/style.hpp"
#include "ogui/widget.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace ogui;

namespace {

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) return;
    std::cout << "asset_test: FAIL " << what << "\n";
    ++g_failures;
}

std::vector<uint8_t> diskBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

// A host that owns a few names and declines everything else.
class FakeHost {
public:
    std::vector<std::string> asked;

    void addText(const std::string& name, const std::string& text) {
        m_files[name] = std::vector<uint8_t>(text.begin(), text.end());
    }
    // Serve a real file under a name the filesystem has never seen.
    bool addFile(const std::string& name, const std::string& diskPath) {
        auto bytes = diskBytes(diskPath);
        if (bytes.empty()) return false;
        m_files[name] = std::move(bytes);
        return true;
    }
    void install() {
        setAssetReader([this](const std::string& name)
                           -> std::optional<std::vector<uint8_t>> {
            asked.push_back(name);
            auto it = m_files.find(name);
            if (it == m_files.end()) return std::nullopt;
            return it->second;
        });
    }

private:
    std::map<std::string, std::vector<uint8_t>> m_files;
};

// No reader may outlive the test that installed it: it is process-wide state.
struct ScopedHost {
    ~ScopedHost() { setAssetReader(nullptr); }
};

bool askedFor(const FakeHost& host, const std::string& name) {
    return std::find(host.asked.begin(), host.asked.end(), name) !=
           host.asked.end();
}

// ---- tests ---------------------------------------------------------------

// The default: nobody installed a reader, so the seam is the filesystem.
void noReaderMeansPlainFiles() {
    ScopedHost guard;
    check(!assetReader(), "a host that installs nothing has no reader");

    const std::string file = std::string(OGUI_SOURCE_DIR) + "/examples/hello.xml";
    auto bytes = readFile(file);
    check(bytes.has_value(), "readFile() reads the file it always read");
    check(!bytes->empty() && bytes->front() == '<',
          "and hands back its bytes, not an error");

    auto missing = readFile(file + "-not-here.xml");
    check(!missing.has_value(), "a name that does not exist is nullopt");
}

// An empty file exists; that is not the same answer as "I don't have it".
void emptyIsNotMissing() {
    ScopedHost guard;
    FakeHost host;
    host.addText("memory://empty.xml", "");
    host.install();
    auto bytes = readFile("memory://empty.xml");
    check(bytes.has_value() && bytes->empty(),
          "a zero-length resource reads as empty, not absent");
}

// The parser is the reason the seam exists: a whole screen from host bytes.
void layoutsComeFromTheHost() {
    ScopedHost guard;
    FakeHost host;
    host.addText("memory://main.xml",
                 R"XML(<screen name="mem" width="200" height="100">
  <label id="hi" text="from memory" x="10" y="10"/>
</screen>)XML");
    host.install();

    XmlParser p;
    auto res = p.parseFile("memory://main.xml");
    check(res.errors.empty(), "a layout the host owns parses with no file");
    check(res.root != nullptr, "and yields a tree");
    if (res.root) {
        const auto& kids = res.root->children();
        check(kids.size() == 1, "the screen holds what the markup declared");
        auto* lab = kids.empty() ? nullptr : dynamic_cast<Label*>(kids.front().get());
        check(lab && lab->text() == "from memory",
              "and it is the label with its text");
    }
    check(askedFor(host, "memory://main.xml"), "the host was asked for the name");
}

// The parser's error still names the file, so a bad asset path is a readable
// mistake rather than an empty screen.
void aMissingLayoutIsReported() {
    ScopedHost guard;
    FakeHost host;
    host.install();
    XmlParser p;
    auto res = p.parseFile("memory://gone.xml");
    check(!res.errors.empty(), "a layout nobody has is an error, not a nil tree");
    check(!res.errors.empty() &&
              res.errors.front().find("memory://gone.xml") != std::string::npos,
          "and the message says which name was asked for");
}

void themesComeFromTheHost() {
    ScopedHost guard;
    FakeHost host;
    host.addText("memory://dark.theme",
                 R"XML(<theme base="dark"><palette accent="#123456"/></theme>)XML");
    host.install();

    XmlParser p;
    auto res = p.parseThemeFile("memory://dark.theme", darkTheme());
    check(res.errors.empty(), "a theme the host owns loads");
    check(res.theme.palette.accent.r == 0x12 &&
              res.theme.palette.accent.g == 0x34 &&
              res.theme.palette.accent.b == 0x56,
          "and its colours reach the theme");
}

void scriptsComeFromTheHost() {
    if (!ScriptHost::available()) {
        std::cout << "asset_test: lua unavailable, script check skipped\n";
        return;
    }
    ScopedHost guard;
    FakeHost host;
    host.addText("memory://init.lua",
                 "local ran = true\nogui.set('nothing-here', 'text', 'x')\n");
    host.install();

    ScriptHost sh;
    std::string err;
    check(sh.loadFile("memory://init.lua", &err), "a script the host owns runs");
    // The chunk itself is fine; only the ogui.set on a missing widget is not,
    // and that arrives as a captured error instead of a load failure.
    check(err.empty(), "loading reported nothing: " + err);
    check(askedFor(host, "memory://init.lua"), "the host was asked for the script");
}

// A family registered against an asset name is how a font ships in an APK.
void fontsComeFromTheHost() {
    ScopedHost guard;
    FakeHost host;
    const std::string ttf =
        std::string(OGUI_ASSETS_DIR) + "/Anton-Regular.ttf";
    if (!host.addFile("memory://face.ttf", ttf)) {
        std::cout << "asset_test: " << ttf << " missing, font check skipped\n";
        return;
    }
    host.install();

    FontManager& fm = FontManager::instance();
    fm.setFontPath("asset face", "memory://face.ttf");
    const Font* f = fm.get("asset face", 18);
    check(f != nullptr, "a face the host owns bakes an atlas");
    if (f) check(f->measure("Ag") > 0.0f, "and measures the glyphs it baked");
    fm.setFontPath("asset face", "");
    fm.clear();
}

void imagesComeFromTheHost() {
    ScopedHost guard;
    FakeHost host;
    const std::string png = std::string(OGUI_ASSETS_DIR) + "/checker.png";
    if (!host.addFile("memory://pic.png", png)) {
        std::cout << "asset_test: " << png << " missing, image check skipped\n";
        return;
    }
    host.install();

    const ImageData* img = ImageManager::instance().load("memory://pic.png");
    check(img != nullptr, "a png the host owns decodes from memory");
    if (img) check(img->w > 0 && img->h > 0 && img->rgba.size() ==
                       static_cast<size_t>(img->w) * img->h * 4,
                   "and comes out as the pixels the file held");
}

// Declining is not failing: the disk stays the answer for names the host does
// not own, which is what lets a host virtualize some assets only.
void declinedNamesFallThroughToDisk() {
    ScopedHost guard;
    FakeHost host;
    host.addText("memory://only-this.xml", "<screen/>");
    host.install();

    const std::string file = std::string(OGUI_SOURCE_DIR) + "/examples/hello.xml";
    auto bytes = readFile(file);
    check(bytes.has_value() && !bytes->empty(),
          "a name the reader declined still reads off the disk");
    XmlParser p;
    auto res = p.parseFile(file);
    check(res.errors.empty() && res.root != nullptr,
          "and the loaders use the same fall-through");
}

}  // namespace

int main() {
    noReaderMeansPlainFiles();
    emptyIsNotMissing();
    layoutsComeFromTheHost();
    aMissingLayoutIsReported();
    themesComeFromTheHost();
    scriptsComeFromTheHost();
    fontsComeFromTheHost();
    imagesComeFromTheHost();
    declinedNamesFallThroughToDisk();

    if (g_failures) {
        std::cout << "asset_test: " << g_failures << " checks failed\n";
        return 1;
    }
    std::cout << "asset_test: OK\n";
    return 0;
}
