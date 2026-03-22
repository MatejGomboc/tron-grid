/*
    Copyright (C) 2026 Matej Gomboc https://github.com/MatejGomboc/tron-grid

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.
*/

#include "test_fixture/test_fixture.hpp"
#include <math/mat.hpp>
#include <math/projection.hpp>
#include <math/quat.hpp>
#include <math/vec.hpp>
#include <cmath>

//! Approximate equality for floating-point comparisons.
static bool approx(float a, float b, float epsilon = 1e-5f)
{
    return std::fabs(a - b) < epsilon;
}

static bool approxVec3(const MathLib::Vec3& a, const MathLib::Vec3& b, float epsilon = 1e-5f)
{
    return approx(a.x, b.x, epsilon) && approx(a.y, b.y, epsilon) && approx(a.z, b.z, epsilon);
}

static bool approxVec4(const MathLib::Vec4& a, const MathLib::Vec4& b, float epsilon = 1e-5f)
{
    return approx(a.x, b.x, epsilon) && approx(a.y, b.y, epsilon) && approx(a.z, b.z, epsilon) && approx(a.w, b.w, epsilon);
}

static bool approxMat4(const MathLib::Mat4& a, const MathLib::Mat4& b, float epsilon = 1e-5f)
{
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!approx(a.m[col][row], b.m[col][row], epsilon)) {
                return false;
            }
        }
    }
    return true;
}

// ── Vec2 ──

TEST_CASE(vec2_default_zero)
{
    MathLib::Vec2 v;
    TEST_CHECK(v.x == 0.0f && v.y == 0.0f);
}

TEST_CASE(vec2_dot)
{
    MathLib::Vec2 a{1.0f, 2.0f};
    MathLib::Vec2 b{3.0f, 4.0f};
    TEST_CHECK(approx(a.dot(b), 11.0f));
}

TEST_CASE(vec2_length)
{
    MathLib::Vec2 v{3.0f, 4.0f};
    TEST_CHECK(approx(v.length(), 5.0f));
}

TEST_CASE(vec2_normalised)
{
    MathLib::Vec2 v{3.0f, 4.0f};
    MathLib::Vec2 n = v.normalised();
    TEST_CHECK(approx(n.length(), 1.0f));
}

TEST_CASE(vec2_normalised_zero)
{
    MathLib::Vec2 v{0.0f, 0.0f};
    MathLib::Vec2 n = v.normalised();
    TEST_CHECK(n.x == 0.0f && n.y == 0.0f);
}

TEST_CASE(vec2_arithmetic)
{
    MathLib::Vec2 a{1.0f, 2.0f};
    MathLib::Vec2 b{3.0f, 4.0f};
    MathLib::Vec2 sum = a + b;
    MathLib::Vec2 diff = a - b;
    MathLib::Vec2 scaled = a * 2.0f;
    TEST_CHECK((sum == MathLib::Vec2{4.0f, 6.0f}));
    TEST_CHECK((diff == MathLib::Vec2{-2.0f, -2.0f}));
    TEST_CHECK((scaled == MathLib::Vec2{2.0f, 4.0f}));
}

// ── Vec3 ──

