// OpenGL 3.3 core / OpenGL ES 3.0 backend. All entry points are resolved at
// runtime through the host's GetProc; this file never includes <GL/gl.h> and
// never links against libGL/opengl32/libGLESv2, so the library stays
// dependency-free.
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
using GLubyte = unsigned char;
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
constexpr GLenum GL_RGBA8              = 0x8058;
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
// ES 3.0 only exposes the per-component swizzles; the aggregate
// GL_TEXTURE_SWIZZLE_RGBA (0x8E46) is desktop-only, so setting the four
// channels one by one is the single path that works on both.
constexpr GLenum GL_TEXTURE_SWIZZLE_R  = 0x8E42;
constexpr GLenum GL_TEXTURE_SWIZZLE_G  = 0x8E43;
constexpr GLenum GL_TEXTURE_SWIZZLE_B  = 0x8E44;
constexpr GLenum GL_TEXTURE_SWIZZLE_A  = 0x8E45;
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
// string queries (profile detection, and what a shader failure is quoted with)
constexpr GLenum GL_VERSION                  = 0x1F02;
constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
// ---- 3D pass ----
constexpr GLenum GL_STATIC_DRAW        = 0x88E4;
constexpr GLenum GL_LEQUAL             = 0x0203;
constexpr GLenum GL_BACK               = 0x0405;
constexpr GLenum GL_CCW                = 0x0901;
constexpr GLenum GL_DEPTH_BUFFER_BIT   = 0x00000100;
constexpr GLenum GL_COLOR_BUFFER_BIT   = 0x00004000;
constexpr GLenum GL_DEPTH_WRITEMASK    = 0x0B72;
constexpr GLenum GL_DEPTH_FUNC         = 0x0B74;
constexpr GLenum GL_CULL_FACE_MODE     = 0x0B45;
constexpr GLenum GL_FRONT_FACE         = 0x0B46;
constexpr GLenum GL_FRAMEBUFFER        = 0x8D40;
constexpr GLenum GL_FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum GL_RENDERBUFFER       = 0x8D41;
constexpr GLenum GL_RENDERBUFFER_BINDING = 0x8CA7;
constexpr GLenum GL_COLOR_ATTACHMENT0  = 0x8CE0;
constexpr GLenum GL_DEPTH_ATTACHMENT   = 0x8D00;
constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_DEPTH_COMPONENT16  = 0x81A5;
} // namespace

