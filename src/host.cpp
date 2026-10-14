#include "ogui/host.hpp"
#include "ogui/clipboard.hpp"

#include <cstring>
#include <string>
#include <vector>

// The native host implementation currently targets Win32 + WGL (mirroring the
// reference demo that used to live in examples/). Every other platform gets a
// stub that fails open() with a clear message; GlBackend + Kernel remain fully
// portable for custom hosts.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <tchar.h>

namespace ogui {

namespace {

// ---- UTF-16 <-> UTF-8 (system clipboard / window title) --------------------
std::string wideToUtf8(const wchar_t* w) {
    if (!w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::vector<char> buf(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, w, -1, buf.data(), n, nullptr, nullptr);
    return std::string(buf.data(), static_cast<size_t>(n - 1));   // drop the NUL
}

bool utf8ToWide(const std::string& s, std::vector<wchar_t>& out) {
    out.clear();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                      static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n) + 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), n);
    return true;
}

// ---- GL 1.1 entry points (exported by opengl32.dll) ------------------------
typedef void (WINAPI* PFNGLCLEAR)(unsigned int);
typedef void (WINAPI* PFNGLCLEARCOLOR)(float, float, float, float);
typedef void (WINAPI* PFNGLVIEWPORT)(int, int, int, int);
static constexpr unsigned GL_COLOR_BUFFER_BIT_ = 0x00004000;

// Map a Win32 virtual key to ogui::KeyCode (subset; enough for the demo).
KeyCode mapKey(WPARAM vk) {
    using K = KeyCode;
    if (vk >= 'A' && vk <= 'Z') return static_cast<K>(K::Key_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return static_cast<K>(K::Key_0 + (vk - '0'));
    switch (vk) {
        case VK_SPACE:   return K::Key_Space;
        case VK_BACK:    return K::Key_Backspace;
        case VK_RETURN:  return K::Key_Enter;
        case VK_TAB:     return K::Key_Tab;
        case VK_ESCAPE:  return K::Key_Escape;
        case VK_DELETE:  return K::Key_Delete;
        case VK_HOME:    return K::Key_Home;
        case VK_END:     return K::Key_End;
        case VK_LEFT:    return K::Key_Left;
        case VK_RIGHT:   return K::Key_Right;
        case VK_UP:      return K::Key_Up;
        case VK_DOWN:    return K::Key_Down;
        case VK_SHIFT:   return K::Key_LSHIFT;
        case VK_CONTROL: return K::Key_LCTRL;
        case VK_MENU:    return K::Key_LALT;
        default:         return K::Key_None;
    }
}

// WM_CHAR reports a UTF-16 code unit (the system already applied the keyboard
// layout). Assemble a full code point, handling surrogate pairs, and hand the
// result to TextInput::onChar as a single codepoint.
struct CharAssembler {
    // Feed one UTF-16 code unit; returns a finished codepoint or 0 if this
    // unit was only half of a pair (or nothing yet).
    uint32_t push(uint16_t cu) {
        if (cu >= 0xD800 && cu <= 0xDBFF) {       // high surrogate
            m_pendingHigh = cu & 0x03FF;
            return 0;
        }
        if (cu >= 0xDC00 && cu <= 0xDFFF) {       // low surrogate
            if (m_pendingHigh == 0xFFFF) return 0; // stray low
            const uint32_t cp = 0x10000 + (static_cast<uint32_t>(m_pendingHigh) << 10)
                                + (cu & 0x03FF);
            m_pendingHigh = 0xFFFF;
            return cp;
        }
        if (m_pendingHigh != 0xFFFF) m_pendingHigh = 0xFFFF; // lone high: drop
        return cu;                                           // BMP / ASCII
    }
private:
    uint16_t m_pendingHigh = 0xFFFF;
};

} // namespace

struct Host::Native {
    HWND hwnd = nullptr;
    HDC dc = nullptr;
    HGLRC rc = nullptr;
    HICON hIcon = nullptr;

    PFNGLCLEAR glClear = nullptr;
    PFNGLCLEARCOLOR glClearColor = nullptr;
    PFNGLVIEWPORT glViewport = nullptr;

    ULONGLONG startTick = 0;
    std::vector<uint32_t> pendingChars;   // full Unicode code points
    CharAssembler chars;

    // Borderless fullscreen support: previous window placement + F11 edge.
    WINDOWPLACEMENT restore{};
    bool prevF11 = false;
    bool m_fullscreen = false;

    std::function<void(const std::vector<std::string>&)> onFilesDropped;

    InputState input;
};

