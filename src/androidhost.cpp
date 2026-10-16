// androidhost.cpp -- the ANativeWindow/EGL/AAsset side of ogui.
//
// Everything here is Android-only by necessity: the display, the surface, the
// asset archive and the input event getters do not exist anywhere else. What
// does not need a phone -- which finger the tree sees, what a cancellation is,
// what key a key code means -- is in androidinput.cpp and is tested there.
//
// Off Android the class keeps its promises by refusing cleanly, the way the
// desktop Host does off Windows, so one source tree builds for both.
#include "ogui/androidhost.hpp"
#include "ogui/assets.hpp"
#include "ogui/glbackend.hpp"

#include <cstdint>

#ifdef __ANDROID__

#include "androidime.hpp"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <cstdarg>
#include <ctime>
#include <vector>

namespace {

void loge(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_ERROR, "ogui", fmt, ap);
    va_end(ap);
}

double monoSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + ts.tv_nsec * 1e-9;
}

const char* eglName(EGLint e) {
    switch (e) {
    case EGL_SUCCESS:           return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:   return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:         return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:     return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:        return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:       return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:       return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:         return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:     return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:       return "EGL_BAD_SURFACE";
    default:                    return "an EGL error";
    }
}

// The config an Android window can be drawn into. The one whose
// EGL_NATIVE_VISUAL_ID *is* the window's pixel format is the one to take: the
// compositor then copies instead of converting, and eglCreateWindowSurface
// answers instead of EGL_BAD_MATCH.
EGLConfig pickConfig(EGLDisplay dpy, ANativeWindow* win, std::string& err) {
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE};
    EGLConfig found[16];
    EGLint n = 0;
    if (!eglChooseConfig(dpy, attribs, found, 16, &n) || n < 1) {
        err = "no renderable config for this window (EGL " +
              std::string(eglName(eglGetError())) + ")";
        return nullptr;
    }
    const int32_t want = ANativeWindow_getFormat(win);
    for (EGLint i = 0; i < n; ++i) {
        EGLint visual = 0;
        eglGetConfigAttrib(dpy, found[i], EGL_NATIVE_VISUAL_ID, &visual);
        if (want > 0 && visual == want) return found[i];
    }
    // Nothing lines up: ask the window for the format the config does render,
    // which is the other half of the same handshake.
    EGLint visual = 0;
    eglGetConfigAttrib(dpy, found[0], EGL_NATIVE_VISUAL_ID, &visual);
    ANativeWindow_setBuffersGeometry(win, 0, 0, visual);
    return found[0];
}

bool motionAction(int32_t raw, ogui::AndroidAction& out) {
    switch (raw & AMOTION_EVENT_ACTION_MASK) {
    case AMOTION_EVENT_ACTION_DOWN:         out = ogui::AndroidAction::Down;         return true;
    case AMOTION_EVENT_ACTION_POINTER_DOWN: out = ogui::AndroidAction::PointerDown;  return true;
    case AMOTION_EVENT_ACTION_MOVE:         out = ogui::AndroidAction::Move;         return true;
    case AMOTION_EVENT_ACTION_UP:           out = ogui::AndroidAction::Up;           return true;
    case AMOTION_EVENT_ACTION_POINTER_UP:   out = ogui::AndroidAction::PointerUp;    return true;
    case AMOTION_EVENT_ACTION_CANCEL:       out = ogui::AndroidAction::Cancel;       return true;
    default: return false;   // a hover, a wheel, a button state: not a finger
    }
}

} // namespace

namespace ogui {

AndroidHost::~AndroidHost() { destroy(); }

bool AndroidHost::create(ANativeActivity* activity) {
    if (m_display) return true;
    if (!activity) { m_error = "no ANativeActivity"; return false; }
    m_activity = activity;
    // Before the window, before the first frame: the activity's UI thread can be
    // asked for an input connection at any moment after the class was loaded,
    // and an answer of "no bridge" would put the keyboard up over nothing.
    androidImeAttach(&m_input);

    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        m_error = "eglGetDisplay failed";
        loge("%s", m_error.c_str());
        return false;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        m_error = std::string("eglInitialize: ") + eglName(eglGetError());
        loge("%s", m_error.c_str());
        return false;
    }
    m_display = dpy;
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        m_error = std::string("eglBindAPI(ES): ") + eglName(eglGetError());
        loge("%s", m_error.c_str());
        return false;
    }
    m_startSeconds = monoSeconds();
    return true;
}

