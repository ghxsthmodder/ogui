// OpenGL 3.3 core backend. All entry points are resolved at runtime through
// the host's GetProc; this file never includes <GL/gl.h> and never links
// against libGL/opengl32, so the library stays dependency-free.
#include "ogui/glbackend.hpp"
#include "ogui/imagemanager.hpp"

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  static HMODULE s_opengl32 = nullptr;
#elif defined(__APPLE__)
  #include <dlfcn.h>
#else
  #include <dlfcn.h>
#endif

namespace ogui {

// ---- minimal GL typedefs (avoid depending on system GL headers) -----------
using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

namespace {
constexpr GLenum GL_BLEND              = 0x00BE2;
constexpr GLenum GL_SCISSOR_TEST       = 0x0C11;
constexpr GLenum GL_SCISSOR_BOX        = 0x0C10;
constexpr GLenum GL_TEXTURE_2D         = 0x0DE1;
constexpr GLenum GL_TEXTURE0           = 0x84C0;
constexpr GLenum GL_UNSIGNED_BYTE      = 0x1401;
constexpr GLenum GL_FLOAT              = 0x1406;
constexpr GLenum GL_RGBA               = 0x1908;
constexpr GLenum GL_TRIANGLES          = 0x0004;
constexpr GLenum GL_UNSIGNED_INT       = 0x1405;
constexpr GLenum GL_ARRAY_BUFFER       = 0x8892;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum GL_STREAM_DRAW        = 0x88E0;
constexpr GLenum GL_FRAGMENT_SHADER    = 0x8B30;
constexpr GLenum GL_VERTEX_SHADER      = 0x8B31;
constexpr GLenum GL_COMPILE_STATUS     = 0x8B81;
constexpr GLenum GL_LINK_STATUS        = 0x8B82;
constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
constexpr GLenum GL_TEXTURE_WRAP_S     = 0x2802;
constexpr GLenum GL_TEXTURE_WRAP_T     = 0x2803;
constexpr GLenum GL_LINEAR             = 0x2601;
constexpr GLenum GL_CLAMP_TO_EDGE      = 0x812F;
constexpr GLenum GL_ONE                = 1;
constexpr GLenum GL_R8                 = 0x8229;
constexpr GLenum GL_RED                = 0x1903;
constexpr GLenum GL_UNPACK_ALIGNMENT   = 0x0CF5;
constexpr GLenum GL_TEXTURE_SWIZZLE_RGBA = 0x8E46;
constexpr GLenum GL_SRC_ALPHA          = 0x0302;
constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLboolean GL_FALSE_          = 0;
// state queries (render() saves and restores what it touches)
constexpr GLenum GL_DEPTH_TEST         = 0x0B71;
constexpr GLenum GL_CULL_FACE          = 0x0B44;
constexpr GLenum GL_STENCIL_TEST       = 0x0B90;
constexpr GLenum GL_VIEWPORT           = 0x0BA2;
constexpr GLenum GL_CURRENT_PROGRAM    = 0x8B8D;
constexpr GLenum GL_VERTEX_ARRAY_BINDING = 0x85B5;
constexpr GLenum GL_ACTIVE_TEXTURE     = 0x84E0;
constexpr GLenum GL_TEXTURE_BINDING_2D = 0x8069;
constexpr GLenum GL_ARRAY_BUFFER_BINDING = 0x8894;
constexpr GLenum GL_ELEMENT_ARRAY_BUFFER_BINDING = 0x8895;
constexpr GLenum GL_BLEND_SRC_RGB      = 0x80C9;
constexpr GLenum GL_BLEND_DST_RGB      = 0x80C8;
constexpr GLenum GL_BLEND_SRC_ALPHA    = 0x80CB;
constexpr GLenum GL_BLEND_DST_ALPHA    = 0x80CA;
} // namespace

// ---- function pointer table ------------------------------------------------
struct GlBackend::Api {
    // state
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*BlendFunc)(GLenum, GLenum) = nullptr;
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    // state query / restore
    GLboolean (*IsEnabled)(GLenum) = nullptr;
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    // buffers
    void (*GenVertexArrays)(GLsizei, GLuint*) = nullptr;
    void (*BindVertexArray)(GLuint) = nullptr;
    void (*GenBuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindBuffer)(GLenum, GLuint) = nullptr;
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
    void (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
    void (*DeleteBuffers)(GLsizei, const GLuint*) = nullptr;
    void (*DeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
    // attributes
    void (*EnableVertexAttribArray)(GLuint) = nullptr;
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
    // shaders
    GLuint (*CreateShader)(GLenum) = nullptr;
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
    void (*CompileShader)(GLuint) = nullptr;
    void (*GetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*DeleteShader)(GLuint) = nullptr;
    GLuint (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint, GLuint) = nullptr;
    void (*LinkProgram)(GLuint) = nullptr;
    void (*GetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
    void (*UseProgram)(GLuint) = nullptr;
    void (*DeleteProgram)(GLuint) = nullptr;
    GLint (*GetUniformLocation)(GLuint, const GLchar*) = nullptr;
    void (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;
    void (*Uniform1i)(GLint, GLint) = nullptr;
    // textures
    void (*GenTextures)(GLsizei, GLuint*) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    void (*TexParameteriv)(GLenum, GLenum, const GLint*) = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    // draw
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
};

namespace {

void* defaultGetProc(const char* name) {
#if defined(_WIN32)
    if (!s_opengl32) {
        s_opengl32 = GetModuleHandleA("opengl32.dll");
        // Nothing links opengl32 here, so make sure it is loaded before we
        // start asking it for exports.
        if (!s_opengl32) s_opengl32 = LoadLibraryA("opengl32.dll");
    }
    if (!s_opengl32) return nullptr;
    // Core 1.1 entry points must come from opengl32.dll, not wglGetProcAddress.
    if (void* p = reinterpret_cast<void*>(GetProcAddress(s_opengl32, name))) return p;
    using WglGetProc = void* (WINAPI*)(LPCSTR);
    // FARPROC -> void* -> target: an intermediate object-pointer cast keeps
    // -Wcast-function-type quiet without a memcpy dance.
    static WglGetProc wglGetProc = reinterpret_cast<WglGetProc>(
        reinterpret_cast<void*>(GetProcAddress(s_opengl32, "wglGetProcAddress")));
    if (!wglGetProc) return nullptr;
    void* p = wglGetProc(name);
    // wglGetProcAddress returns small error values, not nullptr.
    auto v = reinterpret_cast<std::intptr_t>(p);
    if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) return nullptr;
    return p;
#elif defined(__APPLE__)
    static void* lib = dlopen(
        "/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY | RTLD_LOCAL);
    return lib ? dlsym(lib, name) : nullptr;
#else
    using GetProcArb = void* (*)(const char*);
    static void* lib = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) lib = dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) return nullptr;
    static GetProcArb getProcArb = reinterpret_cast<GetProcArb>(
        dlsym(lib, "glXGetProcAddressARB"));
    if (getProcArb) {
        if (void* p = getProcArb(name)) return p;
    }
    return dlsym(lib, name);
#endif
}

template <typename T>
bool load(T& fn, void* (*getProc)(const char*), const char* name) {
    fn = reinterpret_cast<T>(getProc(name));
    return fn != nullptr;
}

} // namespace

// ---- lifecycle ------------------------------------------------------------
GlBackend::~GlBackend() {
    shutdown();
    delete m_api;
    m_api = nullptr;
}

bool GlBackend::loadFunctions(GetProcFn getProc) {
    auto gp = getProc ? getProc : &defaultGetProc;
    if (!m_api) m_api = new Api();
    Api& a = *m_api;

    std::string missing;
    auto need = [&](auto& fn, const char* name) {
        if (!load(fn, gp, name)) {
            if (!missing.empty()) missing += ", ";
            missing += name;
        }
    };

    // state
    need(a.Enable, "glEnable");
    need(a.Disable, "glDisable");
    need(a.BlendFunc, "glBlendFunc");
    need(a.BlendFuncSeparate, "glBlendFuncSeparate");
    need(a.Scissor, "glScissor");
    need(a.Viewport, "glViewport");
    need(a.IsEnabled, "glIsEnabled");
    need(a.GetIntegerv, "glGetIntegerv");
    // buffers
    need(a.GenVertexArrays, "glGenVertexArrays");
    need(a.BindVertexArray, "glBindVertexArray");
    need(a.GenBuffers, "glGenBuffers");
    need(a.BindBuffer, "glBindBuffer");
    need(a.BufferData, "glBufferData");
    need(a.BufferSubData, "glBufferSubData");
    need(a.DeleteBuffers, "glDeleteBuffers");
    need(a.DeleteVertexArrays, "glDeleteVertexArrays");
    // attributes
    need(a.EnableVertexAttribArray, "glEnableVertexAttribArray");
    need(a.VertexAttribPointer, "glVertexAttribPointer");
    // shaders
    need(a.CreateShader, "glCreateShader");
    need(a.ShaderSource, "glShaderSource");
    need(a.CompileShader, "glCompileShader");
    need(a.GetShaderiv, "glGetShaderiv");
    need(a.GetShaderInfoLog, "glGetShaderInfoLog");
    need(a.DeleteShader, "glDeleteShader");
    need(a.CreateProgram, "glCreateProgram");
    need(a.AttachShader, "glAttachShader");
    need(a.LinkProgram, "glLinkProgram");
    need(a.GetProgramiv, "glGetProgramiv");
    need(a.GetProgramInfoLog, "glGetProgramInfoLog");
    need(a.UseProgram, "glUseProgram");
    need(a.DeleteProgram, "glDeleteProgram");
    need(a.GetUniformLocation, "glGetUniformLocation");
    need(a.UniformMatrix4fv, "glUniformMatrix4fv");
    need(a.Uniform1i, "glUniform1i");
    // textures
    need(a.GenTextures, "glGenTextures");
    need(a.BindTexture, "glBindTexture");
    need(a.TexImage2D, "glTexImage2D");
    need(a.TexParameteri, "glTexParameteri");
    need(a.TexParameteriv, "glTexParameteriv");
    need(a.PixelStorei, "glPixelStorei");
    need(a.DeleteTextures, "glDeleteTextures");
    need(a.ActiveTexture, "glActiveTexture");
    // draw
    need(a.DrawElements, "glDrawElements");

    if (!missing.empty()) {
        m_error = "missing GL entry points: " + missing;
        return false;
    }
    return true;
}

bool GlBackend::createPipeline() {
    Api& a = *m_api;

    static const char* VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
})";