TEST_CASE(vec3_default_zero)
{
    MathLib::Vec3 v;
    TEST_CHECK(v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
}

TEST_CASE(vec3_dot)
{
    MathLib::Vec3 a{1.0f, 2.0f, 3.0f};
    MathLib::Vec3 b{4.0f, 5.0f, 6.0f};
    TEST_CHECK(approx(a.dot(b), 32.0f));
}

TEST_CASE(vec3_cross)
{
    MathLib::Vec3 x{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 y{0.0f, 1.0f, 0.0f};
    MathLib::Vec3 z = x.cross(y);
    TEST_CHECK(approxVec3(z, {0.0f, 0.0f, 1.0f}));
}

TEST_CASE(vec3_cross_anticommutative)
{
    MathLib::Vec3 a{1.0f, 2.0f, 3.0f};
    MathLib::Vec3 b{4.0f, 5.0f, 6.0f};
    MathLib::Vec3 ab = a.cross(b);
    MathLib::Vec3 ba = b.cross(a);
    TEST_CHECK(approxVec3(ab, -ba));
}

TEST_CASE(vec3_length)
{
    MathLib::Vec3 v{1.0f, 2.0f, 2.0f};
    TEST_CHECK(approx(v.length(), 3.0f));
}

TEST_CASE(vec3_normalised)
{
    MathLib::Vec3 v{0.0f, 3.0f, 4.0f};
    MathLib::Vec3 n = v.normalised();
    TEST_CHECK(approx(n.length(), 1.0f));
    TEST_CHECK(approx(n.x, 0.0f));
}

TEST_CASE(vec3_normalised_zero)
{
    MathLib::Vec3 v{0.0f, 0.0f, 0.0f};
    MathLib::Vec3 n = v.normalised();
    TEST_CHECK(n.x == 0.0f && n.y == 0.0f && n.z == 0.0f);
}

TEST_CASE(vec3_arithmetic)
{
    MathLib::Vec3 a{1.0f, 2.0f, 3.0f};
    MathLib::Vec3 b{4.0f, 5.0f, 6.0f};
    TEST_CHECK(((a + b) == MathLib::Vec3{5.0f, 7.0f, 9.0f}));
    TEST_CHECK(((a - b) == MathLib::Vec3{-3.0f, -3.0f, -3.0f}));
    TEST_CHECK(((a * 2.0f) == MathLib::Vec3{2.0f, 4.0f, 6.0f}));
    TEST_CHECK(((-a) == MathLib::Vec3{-1.0f, -2.0f, -3.0f}));
}

TEST_CASE(vec3_compound_assignment)
{
    MathLib::Vec3 v{1.0f, 2.0f, 3.0f};
    v += MathLib::Vec3{4.0f, 5.0f, 6.0f};
    TEST_CHECK((v == MathLib::Vec3{5.0f, 7.0f, 9.0f}));
    v -= MathLib::Vec3{1.0f, 1.0f, 1.0f};
    TEST_CHECK((v == MathLib::Vec3{4.0f, 6.0f, 8.0f}));
    v *= 0.5f;
    TEST_CHECK((v == MathLib::Vec3{2.0f, 3.0f, 4.0f}));
}

// ── Vec4 ──

TEST_CASE(vec4_from_vec3)
{
    MathLib::Vec3 v{1.0f, 2.0f, 3.0f};
    MathLib::Vec4 v4 = MathLib::Vec4::fromVec3(v, 1.0f);
    TEST_CHECK((v4 == MathLib::Vec4{1.0f, 2.0f, 3.0f, 1.0f}));
}

TEST_CASE(vec4_xyz)
{
    MathLib::Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};
    TEST_CHECK((v.xyz() == MathLib::Vec3{1.0f, 2.0f, 3.0f}));
}

TEST_CASE(vec4_dot)
{
    MathLib::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    MathLib::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    TEST_CHECK(approx(a.dot(b), 70.0f));
}

// ── Mat4 ──

TEST_CASE(mat4_identity)
{
    MathLib::Mat4 id = MathLib::Mat4::identity();
    MathLib::Vec4 v{1.0f, 2.0f, 3.0f, 1.0f};
    TEST_CHECK(approxVec4(id * v, v));
}

TEST_CASE(mat4_identity_multiply)
{
    MathLib::Mat4 id = MathLib::Mat4::identity();
    MathLib::Mat4 result = id * id;
    TEST_CHECK(approxMat4(result, id));
}

TEST_CASE(mat4_translate)
{
    MathLib::Mat4 t = MathLib::Mat4::translate({1.0f, 2.0f, 3.0f});
    MathLib::Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = t * origin;
    TEST_CHECK(approxVec4(result, {1.0f, 2.0f, 3.0f, 1.0f}));
}

TEST_CASE(mat4_translate_direction_unaffected)
{
    MathLib::Mat4 t = MathLib::Mat4::translate({10.0f, 20.0f, 30.0f});
    MathLib::Vec4 dir{1.0f, 0.0f, 0.0f, 0.0f}; // w=0 is a direction
    MathLib::Vec4 result = t * dir;
    TEST_CHECK(approxVec4(result, {1.0f, 0.0f, 0.0f, 0.0f}));
}

TEST_CASE(mat4_scale)
{
    MathLib::Mat4 s = MathLib::Mat4::scale({2.0f, 3.0f, 4.0f});
    MathLib::Vec4 v{1.0f, 1.0f, 1.0f, 1.0f};
    MathLib::Vec4 result = s * v;
    TEST_CHECK(approxVec4(result, {2.0f, 3.0f, 4.0f, 1.0f}));
}

TEST_CASE(mat4_rotate_z_90)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 r = MathLib::Mat4::rotate({0.0f, 0.0f, 1.0f}, PI / 2.0f);
    MathLib::Vec4 v{1.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = r * v;
    TEST_CHECK(approxVec4(result, {0.0f, 1.0f, 0.0f, 1.0f}, 1e-4f));
}

TEST_CASE(mat4_transpose)
{
    MathLib::Mat4 m = MathLib::Mat4::identity();
    m.m[3][0] = 5.0f; // Translation X in column-major
    MathLib::Mat4 t = m.transposed();
    TEST_CHECK(approx(t.m[0][3], 5.0f));
    TEST_CHECK(approx(t.m[3][0], 0.0f));
}

TEST_CASE(mat4_multiply_associative)
{
    MathLib::Mat4 a = MathLib::Mat4::translate({1.0f, 0.0f, 0.0f});
    MathLib::Mat4 b = MathLib::Mat4::scale({2.0f, 2.0f, 2.0f});
    MathLib::Mat4 c = MathLib::Mat4::translate({0.0f, 0.0f, 3.0f});
    MathLib::Mat4 ab_c = (a * b) * c;
    MathLib::Mat4 a_bc = a * (b * c);
    TEST_CHECK(approxMat4(ab_c, a_bc));
}

TEST_CASE(mat4_data_pointer)
{
    MathLib::Mat4 id = MathLib::Mat4::identity();
    const float* ptr = id.data();
    TEST_CHECK(approx(ptr[0], 1.0f)); // m[0][0]
    TEST_CHECK(approx(ptr[5], 1.0f)); // m[1][1]
    TEST_CHECK(approx(ptr[10], 1.0f)); // m[2][2]
    TEST_CHECK(approx(ptr[15], 1.0f)); // m[3][3]
    TEST_CHECK(approx(ptr[1], 0.0f)); // m[0][1]
}

// ── Quat ──

TEST_CASE(quat_identity)
{
    MathLib::Quat q = MathLib::Quat::identity();
    TEST_CHECK(q.w == 1.0f && q.x == 0.0f && q.y == 0.0f && q.z == 0.0f);
}

TEST_CASE(quat_from_axis_angle_identity)
{
    MathLib::Quat q = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.0f);
    TEST_CHECK(approx(q.w, 1.0f));
    TEST_CHECK(approx(q.x, 0.0f));
    TEST_CHECK(approx(q.y, 0.0f));
    TEST_CHECK(approx(q.z, 0.0f));
}

