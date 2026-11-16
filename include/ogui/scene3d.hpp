// ogui/scene3d.hpp : the 3D side of the renderer.
//
// The UI is a 2D triangle soup (see drawlist.hpp). This header is the second
// layer: a camera, lit meshes and a depth-tested pass, drawn by
// GlBackend::renderScene() before (or between) UI frames. Everything here is
// GL-free data, so the host owns the context and the library keeps its
// dependency-free contract.
//
// Two ways to put 3D on screen, matching the two the game needs:
//
//   1. World pass:  backend.renderScene(scene, w, h)  ->  backend.render(ui)
//   2. In a widget: SceneView holds a scene + an offscreen target; the host
//      calls backend.renderSceneToTarget(view.scene(), view.target()) once per
//      frame and the widget samples the target's colour texture in the layout.
//
// Both passes restore the host's GL state, and neither clears the default
// framebuffer: the host owns that depth buffer, which is what lets the game
// draw its own world first with the same depth buffer the UI's 3D layer uses.
//
// Culling is CPU-side and per item: GlBackend test each item's world AABB
// against the camera frustum (Frustum below) and skips what cannot be on
// screen, which is the cheap half of "a farm has a lot of objects".
#pragma once

#include "ogui/api.hpp"
#include "ogui/widget.hpp"

#include <cstdint>
#include <vector>

namespace ogui {

class GlBackend;

// ------------------------------------------------------------------
// Camera3D: a right-handed perspective camera (looks down its own -z).
// ------------------------------------------------------------------
struct Camera3D {
    Vec3 position{0.0f, 0.0f, 3.0f};
    Vec3 target{0.0f, 0.0f, 0.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
    float fovY = 60.0f;      // vertical field of view, degrees
    float nearZ = 0.1f;
    float farZ = 100.0f;

    Matrix4x4 view() const;
    Matrix4x4 projection(float aspect) const;
    Matrix4x4 viewProjection(float aspect) const;
};

// ------------------------------------------------------------------
// Geometry: position + normal + uv, counter-clockwise front faces.
// ------------------------------------------------------------------
struct MeshVertex {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;
    float u = 0.0f, v = 0.0f;

    static MeshVertex make(float x, float y, float z,
                           float nx, float ny, float nz,
                           float u = 0.0f, float v = 0.0f);
};

// A CPU-side mesh. Upload it once through GlBackend::uploadMesh(); the scene
// items then name the returned handle.
struct MeshData {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;

    size_t triangleCount() const { return indices.size() / 3; }
    bool empty() const { return indices.empty(); }
    // Axis-aligned bounds (min/max), for framing a camera or a cheap cull.
    void bounds(Vec3& min, Vec3& max) const;
};

// A cube centred on the origin, 24 vertices so each face carries its own
// normal, counter-clockwise from outside. The smallest thing worth rendering,
// and the mesh the demo spins.
MeshData cubeMesh(float size = 1.0f);

// ------------------------------------------------------------------
// Frustum: the six planes of a view-projection matrix, in world space, each
// stored as (a, b, c, d) with the inside on the side a*x + b*y + c*z + d > 0.
// Used for per-item culling; it is plain data so it can be unit tested.
// ------------------------------------------------------------------
struct FrustumPlane {
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
    float distance(const Vec3& p) const { return a * p.x + b * p.y + c * p.z + d; }
};

struct Frustum {
    FrustumPlane planes[6];   // left, right, bottom, top, near, far

