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
#include "ogui/scene3d.hpp"
#include "ogui/host.hpp"
#include "gallery.hpp"

#include <chrono>
#include <cmath>
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
    // The window is sized in device pixels so the 800x600 logical layout the
    // gallery was parsed for keeps its physical size on a HiDPI display;
    // run() feeds Screen::scale from the same dpi.
    const float ui = ogui::Host::systemDpiScale();
    ogui::Host host({.title = "ogui - GUI para OpenGL via XML",
                     .width = static_cast<int>(W * ui),
                     .height = static_cast<int>(H * ui)});
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

    // ---- the 3D screen (layouts/layout_scene3d.xml) ----------------------
    // Mesh upload needs the context current, so it happens on the first frame
    // that finds the SceneView on screen, not here. From then on one
    // submit() per frame is all the widget asks for: it sizes its offscreen
    // target to the box the layout gave it, renders the scene into it and
    // draws the result in the 2D pass.
    int cube = 0;
    float spin = 0.0f;
    float spinDir = 1.0f;
    bool cubeOffscreen = false;
    auto lastFrame = std::chrono::steady_clock::now();
    host.setBeforeDraw([&](ogui::Kernel& k, int, int) {
        ogui::SceneView* view = nullptr;
        k.eachWidget([&](ogui::Widget* w) {
            if (auto* v = dynamic_cast<ogui::SceneView*>(w)) view = v;
        });
        if (!view) return;

        ogui::GlBackend& backend = host.backend();
        if (!cube) {
            cube = backend.uploadMesh(ogui::cubeMesh(1.7f));
            if (!cube) return;
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastFrame).count();
        lastFrame = now;
        spin += dt * 0.7f * spinDir;

        ogui::Scene3D& scene = view->scene();
        scene.clear();
        const ogui::Matrix4x4 model =
            ogui::Matrix4x4::translation({cubeOffscreen ? 40.0f : 0.0f, 0.0f, 0.0f}) *
            ogui::Matrix4x4::rotationY(spin) * ogui::Matrix4x4::rotationX(0.35f);
        scene.addMesh(cube, model, {ogui::Color{214, 178, 96, 255}, 0});

        const float s = k.scale();
        const ogui::Rect b = view->bounds();
        view->submit(backend, static_cast<int>(b.w * s), static_cast<int>(b.h * s));

        // The culled count is the proof the CPU skip works: the cube jumps
        // 40 units to the side, leaves the frustum and costs no draw call.
        k.setText("lbl3d_culled", "drawn: " +
                  std::to_string(backend.sceneStats().drawn) + "  culled: " +
                  std::to_string(backend.sceneStats().culled));
    });
    kernel.bind("btn3d_spin", [&] { spinDir = -spinDir; });
    kernel.bind("btn3d_cull", [&] { cubeOffscreen = !cubeOffscreen; });

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