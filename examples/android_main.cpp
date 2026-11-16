// ogui on a phone: a NativeActivity that draws the same XML screens the desktop
// demo draws.
//
// The NDK's native_app_glue owns the activity thread, its looper and the
// lifecycle messages; this file owns the remaining three hundred lines of what
// a GUI host is: react to the window, feed events to the Kernel, present the
// draw list. ogui::AndroidHost does the parts that are the same on every
// Android app -- EGL, the asset archive, the input translation -- so an app can
// be this short.
//
// Build: ./scripts/build.sh android  (NDK toolchain, arm64-v8a + the APK)
#include "ogui/androidhost.hpp"
#include "ogui/kernel.hpp"
#include "ogui/glbackend.hpp"
#include "ogui/scene3d.hpp"
#include "ogui/style.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

#include "gallery.hpp"
#include <android_native_app_glue.h>

#include <algorithm>
#include <cstdint>

namespace {

struct App {
    ogui::AndroidHost host;
    ogui::Kernel kernel;
    ogui::InputState input;
    gallery::Gallery gallery;
};

App& of(struct android_app* app) {
    return *static_cast<App*>(app->userData);
}

// The gallery screens are authored for this fixed logical canvas. fit() maps it
// onto the device, so it is the one number the demo's layout and its scale share.
constexpr float kDesignWidth = 800.0f;
constexpr float kDesignHeight = 600.0f;

// The screen is sized in device pixels and laid out in logical units, so a
// phone's smaller canvas is the scale's business, not the layout's. The gallery
// is a fixed 800x600 design, so the scale is the one that fits *both* axes
// (fitToDesignScale): a narrow phone zooms out until the design spans the width,
// a landscape phone until it spans the height, and a screen wide enough at its
// own density keeps the density. Sizing only the width used to leave a landscape
// phone with 384 logical units of height, so everything below y=384 was off the
// screen.
//
// Every gallery screen is sized, not just the active one: the nav bar swaps
// screens without a resize, and a screen still holding the 800x600 it was parsed
// with would lay out at the wrong scale the moment it was shown.
void fit(App& a) {
    if (!a.host.hasWindow()) return;
    const float w = static_cast<float>(a.host.width());
    const float h = static_cast<float>(a.host.height());
    const float scale = ogui::fitToDesignScale(w, h, kDesignWidth, kDesignHeight,
                                              a.host.dpiScale());
    for (auto& screen : a.gallery.screens) {
        if (!screen) continue;
        screen->width = w;
        screen->height = h;
        screen->scale = scale;
    }
    a.kernel.invalidate();
}

void frame(App& a) {
    if (!a.host.hasWindow()) return;
    a.input.time = a.host.elapsed();
    // What the keyboard typed goes into the tree before the frame that has to
    // draw it; what the tree has focused goes to the keyboard after the frame
    // that moved it. Between those two calls an app types, deletes and submits.
    a.host.applyImeEdits(a.kernel, a.input);
    // The bars move too -- a keyboard sliding up is a window resize the surface
    // never sees -- and the tree has to be laid out for the rectangle that is
    // left before anything is drawn into it.
    a.host.applyWindowInsets(a.kernel);
    a.host.input().deliverChar(a.input);
    a.kernel.frame(a.input);
    a.host.input().endFrame(a.input);
    a.host.syncIme(a.kernel);
    a.host.present(a.kernel);
}

void onAppCmd(struct android_app* app, int32_t cmd) {
    App& a = of(app);
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window && a.host.setWindow(app->window)) {
            fit(a);
            frame(a);   // draw immediately: the OS is waiting for the first one
        }
        break;
    case APP_CMD_TERM_WINDOW:
        // The surface goes, the context stays: what is on the GPU (font atlases,
        // the pipeline) is still valid when the window comes back.
        a.host.releaseWindow();
        a.host.input().reset();
        break;
    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONTENT_RECT_CHANGED:
    case APP_CMD_CONFIG_CHANGED:
        // The geometry is read again here because a rotation that the activity
        // survives through configChanges never comes back through setWindow():
        // same window, same surface, new size, and only these commands say so.
        a.host.refreshWindowSize();
        fit(a);
        frame(a);
        break;
    case APP_CMD_WINDOW_REDRAW_NEEDED:
        frame(a);
        break;
    case APP_CMD_LOST_FOCUS:
        a.host.input().reset();   // no key was released while we were gone
        break;
    case APP_CMD_PAUSE:
        a.host.hideKeyboard();
        break;
    default:
        break;
    }
}

// Returning 1 says the event is ours and the looper should stop handing it out;
// the frame is drawn in the same wake-up, which is what makes a tap feel
// instant instead of up to a tick late.
int32_t onInput(struct android_app* app, AInputEvent* event) {
    App& a = of(app);
    if (!a.host.onInputEvent(event, a.input)) return 0;
    frame(a);
    return 1;
}

} // namespace

