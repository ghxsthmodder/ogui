#pragma once
#include "ogui/api.hpp"
#include "ogui/drawlist.hpp"
#include "ogui/font.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace ogui {

// ------------------------------------------------------------------
// GlBackend: renders a DrawList with OpenGL 3.3 core or OpenGL ES 3.0.
//
// The library never links against GL: every entry point is resolved at
// runtime through a host-supplied getter (wglGetProcAddress, glXGetProcAddress,
// eglGetProcAddress, SDL_GL_GetProcAddress, glfwGetProcAddress, ...). Call
// init() once, after the host has made its GL context current.
// ------------------------------------------------------------------
class GlBackend {
public:
    using GetProcFn = void* (*)(const char* name);

    // Which shader dialect the pipeline compiles for. The host created the
    // context and always knows; Auto reads the reported GLSL version, which
    // carries "ES" on every OpenGL ES implementation.
    enum class Profile { Auto, DesktopGL, OpenGLES };

    GlBackend() = default;
    ~GlBackend();

    GlBackend(const GlBackend&) = delete;
    GlBackend& operator=(const GlBackend&) = delete;

    // Resolves GL entry points, picks the shader dialect and builds the
    // pipeline.
    // getProc may be null (a platform default is used: opengl32 on Windows,
    // the OpenGL framework on macOS, libGL on X11, libGLESv2 plus
    // eglGetProcAddress on Android).
    // Returns false and fills lastError() on failure.
    bool init(GetProcFn getProc = nullptr, Profile profile = Profile::Auto);
    bool ready() const { return m_ready; }
    // True once init() has settled on an OpenGL ES context.
    bool isGLES() const { return m_es; }
    const std::string& lastError() const { return m_error; }

    // Uploads an RGBA8 image; returns a texture id (>0) or 0 on failure.
    int createTexture(const uint8_t* rgba, int w, int h);
    int createTextureA8(const uint8_t* alpha, int w, int h);

    // Makes sure the font's atlas lives on the GPU. The font keeps a stable
    // (negative) slot id; this backend resolves it to a GL texture.
    bool ensureFontTexture(Font& font);

    // Uploads every atlas cached by FontManager that has no texture yet and
    // retires textures whose font went away. render() calls this every frame,
    // so hosts normally never have to.
    bool syncFonts();

    // Uploads every image cached by ImageManager that has no texture yet, so
    // their positive slot ids resolve during render(). Like syncFonts(), this
    // runs from render() every frame; hosts normally never call it.
    bool syncImages();

    // Draws the list into the currently bound framebuffer.
    // flipY: set when the projection origin must be bottom-left.
    // Saves and restores the GL state it touches (blend, scissor, depth,
    // cull, stencil, program, VAO, buffer bindings, texture bindings).
    void render(const DrawList& list, int fbWidth, int fbHeight, bool flipY = false);

    // Releases every GL object this backend created (pipeline, buffers and
    // the textures handed out by createTexture()/ensureFontTexture()).
    // Must run while the context is still current; the destructor calls it,
    // so destroy the backend before destroying the context.
    void shutdown();

private:
    bool loadFunctions(GetProcFn getProc);
    bool createPipeline();
    // glGetString, "" when the entry points are not resolved yet.
    std::string queryString(unsigned name) const;
    // "OpenGL ES 3.0 (GLSL 3.00 ES)" style label, for error messages.
    std::string describe() const;

    std::string m_error;
    bool m_ready = false;
    bool m_es = false;

    unsigned m_program = 0;
    unsigned m_vao = 0;
    unsigned m_vbo = 0;
    unsigned m_ebo = 0;
    size_t m_vboCap = 0;   // allocated bytes; grow-only, refilled with SubData
    size_t m_eboCap = 0;
    unsigned m_whiteTex = 0;
    int m_uProj = -1;
    int m_uTex = -1;

    // Resolved entry points (opaque so the header stays GL-free).
    struct Api;
    Api* m_api = nullptr;

    // Font atlas slot (Font::atlasTexture(), negative) -> GL texture.
    std::unordered_map<int, unsigned> m_fontTexs;
    // ... and the atlas revision that is currently on the GPU. Glyphs bake on
    // demand, so a texture is re-uploaded when the two differ.
    std::unordered_map<int, unsigned> m_fontTexRev;

    // Image slot (ImageManager id, positive) -> GL texture.
    std::unordered_map<int, unsigned> m_imageTexs;
};

} // namespace ogui
