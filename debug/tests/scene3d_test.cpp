// The 3D layer's CPU half -- the part that can be checked without a GPU.
//
// The depth-tested pass itself lives in GlBackend and is only exercised by
// ogui_gl_test (EGL); what is asserted here is the maths that pass depends on
// (projection, view, model composition) and the one thing SceneView puts into
// a DrawList: a texture slot covering its bounds, with the V axis flipped.
#include "ogui/rasterizer.hpp"
#include "ogui/scene3d.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";         \
            ++g_failures;                                                \
        }                                                                \
    } while (false)

#define CHECK_NEAR(a, b, eps)                                            \
    do {                                                                 \
        const double av = static_cast<double>(a);                        \
        const double bv = static_cast<double>(b);                        \
        if (std::fabs(av - bv) > (eps)) {                                \
            std::cerr << "FAIL " << __LINE__ << ": " #a " == " << av     \
                      << " expected " << bv << "\n";                     \
            ++g_failures;                                                \
        }                                                                \
    } while (false)

using namespace ogui;

void testMatrixBasics() {
    const Matrix4x4 id;
    const Vec3 p = id.transformPoint({1.0f, 2.0f, 3.0f});
    CHECK_NEAR(p.x, 1.0f, 1e-6);
    CHECK_NEAR(p.y, 2.0f, 1e-6);
    CHECK_NEAR(p.z, 3.0f, 1e-6);

    // RHS is applied first: translate(1,2,3) * scale(2) maps (1,1,1) to (3,4,5).
    const Matrix4x4 m = Matrix4x4::translation({1.0f, 2.0f, 3.0f}) *
                        Matrix4x4::scaling({2.0f, 2.0f, 2.0f});
    const Vec3 q = m.transformPoint({1.0f, 1.0f, 1.0f});
    CHECK_NEAR(q.x, 3.0f, 1e-6);
    CHECK_NEAR(q.y, 4.0f, 1e-6);
    CHECK_NEAR(q.z, 5.0f, 1e-6);

    // Multiplication is not commutative, and this order difference is the
    // whole point of documenting it.
    const Matrix4x4 n = Matrix4x4::scaling({2.0f, 2.0f, 2.0f}) *
                        Matrix4x4::translation({1.0f, 2.0f, 3.0f});
    const Vec3 r = n.transformPoint({1.0f, 1.0f, 1.0f});
    CHECK_NEAR(r.x, 4.0f, 1e-6);
    CHECK_NEAR(r.y, 6.0f, 1e-6);
    CHECK_NEAR(r.z, 8.0f, 1e-6);
}

void testRotations() {
    const float halfPi = 1.57079632679f;
    Vec3 v = Matrix4x4::rotationZ(halfPi).transformPoint({1.0f, 0.0f, 0.0f});
    CHECK_NEAR(v.x, 0.0f, 1e-5);
    CHECK_NEAR(v.y, 1.0f, 1e-5);

    v = Matrix4x4::rotationX(halfPi).transformPoint({0.0f, 1.0f, 0.0f});
    CHECK_NEAR(v.y, 0.0f, 1e-5);
    CHECK_NEAR(v.z, 1.0f, 1e-5);

    v = Matrix4x4::rotationY(halfPi).transformPoint({0.0f, 0.0f, 1.0f});
    CHECK_NEAR(v.x, 1.0f, 1e-5);
    CHECK_NEAR(v.z, 0.0f, 1e-5);
}

void testNormalize() {
    const Vec3 n = normalize({0.0f, 3.0f, 4.0f});
    CHECK_NEAR(n.x, 0.0f, 1e-6);
    CHECK_NEAR(n.y, 0.6f, 1e-6);
    CHECK_NEAR(n.z, 0.8f, 1e-6);

    // Zero length must not produce NaN: lookAt() feeds it a cross() that can be
    // zero when up is parallel to the view direction.
    const Vec3 z = normalize({0.0f, 0.0f, 0.0f});
    CHECK(z.x == 0.0f && z.y == 0.0f && z.z == 0.0f);
}

void testPerspective() {
    const float nearZ = 1.0f, farZ = 10.0f;
    const Matrix4x4 proj = Matrix4x4::perspective(90.0f, 1.0f, nearZ, farZ);

    // A point on the near plane lands on ndc z = -1, the far plane on +1.
    const Vec3 n = proj.transformPoint({0.0f, 0.0f, -nearZ});
    CHECK_NEAR(n.z, -1.0f, 1e-5);
    const Vec3 f = proj.transformPoint({0.0f, 0.0f, -farZ});
    CHECK_NEAR(f.z, 1.0f, 1e-5);

    // 90 degrees of vertical fov at aspect 1: the top edge of the frustum at
    // z = -1 is y = 1, which is ndc y = 1.
    const Vec3 top = proj.transformPoint({0.0f, 1.0f, -1.0f});
    CHECK_NEAR(top.y, 1.0f, 1e-5);

    // Twice the aspect means half the ndc x for the same world x.
    const Matrix4x4 wide = Matrix4x4::perspective(90.0f, 2.0f, nearZ, farZ);
    const Vec3 side = wide.transformPoint({1.0f, 0.0f, -1.0f});
    CHECK_NEAR(side.x, 0.5f, 1e-5);
}