TEST_CASE(quat_rotate_vec3)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat q = MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, PI / 2.0f);
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 result = q.rotate(v);
    TEST_CHECK(approxVec3(result, {0.0f, 1.0f, 0.0f}, 1e-4f));
}

TEST_CASE(quat_to_mat4_matches_rotate)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Vec3 axis{0.0f, 1.0f, 0.0f};
    float angle = PI / 3.0f;

    MathLib::Mat4 from_quat = MathLib::Quat::fromAxisAngle(axis, angle).toMat4();
    MathLib::Mat4 from_mat = MathLib::Mat4::rotate(axis, angle);

    TEST_CHECK(approxMat4(from_quat, from_mat, 1e-4f));
}

TEST_CASE(quat_multiply_combines_rotations)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat q1 = MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, PI / 2.0f);
    MathLib::Quat q2 = MathLib::Quat::fromAxisAngle({0.0f, 0.0f, 1.0f}, PI / 2.0f);
    MathLib::Quat combined = q1 * q2; // 180 degrees around Z
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 result = combined.rotate(v);
    TEST_CHECK(approxVec3(result, {-1.0f, 0.0f, 0.0f}, 1e-4f));
}

TEST_CASE(quat_normalised)
{
    MathLib::Quat q{2.0f, 0.0f, 0.0f, 0.0f};
    MathLib::Quat n = q.normalised();
    TEST_CHECK(approx(n.length(), 1.0f));
    TEST_CHECK(approx(n.w, 1.0f));
}

