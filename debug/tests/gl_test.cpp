// ogui_gl_test.cpp : runs GlBackend on a real GL context, headless.
//
// GlBackend is documented to draw what SoftwareRenderer draws, and until now
// nothing ever checked that: the golden test only exercises the CPU path. This
// test opens an EGL context with no window system (surfaceless, falling back to
// the default display), renders the shared golden scene through GlBackend,
// reads the pixels back and compares them against the SoftwareRenderer frame of
// the same DrawList.
//
// Two passes, when the driver offers them:
//   * OpenGL ES 3.x -- the Android path, driven by a host-supplied entry point
//     getter (dlsym libGLESv2, then eglGetProcAddress) the way a NativeActivity
//     host does it, and Profile::Auto has to recognise the context by itself.
//   * desktop OpenGL -- through EGL with the library's own default loader, so
//     the "#version 330 core" dialect and the libGL branch stay covered too.
//
// A machine without EGL, without a driver or without a renderable config SKIPS
// and returns 0: this test reports capability, not desire. Only a real mismatch
// fails, and the failing frame is dumped as gl_actual.png in the build
// directory (gitignored) for inspection.
//
//   OGUI_GL_TEST_API=es|gl ./ogui_gl_test   (force a single pass)
#include "ogui/font.hpp"
#include "ogui/glbackend.hpp"
#include "ogui/kernel.hpp"
#include "ogui/software.hpp"
#include "ogui/style.hpp"
#include "golden_scene.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// stb is third-party; its designated-initializer style trips -Wextra.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

constexpr int kW = oguitest::kSceneW;
constexpr int kH = oguitest::kSceneH;

// GPU and CPU agree on this scene once the 1-pixel seam between adjacent
// triangles is averaged out (see the comparison below): a couple of units per
// channel, on a hair of the pixels.
constexpr int kMaxChannelDelta = 8;
constexpr double kMaxBadPixelRatio = 0.005;

int g_failures = 0;

void check(bool ok, const std::string& what) {
    if (ok) return;
    std::printf("gl_test: FAIL %s\n", what.c_str());
    ++g_failures;
}

// ---- EGL context with no window system ------------------------------------
class GlContext {
public:
    GlContext() = default;
    ~GlContext() { close(); }

    GlContext(const GlContext&) = delete;
    GlContext& operator=(const GlContext&) = delete;

    bool open(int w, int h, bool es, std::string& why) {
        // Headless first: eglGetDisplay() would try to open X11 or Wayland, and
        // a CI container has neither.
#ifdef EGL_PLATFORM_SURFACELESS_MESA
        auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (getPlatformDisplay)
            dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY,
                                     nullptr);
#endif
        if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (dpy == EGL_NO_DISPLAY) {
            why = "no EGL display (no surfaceless platform, no window system)";
            return false;
        }
        EGLint major = 0, minor = 0;
        if (!eglInitialize(dpy, &major, &minor)) {
            why = "eglInitialize failed";
            dpy = EGL_NO_DISPLAY;
            return false;
        }
        if (!eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API)) {
            why = es ? "EGL cannot bind OpenGL ES" : "EGL cannot bind OpenGL";
            return false;
        }
        const EGLint cfgAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                     EGL_RENDERABLE_TYPE,
                                     es ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_BIT,
                                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                     EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                     EGL_NONE};
        EGLConfig cfg = nullptr;
        EGLint n = 0;
        if (!eglChooseConfig(dpy, cfgAttribs, &cfg, 1, &n) || n < 1) {
            why = es ? "no pbuffer config that can render OpenGL ES 3.0"
                     : "no pbuffer config that can render OpenGL";
            return false;
        }
        const EGLint surfAttribs[] = {EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE};
        surf = eglCreatePbufferSurface(dpy, cfg, surfAttribs);
        if (surf == EGL_NO_SURFACE) {
            why = "eglCreatePbufferSurface failed";
            return false;
        }
        const EGLint esAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        const EGLint glAttribs[] = {EGL_NONE};
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es ? esAttribs : glAttribs);
        if (ctx == EGL_NO_CONTEXT) {
            why = es ? "no OpenGL ES 3.0 context available"
                     : "no desktop OpenGL context available";
            return false;
        }
        if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
            why = "eglMakeCurrent failed";
            return false;
        }
        width = w;
        height = h;
        return true;
    }

    void close() {
        if (dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
            if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
            eglTerminate(dpy);
        }
        dpy = EGL_NO_DISPLAY;
        surf = EGL_NO_SURFACE;
        ctx = EGL_NO_CONTEXT;
    }

    int width = 0, height = 0;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
};