namespace {

Host::Native* g_windowProcSelf = nullptr;   // set during open(), used by wndProc

std::wstring toWide(const std::string& s) {
    std::vector<wchar_t> tmp;
    if (!utf8ToWide(s, tmp)) return L"ogui";
    return std::wstring(tmp.data());
}

// Device pixels per logical unit (96 dpi == 1.0). GetDpiForWindow is Win10
// 1607+, so it is looked up instead of linked; without a window -- or on an
// older system -- the DC reports the system dpi.
float dpiScaleFor(HWND hwnd) {
    using GetDpiFn = UINT(WINAPI*)(HWND);
    static auto getDpiForWindow = reinterpret_cast<GetDpiFn>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow"));
    UINT dpi = (getDpiForWindow && hwnd) ? getDpiForWindow(hwnd) : 0;
    if (!dpi) {
        if (HDC dc = GetDC(hwnd)) {
            dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
            ReleaseDC(hwnd, dc);
        }
    }
    if (!dpi) dpi = 96;
    return static_cast<float>(dpi) / 96.0f;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Host::Native* self = g_windowProcSelf;
    if (!self) return DefWindowProc(hwnd, msg, wp, lp);
    switch (msg) {
        case WM_MOUSEMOVE:
            self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp)));
            self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp)));
            return 0;

        case WM_LBUTTONDOWN: self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))); self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))); self->input.mouseDown[0] = true; self->input.mouseButton = 0; SetCapture(hwnd); return 0;
        case WM_RBUTTONDOWN: self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))); self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))); self->input.mouseDown[1] = true; self->input.mouseButton = 1; SetCapture(hwnd); return 0;
        case WM_MBUTTONDOWN: self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))); self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))); self->input.mouseDown[2] = true; self->input.mouseButton = 2; SetCapture(hwnd); return 0;
        // The second press of a double-click arrives as DBLCLK, not DOWN:
        // treat it as a fresh press so the kernel's click count reaches 2.
        case WM_LBUTTONDBLCLK: self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))); self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))); self->input.mouseDown[0] = true; self->input.mouseButton = 0; SetCapture(hwnd); return 0;
        case WM_RBUTTONDBLCLK: self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))); self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))); self->input.mouseDown[1] = true; self->input.mouseButton = 1; SetCapture(hwnd); return 0;
        case WM_MBUTTONDBLCLK: self->input.mouseX = static_cast<float>(static_cast<short>(LOWORD(lp))); self->input.mouseY = static_cast<float>(static_cast<short>(HIWORD(lp))); self->input.mouseDown[2] = true; self->input.mouseButton = 2; SetCapture(hwnd); return 0;
        case WM_LBUTTONUP:   self->input.mouseDown[0] = false; self->input.mouseButton = -1; ReleaseCapture(); return 0;
        case WM_RBUTTONUP:   self->input.mouseDown[1] = false; self->input.mouseButton = -1; ReleaseCapture(); return 0;
        case WM_MBUTTONUP:   self->input.mouseDown[2] = false; self->input.mouseButton = -1; ReleaseCapture(); return 0;

        case WM_MOUSEWHEEL:
            self->input.scrollY += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) /
                                   static_cast<float>(WHEEL_DELTA);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            KeyCode k = mapKey(wp);
            if (k != Key_None) {
                self->input.keyDown[k] = true;
                // The 30th bit of lParam marks an auto-repeat in Win32; the
                // kernel re-sends onKeyDown each frame for held keys.
                self->input.keyRepeat[k] = (lp & 0x40000000) != 0;
            }
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            KeyCode k = mapKey(wp);
            if (k != Key_None) {
                self->input.keyDown[k] = false;
                self->input.keyRepeat[k] = false;
            }
            return 0;
        }

        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::string> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::vector<wchar_t> buf(len + 1);
                DragQueryFileW(drop, i, buf.data(), len + 1);
                files.push_back(wideToUtf8(buf.data()));
            }
            DragFinish(drop);
            if (self->onFilesDropped) self->onFilesDropped(files);
            return 0;
        }

        case WM_DPICHANGED: {
            // Per-monitor DPI aware: move/resize into the suggested RECT; the
            // run loop picks the new client size up on its own.
            const RECT* sugg = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, sugg->left, sugg->top,
                         sugg->right - sugg->left, sugg->bottom - sugg->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }

        case WM_CHAR:
            // wParam is a UTF-16 code unit when the window uses W. Assemble
            // surrogate pairs and queue only finished code points; control
            // characters never leave the proc. Navigation keys already arrive
            // as WM_KEYDOWN and are handled by Kernel::onKeyDown.
            if (wp >= 32 && wp != 127) {
                const uint32_t cp = self->chars.push(static_cast<uint16_t>(wp));
                if (cp != 0) self->pendingChars.push_back(cp);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;   // we repaint via GL

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// WGL entry points (resolved at runtime; the library never links GL).
using WglCreateCtx = HGLRC(WINAPI*)(HDC);
using WglMakeCurrent = BOOL(WINAPI*)(HDC, HGLRC);
using WglDeleteCtx = BOOL(WINAPI*)(HGLRC);
using WglSwapInt = BOOL(WINAPI*)(UINT);

} // namespace

