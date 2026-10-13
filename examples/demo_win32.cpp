#include <fstream>
// ogui demo (Win32): the library owns the window now.
//
// ogui::Host creates the OS window, the WGL context, runs the event loop,
// feeds input into the Kernel and renders every frame. The application just
// wires a layout, handlers and a screen -- no Win32 boilerplate left.
//
// Build (mingw, static):
//   x86_64-w64-mingw32-g++ -std=c++20 -O2 -static -static-libgcc
//       -static-libstdc++ -Iinclude examples/demo_win32.cpp libogui.a
//       -lgdi32 -lopengl32 -lshell32 -mwindows -o demo.exe
//
// Tips: F11 toggles borderless fullscreen; drag & drop files onto the window
// to see the drop callback in action.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "ogui/kernel.hpp"
#include "ogui/xmlparser.hpp"
#include "ogui/widgets.hpp"
#include "ogui/host.hpp"
#include "gallery.hpp"

#include <cstdio>
#include <fstream>
#include <string>

namespace {

void fail(const std::string& msg) {
#if defined(_WIN32)
    MessageBoxA(nullptr, msg.c_str(), "ogui", MB_ICONERROR);
#endif
    std::fprintf(stderr, "ogui: %s\n", msg.c_str());
}

} // namespace

int main() {
    const float W = 800.0f, H = 600.0f;

    // ---- parse the grouped gallery screens -------------------------------
    gallery::Gallery g = gallery::loadAll(W, H);

    ogui::Kernel kernel;
    kernel.init();

    if (g.screens.empty()) {
        fail("Nenhum layout encontrado. Coloque layout_demo.xml e a pasta "
             "layouts/ ao lado do executavel.");
        return 1;
    }

    // ---- window (library-owned) ------------------------------------------
    ogui::Host host({.title = "ogui - GUI para OpenGL via XML",
                     .width = 800, .height = 600});
    const ogui::Color win = ogui::palette().window;
    host.setClearColor(win.r / 255.0f, win.g / 255.0f, win.b / 255.0f);

    int navIndex = 0;
    gallery::install(kernel, g, navIndex, 0, W, H);   // pushes screen 0 + wires navPrev/Next

    // ---- handlers --------------------------------------------------------
    kernel.bind("onStart", [&] {
        kernel.setText("lbl_status", "Botao Iniciar pressionado!");
        kernel.addLog("onStart");
    });
    kernel.bind("onLoad", [&] {
        kernel.setText("lbl_status", "Botao Carregar pressionado!");
        kernel.addLog("onLoad");
    });

    // The slider drives the progress bar through its value callback (both live
    // on the first screen, which is the active one right after install()).
    if (auto* sl = dynamic_cast<ogui::Slider*>(kernel.findWidget("sl_demo")))
        if (auto* pb = dynamic_cast<ogui::ProgressBar*>(kernel.findWidget("pb_demo")))
            sl->setOnChanged([pb](float v) { pb->setValue(v / 100.0f); });

    host.setOnFilesDropped([&](const std::vector<std::string>& files) {
        std::string msg = files.empty() ? "Drop vazio?" : "Arquivos:";
        for (size_t i = 0; i < files.size() && i < 3; ++i) {
            msg += "\n  " + files[i];
            if (msg.size() > 140) { msg += "\n  ..."; break; }
        }
        kernel.setText("lbl_status", msg);
        kernel.addLog("drop " + std::to_string(files.size()));
    });
    if (!host.open()) {
        fail(host.lastError());
        return 1;
    }
    host.run(kernel);
    return 0;
}