void AndroidHost::destroy() {
    if (m_display) {
        // The backend frees its GL objects, which only works while a context is
        // current -- so the surface goes last, not first.
        if (m_surface) makeCurrent();
        if (m_gl) {
            if (m_context) makeCurrent();
            m_gl->shutdown();
            delete m_gl;
            m_gl = nullptr;
        }
        m_glReady = false;
        releaseWindow();
        if (m_context) {
            eglMakeCurrent(static_cast<EGLDisplay>(m_display), EGL_NO_SURFACE,
                           EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(static_cast<EGLDisplay>(m_display),
                              static_cast<EGLContext>(m_context));
            m_context = nullptr;
        }
        eglTerminate(static_cast<EGLDisplay>(m_display));
        m_display = nullptr;
    }
    m_config = nullptr;
    m_window = nullptr;
    m_width = m_height = 0;
    // The reader we installed points at this activity's archive; leaving it in
    // place would hand a dangling AAssetManager to the next readFile().
    if (m_activity) setAssetReader(nullptr);
    m_activity = nullptr;
    // The bridge is a member of this host, and this host is on the app thread's
    // stack: after destroy() returns there is nothing for a late UI-thread call
    // to point at, so it gets a null bridge instead of a freed one.
    androidImeDetach();
    m_input.reset();
}

bool AndroidHost::setWindow(ANativeWindow* window) {
    if (!m_display) { m_error = "create() before setWindow()"; return false; }
    if (!window) { m_error = "no window"; return false; }
    auto dpy = static_cast<EGLDisplay>(m_display);
    if (m_surface) releaseWindow();

    EGLConfig cfg = pickConfig(dpy, window, m_error);
    if (!cfg) { loge("%s", m_error.c_str()); return false; }

    if (!m_context || m_config != static_cast<void*>(cfg)) {
        if (m_context) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(dpy, static_cast<EGLContext>(m_context));
            m_context = nullptr;
            // The pipeline was built for the context that is about to go.
            delete m_gl;
            m_gl = nullptr;
            m_glReady = false;
        }
        m_config = cfg;
        const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttribs);
        if (ctx == EGL_NO_CONTEXT) {
            m_error = std::string("eglCreateContext(ES 3): ") +
                      eglName(eglGetError());
            loge("%s", m_error.c_str());
            return false;
        }
        m_context = ctx;
    }

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, window, nullptr);
    if (surf == EGL_NO_SURFACE) {
        // A window that kept its own format anyway: insist on the config's.
        EGLint visual = 0;
        eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &visual);
        ANativeWindow_setBuffersGeometry(window, 0, 0, visual);
        surf = eglCreateWindowSurface(dpy, cfg, window, nullptr);
    }
    if (surf == EGL_NO_SURFACE) {
        m_error = std::string("eglCreateWindowSurface: ") +
                  eglName(eglGetError());
        loge("%s", m_error.c_str());
        return false;
    }
    m_surface = surf;
    m_window = window;

    if (!makeCurrent()) return false;
    eglQuerySurface(dpy, surf, EGL_WIDTH, &m_width);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &m_height);

    if (!m_gl) m_gl = new GlBackend();
    if (!m_glReady) {
        // No getter: the backend's own Android loader tries libGLESv2 and then
        // eglGetProcAddress, which is the whole of what a phone offers.
        if (!m_gl->init(nullptr, GlBackend::Profile::OpenGLES)) {
            m_error = "GlBackend: " + m_gl->lastError();
            loge("%s", m_error.c_str());
            return false;
        }
        m_glReady = true;
    }
    return true;
}

void AndroidHost::releaseWindow() {
    if (!m_display) return;
    auto dpy = static_cast<EGLDisplay>(m_display);
    if (m_surface) {
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       static_cast<EGLContext>(m_context));
        eglDestroySurface(dpy, static_cast<EGLSurface>(m_surface));
        m_surface = nullptr;
    }
    m_window = nullptr;
    m_width = m_height = 0;
}

bool AndroidHost::hasWindow() const { return m_surface != nullptr; }

bool AndroidHost::refreshWindowSize() {
    if (!m_window || !m_surface) return false;
    // The window is the thing that was resized; the surface is the same object
    // it was before, so the geometry is read from the window.
    const int32_t w = ANativeWindow_getWidth(m_window);
    const int32_t h = ANativeWindow_getHeight(m_window);
    if (w <= 0 || h <= 0) return false;
    m_width = w;
    m_height = h;
    return true;
}

bool AndroidHost::makeCurrent() {
    auto dpy = static_cast<EGLDisplay>(m_display);
    if (!eglMakeCurrent(dpy, static_cast<EGLSurface>(m_surface),
                        static_cast<EGLSurface>(m_surface),
                        static_cast<EGLContext>(m_context))) {
        m_error = std::string("eglMakeCurrent: ") + eglName(eglGetError());
        return false;
    }
    return true;
}

