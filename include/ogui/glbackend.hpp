#pragma once
#include "ogui/api.hpp"
#include "ogui/drawlist.hpp"
#include "ogui/font.hpp"
#include "ogui/scene3d.hpp"
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
    //
    // The 2D pass always disables depth testing: the UI is painted in order
    // and must sit on top of whatever 3D pass ran before it.
    void render(const DrawList& list, int fbWidth, int fbHeight, bool flipY = false);

    // ---- 3D -----------------------------------------------------------
    // Uploads a mesh and returns its handle (>0), or 0 when the data is empty
    // or the backend is not initialised. Handles stay valid until
    // releaseMesh()/shutdown().
    int uploadMesh(const MeshData& mesh);
    void releaseMesh(int mesh);

    // Draws a scene with depth testing and back-face culling into the
    // currently bound framebuffer. Nothing is cleared: the host owns that
    // buffer, and keeping its depth is what lets a game draw its own world
    // before the UI's 3D layer. Saves and restores the GL state it touches
    // (depth test/func/mask, cull/front-face, blend, scissor, program, VAO,
    // buffers, texture bindings, viewport).
    //
    // Items whose world-space bounds are outside the camera frustum are
    // skipped on the CPU, before any draw call.
    void renderScene(const Scene3D& scene, int fbWidth, int fbHeight);

    // What the last 3D pass did: items submitted vs items skipped by the
    // frustum cull. Written by renderScene() and renderSceneToTarget().
    struct SceneStats {
        int drawn = 0;
        int culled = 0;
    };
    const SceneStats& sceneStats() const { return m_sceneStats; }

    // Offscreen colour+depth target owned by the backend, for SceneView (and
    // for any host texture that a DrawCmd should sample). create returns a
    // handle (>0) or 0; the colour texture is exposed as a stable DrawList
    // texture slot via sceneTargetSlot(), which survives resizeSceneTarget().
    int createSceneTarget(int width, int height);
    // False when the handle is unknown or the new framebuffer is incomplete
    // (the target is then gone; createSceneTarget() allocates a fresh one).
    bool resizeSceneTarget(int handle, int width, int height);
    void destroySceneTarget(int handle);
    int sceneTargetSlot(int handle) const;

    // Renders `scene` into the target: clears it with scene.background (colour
    // only when its alpha is non-zero) plus depth, then draws with the same
    // shader renderScene() uses. The host's bound framebuffer and viewport are
    // restored before returning.
    bool renderSceneToTarget(const Scene3D& scene, int handle);

    // Registers a host-owned GL texture under a DrawList slot (a SceneView
    // looks like any other textured quad). The backend does not own it:
    // unregisterTexture() drops the mapping and never deletes the texture.
    int registerTexture(unsigned glTexture);
    void unregisterTexture(int slot);

    // Releases every GL object this backend created (pipeline, buffers and
    // the textures handed out by createTexture()/ensureFontTexture()).
    // Must run while the context is still current; the destructor calls it,
    // so destroy the backend before destroying the context.
    void shutdown();

private:
    // A mesh on the GPU. One VAO per mesh keeps the 3D pass from re-pointing
    // vertex attributes every draw.
    struct Mesh3D {
        unsigned vao = 0, vbo = 0, ebo = 0;
        int indexCount = 0;
        Vec3 min, max;   // object-space bounds, culled after the model matrix
    };
    // An offscreen colour+depth target, as created by createSceneTarget().
    struct Target3D {
        unsigned fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
        int slot = 0;
    };

    bool loadFunctions(GetProcFn getProc);
    bool createPipeline();
    // Compiles + links `vsBody`/`fsBody` in the context's dialect; 0 on
    // failure, with lastError() set (2D and 3D share the error paths).
    unsigned buildProgram(const char* vsBody, const char* fsBody, const char* what);
    // Lazily builds the 3D pipeline; false when the context lacks the entries
    // the 3D pass needs (they are optional so init() stays a 2D-only contract).
    bool ensure3DProgram();
    // Shared body of renderScene()/renderSceneToTarget(): the caller sets the
    // viewport and does any clearing.
    void drawScene(const Scene3D& scene, float aspect);
    // texId -> GL name, following the DrawCmd convention (0 = white, negative
    // = font atlas, otherwise host slot, image slot or raw GL name).
    unsigned resolveTexture(int texId) const;
    // Colour texture + depth renderbuffer attached to a fresh FBO.
    bool allocTarget(Target3D& t);
    void freeTarget(Target3D& t);
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

    // ---- 3D ----
    bool m_3dReady = false;
    unsigned m_3dProgram = 0;
    int m_u3dViewProj = -1, m_u3dModel = -1;
    int m_u3dLightDir = -1, m_u3dLightColor = -1, m_u3dAmbient = -1;
    int m_u3dTint = -1, m_u3dTex = -1, m_u3dUseTex = -1;

    std::unordered_map<int, Mesh3D> m_meshes;
    std::unordered_map<int, Target3D> m_targets;
    SceneStats m_sceneStats;
    // Host texture slots are handed out well above ImageManager's ids so the
    // two namespaces cannot collide by accident.
    std::unordered_map<int, unsigned> m_hostTexs;
    int m_nextHostSlot = 1000000;
    int m_nextMesh = 1;
    int m_nextTarget = 1;
};

} // namespace ogui