TEST_CASE(quat_normalised_zero)
{
    MathLib::Quat q{0.0f, 0.0f, 0.0f, 0.0f};
    MathLib::Quat n = q.normalised();
    TEST_CHECK(n == MathLib::Quat::identity());
}

TEST_CASE(quat_slerp_endpoints)
{
    MathLib::Quat a = MathLib::Quat::identity();
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat b = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, PI / 2.0f);

    MathLib::Quat at0 = MathLib::Quat::slerp(a, b, 0.0f);
    MathLib::Quat at1 = MathLib::Quat::slerp(a, b, 1.0f);

    TEST_CHECK(approx(at0.w, a.w) && approx(at0.x, a.x) && approx(at0.y, a.y) && approx(at0.z, a.z));
    TEST_CHECK(approx(at1.w, b.w, 1e-4f) && approx(at1.x, b.x, 1e-4f) && approx(at1.y, b.y, 1e-4f) && approx(at1.z, b.z, 1e-4f));
}

TEST_CASE(quat_slerp_midpoint)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat a = MathLib::Quat::identity();
    MathLib::Quat b = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, PI / 2.0f);
    MathLib::Quat mid = MathLib::Quat::slerp(a, b, 0.5f);
    // Midpoint should be a 45-degree rotation around Y
    MathLib::Quat expected = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, PI / 4.0f);
    TEST_CHECK(approx(mid.w, expected.w, 1e-4f) && approx(mid.y, expected.y, 1e-4f));
}

// ── Projection ──

TEST_CASE(perspective_near_plane_maps_to_zero)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 proj = MathLib::perspective(PI / 2.0f, 1.0f, 0.1f, 100.0f);
    // A point on the near plane (z = -near in view space, right-handed)
    MathLib::Vec4 near_point{0.0f, 0.0f, -0.1f, 1.0f};
    MathLib::Vec4 clip = proj * near_point;
    float ndc_z = clip.z / clip.w;
    TEST_CHECK(approx(ndc_z, 0.0f, 1e-4f));
}

TEST_CASE(perspective_far_plane_maps_to_one)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 proj = MathLib::perspective(PI / 2.0f, 1.0f, 0.1f, 100.0f);
    // A point on the far plane (z = -far in view space)
    MathLib::Vec4 far_point{0.0f, 0.0f, -100.0f, 1.0f};
    MathLib::Vec4 clip = proj * far_point;
    float ndc_z = clip.z / clip.w;
    TEST_CHECK(approx(ndc_z, 1.0f, 1e-4f));
}

TEST_CASE(perspective_flips_y)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 proj = MathLib::perspective(PI / 2.0f, 1.0f, 0.1f, 100.0f);
    // A point above centre in view space should have negative Y in clip space (Vulkan Y-down)
    MathLib::Vec4 above{0.0f, 1.0f, -1.0f, 1.0f};
    MathLib::Vec4 clip = proj * above;
    TEST_CHECK(clip.y < 0.0f);
}

// ── View matrices ──

TEST_CASE(look_at_identity_position)
{
    MathLib::Mat4 view = MathLib::lookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f});
    // Origin should remain at origin in view space
    MathLib::Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = view * origin;
    TEST_CHECK(approxVec4(result, {0.0f, 0.0f, 0.0f, 1.0f}, 1e-4f));
}