void testLookAtAndCamera() {
    const Matrix4x4 view = Matrix4x4::lookAt({0.0f, 0.0f, 5.0f}, {}, {0.0f, 1.0f, 0.0f});
    const Vec3 origin = view.transformPoint({});
    CHECK_NEAR(origin.x, 0.0f, 1e-6);
    CHECK_NEAR(origin.y, 0.0f, 1e-6);
    CHECK_NEAR(origin.z, -5.0f, 1e-6);   // the camera looks down -z

    // A point above the eye stays above the axis, and one to the right stays
    // to the right.
    const Vec3 up = view.transformPoint({0.0f, 1.0f, 0.0f});
    CHECK_NEAR(up.y, 1.0f, 1e-6);
    const Vec3 right = view.transformPoint({1.0f, 0.0f, 0.0f});
    CHECK_NEAR(right.x, 1.0f, 1e-6);

    // up parallel to the view direction: the fallback basis keeps the matrix
    // finite instead of collapsing to zeros.
    const Matrix4x4 degenerate =
        Matrix4x4::lookAt({0.0f, 5.0f, 0.0f}, {}, {0.0f, 1.0f, 0.0f});
    const Vec3 d = degenerate.transformPoint({0.0f, 0.0f, -1.0f});
    CHECK(std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z));

    // The camera's own viewProjection must put its target in the middle of the
    // screen: x and y of ndc are zero, and it is in front of the camera.
    Camera3D cam;
    cam.position = {2.0f, 3.0f, 7.0f};
    cam.target = {1.0f, -1.0f, 0.5f};
    const Vec3 mid = cam.viewProjection(16.0f / 9.0f).transformPoint(cam.target);
    CHECK_NEAR(mid.x, 0.0f, 1e-4);
    CHECK_NEAR(mid.y, 0.0f, 1e-4);
    CHECK(mid.z > -1.0f && mid.z < 1.0f);
}

void testMeshBounds() {
    MeshData mesh;
    mesh.vertices.push_back(MeshVertex::make(-1.0f, -2.0f, -3.0f, 0, 1, 0));
    mesh.vertices.push_back(MeshVertex::make(4.0f, 5.0f, 6.0f, 0, 1, 0));
    mesh.indices = {0, 1, 0};
    CHECK(mesh.triangleCount() == 1);

    Vec3 min, max;
    mesh.bounds(min, max);
    CHECK_NEAR(min.x, -1.0f, 1e-6);
    CHECK_NEAR(min.y, -2.0f, 1e-6);
    CHECK_NEAR(max.z, 6.0f, 1e-6);

    MeshData empty;
    empty.bounds(min, max);
    CHECK(min.x == 0.0f && max.x == 0.0f);
}

void testScene() {
    Scene3D scene;
    CHECK(scene.empty());

    scene.addMesh(3, Matrix4x4::translation({1.0f, 0.0f, 0.0f}));
    scene.addMesh(0, Matrix4x4{});    // a failed upload: dropped, not pointed at
    CHECK(scene.itemCount() == 1);
    CHECK(scene.items()[0].mesh == 3);
    CHECK(scene.items()[0].material.texId == 0);

    scene.clear();
    CHECK(scene.empty());
}

void testSceneViewDraws() {
    SceneView view;
    view.setBounds({10.0f, 20.0f, 100.0f, 50.0f});

    // Without a target the widget still covers its box, so a software frame
    // shows a panel instead of a hole.
    {
        Rasterizer r;
        r.beginFrame({0.0f, 0.0f, 1000.0f, 1000.0f});
        view.onDraw(r);
        const DrawList& dl = r.drawList();
        CHECK(dl.cmds.size() == 1);
        if (!dl.cmds.empty()) CHECK(dl.cmds[0].texId == 0);
        CHECK(dl.indices.size() == 6);
        CHECK(dl.vertices.size() == 4);
    }

    // With a slot it samples that texture over exactly its bounds.
    view.setTextureSlot(1000007);
    CHECK(view.textureSlot() == 1000007);
    view.setTarget(4);
    CHECK(view.target() == 4);

    Rasterizer r;
    r.beginFrame({0.0f, 0.0f, 1000.0f, 1000.0f});
    view.onDraw(r);
    const DrawList& dl = r.drawList();
    CHECK(dl.cmds.size() == 1);
    if (!dl.cmds.empty()) CHECK(dl.cmds[0].texId == 1000007);
    if (dl.cmds.size() == 1) CHECK(dl.cmds[0].indexCount == 6);
    CHECK(dl.vertices.size() == 4);
    if (dl.vertices.size() == 4) {
        CHECK_NEAR(dl.vertices[0].x, 10.0f, 1e-5);
        CHECK_NEAR(dl.vertices[0].y, 20.0f, 1e-5);
        CHECK_NEAR(dl.vertices[2].x, 110.0f, 1e-5);
        CHECK_NEAR(dl.vertices[2].y, 70.0f, 1e-5);
        // GL targets are bottom-up; the UV is flipped for the y-down list.
        CHECK_NEAR(dl.vertices[0].v, 1.0f, 1e-5);
        CHECK_NEAR(dl.vertices[2].v, 0.0f, 1e-5);
        CHECK_NEAR(dl.vertices[0].u, 0.0f, 1e-5);
        CHECK_NEAR(dl.vertices[2].u, 1.0f, 1e-5);
    }
}