// The shape an Android host hands the backend: the entry points libGLESv2
// exports directly, then whatever eglGetProcAddress knows about.
void* hostGetProc(const char* name) {
    static void* gles = [] {
        void* h = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
        return h ? h : dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
    }();
    if (gles) {
        if (void* p = dlsym(gles, name)) return p;
    }
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

const char* queryAscii(GLenum name) {
    const GLubyte* s = glGetString(name);
    return s ? reinterpret_cast<const char*>(s) : "?";
}

// ---- state the backend must hand back untouched ----------------------------
// GlBackend::render() promises to save and restore the GL state it touches; a
// host mixing the UI with its own scene depends on that promise.
constexpr size_t kStateCount = 5 + 6 + 4 + 4;
const char* kStateNames[kStateCount] = {
    "BLEND", "SCISSOR_TEST", "DEPTH_TEST", "CULL_FACE", "STENCIL_TEST",
    "CURRENT_PROGRAM", "VERTEX_ARRAY_BINDING", "ARRAY_BUFFER_BINDING",
    "ELEMENT_ARRAY_BUFFER_BINDING", "ACTIVE_TEXTURE", "TEXTURE_BINDING_2D",
    "VIEWPORT.x", "VIEWPORT.y", "VIEWPORT.w", "VIEWPORT.h",
    "SCISSOR_BOX.x", "SCISSOR_BOX.y", "SCISSOR_BOX.w", "SCISSOR_BOX.h"};

std::array<GLint, kStateCount> captureState() {
    std::array<GLint, kStateCount> s{};
    size_t i = 0;
    s[i++] = glIsEnabled(GL_BLEND);
    s[i++] = glIsEnabled(GL_SCISSOR_TEST);
    s[i++] = glIsEnabled(GL_DEPTH_TEST);
    s[i++] = glIsEnabled(GL_CULL_FACE);
    s[i++] = glIsEnabled(GL_STENCIL_TEST);
    glGetIntegerv(GL_CURRENT_PROGRAM, &s[i++]);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s[i++]);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s[i++]);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &s[i++]);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &s[i++]);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &s[i++]);
    glGetIntegerv(GL_VIEWPORT, &s[i]);
    i += 4;
    glGetIntegerv(GL_SCISSOR_BOX, &s[i]);
    return s;
}

