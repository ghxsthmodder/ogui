#pragma once
#include "ogui/kernel.hpp"
#include "ogui/glbackend.hpp"
#include <functional>
#include <string>
#include <vector>

namespace ogui {

// ------------------------------------------------------------------
// Host: a native application window owned by ogui.
//
// ogui normally leaves the window + GL context to the host (see GlBackend).
// Host is the opposite arrangement for simple desktop programs: here the
// library creates the OS window, makes an OpenGL 3.3 context current, pumps
// the message loop into an InputState and renders the Kernel's DrawList
// every frame. System clipboard integration (CF_UNICODETEXT on Windows) is
// installed automatically.
//
// Current implementation: Win32 + WGL. On other platforms open() fails
// cleanly with a message in lastError() -- custom hosts still use the
// Kernel + GlBackend directly, which is fully portable.
// ------------------------------------------------------------------
class Host {
public:
    struct Settings {
        std::string title = "ogui";
        int width = 800;
        int height = 600;
        bool vsync = true;
        bool fullscreen = false;      // start in fullscreen (borderless)
        std::string iconPath;         // .ico file loaded as the window icon
    };

    Host() : Host(Settings{}) {}
    explicit Host(Settings settings);
    ~Host();

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Creates the native window + GL context. Returns false and fills
    // lastError() on failure.
    bool open();
    void close();                       // idempotent; ~Host() calls it

    bool isOpen() const { return m_native != nullptr; }
    const std::string& lastError() const { return m_error; }

    // Runs the loop until the window is closed or kernel.requestQuit() is
    // called. Each frame:
    //   1. pump OS messages into an InputState
    //   2. apply window resizes to the current Screen's width/height
    //   3. deliver one pending text character to InputState::lastChar
    //   4. call beforeFrame(kernel, input) if set
    //   5. kernel.frame(input) (input + layout + draw list)
    //   6. clear + render the draw list, present
    //   7. call afterFrame(kernel, drawList) if set
    void run(Kernel& kernel);

    // Optional per-frame hooks.
    void setBeforeFrame(std::function<void(Kernel&, InputState&)> fn);
    void setAfterFrame(std::function<void(Kernel&, const DrawList&)> fn);

    // Toggles borderless fullscreen (F11 inside run()). The previous window
    // position/size is restored when leaving fullscreen.
    void toggleFullscreen();

    // Called when a set of files is dropped onto the window (Win32 drag & drop,
    // UTF-8 paths, no-op on other platforms).
    void setOnFilesDropped(std::function<void(const std::vector<std::string>&)> fn);

    void setClearColor(float r, float g, float b);

public:
    struct Native;
    Settings m_settings;
    std::string m_error;
    std::function<void(Kernel&, InputState&)> m_beforeFrame;
    std::function<void(Kernel&, const DrawList&)> m_afterFrame;
    GlBackend m_backend;
    Native* m_native = nullptr;
    float m_clearR = 0.06f, m_clearG = 0.06f, m_clearB = 0.07f;
};

} // namespace ogui