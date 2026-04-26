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

#pragma once

#include "math/matrix.hpp"
#include "math/quaternion.hpp"
#include "math/vector.hpp"
#include <array>
#include <cmath>

namespace MathLib
{

    /*!
        Returns a Vulkan-compatible perspective projection matrix.

        Vulkan clip space: X right, Z into screen, depth [0, 1].
        Y-flip is handled by slangc `-fvk-invert-y`, not this matrix.

        \param fov_y Vertical field of view in radians.
        \param aspect Width / height aspect ratio.
        \param near_plane Near clipping plane distance (must be > 0).
        \param far_plane Far clipping plane distance (must be > near_plane).
        \return Column-major 4x4 perspective projection matrix.
    */
    [[nodiscard]] inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane)
    {
        float tan_half_fov = std::tan(fov_y * 0.5f);

        Mat4 result{};
        result(0, 0) = 1.0f / (aspect * tan_half_fov);
        result(1, 1) = 1.0f / tan_half_fov; // Y-flip handled by slangc -fvk-invert-y
        result(2, 2) = far_plane / (near_plane - far_plane);
        result(2, 3) = -1.0f;
        result(3, 2) = (near_plane * far_plane) / (near_plane - far_plane);
        return result;
    }

    /*!
        Returns a view matrix for a camera at \p eye looking at \p target.

        Aviation / free-flight mode: the camera is positioned at \p eye and
        oriented to look at \p target with \p up defining the world up direction.

        \param eye Camera position in world space.
        \param target Point the camera looks at.
        \param up World up direction (typically {0, 1, 0}).
        \return Column-major 4x4 view matrix.
    */
    [[nodiscard]] inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up)
    {
        Vec3 f = (target - eye).normalised(); // Forward
        Vec3 r = f.cross(up).normalised(); // Right
        Vec3 u = r.cross(f); // Recomputed up

        Mat4 result{Mat4::identity()};
        result(0, 0) = r.x;
        result(1, 0) = r.y;
        result(2, 0) = r.z;

        result(0, 1) = u.x;
        result(1, 1) = u.y;
        result(2, 1) = u.z;

        result(0, 2) = -f.x;
        result(1, 2) = -f.y;
        result(2, 2) = -f.z;

        result(3, 0) = -r.dot(eye);
        result(3, 1) = -u.dot(eye);
        result(3, 2) = f.dot(eye);
        return result;
    }

    /*!
        Returns a view matrix from a position and quaternion orientation.

        Aviation / free-flight mode: the camera is at \p position with
        orientation defined by \p orientation. The quaternion encodes the
        camera's rotation relative to the default forward direction (0, 0, -1).

        \param position Camera position in world space.
        \param orientation Camera orientation as a unit quaternion.
        \return Column-major 4x4 view matrix.
    */
    [[nodiscard]] inline Mat4 viewFromQuaternion(const Vec3& position, const Quat& orientation)
    {
        // The view matrix is the inverse of the camera's model matrix.
        // For a rotation quaternion, inverse = conjugate (transpose of rotation part).
        Mat4 rotation = orientation.toMat4().transposed();
        Mat4 translation = Mat4::translate(-position);
        return rotation * translation;
    }

    /*!
        Returns a view matrix for an orbit camera in spherical polar coordinates.

        The camera orbits around \p target at the given \p radius, positioned
        by \p azimuth (horizontal angle, radians) and \p elevation (vertical
        angle, radians). Elevation of 0 is on the horizontal plane; positive
        looks from above.

        \param target The point the camera orbits around.
        \param azimuth Horizontal angle in radians (0 = +X direction).
        \param elevation Vertical angle in radians (0 = horizontal, +pi/2 = top).
        \param radius Distance from the target.
        \return Column-major 4x4 view matrix.
    */
    [[nodiscard]] inline Mat4 viewFromSpherical(const Vec3& target, float azimuth, float elevation, float radius)
    {
        float cos_elev = std::cos(elevation);
        Vec3 eye{
            target.x + radius * cos_elev * std::cos(azimuth),
            target.y + radius * std::sin(elevation),
            target.z + radius * cos_elev * std::sin(azimuth),
        };
        return lookAt(eye, target, {0.0f, 1.0f, 0.0f});
    }

    //! A plane in the form ax + by + cz + d = 0, stored as Vec4(a, b, c, d).
    using Plane = Vec4;

    //! Six frustum planes: left, right, bottom, top, near_plane, far_plane.
    struct Frustum {
        std::array<Plane, 6> planes{}; //!< Frustum planes (normals point inward).
    };

    /*!
        Extracts 6 frustum planes from a view-projection matrix (Gribb-Hartmann method).

        The planes are normalised so that distance tests return true world-space distances.
        Normals point inward — a point is inside the frustum if it is on the positive side
        of all 6 planes.

        \param vp The combined view × projection matrix.
        \return The 6 frustum planes.
    */
    [[nodiscard]] inline Frustum extractFrustum(const Mat4& vp)
    {
        Frustum f{};

        // Left:   row3 + row0
        f.planes[0] = {vp(0, 3) + vp(0, 0), vp(1, 3) + vp(1, 0), vp(2, 3) + vp(2, 0), vp(3, 3) + vp(3, 0)};
        // Right:  row3 - row0
        f.planes[1] = {vp(0, 3) - vp(0, 0), vp(1, 3) - vp(1, 0), vp(2, 3) - vp(2, 0), vp(3, 3) - vp(3, 0)};
        // Bottom: row3 + row1
        f.planes[2] = {vp(0, 3) + vp(0, 1), vp(1, 3) + vp(1, 1), vp(2, 3) + vp(2, 1), vp(3, 3) + vp(3, 1)};
        // Top:    row3 - row1
        f.planes[3] = {vp(0, 3) - vp(0, 1), vp(1, 3) - vp(1, 1), vp(2, 3) - vp(2, 1), vp(3, 3) - vp(3, 1)};
        // Near:   row2 (Vulkan depth [0,1] — not row3 + row2 which is for OpenGL [-1,1])
        f.planes[4] = {vp(0, 2), vp(1, 2), vp(2, 2), vp(3, 2)};
        // Far:    row3 - row2
        f.planes[5] = {vp(0, 3) - vp(0, 2), vp(1, 3) - vp(1, 2), vp(2, 3) - vp(2, 2), vp(3, 3) - vp(3, 2)};

        // Normalise planes so distance tests give true distances
        for (uint32_t i{0}; i < 6; ++i) {
            float len{Vec3{f.planes[i].x, f.planes[i].y, f.planes[i].z}.length()};
            if (len > 0.0f) {
                float inv{1.0f / len};
                f.planes[i] = {f.planes[i].x * inv, f.planes[i].y * inv, f.planes[i].z * inv, f.planes[i].w * inv};
            }
        }

        return f;
    }

    /*!
        Tests whether a bounding sphere is inside or intersects the frustum.

        \param frustum The 6 frustum planes.
        \param centre World-space centre of the bounding sphere.
        \param radius Bounding sphere radius.
        \return True if the sphere is at least partially inside the frustum.
    */
    [[nodiscard]] inline bool isInsideFrustum(const Frustum& frustum, const Vec3& centre, float radius)
    {
        for (uint32_t i{0}; i < 6; ++i) {
            float distance = frustum.planes[i].x * centre.x + frustum.planes[i].y * centre.y + frustum.planes[i].z * centre.z + frustum.planes[i].w;
            if (distance < -radius) {
                return false;
            }
        }
        return true;
    }

} // namespace MathLib