// ---- function pointer table ------------------------------------------------
struct GlBackend::Api {
    // state
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    // state query / restore
    GLboolean (*IsEnabled)(GLenum) = nullptr;
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
    void (*BlendFuncSeparate)(GLenum, GLenum, GLenum, GLenum) = nullptr;
    const GLubyte* (*GetString)(GLenum) = nullptr;
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
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*ActiveTexture)(GLenum) = nullptr;
    // draw
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*) = nullptr;
    // ---- 3D pass ----
    // Loaded but not required: a 2D-only host must keep initialising even if
    // the context lacks one of these, and the 3D entry points report it then.
    void (*DepthFunc)(GLenum) = nullptr;
    void (*DepthMask)(GLboolean) = nullptr;
    void (*CullFace)(GLenum) = nullptr;
    void (*FrontFace)(GLenum) = nullptr;
    void (*Clear)(unsigned) = nullptr;
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Uniform1f)(GLint, GLfloat) = nullptr;
    void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    // offscreen targets (Framebuffer objects, ES 3.0 / GL 3.0 core)
    void (*GenFramebuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindFramebuffer)(GLenum, GLuint) = nullptr;
    void (*DeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
    GLenum (*CheckFramebufferStatus)(GLenum) = nullptr;
    void (*GenRenderbuffers)(GLsizei, GLuint*) = nullptr;
    void (*BindRenderbuffer)(GLenum, GLuint) = nullptr;
    void (*DeleteRenderbuffers)(GLsizei, const GLuint*) = nullptr;
    void (*RenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei) = nullptr;
    void (*FramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint) = nullptr;
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
#elif defined(__ANDROID__)
    // An ES-only platform: nothing here may reach for libGL. The GL ES entry
    // points are exported by libGLESv2, the getters by libEGL.
    static void* gles = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
    if (gles) {
        if (void* p = dlsym(gles, name)) return p;
    }
    using EglGetProc = void* (*)(const char*);
    static EglGetProc eglGetProc = [] {
        static void* egl = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
        return egl ? reinterpret_cast<EglGetProc>(dlsym(egl, "eglGetProcAddress"))
                   : nullptr;
    }();
    return eglGetProc ? eglGetProc(name) : nullptr;
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
    need(a.BlendFuncSeparate, "glBlendFuncSeparate");
    need(a.Scissor, "glScissor");
    need(a.Viewport, "glViewport");
    need(a.IsEnabled, "glIsEnabled");
    need(a.GetIntegerv, "glGetIntegerv");
    need(a.GetString, "glGetString");
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
    need(a.PixelStorei, "glPixelStorei");
    need(a.DeleteTextures, "glDeleteTextures");
    need(a.ActiveTexture, "glActiveTexture");
    // draw
    need(a.DrawElements, "glDrawElements");

    // 3D: best effort. A context that lacks these still gives a working 2D
    // backend; renderScene() then fails through lastError().
    load(a.DepthFunc, gp, "glDepthFunc");
    load(a.DepthMask, gp, "glDepthMask");
    load(a.CullFace, gp, "glCullFace");
    load(a.FrontFace, gp, "glFrontFace");
    load(a.Clear, gp, "glClear");
    load(a.ClearColor, gp, "glClearColor");
    load(a.Uniform1f, gp, "glUniform1f");
    load(a.Uniform3f, gp, "glUniform3f");
    load(a.Uniform4f, gp, "glUniform4f");
    load(a.GenFramebuffers, gp, "glGenFramebuffers");
    load(a.BindFramebuffer, gp, "glBindFramebuffer");
    load(a.DeleteFramebuffers, gp, "glDeleteFramebuffers");
    load(a.FramebufferTexture2D, gp, "glFramebufferTexture2D");
    load(a.CheckFramebufferStatus, gp, "glCheckFramebufferStatus");
    load(a.GenRenderbuffers, gp, "glGenRenderbuffers");
    load(a.BindRenderbuffer, gp, "glBindRenderbuffer");
    load(a.DeleteRenderbuffers, gp, "glDeleteRenderbuffers");
    load(a.RenderbufferStorage, gp, "glRenderbufferStorage");
    load(a.FramebufferRenderbuffer, gp, "glFramebufferRenderbuffer");

    if (!missing.empty()) {
        m_error = "missing GL entry points: " + missing;
        return false;
    }
    return true;
}

// ---- shader sources -------------------------------------------------------
// One shader body per pass, two dialects each: the desktop and ES 3.00
// versions of GLSL agree on everything these pipelines use (layout locations,
// `in`/`out`, `texture()`), so only the #version line and the ES precision
// statement change. highp matches the desktop arithmetic, so both paths round
// alike.
namespace {
const char* const k2dVs = R"(
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
}
)";

const char* const k2dFs = R"(
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV) * vColor;
}
)";

// 3D: position, normal, uv in; one directional light with an ambient term;
// the normal matrix is derived from the model matrix so a non-uniform scale
// still lights correctly.
const char* const k3dVs = R"(
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec3 vNormal;
out vec2 vUV;
void main() {
    vNormal = transpose(inverse(mat3(uModel))) * aNormal;
    vUV = aUV;
    gl_Position = uViewProj * (uModel * vec4(aPos, 1.0));
}
)";

const char* const k3dFs = R"(
in vec3 vNormal;
in vec2 vUV;
uniform sampler2D uTex;
uniform int uUseTex;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec4 uTint;
uniform float uAmbient;
out vec4 fragColor;
void main() {
    float ndl = max(dot(normalize(vNormal), normalize(-uLightDir)), 0.0);
    vec3 lit = clamp(uAmbient + ndl, 0.0, 1.0) * uLightColor;
    vec4 base = uTint;
    if (uUseTex != 0) base *= texture(uTex, vUV);
    fragColor = vec4(base.rgb * lit, base.a);
}
)";
} // namespace