    static const char* FS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV) * vColor;
})";

    auto compile = [&](GLenum type, const char* src) -> GLuint {
        GLuint sh = a.CreateShader(type);
        a.ShaderSource(sh, 1, &src, nullptr);
        a.CompileShader(sh);
        GLint ok = 0;
        a.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            GLsizei n = 0;
            a.GetShaderInfoLog(sh, sizeof(log), &n, log);
            m_error = std::string("shader compile failed: ") + log;
            a.DeleteShader(sh);
            return 0;
        }
        return sh;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, VS);
    if (!vs) return false;
    GLuint fs = compile(GL_FRAGMENT_SHADER, FS);
    if (!fs) { a.DeleteShader(vs); return false; }

    m_program = a.CreateProgram();
    a.AttachShader(m_program, vs);
    a.AttachShader(m_program, fs);
    a.LinkProgram(m_program);
    a.DeleteShader(vs);
    a.DeleteShader(fs);

    GLint ok = 0;
    a.GetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        GLsizei n = 0;
        a.GetProgramInfoLog(m_program, sizeof(log), &n, log);
        m_error = std::string("program link failed: ") + log;
        return false;
    }

    m_uProj = a.GetUniformLocation(m_program, "uProj");
    m_uTex = a.GetUniformLocation(m_program, "uTex");
    if (m_uProj < 0 || m_uTex < 0) {
        m_error = "shader uniforms missing (uProj/uTex)";
        return false;
    }

    a.GenVertexArrays(1, &m_vao);
    a.GenBuffers(1, &m_vbo);
    a.GenBuffers(1, &m_ebo);
    a.BindVertexArray(m_vao);
    a.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    a.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    a.EnableVertexAttribArray(0);
    a.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE_, 8 * sizeof(float), reinterpret_cast<void*>(0));
    a.EnableVertexAttribArray(1);
    a.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE_, 8 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    a.EnableVertexAttribArray(2);
    a.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE_, 8 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
    a.BindVertexArray(0);

    // 1x1 white fallback texture (texId 0 in a DrawCmd maps to this).
    const uint8_t white[4] = {255, 255, 255, 255};
    m_whiteTex = static_cast<unsigned>(createTexture(white, 1, 1));
    if (m_whiteTex == 0) return false;
    return true;
}