// The pbuffer is read bottom-up; the DrawList and SoftwareRenderer are y-down.
void readInto(ogui::Framebuffer& fb) {
    std::vector<uint8_t> rows(static_cast<size_t>(fb.w) * fb.h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fb.w, fb.h, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
    glFinish();
    const size_t stride = static_cast<size_t>(fb.w) * 4;
    for (int y = 0; y < fb.h; ++y)
        std::memcpy(fb.at(0, y), rows.data() + stride * static_cast<size_t>(fb.h - 1 - y),
                    stride);
}

struct CompareResult {
    long differingPixels = 0;
    long badPixels = 0;
    int maxDelta = 0;
};

CompareResult compare(const ogui::Framebuffer& a, const ogui::Framebuffer& b) {
    CompareResult r;
    if (a.w != b.w || a.h != b.h) return r;
    const long total = static_cast<long>(a.w) * a.h;
    for (long i = 0; i < total; ++i) {
        int pixelMax = 0;
        for (int c = 0; c < 4; ++c)
            pixelMax = std::max(pixelMax, std::abs(int(a.px[size_t(i) * 4 + c]) -
                                                   int(b.px[size_t(i) * 4 + c])));
        if (pixelMax == 0) continue;
        ++r.differingPixels;
        r.maxDelta = std::max(r.maxDelta, pixelMax);
        if (pixelMax > kMaxChannelDelta) ++r.badPixels;
    }
    return r;
}

// The same comparison, but each pixel of `got` may match any pixel of `want`
// within a one-pixel radius. That is the difference between the two
// rasterizers that is not worth failing on: SoftwareRenderer tests every
// triangle edge inclusively, while GL applies the top-left rule, so a pixel
// sitting exactly on the boundary of a glyph quad is covered by one and not the
// other. What stays visible through that slack is what matters -- a shape in
// the wrong place, or a colour that is wrong by more than a pixel's width.
CompareResult compareWithinOnePixel(const ogui::Framebuffer& got,
                                    const ogui::Framebuffer& want) {
    CompareResult r;
    if (got.w != want.w || got.h != want.h) return r;
    for (int y = 0; y < got.h; ++y) {
        for (int x = 0; x < got.w; ++x) {
            int best = 1 << 30;
            for (int dy = -1; dy <= 1 && best > 0; ++dy) {
                const int yy = y + dy;
                if (yy < 0 || yy >= got.h) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx;
                    if (xx < 0 || xx >= got.w) continue;
                    const size_t g = (size_t(y) * got.w + x) * 4;
                    const size_t w = (size_t(yy) * want.w + xx) * 4;
                    int d = 0;
                    for (int c = 0; c < 4; ++c)
                        d = std::max(d, std::abs(int(got.px[g + c]) - int(want.px[w + c])));
                    best = std::min(best, d);
                }
            }
            if (best == 0) continue;
            ++r.differingPixels;
            r.maxDelta = std::max(r.maxDelta, best);
            if (best > kMaxChannelDelta) ++r.badPixels;
        }
    }
    return r;
}

// Pixels that are not the flat background: a UI that drew nothing at all would
// agree with itself perfectly on an empty frame.
long countInk(const ogui::Framebuffer& fb) {
    const ogui::Color bg = ogui::palette().screen;
    long painted = 0;
    const long total = static_cast<long>(fb.w) * fb.h;
    for (long i = 0; i < total; ++i) {
        const uint8_t* p = &fb.px[size_t(i) * 4];
        if (p[0] != bg.r || p[1] != bg.g || p[2] != bg.b) ++painted;
    }
    return painted;
}

// ---- one pass (ES or desktop) ----------------------------------------------