Host::Host(Settings settings) : m_settings(std::move(settings)) {}

Host::~Host() {
    close();
}

bool Host::open() {
    if (m_native) return true;   // already open

    // ---- DPI awareness -----------------------------------------------------
    // Ask for per-monitor awareness first (shcore.dll, Win 8.1+); fall back to
    // system DPI awareness. Keeps coordinates aligned to physical pixels on
    // HiDPI displays. Must run before any window is created.
    if (HMODULE shcore = LoadLibraryA("shcore.dll")) {
        typedef HRESULT(WINAPI* SetDpiAwareness)(int);
        if (auto setDpi = reinterpret_cast<SetDpiAwareness>(
                GetProcAddress(shcore, "SetProcessDpiAwareness")))
            setDpi(2);   // PROCESS_PER_MONITOR_DPI_AWARE
        FreeLibrary(shcore);
    } else {
        SetProcessDPIAware();   // system DPI aware fallback
    }

    // ---- window ------------------------------------------------------------
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "ogui_window_v1";
    RegisterClassExA(&wc);

    RECT want{0, 0, m_settings.width, m_settings.height};
    AdjustWindowRect(&want, WS_OVERLAPPEDWINDOW, FALSE);
    std::wstring title = toWide(m_settings.title);
    HWND hwnd = CreateWindowExW(
        0, L"ogui_window_v1", title.c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        want.right - want.left, want.bottom - want.top,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        m_error = "CreateWindowEx failed";
        return false;
    }

    if (!m_native) m_native = new Native();
    m_native->hwnd = hwnd;
    g_windowProcSelf = m_native;

    // ---- window icon / drag & drop -------------------------------------------
    if (!m_settings.iconPath.empty()) {
        std::vector<wchar_t> iconPath;
        if (utf8ToWide(m_settings.iconPath, iconPath)) {
            if (HICON icon = static_cast<HICON>(LoadImageW(
                    nullptr, iconPath.data(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE))) {
                SetClassLongPtrW(hwnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(icon));
                m_native->hIcon = icon;
            }
        }
    }
    DragAcceptFiles(hwnd, TRUE);

    // ---- system clipboard ----------------------------------------------------
    ogui::Clipboard::instance().setHandlers(
        [hwnd]() -> std::string {
            std::string out;
            if (!OpenClipboard(hwnd)) return out;
            if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
                if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
                    out = wideToUtf8(w);
                    GlobalUnlock(h);
                }
            }
            CloseClipboard();
            return out;
        },
        [hwnd](const std::string& text) {
            std::vector<wchar_t> buf;
            if (!utf8ToWide(text, buf)) return;
            if (!OpenClipboard(hwnd)) return;
            EmptyClipboard();
            if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, buf.size() * sizeof(wchar_t))) {
                if (void* p = GlobalLock(mem)) {
                    std::memcpy(p, buf.data(), buf.size() * sizeof(wchar_t));
                    GlobalUnlock(mem);
                    SetClipboardData(CF_UNICODETEXT, mem);
                } else {
                    GlobalFree(mem);
                }
            }
            CloseClipboard();
        });

    // ---- GL context ---------------------------------------------------------
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) {
        m_error = "SetPixelFormat failed";
        close();
        return false;
    }

    HMODULE gl32 = GetModuleHandleA("opengl32.dll");
    auto proc = [gl32](const char* n) -> void* {
        return reinterpret_cast<void*>(GetProcAddress(gl32, n));
    };
    auto* wglCreateCtx = reinterpret_cast<WglCreateCtx>(proc("wglCreateContext"));
    auto* wglMakeCur = reinterpret_cast<WglMakeCurrent>(proc("wglMakeCurrent"));
    auto* wglDelCtx = reinterpret_cast<WglDeleteCtx>(proc("wglDeleteContext"));
    auto* wglSwap = reinterpret_cast<WglSwapInt>(proc("wglSwapIntervalEXT"));

    HGLRC rc = wglCreateCtx ? wglCreateCtx(dc) : nullptr;
    if (!rc || (wglMakeCur && !wglMakeCur(dc, rc))) {
        m_error = "Failed to create the OpenGL context";
        close();
        return false;
    }

    m_native->dc = dc;
    m_native->rc = rc;
    if (m_settings.vsync && wglSwap) wglSwap(1);

    // GL 1.1 entry points live in opengl32.dll itself.
    m_native->glClear = reinterpret_cast<PFNGLCLEAR>(
        reinterpret_cast<void*>(GetProcAddress(gl32, "glClear")));
    m_native->glClearColor = reinterpret_cast<PFNGLCLEARCOLOR>(
        reinterpret_cast<void*>(GetProcAddress(gl32, "glClearColor")));
    m_native->glViewport = reinterpret_cast<PFNGLVIEWPORT>(
        reinterpret_cast<void*>(GetProcAddress(gl32, "glViewport")));
    if (!m_native->glClear || !m_native->glClearColor || !m_native->glViewport) {
        m_error = "Failed to resolve glClear/glViewport";
        close();
        return false;
    }

    // ---- ogui GL backend -----------------------------------------------------
    if (!m_backend.init()) {
        m_error = m_backend.lastError();
        close();
        return false;
    }

    m_error.clear();
    m_native->startTick = GetTickCount64();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (m_settings.fullscreen) toggleFullscreen();
    return true;
}

