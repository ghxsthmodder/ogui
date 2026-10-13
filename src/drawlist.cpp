#include "ogui/drawlist.hpp"
#include <algorithm>

namespace ogui {

void DrawList::clear() {
    vertices.clear();
    indices.clear();
    cmds.clear();
}

void DrawList::swap(DrawList& o) noexcept {
    vertices.swap(o.vertices);
    indices.swap(o.indices);
    cmds.swap(o.cmds);
}

DrawCmd* DrawList::currentCmd(const Rect& clip, int texId) {
    if (!cmds.empty()) {
        DrawCmd& last = cmds.back();
        const bool sameClip = last.clip.x == clip.x && last.clip.y == clip.y &&
                              last.clip.w == clip.w && last.clip.h == clip.h;
        if (sameClip && last.texId == texId) {
            return &last;
        }
    }
    DrawCmd cmd;
    cmd.clip = clip;
    cmd.texId = texId;
    cmd.indexOffset = static_cast<uint32_t>(indices.size());
    cmd.indexCount = 0;
    cmds.push_back(cmd);
    return &cmds.back();
}

void DrawList::addQuad(const Vertex& v0, const Vertex& v1, const Vertex& v2,
                       const Vertex& v3, const Rect& clip, int texId) {
    // Cull degenerate quads early (keeps batches tight).
    if ((v0.x == v1.x && v1.x == v2.x && v2.x == v3.x) ||
        (v0.y == v1.y && v1.y == v2.y && v2.y == v3.y)) {
        return;
    }

    DrawCmd* cmd = currentCmd(clip, texId);
    uint32_t base = static_cast<uint32_t>(vertices.size());

    vertices.push_back(v0);
    vertices.push_back(v1);
    vertices.push_back(v2);
    vertices.push_back(v3);

    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);

    cmd->indexCount += 6;
}

void DrawList::addTriangles(const Vertex* verts, size_t count,
                            const uint32_t* idx, size_t idxCount,
                            const Rect& clip, int texId) {
    if (!verts || !idx || count == 0 || idxCount == 0) return;

    DrawCmd* cmd = currentCmd(clip, texId);
    uint32_t base = static_cast<uint32_t>(vertices.size());

    vertices.insert(vertices.end(), verts, verts + count);
    for (size_t i = 0; i < idxCount; ++i) {
        indices.push_back(base + idx[i]);
    }
    cmd->indexCount += static_cast<uint32_t>(idxCount);
}

size_t DrawList::lastCmdIndexCount() const {
    return cmds.empty() ? 0 : cmds.back().indexCount;
}

} // namespace ogui