unsigned GlBackend::buildProgram(const char* vsBody, const char* fsBody,
                                const char* what) {
    Api& a = *m_api;

    const std::string srcVs = std::string(m_es ? "#version 300 es\n"
                                               : "#version 330 core\n") + vsBody;
    std::string srcFs =
        std::string(m_es ? "#version 300 es\n" : "#version 330 core\n");
    if (m_es) srcFs += "precision highp float;\n";
    srcFs += fsBody;

    auto compile = [&](GLenum type, const std::string& src) -> GLuint {
        GLuint sh = a.CreateShader(type);
        const char* p = src.c_str();
        a.ShaderSource(sh, 1, &p, nullptr);
        a.CompileShader(sh);
        GLint ok = 0;
        a.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {0};
            GLsizei n = 0;
            a.GetShaderInfoLog(sh, sizeof(log), &n, log);
            m_error = describe() + ": " + what + " shader compile failed: " + log;
            a.DeleteShader(sh);
            return 0;
        }
        return sh;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, srcVs);
    if (!vs) return 0;
    GLuint fs = compile(GL_FRAGMENT_SHADER, srcFs);
    if (!fs) { a.DeleteShader(vs); return 0; }

    GLuint prog = a.CreateProgram();
    a.AttachShader(prog, vs);
    a.AttachShader(prog, fs);
    a.LinkProgram(prog);
    a.DeleteShader(vs);
    a.DeleteShader(fs);

    GLint ok = 0;
    a.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        GLsizei n = 0;
        a.GetProgramInfoLog(prog, sizeof(log), &n, log);
        m_error = describe() + ": " + what + " program link failed: " + log;
        a.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