bool runPass(bool es, const char* label) {
    std::printf("gl_test: %s pass\n", label);
    GlContext gl;
    std::string why;
    if (!gl.open(kW, kH, es, why)) {
        std::printf("gl_test:   SKIP - %s\n", why.c_str());
        return false;
    }
    std::printf("gl_test:   GL_VERSION=%s GLSL=%s RENDERER=%s\n", queryAscii(GL_VERSION),
                queryAscii(GL_SHADING_LANGUAGE_VERSION), queryAscii(GL_RENDERER));

    oguitest::GoldenScene scene;
    if (!scene.build(ogui::darkTheme())) {
        check(false, std::string(label) + ": scene could not be built");
        return true;
    }

    // A host that created the context supplies the getter; the desktop pass
    // leaves it null to exercise the library's own libGL loader.
    ogui::GlBackend backend;
    const bool initOk = es ? backend.init(&hostGetProc, ogui::GlBackend::Profile::Auto)
                           : backend.init(nullptr, ogui::GlBackend::Profile::Auto);
    if (!initOk) {
        check(false, std::string(label) + ": init failed: " + backend.lastError());
        return true;
    }
    // Auto-detection is all that stands between a host and a shader that will
    // never compile, so it has to answer with the API that was asked for.
    check(backend.isGLES() == es,
          std::string(label) + ": Profile::Auto reported " +
              (backend.isGLES() ? "OpenGL ES" : "OpenGL"));

    // Reference frame: the CPU rasterizer 1:1, no supersampling, so both sides
    // see the same hard edges.
    ogui::Framebuffer want, got, again;
    want.resize(kW, kH);
    got.resize(kW, kH);
    again.resize(kW, kH);
    ogui::SoftwareRenderer sr;
    sr.syncFonts();
    sr.syncImages();
    sr.clear(want, ogui::palette().screen);
    sr.render(scene.drawList(), want, 1, nullptr);
    const long ink = countInk(want);
    check(ink > static_cast<long>(kW) * kH / 40,
          "reference frame is mostly empty (" + std::to_string(ink) + " painted pixels)");

    const ogui::Color bg = ogui::palette().screen;
    const auto before = captureState();
    glClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, bg.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    backend.render(scene.drawList(), kW, kH, /*flipY=*/false);
    readInto(got);
    const auto after = captureState();
    for (size_t i = 0; i < kStateCount; ++i) {
        check(before[i] == after[i], std::string(label) + ": render() left GL_" +
                                         kStateNames[i] + " at " +
                                         std::to_string(after[i]) + ", the host had " +
                                         std::to_string(before[i]));
    }

    // The strict 1:1 comparison is printed for information; the assertion is
    // the one-pixel-slack comparison, which is insensitive to how each
    // rasterizer decides coverage on a triangle edge but not to a shape in the
    // wrong place or a colour that is off by more than a pixel's width.
    const CompareResult exact = compare(want, got);
    const CompareResult r = compareWithinOnePixel(got, want);
    const long total = static_cast<long>(kW) * kH;
    const double ratio = double(r.badPixels) / double(total);
    std::printf("gl_test:   1:1 %ld/%ld differ (%ld beyond %d, max %d); "
                "1px slack %ld beyond %d (max %d, %.4f%% bad)\n",
                exact.differingPixels, total, exact.badPixels, kMaxChannelDelta,
                exact.maxDelta, r.badPixels, kMaxChannelDelta, r.maxDelta, ratio * 100.0);
    if (ratio > kMaxBadPixelRatio) {
        // Dumping both frames and their absolute difference (x8) is what makes
        // a mismatch like this diagnosable instead of just a number.
        ogui::Framebuffer diff;
        diff.resize(got.w, got.h);
        for (size_t i = 0; i < diff.px.size(); ++i) {
            if (i % 4 == 3) {
                diff.px[i] = 255;
                continue;
            }
            const int d = std::abs(int(got.px[i]) - int(want.px[i]));
            diff.px[i] = static_cast<uint8_t>(std::min(255, d * 8));
        }
        stbi_write_png("gl_actual.png", got.w, got.h, 4, got.data(), got.w * 4);
        stbi_write_png("gl_want.png", want.w, want.h, 4, want.data(), want.w * 4);
        stbi_write_png("gl_diff.png", diff.w, diff.h, 4, diff.data(), diff.w * 4);
        std::printf("gl_test:   frames dumped to gl_actual.png / gl_want.png / "
                    "gl_diff.png\n");
        check(false, std::string(label) + ": GL frame differs from the software frame");
    }

    // Second frame through the same buffers: the grow-only VBO/IBO are refilled
    // with glBufferSubData and the font atlases must still be on the GPU.
    glClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, bg.a / 255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    backend.render(scene.drawList(), kW, kH, /*flipY=*/false);
    readInto(again);
    const CompareResult rr = compare(got, again);
    check(rr.differingPixels == 0,
          std::string(label) + ": a second render differs from the first in " +
              std::to_string(rr.differingPixels) + " pixels");

    backend.shutdown();
    return true;
}

} // namespace

int main() {
    // A scene without text would compare almost nothing.
    if (!ogui::FontManager::instance().get("sans", 14)) {
        std::printf("gl_test: SKIP - no font available for family \"sans\"\n");
        return 0;
    }

    const char* only = std::getenv("OGUI_GL_TEST_API");
    const bool wantES = !only || std::strcmp(only, "gl") != 0;
    const bool wantGL = !only || std::strcmp(only, "es") != 0;

    const bool esRan = wantES ? runPass(true, "OpenGL ES") : false;
    const bool glRan = wantGL ? runPass(false, "OpenGL desktop") : false;

    if (!esRan && !glRan) {
        std::printf("gl_test: SKIP - no usable EGL GL context on this machine\n");
        return 0;
    }
    if (g_failures) {
        std::printf("gl_test: %d checks failed\n", g_failures);
        return 1;
    }
    std::printf("gl_test: OK (%s%s%s)\n", esRan ? "GLES" : "",
                (esRan && glRan) ? " + " : "", glRan ? "desktop GL" : "");
    return 0;
}
