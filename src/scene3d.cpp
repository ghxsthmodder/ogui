// The 3D layer's CPU half: camera matrices, mesh helpers and the SceneView
// widget. Nothing here touches a GPU -- GlBackend consumes this data.
#include "ogui/scene3d.hpp"
#include "ogui/glbackend.hpp"
#include "ogui/rasterizer.hpp"
#include "ogui/style.hpp"

#include <algorithm>
#include <cmath>

namespace ogui {

// ---- camera ---------------------------------------------------------------
Matrix4x4 Camera3D::view() const {
    return Matrix4x4::lookAt(position, target, up);
}

Matrix4x4 Camera3D::projection(float aspect) const {
    return Matrix4x4::perspective(fovY, aspect, nearZ, farZ);
}

Matrix4x4 Camera3D::viewProjection(float aspect) const {
    return projection(aspect) * view();
}

// ---- mesh -----------------------------------------------------------------
MeshVertex MeshVertex::make(float x, float y, float z,
                            float nx, float ny, float nz,
                            float u, float v) {
    MeshVertex out;
    out.x = x; out.y = y; out.z = z;
    out.nx = nx; out.ny = ny; out.nz = nz;
    out.u = u; out.v = v;
    return out;
}

void MeshData::bounds(Vec3& min, Vec3& max) const {
    if (vertices.empty()) {
        min = max = Vec3{};
        return;
    }
    min = {vertices[0].x, vertices[0].y, vertices[0].z};
    max = min;
    for (const MeshVertex& v : vertices) {
        min.x = std::min(min.x, v.x);
        min.y = std::min(min.y, v.y);
        min.z = std::min(min.z, v.z);
        max.x = std::max(max.x, v.x);
        max.y = std::max(max.y, v.y);
        max.z = std::max(max.z, v.z);
    }
}

// ---- scene ----------------------------------------------------------------
void Scene3D::addMesh(int mesh, const Matrix4x4& model,
                      const Material& material) {
    if (mesh <= 0) return;   // an upload that failed: nothing to point at
    m_items.push_back(SceneItem{mesh, model, material});
}

void Scene3D::clear() {
    m_items.clear();
}

// ---- frustum --------------------------------------------------------------
namespace {

// Row i of a column-major matrix: the four elements whose row index is i.
FrustumPlane row(const std::array<float, 16>& m, int i) {
    return FrustumPlane{m[static_cast<size_t>(i)], m[static_cast<size_t>(4 + i)],
                        m[static_cast<size_t>(8 + i)], m[static_cast<size_t>(12 + i)]};
}

FrustumPlane add(const FrustumPlane& p, const FrustumPlane& q) {
    return FrustumPlane{p.a + q.a, p.b + q.b, p.c + q.c, p.d + q.d};
}

FrustumPlane sub(const FrustumPlane& p, const FrustumPlane& q) {
    return FrustumPlane{p.a - q.a, p.b - q.b, p.c - q.c, p.d - q.d};
}

} // namespace

Frustum Frustum::fromViewProjection(const Matrix4x4& viewProj) {
    // Gribb-Hartmann: the six clip-space conditions -w <= x,y,z <= w become six
    // world-space planes, inside where a*x + b*y + c*z + d >= 0. Normals are
    // deliberately left unnormalised: culling only needs the sign.
    const FrustumPlane r0 = row(viewProj.m, 0);
    const FrustumPlane r1 = row(viewProj.m, 1);
    const FrustumPlane r2 = row(viewProj.m, 2);
    const FrustumPlane r3 = row(viewProj.m, 3);
    Frustum out;
    out.planes[0] = add(r3, r0);   // left
    out.planes[1] = sub(r3, r0);   // right
    out.planes[2] = add(r3, r1);   // bottom
    out.planes[3] = sub(r3, r1);   // top
    out.planes[4] = add(r3, r2);   // near
    out.planes[5] = sub(r3, r2);   // far
    return out;
}

bool Frustum::intersects(const Vec3& min, const Vec3& max) const {
    for (const FrustumPlane& p : planes) {
        // The corner farthest along the normal: if even that one is behind the
        // plane, every corner is, and the whole box is outside.
        const Vec3 far{p.a >= 0.0f ? max.x : min.x,
                       p.b >= 0.0f ? max.y : min.y,
                       p.c >= 0.0f ? max.z : min.z};
        if (p.distance(far) < 0.0f) return false;
    }
    return true;
}

bool Frustum::contains(const Vec3& p) const {
    for (const FrustumPlane& pl : planes)
        if (pl.distance(p) < 0.0f) return false;
    return true;
}

void aabbCorners(const Vec3& min, const Vec3& max, Vec3 out[8]) {
    int i = 0;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                out[i++] = Vec3{x ? max.x : min.x, y ? max.y : min.y,
                                z ? max.z : min.z};
}

void transformAabb(const Matrix4x4& m, const Vec3& min, const Vec3& max,
                   Vec3& outMin, Vec3& outMax) {
    Vec3 corners[8];
    aabbCorners(min, max, corners);
    outMin = outMax = m.transformPoint(corners[0]);
    for (int i = 1; i < 8; ++i) {
        const Vec3 p = m.transformPoint(corners[i]);
        outMin.x = std::min(outMin.x, p.x);
        outMin.y = std::min(outMin.y, p.y);
        outMin.z = std::min(outMin.z, p.z);
        outMax.x = std::max(outMax.x, p.x);
        outMax.y = std::max(outMax.y, p.y);
        outMax.z = std::max(outMax.z, p.z);
    }
}

// ---- built-in geometry -----------------------------------------------------
MeshData cubeMesh(float size) {
    const float h = size * 0.5f;
    MeshData out;
    // One quad per face, its own normal so the lighting stays flat: the six
    // faces in order +x, -x, +y, -y, +z, -z.
    struct Face {
        Vec3 normal;
        Vec3 a, b, c, d;   // counter-clockwise seen from outside
    };
    const Face faces[6] = {
        {{ 1, 0, 0}, { h,-h, h}, { h,-h,-h}, { h, h,-h}, { h, h, h}},
        {{-1, 0, 0}, {-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}},
        {{ 0, 1, 0}, {-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}},
        {{ 0,-1, 0}, {-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}},
        {{ 0, 0, 1}, {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}},
        {{ 0, 0,-1}, { h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}},
    };
    for (const Face& f : faces) {
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        const Vec3 corners[4] = {f.a, f.b, f.c, f.d};
        for (int i = 0; i < 4; ++i) {
            const Vec3& p = corners[i];
            const float u = (i == 0 || i == 3) ? 0.0f : 1.0f;
            const float v = (i < 2) ? 1.0f : 0.0f;
            out.vertices.push_back(MeshVertex::make(
                p.x, p.y, p.z, f.normal.x, f.normal.y, f.normal.z, u, v));
        }
        out.indices.insert(out.indices.end(),
                           {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return out;
}

// ---- SceneView ------------------------------------------------------------
bool SceneView::submit(GlBackend& backend, int widthPx, int heightPx) {
    if (widthPx <= 0 || heightPx <= 0) return false;
    // The context behind the handles can be gone and replaced by another one
    // (Android does this on resume). The target then belongs to nothing that
    // exists, and there is no GL object left to delete: forget it and build a
    // new one in the backend that is asking.
    if (m_backend != &backend) {
        m_backend = &backend;
        m_target = 0;
        m_slot = 0;
        m_ownsTarget = false;
    }
    if (!m_ownsTarget || m_slot <= 0) {
        m_target = backend.createSceneTarget(widthPx, heightPx);
        if (m_target <= 0) return false;
        m_ownsTarget = true;
        m_w = widthPx;
        m_h = heightPx;
    } else if (m_w != widthPx || m_h != heightPx) {
        if (!backend.resizeSceneTarget(m_target, widthPx, heightPx)) return false;
        m_w = widthPx;
        m_h = heightPx;
    }
    m_slot = backend.sceneTargetSlot(m_target);
    return backend.renderSceneToTarget(m_scene, m_target);
}

void SceneView::onDraw(Rasterizer& r) const {
    const Rect b = bounds();
    if (b.w <= 0.0f || b.h <= 0.0f) return;

    if (m_slot == 0) {
        // No offscreen target yet: a flat panel keeps the layout readable in
        // the software renderer and in golden frames, which cannot show 3D.
        r.drawRect(b, palette().surface);
        return;
    }

    // The target's colour texture is GL bottom-up, the DrawList is y-down, so
    // the V axis is flipped here rather than in the backend (which would then
    // have to know how the texture is going to be displayed).
    r.drawRectUV(b, Rect{0.0f, 1.0f, 1.0f, -1.0f}, Color{255, 255, 255, 255},
                 m_slot);
}

} // namespace ogui