bool GlBackend::createPipeline() {
    Api& a = *m_api;

    m_program = buildProgram(k2dVs, k2dFs, "2D");
    if (!m_program) return false;

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

bool GlBackend::init(GetProcFn getProc, Profile profile) {
    if (m_ready) return true;
    m_error.clear();
    if (!loadFunctions(getProc)) return false;
    // The context already exists (the host made it current), so the reported
    // versions decide the shader dialect: a GLSL ES string always says "ES".
    const std::string gl = queryString(GL_VERSION);
    const std::string glsl = queryString(GL_SHADING_LANGUAGE_VERSION);
    switch (profile) {
        case Profile::OpenGLES: m_es = true; break;
        case Profile::DesktopGL: m_es = false; break;
        case Profile::Auto:
            m_es = gl.rfind("OpenGL ES", 0) == 0 || glsl.find("ES") != std::string::npos;
            break;
    }
    // A previous attempt may have died halfway through createPipeline():
    // drop whatever GL objects it managed to create before starting over.
    shutdown();
    if (!createPipeline()) return false;
    m_ready = true;
    return true;
}

std::string GlBackend::queryString(unsigned name) const {
    if (!m_api || !m_api->GetString) return std::string();
    const GLubyte* s = m_api->GetString(static_cast<GLenum>(name));
    return s ? std::string(reinterpret_cast<const char*>(s)) : std::string();
}

std::string GlBackend::describe() const {
    std::string s = queryString(GL_VERSION);
    if (s.empty()) s = m_es ? "OpenGL ES" : "OpenGL";
    return s + " (GLSL " + queryString(GL_SHADING_LANGUAGE_VERSION) + ")";
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
    // Sized internalformat: ES 3.0 rejects the unsized GL_RGBA there, and
    // RGBA8 is what both APIs would choose anyway.
    a.TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8), w, h, 0,
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
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, static_cast<GLint>(GL_ONE));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, static_cast<GLint>(GL_ONE));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, static_cast<GLint>(GL_ONE));
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, static_cast<GLint>(GL_RED));
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
    // Source-over, stated separately because the two channels do not follow the
    // same rule: colour blends by the source alpha, while the destination alpha
    // must accumulate as src + dst * (1 - src). Reaching for GL_SRC_ALPHA there
    // too would square the source alpha and leave the frame's alpha below what
    // SoftwareRenderer, which follows the definition, writes.
    a.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
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
            // Host textures first: a SceneView target's slot lives here, and
            // its id is deliberately outside ImageManager's range.
            auto host = m_hostTexs.find(cmd.texId);
            auto it = m_imageTexs.find(cmd.texId);
            if (host != m_hostTexs.end()) {
                tex = host->second;
            } else if (it != m_imageTexs.end()) {
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

// ---- 3D pass --------------------------------------------------------------
bool GlBackend::ensure3DProgram() {
    if (m_3dReady) return true;
    if (!m_ready) {
        m_error = "GlBackend not initialised";
        return false;
    }
    Api& a = *m_api;
    // These are the entries the depth-tested pass cannot do without. They are
    // optional in loadFunctions() so a 2D-only context still initialises.
    if (!a.DepthFunc || !a.DepthMask || !a.CullFace || !a.FrontFace ||
        !a.Clear || !a.ClearColor || !a.Uniform1f || !a.Uniform3f ||
        !a.Uniform4f) {
        m_error = "3D needs depth/cull entry points the context did not provide";
        return false;
    }

    m_3dProgram = buildProgram(k3dVs, k3dFs, "3D");
    if (!m_3dProgram) return false;

    m_u3dViewProj   = a.GetUniformLocation(m_3dProgram, "uViewProj");
    m_u3dModel      = a.GetUniformLocation(m_3dProgram, "uModel");
    m_u3dLightDir   = a.GetUniformLocation(m_3dProgram, "uLightDir");
    m_u3dLightColor = a.GetUniformLocation(m_3dProgram, "uLightColor");
    m_u3dAmbient    = a.GetUniformLocation(m_3dProgram, "uAmbient");
    m_u3dTint       = a.GetUniformLocation(m_3dProgram, "uTint");
    m_u3dTex        = a.GetUniformLocation(m_3dProgram, "uTex");
    m_u3dUseTex     = a.GetUniformLocation(m_3dProgram, "uUseTex");
    if (m_u3dViewProj < 0 || m_u3dModel < 0 || m_u3dLightDir < 0 ||
        m_u3dLightColor < 0 || m_u3dAmbient < 0 || m_u3dTint < 0 ||
        m_u3dTex < 0 || m_u3dUseTex < 0) {
        m_error = "3D shader uniforms missing";
        a.DeleteProgram(m_3dProgram);
        m_3dProgram = 0;
        return false;
    }
    m_3dReady = true;
    return true;
}

unsigned GlBackend::resolveTexture(int texId) const {
    if (texId < 0) {
        auto it = m_fontTexs.find(texId);
        return it == m_fontTexs.end() ? m_whiteTex : it->second;
    }
    if (texId == 0) return m_whiteTex;
    auto host = m_hostTexs.find(texId);
    if (host != m_hostTexs.end()) return host->second;
    auto img = m_imageTexs.find(texId);
    if (img != m_imageTexs.end()) return img->second;
    return static_cast<unsigned>(texId);
}

int GlBackend::uploadMesh(const MeshData& mesh) {
    if (!m_ready || mesh.vertices.empty() || mesh.indices.empty()) return 0;
    Api& a = *m_api;

    GLint prevVao = 0, prevArrayBuf = 0;
    a.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    a.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuf);

    Mesh3D m;
    m.indexCount = static_cast<int>(mesh.indices.size());
    mesh.bounds(m.min, m.max);
    a.GenVertexArrays(1, &m.vao);
    a.GenBuffers(1, &m.vbo);
    a.GenBuffers(1, &m.ebo);
    a.BindVertexArray(m.vao);
    a.BindBuffer(GL_ARRAY_BUFFER, m.vbo);
    a.BufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(MeshVertex)),
                 mesh.vertices.data(), GL_STATIC_DRAW);
    const GLsizei stride = static_cast<GLsizei>(sizeof(MeshVertex));
    a.EnableVertexAttribArray(0);
    a.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE_, stride,
                          reinterpret_cast<void*>(0));
    a.EnableVertexAttribArray(1);
    a.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE_, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));
    a.EnableVertexAttribArray(2);
    a.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE_, stride,
                          reinterpret_cast<void*>(6 * sizeof(float)));
    a.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    a.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    a.BindVertexArray(static_cast<GLuint>(prevVao));
    a.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuf));

    const int handle = m_nextMesh++;
    m_meshes[handle] = m;
    return handle;
}