extern "C" void android_main(struct android_app* app) {
    App a;
    a.kernel.init();

    // Before anything is parsed: the layouts, the theme, the scripts, the font
    // faces and the pictures the demo shows are all inside the APK, and the
    // asset reader is the only way to name them.
    if (!a.host.create(app->activity)) {
        // No EGL is not a crash-worthy condition on a device with no working
        // graphics driver; say so in logcat and let the activity finish.
        ANativeActivity_finish(app->activity);
        return;
    }
    a.host.installAssetReader();

    // Landscape gets the whole display. The gallery is authored for 800x600 and
    // a phone on its side is short, so the status bar and the gesture bar were
    // costing it two of the rows it has no height to spare; hiding them leaves
    // the insets at zero and the layout moves out on its own. Portrait and
    // square windows keep them, which is what an app that is also a document
    // wants -- and the host re-decides on every rotation.
    a.host.setHideSystemBarsInLandscape(true);

    a.gallery = gallery::loadAll(kDesignWidth, kDesignHeight);
    int navIndex = 0;
    gallery::install(a.kernel, a.gallery, navIndex, 0, kDesignWidth, kDesignHeight);

    // ---- the 3D screen ---------------------------------------------------
    // The SceneView on screen 3D is wired exactly like the desktop demo's: the
    // mesh upload needs the context, so it waits for the first frame that finds
    // the widget, and from then on one submit() per frame fills its target and
    // leaves a textured quad for the 2D pass. The phone used to show that
    // screen with nothing in it -- the layout was there and no host ever ran a
    // pass -- so the cube is the proof this path is the same one the desktop
    // exercises, not a second implementation of it.
    struct ScenePass {
        int cube = 0;
        float angle = 0.0f;
        float dir = 1.0f;
        float last = -1.0f;
        bool offscreen = false;
        // Which backend the mesh id belongs to: a context the phone re-creates
        // makes a new backend, and the old id then names nothing.
        const void* owner = nullptr;
    } scene;
    a.host.setBeforeDraw([&](ogui::Kernel& k, int, int) {
        ogui::SceneView* view = nullptr;
        k.eachWidget([&](ogui::Widget* w) {
            if (auto* v = dynamic_cast<ogui::SceneView*>(w)) view = v;
        });
        if (!view) {
            // Off screen: drop the clock, so coming back does not advance the
            // cube by the seconds spent on another screen.
            scene.last = -1.0f;
            return;
        }

        ogui::GlBackend* backend = a.host.backend();
        if (!backend) return;
        if (scene.owner != backend) {
            scene.owner = backend;
            scene.cube = 0;
        }
        if (!scene.cube) {
            scene.cube = backend->uploadMesh(ogui::cubeMesh(1.7f));
            if (!scene.cube) return;
        }

        const float now = a.host.elapsed();
        const float dt = scene.last < 0.0f ? 0.0f :
                         std::min(0.1f, now - scene.last);
        scene.last = now;
        scene.angle += dt * 0.7f * scene.dir;

        ogui::Scene3D& s = view->scene();
        s.clear();
        const ogui::Matrix4x4 model =
            ogui::Matrix4x4::translation(
                {scene.offscreen ? 40.0f : 0.0f, 0.0f, 0.0f}) *
            ogui::Matrix4x4::rotationY(scene.angle) *
            ogui::Matrix4x4::rotationX(0.35f);
        s.addMesh(scene.cube, model, {ogui::Color{214, 178, 96, 255}, 0});

        // The box is in logical units and the target in device pixels, which is
        // the screen's fit scale -- the same factor the 2D pass uses.
        const float sc = k.scale();
        const ogui::Rect b = view->bounds();
        view->submit(*backend, static_cast<int>(b.w * sc),
                     static_cast<int>(b.h * sc));

        k.setText("lbl3d_culled", "drawn: " +
                  std::to_string(backend->sceneStats().drawn) + "  culled: " +
                  std::to_string(backend->sceneStats().culled));
    });
    a.kernel.bind("btn3d_spin", [&scene] { scene.dir = -scene.dir; });
    a.kernel.bind("btn3d_cull", [&scene] { scene.offscreen = !scene.offscreen; });

    const ogui::Color& bg = ogui::activeTheme().palette.screen;
    a.host.setClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f);

    app->userData = &a;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInput;

    while (!app->destroyRequested) {
        int events = 0;
        android_poll_source* source = nullptr;
        // Blocked with no surface to draw on, otherwise a 60 Hz tick: a finger
        // wakes the looper at once, an animation only needs this clock.
        const int budget = a.host.hasWindow() ? 16 : -1;
        // ALooper_pollAll is obsoleted (it may swallow a wake), so drain by hand:
        // wait once with the tick's budget, then take everything else that is
        // already queued without waiting again.
        bool ticked = false;
        for (int timeout = budget;; timeout = 0) {
            const int id = ALooper_pollOnce(timeout, nullptr, &events,
                                            reinterpret_cast<void**>(&source));
            if (id == ALOOPER_POLL_TIMEOUT) { ticked = true; break; }
            if (id == ALOOPER_POLL_ERROR) break;
            if (source) source->process(app, source);
            if (app->destroyRequested) break;
        }
        // A timeout is the animation clock. Input drew itself, in the callback
        // that read it, so a wake-up that came from a finger needs no second
        // frame here.
        if (ticked && a.host.hasWindow()) frame(a);
    }

    a.host.destroy();
}