TEST_CASE(look_at_target_along_negative_z)
{
    MathLib::Mat4 view = MathLib::lookAt({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    // The target (origin) should be at z = -5 in view space
    MathLib::Vec4 target{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = view * target;
    TEST_CHECK(approx(result.z, -5.0f, 1e-4f));
}

TEST_CASE(view_from_quaternion_matches_look_at)
{
    MathLib::Vec3 eye{0.0f, 0.0f, 5.0f};
    MathLib::Vec3 target{0.0f, 0.0f, 0.0f};
    MathLib::Mat4 from_look_at = MathLib::lookAt(eye, target, {0.0f, 1.0f, 0.0f});
    // Identity quaternion looks along -Z, which matches lookAt from (0,0,5) to (0,0,0)
    MathLib::Mat4 from_quat = MathLib::viewFromQuaternion(eye, MathLib::Quat::identity());
    TEST_CHECK(approxMat4(from_look_at, from_quat, 1e-4f));
}

TEST_CASE(view_from_spherical_at_zero_elevation)
{
    MathLib::Vec3 target{0.0f, 0.0f, 0.0f};
    MathLib::Mat4 view = MathLib::viewFromSpherical(target, 0.0f, 0.0f, 5.0f);
    // At azimuth=0, elevation=0, radius=5: camera at (5, 0, 0) looking at origin
    MathLib::Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = view * origin;
    // Origin should be at z = -5 in view space (5 units in front)
    TEST_CHECK(approx(result.z, -5.0f, 1e-4f));
}

// ── Additional Vec2 coverage ──

TEST_CASE(vec2_negate)
{
    MathLib::Vec2 v{3.0f, -4.0f};
    TEST_CHECK((-v == MathLib::Vec2{-3.0f, 4.0f}));
}

TEST_CASE(vec2_compound_assignment)
{
    MathLib::Vec2 v{1.0f, 2.0f};
    v += MathLib::Vec2{3.0f, 4.0f};
    TEST_CHECK((v == MathLib::Vec2{4.0f, 6.0f}));
    v -= MathLib::Vec2{1.0f, 1.0f};
    TEST_CHECK((v == MathLib::Vec2{3.0f, 5.0f}));
    v *= 2.0f;
    TEST_CHECK((v == MathLib::Vec2{6.0f, 10.0f}));
}

// ── Additional Vec4 coverage ──

TEST_CASE(vec4_default_zero)
{
    MathLib::Vec4 v;
    TEST_CHECK(v.x == 0.0f && v.y == 0.0f && v.z == 0.0f && v.w == 0.0f);
}

TEST_CASE(vec4_arithmetic)
{
    MathLib::Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    MathLib::Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    TEST_CHECK(((a + b) == MathLib::Vec4{6.0f, 8.0f, 10.0f, 12.0f}));
    TEST_CHECK(((a - b) == MathLib::Vec4{-4.0f, -4.0f, -4.0f, -4.0f}));
    TEST_CHECK(((a * 2.0f) == MathLib::Vec4{2.0f, 4.0f, 6.0f, 8.0f}));
    TEST_CHECK(((-a) == MathLib::Vec4{-1.0f, -2.0f, -3.0f, -4.0f}));
}

// ── Additional Mat4 coverage ──

TEST_CASE(mat4_rotate_x_90)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 r = MathLib::Mat4::rotate({1.0f, 0.0f, 0.0f}, PI / 2.0f);
    MathLib::Vec4 v{0.0f, 1.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = r * v;
    TEST_CHECK(approxVec4(result, {0.0f, 0.0f, 1.0f, 1.0f}, 1e-4f));
}

TEST_CASE(mat4_rotate_y_90)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 r = MathLib::Mat4::rotate({0.0f, 1.0f, 0.0f}, PI / 2.0f);
    MathLib::Vec4 v{1.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = r * v;
    TEST_CHECK(approxVec4(result, {0.0f, 0.0f, -1.0f, 1.0f}, 1e-4f));
}

TEST_CASE(mat4_rotate_360_identity)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 r = MathLib::Mat4::rotate({0.0f, 1.0f, 0.0f}, 2.0f * PI);
    TEST_CHECK(approxMat4(r, MathLib::Mat4::identity(), 1e-4f));
}

// ── Additional Quat coverage ──

TEST_CASE(quat_rotate_around_x)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat q = MathLib::Quat::fromAxisAngle({1.0f, 0.0f, 0.0f}, PI / 2.0f);
    MathLib::Vec3 v{0.0f, 1.0f, 0.0f};
    MathLib::Vec3 result = q.rotate(v);
    TEST_CHECK(approxVec3(result, {0.0f, 0.0f, 1.0f}, 1e-4f));
}

TEST_CASE(quat_rotate_around_y)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat q = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, PI / 2.0f);
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    MathLib::Vec3 result = q.rotate(v);
    TEST_CHECK(approxVec3(result, {0.0f, 0.0f, -1.0f}, 1e-4f));
}

