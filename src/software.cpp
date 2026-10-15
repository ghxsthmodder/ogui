#include "ogui/software.hpp"
#include "ogui/font.hpp"
#include "ogui/imagemanager.hpp"

#include <algorithm>
#include <cmath>

namespace ogui {

namespace {

// CLAMP_TO_EDGE bilinear fetch, matching GlBackend's sampler state.
void sampleBilinear(const TexInfo& t, float u, float v, float out[4]) {
    const float fx = std::clamp(u * t.w - 0.5f, 0.0f, static_cast<float>(t.w - 1));
    const float fy = std::clamp(v * t.h - 0.5f, 0.0f, static_cast<float>(t.h - 1));
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, t.w - 1);
    const int y1 = std::min(y0 + 1, t.h - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    auto fetch = [&](int x, int y) {
        return t.data + static_cast<size_t>(y) * t.rowBytes +
               static_cast<size_t>(x) * (t.alphaOnly ? 1 : 4);
    };
    const uint8_t* p00 = fetch(x0, y0);
    const uint8_t* p10 = fetch(x1, y0);
    const uint8_t* p01 = fetch(x0, y1);
    const uint8_t* p11 = fetch(x1, y1);

    if (t.alphaOnly) {
        // Single channel: white with interpolated coverage as alpha.
        const float top = p00[0] + (p10[0] - p00[0]) * tx;
        const float bot = p01[0] + (p11[0] - p01[0]) * tx;
        out[0] = out[1] = out[2] = 1.0f;
        out[3] = (top + (bot - top) * ty) / 255.0f;
        return;
    }

    for (int c = 0; c < 4; ++c) {
        const float top = p00[c] + (p10[c] - p00[c]) * tx;
        const float bot = p01[c] + (p11[c] - p01[c]) * tx;
        out[c] = (top + (bot - top) * ty) / 255.0f;
    }
}

// Fullscreen white texture, emulating GlBackend's texId 0.
const TexInfo& whiteTex() {
    static const uint8_t kWhite[4] = {255, 255, 255, 255};
    static const TexInfo t{kWhite, 1, 1, 4};
    return t;
}

inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void fillTriangle(Framebuffer& fb, const Vertex& v0, const Vertex& v1,
                  const Vertex& v2, const Rect& clip, const TexInfo* tex) {
    const float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    if (area == 0.0f) return;

    const float cx0 = std::max(clip.x, 0.0f);
    const float cy0 = std::max(clip.y, 0.0f);
    const float cx1 = std::min(clip.x + clip.w, static_cast<float>(fb.w));
    const float cy1 = std::min(clip.y + clip.h, static_cast<float>(fb.h));
    if (cx0 >= cx1 || cy0 >= cy1) return;

    const int x0c = static_cast<int>(std::floor(cx0));
    const int y0c = static_cast<int>(std::floor(cy0));
    const int x1c = static_cast<int>(std::ceil(cx1));
    const int y1c = static_cast<int>(std::ceil(cy1));

    // Also bound the scan by the triangle itself: the edge tests would reject
    // everything outside it anyway, and a glyph quad's clip is often the whole
    // frame, which used to make every triangle scan every pixel of it.
    const int x0 = std::max(x0c, static_cast<int>(std::floor(
                                     std::min({v0.x, v1.x, v2.x}))));
    const int y0 = std::max(y0c, static_cast<int>(std::floor(
                                     std::min({v0.y, v1.y, v2.y}))));
    const int x1 = std::min(x1c, static_cast<int>(std::ceil(
                                     std::max({v0.x, v1.x, v2.x}))));
    const int y1 = std::min(y1c, static_cast<int>(std::ceil(
                                     std::max({v0.y, v1.y, v2.y}))));
    if (x0 >= x1 || y0 >= y1) return;

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float px = x + 0.5f;
            const float py = y + 0.5f;

            const float w0 = edge(v1.x, v1.y, v2.x, v2.y, px, py);
            const float w1 = edge(v2.x, v2.y, v0.x, v0.y, px, py);
            const float w2 = edge(v0.x, v0.y, v1.x, v1.y, px, py);

            // Works for either winding; area<0 flips the sign of every edge.
            if (w0 * area < 0.0f || w1 * area < 0.0f || w2 * area < 0.0f) continue;

            const float l0 = w0 / area;
            const float l1 = w1 / area;
            const float l2 = w2 / area;

            const float u = l0 * v0.u + l1 * v1.u + l2 * v2.u;
            const float vv = l0 * v0.v + l1 * v1.v + l2 * v2.v;
            float tr = 1.0f, tg = 1.0f, tb = 1.0f, ta = 1.0f;
            if (tex) {
                // sampleBilinear writes 4 floats: keep them in a real array.
                // (&tr + 3 is undefined behaviour and scrambled the alpha,
                // which painted every glyph as a solid block.)
                float raw[4];
                sampleBilinear(*tex, u, vv, raw);
                tr = raw[0]; tg = raw[1]; tb = raw[2]; ta = raw[3];
            }

            // fragColor = texture * vColor (matches the GL fragment shader).
            const float sr = tr * (l0 * v0.r + l1 * v1.r + l2 * v2.r);
            const float sg = tg * (l0 * v0.g + l1 * v1.g + l2 * v2.g);
            const float sb = tb * (l0 * v0.b + l1 * v1.b + l2 * v2.b);
            const float sa = ta * (l0 * v0.a + l1 * v1.a + l2 * v2.a);
            if (sa <= 0.0f) continue;

            uint8_t* d = fb.at(x, y);
            // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
            //                     GL_ONE, GL_ONE_MINUS_SRC_ALPHA)
            const float ia = 1.0f - sa;
            // Blending into an RGBA8 target rounds to nearest, so this does
            // too: truncating left every blended pixel up to a unit below what
            // the GPU writes, in colour and in alpha.
            d[0] = static_cast<uint8_t>(std::clamp(sr * sa + (d[0] / 255.0f) * ia, 0.0f, 1.0f) * 255.0f + 0.5f);
            d[1] = static_cast<uint8_t>(std::clamp(sg * sa + (d[1] / 255.0f) * ia, 0.0f, 1.0f) * 255.0f + 0.5f);
            d[2] = static_cast<uint8_t>(std::clamp(sb * sa + (d[2] / 255.0f) * ia, 0.0f, 1.0f) * 255.0f + 0.5f);
            d[3] = static_cast<uint8_t>(std::clamp(sa + (d[3] / 255.0f) * ia, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

} // namespace

// ---- Framebuffer ----------------------------------------------------------
void Framebuffer::resize(int nw, int nh) {
    w = nw;
    h = nh;
    px.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0);
}

uint8_t* Framebuffer::at(int x, int y) {
    return px.data() + (static_cast<size_t>(y) * w + x) * 4;
}

const uint8_t* Framebuffer::at(int x, int y) const {
    return px.data() + (static_cast<size_t>(y) * w + x) * 4;
}

// ---- SoftwareRenderer -----------------------------------------------------
void SoftwareRenderer::syncFonts() {
    FontManager::instance().forEach([this](Font& f) {
        if (!f.loaded() || !f.atlas()) return;
        m_tex[f.atlasTexture()] =
            TexInfo{f.atlas(), f.atlasWidth(), f.atlasHeight(),
                    f.atlasRowBytes(), true};
    });
}

void SoftwareRenderer::syncImages() {
    ImageManager::instance().forEach([this](const ImageData& img) {
        m_tex[img.id] =
            TexInfo{img.rgba.data(), img.w, img.h, img.w * 4};
    });
}

void SoftwareRenderer::setTexture(int texId, const TexInfo& tex) {
    m_tex[texId] = tex;
}

void SoftwareRenderer::clear(Framebuffer& fb, const Color& col) const {
    const uint8_t r = static_cast<uint8_t>(col.r);
    const uint8_t g = static_cast<uint8_t>(col.g);
    const uint8_t b = static_cast<uint8_t>(col.b);
    const uint8_t a = static_cast<uint8_t>(col.a);
    for (size_t i = 0; i + 3 < fb.bytes(); i += 4) {
        fb.px[i + 0] = r;
        fb.px[i + 1] = g;
        fb.px[i + 2] = b;
        fb.px[i + 3] = a;
    }
}

void SoftwareRenderer::render(const DrawList& dl, Framebuffer& fb, int ssaa,
                              std::vector<int>* missing) const {
    if (ssaa < 1) ssaa = 1;
    if (missing) missing->clear();

    for (const DrawCmd& cmd : dl.cmds) {
        const TexInfo* tex = nullptr;
        if (cmd.texId != 0) {
            auto it = m_tex.find(cmd.texId);
            if (it != m_tex.end()) {
                tex = &it->second;
            } else if (missing) {
                missing->push_back(cmd.texId);
            }
        }
        if (!tex) tex = &whiteTex();

        const Rect clip{cmd.clip.x * ssaa, cmd.clip.y * ssaa,
                        cmd.clip.w * ssaa, cmd.clip.h * ssaa};

        const size_t end = cmd.indexOffset + cmd.indexCount;
        for (size_t i = cmd.indexOffset; i + 2 < end; i += 3) {
            const Vertex& a = dl.vertices[dl.indices[i + 0]];
            const Vertex& b = dl.vertices[dl.indices[i + 1]];
            const Vertex& c = dl.vertices[dl.indices[i + 2]];
            const Vertex sa{a.x * ssaa, a.y * ssaa, a.u, a.v, a.r, a.g, a.b, a.a};
            const Vertex sb{b.x * ssaa, b.y * ssaa, b.u, b.v, b.r, b.g, b.b, b.a};
            const Vertex sc{c.x * ssaa, c.y * ssaa, c.u, c.v, c.r, c.g, c.b, c.a};
            fillTriangle(fb, sa, sb, sc, clip, tex);
        }
    }
}

void SoftwareRenderer::downsample(const Framebuffer& src, Framebuffer& dst, int ssaa) {
    if (ssaa < 1) ssaa = 1;
    if (ssaa == 1) {
        dst = src;
        return;
    }
    dst.resize(src.w / ssaa, src.h / ssaa);
    const float inv = 1.0f / static_cast<float>(ssaa * ssaa);
    for (int y = 0; y < dst.h; ++y) {
        for (int x = 0; x < dst.w; ++x) {
            int acc[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < ssaa; ++sy) {
                for (int sx = 0; sx < ssaa; ++sx) {
                    const uint8_t* p = src.at(x * ssaa + sx, y * ssaa + sy);
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                }
            }
            uint8_t* d = dst.at(x, y);
            for (int c = 0; c < 4; ++c)
                d[c] = static_cast<uint8_t>(acc[c] * inv);
        }
    }
}

} // namespace ogui