float AndroidHost::elapsed() const {
    return static_cast<float>(monoSeconds() - m_startSeconds);
}

float AndroidHost::dpiScale() const {
    if (!m_activity || !m_activity->assetManager) return 1.0f;
    AConfiguration* cfg = AConfiguration_new();
    if (!cfg) return 1.0f;
    AConfiguration_fromAssetManager(cfg, m_activity->assetManager);
    const int32_t d = AConfiguration_getDensity(cfg);
    AConfiguration_delete(cfg);
    // The constants in <android/configuration.h> *are* the dpi numbers --
    // DENSITY_LOW is 120, DENSITY_HIGH 240, DENSITY_XXHIGH 480 -- and so is
    // every density the header has no name for, which is the half of the
    // question that matters: the phone this was first run on reports 450, one
    // of the unnamed ones, and a switch over the seven named buckets answered
    // 1.0 for it, drawing a whole layout at mdpi size on a screen three times
    // as dense. Screen::scale is device pixels per logical unit, and mdpi is
    // the unit a layout is authored in, so the answer is the division. The
    // sentinels (DEFAULT 0, ANY 0xfffe, NONE 0xffff) are not densities and stay
    // at 1.0, and so does anything else that could not be one.
    if (d >= 100 && d <= 4000) return static_cast<float>(d) / 160.0f;
    return 1.0f;
}

bool AndroidHost::installAssetReader() {
    AAssetManager* am = m_activity ? m_activity->assetManager : nullptr;
    if (!am) { m_error = "the activity carries no AAssetManager"; return false; }
    setAssetReader([am](const std::string& name)
                       -> std::optional<std::vector<uint8_t>> {
        AAsset* a = AAssetManager_open(am, name.c_str(), AASSET_MODE_BUFFER);
        // Declined, not empty: a name the archive does not have still belongs
        // to the filesystem, and readFile() falls through to it.
        if (!a) return std::nullopt;
        const off64_t len = AAsset_getLength64(a);
        std::vector<uint8_t> out;
        if (len > 0) {
            out.resize(static_cast<size_t>(len));
            if (AAsset_read(a, out.data(), out.size()) !=
                static_cast<int>(out.size()))
                out.clear();
        }
        AAsset_close(a);
        return out;
    });
    return true;
}