void Host::close() {
    if (!m_native) return;
    if (m_native->hwnd) {
        if (m_native->rc) {
            // Release the context before destroying the DC/window.
            HMODULE gl32 = GetModuleHandleA("opengl32.dll");
            auto proc = [gl32](const char* n) -> void* {
                return reinterpret_cast<void*>(GetProcAddress(gl32, n));
            };
            auto* wglMakeCur = reinterpret_cast<WglMakeCurrent>(proc("wglMakeCurrent"));
            auto* wglDelCtx = reinterpret_cast<WglDeleteCtx>(proc("wglDeleteContext"));
            if (wglMakeCur) wglMakeCur(nullptr, nullptr);
            if (wglDelCtx) wglDelCtx(m_native->rc);
        }
        if (m_native->dc) ReleaseDC(m_native->hwnd, m_native->dc);
        if (m_native->hIcon) DestroyIcon(m_native->hIcon);
        DestroyWindow(m_native->hwnd);
    }
    m_backend.shutdown();
    g_windowProcSelf = nullptr;
    delete m_native;
    m_native = nullptr;
}

float Host::systemDpiScale() { return dpiScaleFor(nullptr); }

float Host::dpiScale() const { return dpiScaleFor(m_native ? m_native->hwnd : nullptr); }

void Host::setClearColor(float r, float g, float b) {
    m_clearR = r; m_clearG = g; m_clearB = b;
}

void Host::setBeforeFrame(std::function<void(Kernel&, InputState&)> fn) {
    m_beforeFrame = std::move(fn);
}

void Host::setAfterFrame(std::function<void(Kernel&, const DrawList&)> fn) {
    m_afterFrame = std::move(fn);
}

void Host::toggleFullscreen() {
    if (!m_native || !m_native->hwnd) return;
    HWND hwnd = m_native->hwnd;
    if (!m_native->m_fullscreen) {
        // Save the current windowed placement, then cover the monitor.
        m_native->restore.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &m_native->restore);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            SetWindowLongPtrW(hwnd, GWL_STYLE, (LONG_PTR)WS_POPUP);
            // Normal Z-order (HWND_TOP, not TOPMOST): a topmost fullscreen
            // would block taskbar apps from ever coming above us.
            SetWindowPos(hwnd, HWND_TOP,
                         mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE);
            m_native->m_fullscreen = true;
        }
    } else {
        // The window never sits in the topmost band, so no z-order change is
        // needed here -- just recompute the frame after the style swap.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowLongPtrW(hwnd, GWL_STYLE, (LONG_PTR)WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &m_native->restore);
        SetWindowPos(hwnd, nullptr,
                     m_native->restore.rcNormalPosition.left,
                     m_native->restore.rcNormalPosition.top,
                     m_native->restore.rcNormalPosition.right -
                         m_native->restore.rcNormalPosition.left,
                     m_native->restore.rcNormalPosition.bottom -
                         m_native->restore.rcNormalPosition.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER);
        m_native->m_fullscreen = false;
    }
}

void Host::setOnFilesDropped(std::function<void(const std::vector<std::string>&)> fn) {
    if (m_native) m_native->onFilesDropped = std::move(fn);
}

