#pragma once
#include "ogui/api.hpp"
#include <vector>
#include <cstdint>

namespace ogui {

// ------------------------------------------------------------------
// DrawCmd: one batch of triangles sharing clip rect + texture.
// ------------------------------------------------------------------
struct DrawCmd {
    Rect clip{};
    int texId = 0;              // 0 = white/untextured
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

// ------------------------------------------------------------------
// DrawList: retained 2D triangle soup produced by the Rasterizer.
// The GL backend consumes it directly; a CPU rasterizer can too.
// ------------------------------------------------------------------
class DrawList {
public:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<DrawCmd> cmds;

    void clear();
    bool empty() const { return indices.empty(); }

    // Appends a quad (2 triangles). Starts a new cmd when clip/tex differ.
    void addQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                 const Vertex& v3, const Rect& clip, int texId);

    // Appends an arbitrary triangle list (already triangulated).
    void addTriangles(const Vertex* verts, size_t count,
                      const uint32_t* idx, size_t idxCount,
                      const Rect& clip, int texId);

    // Index count of the last cmd (0 when there is none).
    size_t lastCmdIndexCount() const;

private:
    DrawCmd* currentCmd(const Rect& clip, int texId);
};

} // namespace ogui