void GlBackend::releaseMesh(int mesh) {
    auto it = m_meshes.find(mesh);
    if (it == m_meshes.end() || !m_api) return;
    Api& a = *m_api;
    if (it->second.vbo) a.DeleteBuffers(1, &it->second.vbo);
    if (it->second.ebo) a.DeleteBuffers(1, &it->second.ebo);
    if (it->second.vao) a.DeleteVertexArrays(1, &it->second.vao);
    m_meshes.erase(it);
}

void GlBackend::renderScene(const Scene3D& scene, int fbWidth, int fbHeight) {
    if (fbWidth <= 0 || fbHeight <= 0 || scene.empty()) return;
    if (!ensure3DProgram()) return;
    drawScene(scene, static_cast<float>(fbWidth) / static_cast<float>(fbHeight));
}

void GlBackend::drawScene(const Scene3D& scene, float aspect) {
    Api& a = *m_api;

    // ---- capture host state -------------------------------------------
    const bool wasDepth   = a.IsEnabled(GL_DEPTH_TEST) != 0;
    const bool wasCull    = a.IsEnabled(GL_CULL_FACE) != 0;
    const bool wasBlend   = a.IsEnabled(GL_BLEND) != 0;
    const bool wasScissor = a.IsEnabled(GL_SCISSOR_TEST) != 0;
    GLint prevProgram = 0, prevVao = 0, prevArrBuf = 0, prevElemBuf = 0;
    GLint prevActiveTex = 0, prevTex = 0, prevTexUnit0 = 0;
    GLint prevDepthFunc = GL_LEQUAL, prevDepthMask = 1;
    GLint prevCullMode = GL_BACK, prevFrontFace = GL_CCW;
    GLint prevViewport[4] = {0, 0, 0, 0};
    a.GetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    a.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    a.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrBuf);
    a.GetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElemBuf);
    a.GetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    prevTexUnit0 = prevTex;
    if (prevActiveTex != GL_TEXTURE0) {
        a.ActiveTexture(GL_TEXTURE0);
        a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexUnit0);
    }
    a.GetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    a.GetIntegerv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    a.GetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);
    a.GetIntegerv(GL_FRONT_FACE, &prevFrontFace);
    a.GetIntegerv(GL_VIEWPORT, prevViewport);

    // ---- setup ---------------------------------------------------------
    a.Enable(GL_DEPTH_TEST);
    a.DepthFunc(GL_LEQUAL);
    a.DepthMask(1);
    a.Enable(GL_CULL_FACE);
    a.CullFace(GL_BACK);
    a.FrontFace(GL_CCW);
    a.Disable(GL_BLEND);          // opaque: depth writes want no blending
    a.Disable(GL_SCISSOR_TEST);   // the 3D pass owns the whole viewport
    a.UseProgram(m_3dProgram);

    const Matrix4x4 vp = scene.camera.viewProjection(aspect);
    const Frustum frustum = Frustum::fromViewProjection(vp);
    m_sceneStats = SceneStats{};
    a.UniformMatrix4fv(m_u3dViewProj, 1, GL_FALSE_, vp.data());
    const Vec3 ld = normalize(scene.light.direction);
    a.Uniform3f(m_u3dLightDir, ld.x, ld.y, ld.z);
    a.Uniform3f(m_u3dLightColor, scene.light.color.r / 255.0f,
                scene.light.color.g / 255.0f, scene.light.color.b / 255.0f);
    a.Uniform1f(m_u3dAmbient, scene.light.ambient);
    a.Uniform1i(m_u3dTex, 0);
    a.ActiveTexture(GL_TEXTURE0);

    for (const SceneItem& item : scene.items()) {
        auto mesh = m_meshes.find(item.mesh);
        if (mesh == m_meshes.end()) continue;   // released or never uploaded
        // Cheap cull: one AABB per item against six planes. Conservative by
        // construction (transformAabb never shrinks a rotated box), so it can
        // only skip work, never visible geometry.
        Vec3 wmin, wmax;
        transformAabb(item.model, mesh->second.min, mesh->second.max, wmin, wmax);
        if (!frustum.intersects(wmin, wmax)) {
            ++m_sceneStats.culled;
            continue;
        }
        ++m_sceneStats.drawn;
        a.UniformMatrix4fv(m_u3dModel, 1, GL_FALSE_, item.model.data());
        const Color& tint = item.material.tint;
        a.Uniform4f(m_u3dTint, tint.r / 255.0f, tint.g / 255.0f,
                    tint.b / 255.0f, tint.a / 255.0f);
        const unsigned tex = resolveTexture(item.material.texId);
        a.Uniform1i(m_u3dUseTex, item.material.texId != 0 && tex != 0 ? 1 : 0);
        a.BindTexture(GL_TEXTURE_2D, tex != 0 ? tex : m_whiteTex);
        a.BindVertexArray(mesh->second.vao);
        a.DrawElements(GL_TRIANGLES, mesh->second.indexCount, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(0));
    }

    // ---- restore host state --------------------------------------------
    a.BindVertexArray(static_cast<GLuint>(prevVao));
    a.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElemBuf));
    a.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrBuf));
    a.UseProgram(static_cast<GLuint>(prevProgram));
    if (prevActiveTex != GL_TEXTURE0) {
        a.ActiveTexture(GL_TEXTURE0);
        a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexUnit0));
    }
    a.ActiveTexture(static_cast<GLenum>(prevActiveTex));
    a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
    a.DepthFunc(static_cast<GLenum>(prevDepthFunc));
    a.DepthMask(prevDepthMask != 0 ? 1 : 0);
    a.CullFace(static_cast<GLenum>(prevCullMode));
    a.FrontFace(static_cast<GLenum>(prevFrontFace));
    a.Viewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    auto setCap = [&](GLenum cap, bool on) { if (on) a.Enable(cap); else a.Disable(cap); };
    setCap(GL_DEPTH_TEST, wasDepth);
    setCap(GL_CULL_FACE, wasCull);
    setCap(GL_BLEND, wasBlend);
    setCap(GL_SCISSOR_TEST, wasScissor);
}

