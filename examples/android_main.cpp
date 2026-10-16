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
#include "ogui/style.hpp"
#include "ogui/widgets.hpp"
#include "ogui/xmlparser.hpp"

#include "gallery.hpp"
#include <android_native_app_glue.h>

#include <cstdint>

namespace {

struct App {
    ogui::AndroidHost host;
    ogui::Kernel kernel;
    ogui::InputState input;
    gallery::Gallery gallery;
    // The widget the IME is answering for. Compared by pointer, which the
    // Kernel keeps alive for the whole walk, so a screen swap that moved focus
    // reads as a change instead of as freed memory.
    ogui::Widget* editing = nullptr;
};

App& of(struct android_app* app) {
    return *static_cast<App*>(app->userData);
}

// The screen is sized in device pixels and laid out in logical units, so a
// phone's smaller canvas is the scale's business, not the layout's. The gallery
// screens are authored for 800x600 logical; a narrow phone clips the right edge
// of the widest row until someone authors a phone layout for them.
void fit(App& a) {
    auto screen = a.kernel.currentScreen();
    if (!screen || !a.host.hasWindow()) return;
    screen->width = static_cast<float>(a.host.width());
    screen->height = static_cast<float>(a.host.height());
    screen->scale = a.host.dpiScale();
    a.kernel.invalidate();
}

void frame(App& a) {
    if (!a.host.hasWindow()) return;
    a.input.time = a.host.elapsed();
    a.host.input().deliverChar(a.input);
    a.kernel.frame(a.input);
    a.host.input().endFrame(a.input);
    if (!a.host.present(a.kernel)) return;

    // The keyboard follows the focus, not a tap: a layout that moves focus with
    // the arrows or from a script gets the IME too, and a widget that is not an
    // editor never asks for it.
    ogui::Widget* focus = a.kernel.focusedWidget();
    if (focus == a.editing) return;
    a.editing = focus;
    if (dynamic_cast<ogui::TextInput*>(focus)) a.host.showKeyboard();
    else a.host.hideKeyboard();
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

    a.gallery = gallery::loadAll(800.0f, 600.0f);
    int navIndex = 0;
    gallery::install(a.kernel, a.gallery, navIndex, 0, 800.0f, 600.0f);

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