bool AndroidHost::onInputEvent(AInputEvent* event, InputState& io) {
    if (!event) return false;
    switch (AInputEvent_getType(event)) {
    case AINPUT_EVENT_TYPE_MOTION: {
        AndroidAction action;
        const int32_t raw = AMotionEvent_getAction(event);
        if (!motionAction(raw, action)) return false;
        const int index = (raw & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
                          AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        const size_t count = AMotionEvent_getPointerCount(event);
        std::vector<AndroidPointer> pts;
        pts.reserve(count);
        for (size_t i = 0; i < count; ++i)
            pts.push_back({AMotionEvent_getPointerId(event, i),
                           AMotionEvent_getX(event, i),
                           AMotionEvent_getY(event, i)});
        m_input.onMotion(io, action, index, pts.data(),
                         static_cast<int>(count));
        return true;
    }
    case AINPUT_EVENT_TYPE_KEY: {
        const int32_t a = AKeyEvent_getAction(event);
        if (a != AKEY_EVENT_ACTION_DOWN && a != AKEY_EVENT_ACTION_UP)
            return false;
        m_input.onKey(io, AKeyEvent_getKeyCode(event),
                      a == AKEY_EVENT_ACTION_DOWN,
                      (AKeyEvent_getFlags(event) & AKEY_EVENT_FLAG_LONG_PRESS)
                          != 0);
        return true;
    }
    default:
        return false;
    }
}

bool AndroidHost::present(Kernel& kernel) {
    if (!m_surface) { m_error = "no window to present"; return false; }
    auto dpy = static_cast<EGLDisplay>(m_display);
    if (!makeCurrent()) return false;

    glViewport(0, 0, m_width, m_height);
    glClearColor(m_clearR, m_clearG, m_clearB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    // flipY stays off: the draw list's y=0 is the top of the screen and GL's y=0
    // is the bottom of the framebuffer, which is the pair the default
    // projection already lines up.
    m_gl->render(kernel.drawList(), m_width, m_height, false);

    if (!eglSwapBuffers(dpy, static_cast<EGLSurface>(m_surface))) {
        const EGLint e = eglGetError();
        m_error = std::string("eglSwapBuffers: ") + eglName(e);
        // A surface the window manager took back is the common case and is
        // recoverable: drop it so the app re-creates it on the next
        // APP_CMD_INIT_WINDOW instead of spinning on a dead swap.
        if (e == EGL_BAD_SURFACE) releaseWindow();
        loge("%s", m_error.c_str());
        return false;
    }
    return true;
}

void AndroidHost::sendImeRequest(const ImeRequest& request) {
    // Sixty times a second is what the frame loop is; the keyboard is not
    // interested in being told the same thing each one of them, and a restart it
    // did not ask for closes the word it is in the middle of composing.
    if (m_imeSentKnown && m_imeSent.inputType == request.inputType &&
        m_imeSent.imeOptions == request.imeOptions &&
        m_imeSent.visible == request.visible) {
        return;
    }
    m_imeSent = request;
    m_imeSentKnown = true;

    if (androidImeAvailable() && androidImeSendRequest(request)) return;

    // No Java bridge: the NDK's own call. It brings the keyboard up over a field
    // that has no input connection, which is the best an app made of nothing but
    // native code can do -- a hardware keyboard's key events still reach the
    // tree through onInputEvent().
    if (!m_activity) return;
    if (request.visible) {
        ANativeActivity_showSoftInput(m_activity,
                                      ANATIVEACTIVITY_SHOW_SOFT_INPUT_IMPLICIT);
    } else {
        ANativeActivity_hideSoftInput(m_activity,
                                      ANATIVEACTIVITY_HIDE_SOFT_INPUT_IMPLICIT_ONLY);
    }
}

void AndroidHost::applyImeEdits(Kernel& kernel, InputState& io) {
    m_input.applyEdits(kernel.focusedWidget(), io);
}

void AndroidHost::syncIme(Kernel& kernel) {
    Widget* focus = kernel.focusedWidget();
    m_input.syncEditor(focus);
    sendImeRequest(imeRequestFor(focus));
}

bool AndroidHost::hasImeBridge() const { return androidImeAvailable(); }

void AndroidHost::hideKeyboard() { sendImeRequest(ImeRequest()); }

void AndroidHost::setClearColor(float r, float g, float b) {
    m_clearR = r; m_clearG = g; m_clearB = b;
}

} // namespace ogui

#else   // !__ANDROID__ -- the host is declared everywhere and opens nowhere else

namespace ogui {

AndroidHost::~AndroidHost() = default;

bool AndroidHost::create(ANativeActivity*) {
    m_error = "AndroidHost requires Android";
    return false;
}
void AndroidHost::destroy() {}
bool AndroidHost::setWindow(ANativeWindow*) {
    m_error = "AndroidHost requires Android";
    return false;
}
void AndroidHost::releaseWindow() {}
bool AndroidHost::hasWindow() const { return false; }
bool AndroidHost::refreshWindowSize() { return false; }
float AndroidHost::elapsed() const { return 0.0f; }
float AndroidHost::dpiScale() const { return 1.0f; }
bool AndroidHost::installAssetReader() {
    m_error = "AndroidHost requires Android";
    return false;
}
bool AndroidHost::onInputEvent(AInputEvent*, InputState&) { return false; }
bool AndroidHost::present(Kernel&) {
    m_error = "AndroidHost requires Android";
    return false;
}
void AndroidHost::applyImeEdits(Kernel&, InputState&) {}
void AndroidHost::syncIme(Kernel&) {}
bool AndroidHost::hasImeBridge() const { return false; }
void AndroidHost::hideKeyboard() {}
void AndroidHost::setClearColor(float r, float g, float b) {
    m_clearR = r; m_clearG = g; m_clearB = b;
}
} // namespace ogui

#endif  // __ANDROID__

namespace ogui {

// The safe area is the one part of this host that needs no phone to be right:
// it moves a number the platform reported into the kernel, and both ends of
// that exist on a desktop. It lives outside the ifdef for that reason -- the
// alternative is a rule about device pixels and logical units that only ever
// runs on the one machine where it cannot be tested.
void AndroidHost::applyWindowInsets(Kernel& kernel) {
    WindowInsets px;
    if (!m_input.takeWindowInsets(px)) return;
    // The kernel's scale and not dpiScale(): a bar is measured against the
    // window, and the window is laid out in whatever unit the screen that owns
    // it decided to be drawn in.
    const float sc = kernel.scale();
    kernel.setInsets(Insets{px.left / sc, px.top / sc, px.right / sc,
                            px.bottom / sc});
}

} // namespace ogui