bool GlBackend::init(GetProcFn getProc) {
    if (m_ready) return true;
    m_error.clear();
    if (!loadFunctions(getProc)) return false;
    // A previous attempt may have died halfway through createPipeline():
    // drop whatever GL objects it managed to create before starting over.
    shutdown();
    if (!createPipeline()) return false;
    m_ready = true;
    return true;
}

int GlBackend::createTexture(const uint8_t* rgba, int w, int h) {
    if (!m_api || !rgba || w <= 0 || h <= 0) return 0;
    Api& a = *m_api;

    // Uploads must not leave the host's texture binding clobbered.
    GLint prevTex = 0;
    a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    GLuint tex = 0;
    a.GenTextures(1, &tex);
    a.BindTexture(GL_TEXTURE_2D, tex);
    a.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA), w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    return static_cast<int>(tex);
}

bool GlBackend::ensureFontTexture(Font& font) {
    if (!m_ready) return false;
    if (!font.atlas()) return false;
    const int slot = font.atlasTexture();
    if (slot == 0) return false;              // atlas never built
    const unsigned revision = font.atlasRevision();
    auto it = m_fontTexs.find(slot);
    if (it != m_fontTexs.end()) {
        // Glyphs bake on demand, so a live atlas keeps changing after it was
        // uploaded. Reserved cells never move, so re-uploading the whole buffer
        // costs nothing at steady state (revision is unchanged) and every
        // already-emitted UV stays valid.
        if (m_fontTexRev[slot] == revision) return true;
        GLint prevTex = 0;
        m_api->GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
        m_api->BindTexture(GL_TEXTURE_2D, it->second);
        GLint prevAlign = 4;
        m_api->GetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        m_api->PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        m_api->TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8),
                          font.atlasWidth(), font.atlasHeight(), 0, GL_RED,
                          GL_UNSIGNED_BYTE, font.atlas());
        m_api->PixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
        m_api->BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
        m_fontTexRev[slot] = revision;
        return true;
    }
    const uint8_t* px = font.atlas();
    const int tex = createTextureA8(px, font.atlasWidth(), font.atlasHeight());
    if (tex == 0) return false;
    m_fontTexs[slot] = static_cast<unsigned>(tex);
    m_fontTexRev[slot] = revision;
    return true;
}