TEST_CASE(quat_length)
{
    MathLib::Quat q{1.0f, 2.0f, 3.0f, 4.0f};
    float expected = std::sqrt(1.0f + 4.0f + 9.0f + 16.0f);
    TEST_CHECK(approx(q.length(), expected));
}

TEST_CASE(quat_slerp_short_path)
{
    // Two quaternions that represent the same rotation but with opposite signs.
    // Slerp should take the short path (negate one).
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Quat a = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.1f);
    MathLib::Quat b = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, 0.2f);
    MathLib::Quat neg_b{-b.w, -b.x, -b.y, -b.z};
    // Slerp with negated b should produce the same result as slerp with b
    // (the short path logic adjusts internally)
    MathLib::Quat result_pos = MathLib::Quat::slerp(a, b, 0.5f);
    MathLib::Quat result_neg = MathLib::Quat::slerp(a, neg_b, 0.5f);
    // Both should rotate a vector the same way
    MathLib::Vec3 v{1.0f, 0.0f, 0.0f};
    TEST_CHECK(approxVec3(result_pos.rotate(v), result_neg.rotate(v), 1e-4f));
}

// ── Additional Projection coverage ──

TEST_CASE(perspective_centre_maps_to_origin)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 proj = MathLib::perspective(PI / 2.0f, 1.0f, 0.1f, 100.0f);
    // A point at the centre of the view (on the -Z axis) should map to (0, 0) in NDC
    MathLib::Vec4 centre{0.0f, 0.0f, -1.0f, 1.0f};
    MathLib::Vec4 clip = proj * centre;
    float ndc_x = clip.x / clip.w;
    float ndc_y = clip.y / clip.w;
    TEST_CHECK(approx(ndc_x, 0.0f, 1e-4f));
    TEST_CHECK(approx(ndc_y, 0.0f, 1e-4f));
}

TEST_CASE(perspective_aspect_ratio)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Mat4 proj_wide = MathLib::perspective(PI / 2.0f, 2.0f, 0.1f, 100.0f);
    MathLib::Mat4 proj_tall = MathLib::perspective(PI / 2.0f, 0.5f, 0.1f, 100.0f);
    // A point at (1, 0, -1) should have different NDC X for different aspect ratios
    MathLib::Vec4 point{1.0f, 0.0f, -1.0f, 1.0f};
    MathLib::Vec4 clip_wide = proj_wide * point;
    MathLib::Vec4 clip_tall = proj_tall * point;
    float ndc_x_wide = clip_wide.x / clip_wide.w;
    float ndc_x_tall = clip_tall.x / clip_tall.w;
    // Wide aspect should compress X more (smaller NDC X)
    TEST_CHECK(std::fabs(ndc_x_wide) < std::fabs(ndc_x_tall));
}

// ── Additional View coverage ──

TEST_CASE(view_from_spherical_elevated)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Vec3 target{0.0f, 0.0f, 0.0f};
    // Camera directly above, looking down
    MathLib::Mat4 view = MathLib::viewFromSpherical(target, 0.0f, PI / 2.0f, 10.0f);
    MathLib::Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = view * origin;
    // Origin should be at z = -10 in view space (10 units in front of camera)
    TEST_CHECK(approx(result.z, -10.0f, 1e-3f));
}

TEST_CASE(view_from_quaternion_rotated)
{
    constexpr float PI = 3.14159265358979323846f;
    MathLib::Vec3 position{0.0f, 0.0f, 5.0f};
    // Rotate 180 degrees around Y — camera at (0,0,5) now looks along +Z (away from origin)
    MathLib::Quat orientation = MathLib::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, PI);
    MathLib::Mat4 view = MathLib::viewFromQuaternion(position, orientation);
    MathLib::Vec4 origin{0.0f, 0.0f, 0.0f, 1.0f};
    MathLib::Vec4 result = view * origin;
    // Origin should now be BEHIND the camera (positive Z in view space)
    TEST_CHECK(result.z > 0.0f);
}

int main()
{
    return static_cast<int>(TestFixtureLib::runAll());
}
