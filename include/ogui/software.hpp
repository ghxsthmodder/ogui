// ogui/software.hpp : CPU rasterizer for a DrawList.
//
// Draws exactly what GlBackend::render() draws (same vertex transforms, same
// bilinear atlas sampling, same src-alpha blending) without a GL context, so
// headless environments can inspect or diff real pixels.
#pragma once

#include "ogui/drawlist.hpp"

#include <cstdint>
#include <map>
#include <vector>

namespace ogui {

// ------------------------------------------------------------------
// Texture the software renderer samples.
// texId 0 is GlBackend's white 1x1; font atlases use their (negative) slot
// ids, see Font::atlasTexture().
// ------------------------------------------------------------------
struct TexInfo {
    const uint8_t* data = nullptr;
    int w = 0, h = 0, rowBytes = 0;
    // Font atlases store A8 coverage; sampling them yields (1,1,1,a), the
    // same value the GL backend's swizzled GL_R8 texture produces.
    bool alphaOnly = false;
};

// ------------------------------------------------------------------
// Framebuffer: RGBA8, y grows downward (same convention as the DrawList).
// ------------------------------------------------------------------
struct Framebuffer {
    int w = 0, h = 0;
    std::vector<uint8_t> px;

    void resize(int nw, int nh);
    uint8_t* at(int x, int y);
    const uint8_t* at(int x, int y) const;
    const uint8_t* data() const { return px.data(); }
    size_t bytes() const { return px.size(); }
};

// ------------------------------------------------------------------
// SoftwareRenderer: DrawList -> Framebuffer.
// ------------------------------------------------------------------
class SoftwareRenderer {
public:
    // Registers every font atlas under its slot id (Font::atlasTexture()),
    // the way GlBackend::syncFonts() uploads them as GL textures.
    void syncFonts();
    // Registers every ImageManager image under its positive slot id, the
    // way GlBackend::syncImages() uploads them as GL textures.
    void syncImages();
    void setTexture(int texId, const TexInfo& tex);
    void clear(Framebuffer& fb, const Color& col) const;

    // Rasterizes `dl` into `fb`, scaling vertices and clips by `ssaa`
    // (`fb` must already be ssaa times larger than the logical size).
    // texIds with no registered texture are collected into `missing` (they
    // fall back to the white texture, i.e. flat coloured geometry).
    void render(const DrawList& dl, Framebuffer& fb, int ssaa = 1,
                std::vector<int>* missing = nullptr) const;

    // Box-filters the ssaa-times-larger `src` into `dst` (dst = src / ssaa).
    static void downsample(const Framebuffer& src, Framebuffer& dst, int ssaa);

    const std::map<int, TexInfo>& textures() const { return m_tex; }

private:
    std::map<int, TexInfo> m_tex;
};

} // namespace ogui