// A8 coverage atlas -> GL_R8 texture whose swizzle turns samples into
// (1,1,1,coverage): the same value the old RGBA8 white atlas produced, at a
// quarter of the upload size and with no shader change.
int GlBackend::createTextureA8(const uint8_t* alpha, int w, int h) {
    if (!m_api || !alpha || w <= 0 || h <= 0) return 0;
    Api& a = *m_api;

    GLint prevTex = 0;
    a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

    GLuint tex = 0;
    a.GenTextures(1, &tex);
    a.BindTexture(GL_TEXTURE_2D, tex);
    // A8 rows are tightly packed (rowBytes == w); the default unpack alignment
    // of 4 would pad every row on upload and shear the atlas whenever w % 4.
    GLint prevAlign = 4;
    a.GetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    a.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    a.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R8), w, h, 0,
                 GL_RED, GL_UNSIGNED_BYTE, alpha);
    a.PixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    const GLint swizzle[4] = {static_cast<GLint>(GL_ONE), static_cast<GLint>(GL_ONE),
                              static_cast<GLint>(GL_ONE), static_cast<GLint>(GL_RED)};
    a.TexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_LINEAR));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_LINEAR));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
    a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    return static_cast<int>(tex);
}

bool GlBackend::syncFonts() {
    if (!m_ready) return false;
    bool ok = true;

    std::unordered_set<int> live;
    FontManager::instance().forEach([&](Font& f) {
        live.insert(f.atlasTexture());
        if (!ensureFontTexture(f)) ok = false;
    });

    // Retire textures whose font was unloaded: its slot is no longer live.
    for (auto it = m_fontTexs.begin(); it != m_fontTexs.end();) {
        if (live.count(it->first) != 0) { ++it; continue; }
        GLuint tex = it->second;
        m_api->DeleteTextures(1, &tex);
        m_fontTexRev.erase(it->first);
        it = m_fontTexs.erase(it);
    }
    return ok;
}

bool GlBackend::syncImages() {
    if (!m_ready) return false;
    bool ok = true;
    ImageManager::instance().forEach([&](const ImageData& img) {
        if (m_imageTexs.count(img.id) != 0) return;
        const int tex = createTexture(img.rgba.data(), img.w, img.h);
        if (tex == 0) { ok = false; return; }
        m_imageTexs[img.id] = static_cast<unsigned>(tex);
    });
    return ok;
}