bool GlBackend::allocTarget(Target3D& t) {
    Api& a = *m_api;

    // A target is created in the middle of a host frame, so every binding
    // touched here is captured and put back before returning.
    GLint prevFbo = 0, prevActiveTex = 0, prevTexUnit0 = 0, prevRb = 0;
    a.GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    a.GetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexUnit0);
    if (prevActiveTex != GL_TEXTURE0) {
        a.ActiveTexture(GL_TEXTURE0);
        a.GetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexUnit0);
    }
    a.GetIntegerv(GL_RENDERBUFFER_BINDING, &prevRb);
    a.ActiveTexture(GL_TEXTURE0);

    a.GenTextures(1, &t.color);
    if (!t.color) return false;
    a.BindTexture(GL_TEXTURE_2D, t.color);
    a.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.w, t.h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    a.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    a.GenRenderbuffers(1, &t.depth);
    if (!t.depth) return false;
    a.BindRenderbuffer(GL_RENDERBUFFER, t.depth);
    a.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, t.w, t.h);

    a.GenFramebuffers(1, &t.fbo);
    if (!t.fbo) return false;
    a.BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    a.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, t.color, 0);
    a.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, t.depth);
    const bool complete = a.CheckFramebufferStatus(GL_FRAMEBUFFER) ==
                          GL_FRAMEBUFFER_COMPLETE;

    a.BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    a.BindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prevRb));
    a.ActiveTexture(GL_TEXTURE0);
    a.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexUnit0));
    a.ActiveTexture(static_cast<GLenum>(prevActiveTex));
    return complete;
}

void GlBackend::freeTarget(Target3D& t) {
    Api& a = *m_api;
    if (t.fbo) a.DeleteFramebuffers(1, &t.fbo);
    if (t.color) a.DeleteTextures(1, &t.color);
    if (t.depth) a.DeleteRenderbuffers(1, &t.depth);
    t.fbo = t.color = t.depth = 0;
}

int GlBackend::createSceneTarget(int width, int height) {
    if (!m_ready || !ensure3DProgram()) return 0;
    Api& a = *m_api;
    if (!a.GenFramebuffers || !a.BindFramebuffer || !a.DeleteFramebuffers ||
        !a.FramebufferTexture2D || !a.CheckFramebufferStatus ||
        !a.GenRenderbuffers || !a.BindRenderbuffer ||
        !a.RenderbufferStorage || !a.FramebufferRenderbuffer) {
        m_error = "scene targets need framebuffer objects (GL 3.0 / ES 3.0)";
        return 0;
    }

    Target3D t;
    t.w = width > 0 ? width : 1;
    t.h = height > 0 ? height : 1;

    if (!allocTarget(t)) {
        freeTarget(t);
        m_error = "scene target is not a complete framebuffer";
        return 0;
    }

    t.slot = m_nextHostSlot++;
    const int handle = m_nextTarget++;
    m_targets[handle] = t;
    m_hostTexs[t.slot] = t.color;
    return handle;
}