    static Frustum fromViewProjection(const Matrix4x4& viewProj);
    // Conservative: true when any part of the box may be visible (touching
    // counts). A box that is entirely behind one plane is culled.
    bool intersects(const Vec3& min, const Vec3& max) const;
    bool contains(const Vec3& p) const;
};

// The eight corners of an AABB, in the order (-x-y-z), (+x-y-z), ... .
void aabbCorners(const Vec3& min, const Vec3& max, Vec3 out[8]);
// Transforms a box and returns the AABB of the result (rotation-aware, so it
// stays conservative instead of wrongly shrinking).
void transformAabb(const Matrix4x4& m, const Vec3& min, const Vec3& max,
                   Vec3& outMin, Vec3& outMax);

// ------------------------------------------------------------------
// Lighting: one directional light plus a flat ambient term. Enough to read the
// shape of a model; point/spot lights belong in the game's own renderer.
// ------------------------------------------------------------------
struct DirectionalLight {
    // Direction the light travels, in world space (not the direction to the
    // light). The default comes from above, slightly behind the camera.
    Vec3 direction{-0.4f, -1.0f, -0.6f};
    Color color{255, 255, 255, 255};
    float ambient = 0.25f;   // 0 = only the lit side is visible
};

struct Material {
    Color tint{255, 255, 255, 255};
    // 0 = flat colour. A positive id resolves the same way a DrawCmd texId
    // does: ImageManager slot, SceneView target slot, or a raw GL texture name.
    int texId = 0;
};

struct SceneItem {
    int mesh = 0;            // handle from GlBackend::uploadMesh()
    Matrix4x4 model{};
    Material material{};
};

// ------------------------------------------------------------------
// Scene3D: camera + light + the list of meshes to draw.
// ------------------------------------------------------------------
class Scene3D {
public:
    Camera3D camera;
    DirectionalLight light;
    // Colour an offscreen target is cleared to (alpha 0 = leave it alone).
    // renderScene() into a host framebuffer never clears.
    Color background{18, 20, 24, 255};

    void addMesh(int mesh, const Matrix4x4& model,
                 const Material& material = Material{});
    void clear();
    bool empty() const { return m_items.empty(); }
    size_t itemCount() const { return m_items.size(); }
    const std::vector<SceneItem>& items() const { return m_items; }

private:
    std::vector<SceneItem> m_items;
};

// ------------------------------------------------------------------
// SceneView: shows a scene inside the 2D layout.
//
// It draws one textured quad, sampled from an offscreen target that the host
// fills through GlBackend. Two ways to drive it:
//
//   * submit(backend, pxW, pxH) -- the widget owns the target: it creates or
//     resizes it to the requested pixel size, renders its own scene into it and
//     points the quad at the resulting slot. This is the one-liner a host needs
//     per frame, and the one the XML tag uses, because nothing but the widget
//     knows the box it ended up with.
//   * setTarget()/setTextureSlot() -- the host owns the target (a minimap, a
//     second view of the world) and only tells the widget where to sample.
//
// Without either it draws a flat placeholder, so the software renderer and the
// golden frames still have something in that box.
// ------------------------------------------------------------------
class SceneView : public Widget {
public:
    SceneView() = default;
    virtual ~SceneView() override = default;

    // The scene this view shows. The host reads it to render the target, so a
    // SceneView is both the owner of the scene and the widget that displays it.
    Scene3D& scene() { return m_scene; }
    const Scene3D& scene() const { return m_scene; }

    // Backend target handle (GlBackend::createSceneTarget) and the DrawList
    // texture slot that target's colour texture resolves to
    // (GlBackend::sceneTargetSlot). Both are the host's to set: the widget
    // never touches GL.
    void setTarget(int handle) { m_target = handle; }
    int target() const { return m_target; }
    void setTextureSlot(int slot) { m_slot = slot; }
    int textureSlot() const { return m_slot; }

    // Renders m_scene into an owned target of the given pixel size (creating or
    // resizing it as the layout changes) and draws that texture next frame.
    // Returns false when the backend cannot provide a target; the widget then
    // keeps whatever it was showing.
    bool submit(GlBackend& backend, int widthPx, int heightPx);
    bool ownsTarget() const { return m_ownsTarget; }

    void onDraw(Rasterizer& r) const override;

private:
    Scene3D m_scene;
    int m_target = 0;
    int m_slot = 0;
    int m_w = 0, m_h = 0;
    bool m_ownsTarget = false;
};

} // namespace ogui