void GlBackend::render(const DrawList& list, int fbWidth, int fbHeight, bool flipY) {
    if (!m_ready || list.cmds.empty() || fbWidth <= 0 || fbHeight <= 0) return;
    Api& a = *m_api;

    // Fonts are resolved lazily while the list is built, so their atlas may
    // not have reached the GPU yet (first frame, a new size mid-run). The
    // draw list already carries their slots; upload them before drawing.
    syncFonts();
    syncImages();

    // ---- capture host state -------------------------------------------
    const bool wasBlend   = a.IsEnabled(GL_BLEND) != 0;
    const bool wasScissor = a.IsEnabled(GL_SCISSOR_TEST) != 0;
    const bool wasDepth   = a.IsEnabled(GL_DEPTH_TEST) != 0;
    const bool wasCull    = a.IsEnabled(GL_CULL_FACE) != 0;
    const bool wasStencil = a.IsEnabled(GL_STENCIL_TEST) != 0;
    GLint prevProgram = 0, prevVao = 0, prevActiveTex = 0, prevTex2D = 0;
    GLint prevTex2DUnit0 = 0;
    GLint prevArrBuf = 0, prevElemBuf = 0;
    GLint prevSrcRGB = GL_SRC_ALPHA, prevDstRGB = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevSrcA = GL_SRC_ALPHA, prevDstA = GL_ONE_MINUS_SRC_ALPHA;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLint prevScissor[4] = {0, 0, 0, 0};
    a.GetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    a.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    a.GetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);          // bound on prevActiveTex
    prevTex2DUnit0 = prevTex2D;
    if (prevActiveTex != GL_TEXTURE0) {
        // Sampling happens on unit 0, so that binding has to survive too.
        a.ActiveTexture(GL_TEXTURE0);
        a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2DUnit0);
        a.ActiveTexture(static_cast<GLenum>(prevActiveTex));
    }
    a.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrBuf);
    a.GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElemBuf);  // belongs to prevVao
    a.GetIntegerv(GL_BLEND_SRC_RGB, &prevSrcRGB);
    a.GetIntegerv(GL_BLEND_DST_RGB, &prevDstRGB);
    a.GetIntegerv(GL_BLEND_SRC_ALPHA, &prevSrcA);
    a.GetIntegerv(GL_BLEND_DST_ALPHA, &prevDstA);
    a.GetIntegerv(GL_VIEWPORT, prevViewport);
    a.GetIntegerv(GL_SCISSOR_BOX, prevScissor);

    // ---- setup ----------------------------------------------------------
    // The draw list is a y-down triangle soup with no depth information:
    // depth, stencil and face culling (its winding flips under the y-down
    // projection) must not participate, or the whole UI can vanish.
    Matrix4x4 proj = flipY
        ? Matrix4x4::ortho(0.0f, static_cast<float>(fbWidth), 0.0f,
                           static_cast<float>(fbHeight), -1.0f, 1.0f)
        : Matrix4x4::ortho(0.0f, static_cast<float>(fbWidth),
                           static_cast<float>(fbHeight), 0.0f, -1.0f, 1.0f);

    a.Enable(GL_BLEND);
    a.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    a.Enable(GL_SCISSOR_TEST);
    a.Disable(GL_DEPTH_TEST);
    a.Disable(GL_CULL_FACE);
    a.Disable(GL_STENCIL_TEST);
    a.Viewport(0, 0, fbWidth, fbHeight);
    a.UseProgram(m_program);
    a.UniformMatrix4fv(m_uProj, 1, GL_FALSE_, proj.data());
    a.Uniform1i(m_uTex, 0);
    a.ActiveTexture(GL_TEXTURE0);
    a.BindVertexArray(m_vao);

    // Upload only when the frame outgrew the allocated capacity; steady
    // frames take a cheap glBufferSubData into the existing store instead of
    // reallocating the whole buffer every time.
    const auto vbytes = static_cast<GLsizeiptr>(list.vertices.size() * sizeof(Vertex));
    const auto ibytes = static_cast<GLsizeiptr>(list.indices.size() * sizeof(uint32_t));
    a.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (vbytes > static_cast<GLsizeiptr>(m_vboCap)) {
        a.BufferData(GL_ARRAY_BUFFER, vbytes, nullptr, GL_STREAM_DRAW);
        m_vboCap = vbytes;
    }
    if (vbytes > 0) a.BufferSubData(GL_ARRAY_BUFFER, 0, vbytes, list.vertices.data());
    a.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    if (ibytes > static_cast<GLsizeiptr>(m_eboCap)) {
        a.BufferData(GL_ELEMENT_ARRAY_BUFFER, ibytes, nullptr, GL_STREAM_DRAW);
        m_eboCap = ibytes;
    }
    if (ibytes > 0) a.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibytes, list.indices.data());

    for (const DrawCmd& cmd : list.cmds) {
        if (cmd.indexCount == 0) continue;

        // Scissor rect: GL measures from the bottom-left corner. Round the
        // edges outward so a fractional clip never shaves a row/column.
        int sx = static_cast<int>(std::floor(cmd.clip.x));
        int sy = static_cast<int>(std::floor(cmd.clip.y));
        int sw = static_cast<int>(std::ceil(cmd.clip.x + cmd.clip.w)) - sx;
        int sh = static_cast<int>(std::ceil(cmd.clip.y + cmd.clip.h)) - sy;
        if (sw <= 0 || sh <= 0) continue;
        if (sx < 0) { sw += sx; sx = 0; }
        if (sy < 0) { sh += sy; sy = 0; }
        if (sx + sw > fbWidth) sw = fbWidth - sx;
        if (sy + sh > fbHeight) sh = fbHeight - sy;
        if (sw <= 0 || sh <= 0) continue;

        int glY = flipY ? sy : (fbHeight - sy - sh);
        a.Scissor(sx, glY, sw, sh);

        // 0 = untextured, negative = font atlas slot, otherwise a texture
        // created through createTexture() or an ImageManager image slot.
        GLuint tex = m_whiteTex;
        if (cmd.texId < 0) {
            auto it = m_fontTexs.find(cmd.texId);
            if (it == m_fontTexs.end()) continue;   // upload failed: skip
            tex = it->second;
        } else if (cmd.texId != 0) {
            auto it = m_imageTexs.find(cmd.texId);
            if (it != m_imageTexs.end()) {
                tex = it->second;
            } else if (ImageManager::instance().ownsSlot(cmd.texId)) {
                continue;                           // upload failed: skip
            } else {
                tex = static_cast<GLuint>(cmd.texId);
            }
        }
        a.BindTexture(GL_TEXTURE_2D, tex);

        a.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(cmd.indexCount),
                       GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(
                           static_cast<std::uintptr_t>(cmd.indexOffset) * sizeof(uint32_t)));
    }

    // ---- restore host state --------------------------------------------
    a.Scissor(prevScissor[0], prevScissor[1], prevScissor[2], prevScissor[3]);
    a.BindVertexArray(static_cast<GLuint>(prevVao));
    a.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElemBuf));
    a.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrBuf));
    a.UseProgram(static_cast<GLuint>(prevProgram));
    if (prevActiveTex != GL_TEXTURE0) {
        a.ActiveTexture(GL_TEXTURE0);
        a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex2DUnit0));
    }
    a.ActiveTexture(static_cast<GLenum>(prevActiveTex));
    a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex2D));
    a.BlendFuncSeparate(static_cast<GLenum>(prevSrcRGB), static_cast<GLenum>(prevDstRGB),
                        static_cast<GLenum>(prevSrcA), static_cast<GLenum>(prevDstA));
    a.Viewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    auto setCap = [&](GLenum cap, bool on) { if (on) a.Enable(cap); else a.Disable(cap); };
    setCap(GL_BLEND, wasBlend);
    setCap(GL_SCISSOR_TEST, wasScissor);
    setCap(GL_DEPTH_TEST, wasDepth);
    setCap(GL_CULL_FACE, wasCull);
    setCap(GL_STENCIL_TEST, wasStencil);
}

void GlBackend::shutdown() {
    if (!m_api) { m_ready = false; return; }
    Api& a = *m_api;
    if (m_vbo) { a.DeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo) { a.DeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_vboCap = m_eboCap = 0;
    if (m_vao) { a.DeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_program) { a.DeleteProgram(m_program); m_program = 0; }
    if (m_whiteTex) {
        GLuint t = static_cast<GLuint>(m_whiteTex);
        a.DeleteTextures(1, &t);
        m_whiteTex = 0;
    }
    for (auto& kv : m_fontTexs) {
        GLuint t = kv.second;
        a.DeleteTextures(1, &t);
    }
    m_fontTexs.clear();
    for (auto& kv : m_imageTexs) {
        GLuint t = kv.second;
        a.DeleteTextures(1, &t);
    }
    m_imageTexs.clear();
    m_ready = false;
}

} // namespace ogui