bool GlBackend::resizeSceneTarget(int handle, int width, int height) {
    auto it = m_targets.find(handle);
    if (it == m_targets.end() || !m_ready) return false;
    Target3D& t = it->second;
    const int w = width > 0 ? width : 1;
    const int h = height > 0 ? height : 1;
    if (t.w == w && t.h == h) return true;

    freeTarget(t);
    t.w = w;
    t.h = h;
    if (!allocTarget(t)) {
        freeTarget(t);
        m_hostTexs.erase(t.slot);
        m_targets.erase(it);
        m_error = "scene target resize failed (framebuffer incomplete)";
        return false;
    }
    m_hostTexs[t.slot] = t.color;
    return true;
}

void GlBackend::destroySceneTarget(int handle) {
    auto it = m_targets.find(handle);
    if (it == m_targets.end() || !m_api) return;
    freeTarget(it->second);
    m_hostTexs.erase(it->second.slot);
    m_targets.erase(it);
}

int GlBackend::sceneTargetSlot(int handle) const {
    auto it = m_targets.find(handle);
    return it == m_targets.end() ? 0 : it->second.slot;
}

bool GlBackend::renderSceneToTarget(const Scene3D& scene, int handle) {
    auto it = m_targets.find(handle);
    if (it == m_targets.end() || !m_ready) return false;
    if (!ensure3DProgram()) return false;
    Target3D& t = it->second;
    if (t.w <= 0 || t.h <= 0) return false;

    Api& a = *m_api;
    GLint prevFbo = 0, prevViewport[4] = {0, 0, 0, 0};
    a.GetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    a.GetIntegerv(GL_VIEWPORT, prevViewport);

    a.BindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    a.Viewport(0, 0, t.w, t.h);
    const Color& bg = scene.background;
    a.ClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, bg.a / 255.0f);
    // Depth always: a stale depth buffer would hide the new frame. Colour only
    // when the scene asked for a background, so a transparent scene can layer
    // over whatever the target already held.
    a.Clear(static_cast<unsigned>(GL_DEPTH_BUFFER_BIT) |
            (bg.a > 0 ? static_cast<unsigned>(GL_COLOR_BUFFER_BIT) : 0u));

    if (!scene.empty()) {
        drawScene(scene, static_cast<float>(t.w) / static_cast<float>(t.h));
    }

    a.BindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    a.Viewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    return true;
}

int GlBackend::registerTexture(unsigned glTexture) {
    if (glTexture == 0) return 0;
    const int slot = m_nextHostSlot++;
    m_hostTexs[slot] = glTexture;
    return slot;
}

void GlBackend::unregisterTexture(int slot) {
    // Only slots this backend handed out are touched; never the GL texture
    // itself, which belongs to the caller.
    if (slot >= 1000000) m_hostTexs.erase(slot);
}

void GlBackend::shutdown() {
    if (!m_api) { m_ready = false; return; }
    Api& a = *m_api;
    // 3D first: meshes and targets own buffers the 2D teardown knows nothing
    // about, and freeTarget() still needs m_api.
    for (auto& kv : m_meshes) {
        if (kv.second.vbo) a.DeleteBuffers(1, &kv.second.vbo);
        if (kv.second.ebo) a.DeleteBuffers(1, &kv.second.ebo);
        if (kv.second.vao) a.DeleteVertexArrays(1, &kv.second.vao);
    }
    m_meshes.clear();
    for (auto& kv : m_targets) freeTarget(kv.second);
    m_targets.clear();
    // Host textures are the caller's: only the slot mapping goes away.
    m_hostTexs.clear();
    if (m_3dProgram) { a.DeleteProgram(m_3dProgram); m_3dProgram = 0; }
    m_3dReady = false;
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