void Host::run(Kernel& kernel) {
    if (!m_native || !m_native->hwnd) return;

    Native& n = *m_native;
    MSG msg{};
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            // Feed button edges to the kernel right away. Draining the whole
            // queue first lets queued moves overwrite the pointer position
            // before the press is seen, so drag-select started several
            // characters late, at wherever the mouse had reached.
            switch (msg.message) {
                case WM_LBUTTONDOWN:  case WM_LBUTTONUP:   case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN:  case WM_RBUTTONUP:   case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN:  case WM_MBUTTONUP:   case WM_MBUTTONDBLCLK:
                    kernel.frame(n.input);
                    break;
                default:
                    break;
            }
        }
        if (!running || kernel.quitRequested()) break;

        // Size changes: poll the client rect every frame instead of trusting
        // WM_SIZE. Maximize, snap-tiling and DPI transitions can coalesce or
        // reorder size messages, so the flag-based path went stale.
        {
            RECT rc{};
            if (GetClientRect(n.hwnd, &rc)) {
                const int cw = rc.right - rc.left;
                const int ch = rc.bottom - rc.top;
                if (auto screen = kernel.currentScreen()) {
                    const float sc = dpiScaleFor(n.hwnd);
                    if (screen->width != static_cast<float>(cw) ||
                        screen->height != static_cast<float>(ch) ||
                        screen->scale != sc) {
                        screen->width = static_cast<float>(cw);
                        screen->height = static_cast<float>(ch);
                        screen->scale = sc;
                    }
                }
                if (n.glViewport) n.glViewport(0, 0, cw, ch);
            }
        }

        // Flush one frame of text events into the input state.
        if (!n.pendingChars.empty()) {
            n.input.lastChar = n.pendingChars.front();
            n.pendingChars.erase(n.pendingChars.begin());
        } else {
            n.input.lastChar = 0;
        }

        // The tooltip delay runs off this clock (hover a widget ~0.5s).
        n.input.time = static_cast<float>(GetTickCount64() - n.startTick) / 1000.0f;

        // F11 support removed: fullscreen toggle disabled (causes crashes in some cases)
        // const bool f11 = n.input.keyDown[Key_F11];
        // if (f11 && !n.prevF11) toggleFullscreen();
        // n.prevF11 = f11;

        if (m_beforeFrame) m_beforeFrame(kernel, n.input);

        kernel.processInput(n.input);
        kernel.frame(n.input);

        // Clear + draw.
        const DrawList& dl = kernel.drawList();
        RECT rcClient;
        GetClientRect(n.hwnd, &rcClient);
        int fbw = rcClient.right - rcClient.left;
        int fbh = rcClient.bottom - rcClient.top;
        if (n.glViewport) n.glViewport(0, 0, fbw, fbh);
        if (n.glClearColor) n.glClearColor(m_clearR, m_clearG, m_clearB, 1.0f);
        if (n.glClear) n.glClear(GL_COLOR_BUFFER_BIT_);
        m_backend.render(dl, fbw, fbh, /*flipY=*/false);

        SwapBuffers(n.dc);

        if (m_afterFrame) m_afterFrame(kernel, dl);

        // Wheel deltas are per-message; consumed by the kernel this frame.
        n.input.scrollX = 0.0f;
        n.input.scrollY = 0.0f;

        Sleep(1);
    }
}

} // namespace ogui

#else // !_WIN32

namespace ogui {

struct Host::Native {
    // No UI on other platforms; the type exists so the member stays valid
    // even though open() always reports failure here.
};

Host::Host(Settings settings) : m_settings(std::move(settings)) {}

Host::~Host() {
    close();
}

bool Host::open() {
    if (m_native) return true;
    m_error = "ogui::Host is implemented for Win32/WGL only on this build; "
              "use Kernel + GlBackend with your own host instead.";
    return false;
}

float Host::systemDpiScale() { return 1.0f; }

float Host::dpiScale() const { return 1.0f; }

void Host::close() {
    delete m_native;
    m_native = nullptr;
}

void Host::setClearColor(float r, float g, float b) {
    m_clearR = r; m_clearG = g; m_clearB = b;
}

void Host::setBeforeFrame(std::function<void(Kernel&, InputState&)> fn) {
    m_beforeFrame = std::move(fn);
}

void Host::setAfterFrame(std::function<void(Kernel&, const DrawList&)> fn) {
    m_afterFrame = std::move(fn);
}

void Host::toggleFullscreen() {
    // Nothing to toggle without a native window.
}

void Host::setOnFilesDropped(std::function<void(const std::vector<std::string>&)> fn) {
    (void)fn;
}

void Host::run(Kernel& kernel) {
    (void)kernel;   // nothing to run without a native window
}

} // namespace ogui

#endif // _WIN32