// The cube the demo spins is generated here, so its winding and its per-face
// normals are data this test can pin down: a face with a flipped normal is a
// face that goes black in the lit pass, and one with the wrong winding vanishes
// under back-face culling.
void testCubeMesh() {
    const MeshData cube = cubeMesh(2.0f);
    CHECK(cube.vertices.size() == 24);   // 4 per face: flat normals, no sharing
    CHECK(cube.indices.size() == 36);
    CHECK(cube.triangleCount() == 12);

    Vec3 mn, mx;
    cube.bounds(mn, mx);
    CHECK_NEAR(mn.x, -1.0f, 1e-6);
    CHECK_NEAR(mn.y, -1.0f, 1e-6);
    CHECK_NEAR(mn.z, -1.0f, 1e-6);
    CHECK_NEAR(mx.x, 1.0f, 1e-6);
    CHECK_NEAR(mx.y, 1.0f, 1e-6);
    CHECK_NEAR(mx.z, 1.0f, 1e-6);

    for (uint32_t i : cube.indices) CHECK(i < cube.vertices.size());

    // Six distinct outward normals, one per quad, each at the centre of its
    // face -- and the face's vertices all sit on the plane it describes.
    int normals = 0;
    for (size_t face = 0; face < 6; ++face) {
        const MeshVertex& v0 = cube.vertices[face * 4];
        const MeshVertex& v1 = cube.vertices[face * 4 + 1];
        const MeshVertex& v2 = cube.vertices[face * 4 + 2];
        const Vec3 n{v0.nx, v0.ny, v0.nz};
        CHECK_NEAR(std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z), 1.0f, 1e-5);
        CHECK(n.x != 0.0f || n.y != 0.0f || n.z != 0.0f);
        ++normals;
        // Counter-clockwise from outside: the geometric normal of the first
        // triangle must point the same way as the vertex normal.
        const Vec3 a{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
        const Vec3 b{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
        const Vec3 cross{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                         a.x * b.y - a.y * b.x};
        CHECK(cross.x * n.x + cross.y * n.y + cross.z * n.z > 0.0f);
    }
    CHECK(normals == 6);
}

// Culling decides what the GPU is never asked to draw, so both directions have
// to be right: visible geometry kept, geometry behind the camera dropped.
void testFrustum() {
    Camera3D cam;
    cam.position = {0.0f, 0.0f, 5.0f};
    cam.target = {0.0f, 0.0f, 0.0f};
    cam.fovY = 60.0f;
    cam.nearZ = 0.1f;
    cam.farZ = 20.0f;
    const Frustum f = Frustum::fromViewProjection(cam.viewProjection(1.5f));

    CHECK(f.contains({0.0f, 0.0f, 0.0f}));
    CHECK(f.intersects({-1, -1, -1}, {1, 1, 1}));
    CHECK(f.contains({0.0f, 0.0f, 4.0f}));    // between the camera and the origin

    // Behind the camera: nowhere near the view volume.
    CHECK(!f.intersects({-1, -1, 6}, {1, 1, 8}));
    CHECK(!f.contains({0.0f, 0.0f, 10.0f}));
    // Past the far plane.
    CHECK(!f.intersects({-1, -1, -40}, {1, 1, -20}));
    // Well off to the side.
    CHECK(!f.intersects({40, -1, -1}, {42, 1, 1}));
    // A box straddling the left plane is kept (conservative).
    CHECK(f.intersects({-4.5f, -0.5f, -0.5f}, {-3.5f, 0.5f, 0.5f}));

    // Rotation-aware bounding: a 45-degree spin of a unit box grows the AABB
    // instead of keeping the old one -- the cull must never shrink a box.
    Vec3 mn, mx;
    transformAabb(Matrix4x4::rotationY(0.7853981f), Vec3{-1, -1, -1}, Vec3{1, 1, 1},
                  mn, mx);
    CHECK_NEAR(mn.x, -1.4142136f, 1e-5);
    CHECK_NEAR(mx.z, 1.4142136f, 1e-5);
}

}  // namespace

int main() {
    testMatrixBasics();
    testRotations();
    testNormalize();
    testPerspective();
    testLookAtAndCamera();
    testMeshBounds();
    testCubeMesh();
    testFrustum();
    testScene();
    testSceneViewDraws();

    if (g_failures == 0) {
        std::cout << "scene3d: all checks passed\n";
        return 0;
    }
    std::cerr << "scene3d: " << g_failures << " failure(s)\n";
    return 1;
}
